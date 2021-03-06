/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2007-2013 Fachhochschule Potsdam - http://fh-potsdam.de

Fritzing is free software: you can redistribute it and/or modify

it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Fritzing is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fritzing.  If not, see <http://www.gnu.org/licenses/>.

********************************************************************

$Revision: 6995 $:
$Author: irascibl@gmail.com $:
$Date: 2013-04-28 00:56:34 +0200 (So, 28. Apr 2013) $

********************************************************************/

#include <QtGui>
#include <QtXml>
#include <QList>
#include <QFileInfo>
#include <QStringList>
#include <QFileInfoList>
#include <QDir>
#include <QLabel>
#include <QTime>
#include <QSettings>
#include <QRegExp>
#include <QPaintDevice>
#include <QPixmap>
#include <QTimer>
#include <QStackedWidget>
#include <QXmlStreamReader>
#include <QShortcut>

#include "mainwindow.h"
#include "../debugdialog.h"
#include "../connectors/connector.h"
#include "../partsbinpalette/partsbinpalettewidget.h"
#include "fdockwidget.h"
#include "../infoview/htmlinfoview.h"
#include "../waitpushundostack.h"
#include "../layerattributes.h"
#include "../dock/triplenavigator.h"
#include "../sketch/breadboardsketchwidget.h"
#include "../sketch/schematicsketchwidget.h"
#include "../sketch/pcbsketchwidget.h"
#include "../svg/svgfilesplitter.h"
#include "../utils/folderutils.h"
#include "../utils/fmessagebox.h"
#include "../utils/lockmanager.h"
#include "../utils/textutils.h"
#include "../utils/graphicsutils.h"
#include "../items/mysterypart.h"
#include "../items/moduleidnames.h"
#include "../items/pinheader.h"
#include "../items/perfboard.h"
#include "../items/stripboard.h"
#include "../items/partfactory.h"
#include "../dock/layerpalette.h"
#include "../items/paletteitem.h"
#include "../items/virtualwire.h"
#include "../items/screwterminal.h"
#include "../items/dip.h"
#include "../processeventblocker.h"
#include "../help/helper.h"
#include "../sketchtoolbutton.h"
#include "../partsbinpalette/binmanager/binmanager.h"
#include "../fsvgrenderer.h"
#include "../utils/fsizegrip.h"
#include "../utils/expandinglabel.h"
#include "../dock/viewswitcher.h"
#include "../dock/viewswitcherdockwidget.h"
#include "../utils/autoclosemessagebox.h"
#include "../utils/fileprogressdialog.h"
#include "../utils/clickablelabel.h"
#include "../items/resizableboard.h"
#include "../items/resistor.h"
#include "../items/logoitem.h"
#include "../utils/zoomslider.h"
#include "../partseditor/pemainwindow.h"

///////////////////////////////////////////////

struct MissingSvgInfo {
    QString requestedPath;
    QStringList connectorSvgIds;
    ModelPart * modelPart;
    bool equal;
};

bool byConnectorCount(MissingSvgInfo & m1, MissingSvgInfo & m2)
{
    if (m1.connectorSvgIds.count() == m2.connectorSvgIds.count() && m1.modelPart != m2.modelPart) {
        m1.equal = m2.equal = true;
    }

	return (m1.connectorSvgIds.count() > m2.connectorSvgIds.count());
}

///////////////////////////////////////////////

#define ZIP_PART QString("part.")
#define ZIP_SVG  QString("svg.")

///////////////////////////////////////////////

// SwapTimer explained: http://code.google.com/p/fritzing/issues/detail?id=1431

SwapTimer::SwapTimer() : QTimer() 
{
}

void SwapTimer::setAll(const QString & family, const QString & prop, QMap<QString, QString> & propsMap, ItemBase * itemBase)
{
	m_family = family;
	m_prop = prop;
	m_propsMap = propsMap;
	m_itemBase = itemBase;
}

const QString & SwapTimer::family()
{
	return  m_family;
}

const QString & SwapTimer::prop()
{
	return m_prop;
}

QMap<QString, QString> SwapTimer::propsMap()
{
	return m_propsMap;
}

ItemBase * SwapTimer::itemBase()
{
	return m_itemBase;
}

///////////////////////////////////////////////

const QString MainWindow::UntitledSketchName = "Untitled Sketch";
int MainWindow::UntitledSketchIndex = 1;
int MainWindow::CascadeFactorX = 21;
int MainWindow::CascadeFactorY = 19;

static const int MainWindowDefaultWidth = 840;
static const int MainWindowDefaultHeight = 600;

int MainWindow::AutosaveTimeoutMinutes = 10;   // in minutes
bool MainWindow::AutosaveEnabled = true;
QString MainWindow::BackupFolder;

QRegExp MainWindow::GuidMatcher = QRegExp("[A-Fa-f0-9]{32}");

/////////////////////////////////////////////

MainWindow::MainWindow(ReferenceModel *referenceModel, QWidget * parent) :
    FritzingWindow(untitledFileName(), untitledFileCount(), fileExtension(), parent)
{
    m_rolloverQuoteDialog = NULL;
	setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
	setDockOptions(QMainWindow::AnimatedDocks);
	m_sizeGrip = new FSizeGrip(this);

	m_topDock = NULL;
	m_bottomDock = NULL;
	m_dontKeepMargins = true;

    m_settingsPrefix = "main/";
    m_showProgramAct = m_raiseWindowAct = m_showPartsBinIconViewAct = m_showAllLayersAct = m_hideAllLayersAct = m_showInViewHelpAct = m_rotate90cwAct = m_showBreadboardAct = m_showSchematicAct = m_showPCBAct = NULL;
    m_fileMenu = m_editMenu = m_partMenu = m_windowMenu = m_pcbTraceMenu = m_schematicTraceMenu = m_breadboardTraceMenu = m_viewMenu = NULL;
    m_miniViewContainerBreadboard = NULL;
    m_infoView = NULL;
    m_addedToTemp = false;
    setAcceptDrops(true);
	m_activeWire = NULL;
	m_activeConnectorItem = NULL;
	m_swapTimer.setInterval(30);
	m_swapTimer.setParent(this);
	m_swapTimer.setSingleShot(true);
	connect(&m_swapTimer, SIGNAL(timeout()), this, SLOT(swapSelectedTimeout()));

	m_closeSilently = false;
	m_orderFabAct = NULL;
	m_viewFromButtonWidget = m_activeLayerButtonWidget = NULL;
	m_programView = m_programWindow = NULL;
	m_windowMenuSeparator = NULL;
	m_schematicWireColorMenu = m_breadboardWireColorMenu = NULL;
	m_viewSwitcherDock = NULL;
	m_checkForUpdatesAct = NULL;
	m_fileProgressDialog = NULL;
	m_currentGraphicsView = NULL;
	m_comboboxChanged = false;
	m_helper = NULL;

    // Add a timer for autosaving
	m_backingUp = m_autosaveNeeded = false;
    connect(&m_autosaveTimer, SIGNAL(timeout()), this, SLOT(backupSketch()));
    m_autosaveTimer.start(AutosaveTimeoutMinutes * 60 * 1000);

	resize(MainWindowDefaultWidth, MainWindowDefaultHeight);

	m_backupFileNameAndPath = MainWindow::BackupFolder + "/" + TextUtils::getRandText() + FritzingSketchExtension;
    // Connect the undoStack to our autosave stuff
    connect(m_undoStack, SIGNAL(indexChanged(int)), this, SLOT(autosaveNeeded(int)));
    connect(m_undoStack, SIGNAL(cleanChanged(bool)), this, SLOT(undoStackCleanChanged(bool)));

	// Create dot icons
	m_dotIcon = QIcon(":/resources/images/dot.png");
	m_emptyIcon = QIcon();

	m_currentWidget = NULL;
	m_firstOpen = true;

	m_statusBar = new QStatusBar(this);
	setStatusBar(m_statusBar);
	m_statusBar->setSizeGripEnabled(false);

	QSettings settings;
	m_locationLabelUnits = settings.value("LocationInches", "in").toString();

	// leave the m_orderFabEnabled check in case we turn off the fab button in the future
	m_orderFabEnabled = true; // settings.value(ORDERFABENABLED, QVariant(false)).toBool();

	m_locationLabel = new ClickableLabel("", this);
	m_locationLabel->setObjectName("LocationLabel");
	connect(m_locationLabel, SIGNAL(clicked()), this, SLOT(locationLabelClicked()));
    m_locationLabel->setCursor(Qt::PointingHandCursor);
	m_statusBar->addPermanentWidget(m_locationLabel);

	m_zoomSlider = new ZoomSlider(ZoomableGraphicsView::MaxScaleValue, m_statusBar);
	connect(m_zoomSlider, SIGNAL(zoomChanged(double)), this, SLOT(updateViewZoom(double)));
	m_statusBar->addPermanentWidget(m_zoomSlider);

	setAttribute(Qt::WA_DeleteOnClose, true);

#ifdef Q_WS_MAC
        //setAttribute(Qt::WA_QuitOnClose, false);					// restoring this temporarily (2008.12.19)
#endif
    m_dontClose = m_closing = false;

	m_referenceModel = referenceModel;
	m_sketchModel = new SketchModel(true);


	QShortcut * shortcut = new QShortcut(QKeySequence(tr("Ctrl+R", "Rotate Clockwise")), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(rotateIncCW()));
	shortcut = new QShortcut(QKeySequence(tr("Alt+Ctrl+R", "Rotate Clockwise")), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(rotateIncCWRubberBand()));
	shortcut = new QShortcut(QKeySequence(tr("Meta+Ctrl+R", "Rotate Clockwise")), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(rotateIncCWRubberBand()));

	shortcut = new QShortcut(QKeySequence(tr("Shift+Ctrl+R", "Rotate Counterclockwise")), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(rotateIncCCW()));
	shortcut = new QShortcut(QKeySequence(tr("Alt+Shift+Ctrl+R", "Rotate Counterclockwise")), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(rotateIncCCWRubberBand()));
	shortcut = new QShortcut(QKeySequence(tr("Meta+Shift+Ctrl+R", "Rotate Counterclockwise")), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(rotateIncCCWRubberBand()));

	shortcut = new QShortcut(QKeySequence(tr("Shift+Ctrl+Tab", "Toggle Active Layer")), this);
	connect(shortcut, SIGNAL(activated()), this, SLOT(toggleActiveLayer()));


	connect(this, SIGNAL(changeActivationSignal(bool, QWidget *)), qApp, SLOT(changeActivation(bool, QWidget *)), Qt::DirectConnection);
	connect(this, SIGNAL(destroyed(QObject *)), qApp, SLOT(topLevelWidgetDestroyed(QObject *)));
	connect(this, SIGNAL(externalProcessSignal(QString &, QString &, QStringList &)),
			qApp, SLOT(externalProcessSlot(QString &, QString &, QStringList &)), 
			Qt::DirectConnection);
}

QWidget * MainWindow::createTabWidget() {
	return new QStackedWidget(this);
}

void MainWindow::addTab(QWidget * widget, const QString & label) {
	Q_UNUSED(label);
	qobject_cast<QStackedWidget *>(m_tabWidget)->addWidget(widget);
}

int MainWindow::currentTabIndex() {
	return qobject_cast<QStackedWidget *>(m_tabWidget)->currentIndex();
}

void MainWindow::setCurrentTabIndex(int index) {
	qobject_cast<QStackedWidget *>(m_tabWidget)->setCurrentIndex(index);
}

QWidget * MainWindow::currentTabWidget() {
	return qobject_cast<QStackedWidget *>(m_tabWidget)->currentWidget();
}

void MainWindow::init(ReferenceModel *referenceModel, bool lockFiles) {

	m_tabWidget = createTabWidget(); //   FTabWidget(this);
	m_tabWidget->setObjectName("sketch_tabs");
	setCentralWidget(m_tabWidget);

    m_referenceModel = referenceModel;
    m_restarting = false;

	if (m_fileProgressDialog) {
		m_fileProgressDialog->setValue(2);
	}

    initLockedFiles(lockFiles);

    initSketchWidgets();
    initProgrammingWidget();

    m_undoView = new QUndoView();
    m_undoGroup = new QUndoGroup(this);
    m_undoView->setGroup(m_undoGroup);
    m_undoGroup->setActiveStack(m_undoStack);

    initDock();
    initMenus();
    moreInitDock();

	createZoomOptions(m_breadboardWidget);
	createZoomOptions(m_schematicWidget);
	createZoomOptions(m_pcbWidget);

    m_breadboardWidget->setToolbarWidgets(getButtonsForView(ViewLayer::BreadboardView));
    m_schematicWidget->setToolbarWidgets(getButtonsForView(ViewLayer::SchematicView));
	m_pcbWidget->setToolbarWidgets(getButtonsForView(ViewLayer::PCBView));

    initStyleSheet();

    m_breadboardGraphicsView->setItemMenu(breadboardItemMenu());
    m_breadboardGraphicsView->setWireMenu(breadboardWireMenu());

    m_pcbGraphicsView->setWireMenu(pcbWireMenu());
    m_pcbGraphicsView->setItemMenu(pcbItemMenu());

    m_schematicGraphicsView->setItemMenu(schematicItemMenu());
    m_schematicGraphicsView->setWireMenu(schematicWireMenu());

    if (m_infoView) {
        m_breadboardGraphicsView->setInfoView(m_infoView);
        m_pcbGraphicsView->setInfoView(m_infoView);
        m_schematicGraphicsView->setInfoView(m_infoView);
    }

	// make sure to set the connections after the views have been created
	connect(m_tabWidget, SIGNAL(currentChanged ( int )), this, SLOT(tabWidget_currentChanged( int )));

	connectPairs();

	initHelper();

	// do this the first time, since the current_changed signal wasn't sent
	int tab = 0;
    if (m_navigators.count() > 0) {
	    currentNavigatorChanged(m_navigators[tab]);
    }
	tabWidget_currentChanged(tab+1);
	tabWidget_currentChanged(tab);

	this->installEventFilter(this);

	if (m_fileProgressDialog) {
		m_fileProgressDialog->setValue(95);
	}

	QSettings settings;
    if (m_viewSwitcherDock) {
        m_viewSwitcherDock->prestorePreference();
    }
	if(!settings.value(m_settingsPrefix + "state").isNull()) {
		restoreState(settings.value(m_settingsPrefix + "state").toByteArray());
		restoreGeometry(settings.value(m_settingsPrefix + "geometry").toByteArray());
	}
    if (m_viewSwitcherDock) {
        m_viewSwitcherDock->restorePreference();
        m_viewSwitcherDock->setViewSwitcher(m_viewSwitcher);
    }

	setMinimumSize(0,0);
	m_tabWidget->setMinimumWidth(500);
	m_tabWidget->setMinimumWidth(0);

    if (m_miniViewContainerBreadboard) {
	    m_miniViewContainerBreadboard->setView(m_breadboardGraphicsView);
	    m_miniViewContainerSchematic->setView(m_schematicGraphicsView);
	    m_miniViewContainerPCB->setView(m_pcbGraphicsView);
    }

	connect(this, SIGNAL(readOnlyChanged(bool)), this, SLOT(applyReadOnlyChange(bool)));

	m_setUpDockManagerTimer.setSingleShot(true);
	connect(&m_setUpDockManagerTimer, SIGNAL(timeout()), this, SLOT(keepMargins()));
    m_setUpDockManagerTimer.start(1000);

	if (m_fileProgressDialog) {
		m_fileProgressDialog->setValue(98);
	}

}

MainWindow::~MainWindow()
{
    // Delete backup of this sketch if one exists.
    QFile::remove(m_backupFileNameAndPath);	
	
	delete m_sketchModel;

	dontKeepMargins();
	m_setUpDockManagerTimer.stop();

	foreach (LinkedFile * linkedFile, m_linkedProgramFiles) {
		delete linkedFile;
	}
	m_linkedProgramFiles.clear();

	if (!m_fzzFolder.isEmpty()) {
		LockManager::releaseLockedFiles(m_fzzFolder, m_fzzFiles);
		FolderUtils::rmdir(m_fzzFolder);
	}
}	

void MainWindow::initHelper() {
    m_helper = new Helper(this, true);
}

void MainWindow::initLockedFiles(bool lockFiles) {
	LockManager::initLockedFiles("fzz", m_fzzFolder, m_fzzFiles, lockFiles ? LockManager::SlowTime : 0);
	if (lockFiles) {
		QFileInfoList backupList;
		LockManager::checkLockedFiles("fzz", backupList, m_fzzFiles, true, LockManager::SlowTime);
	}
}

void MainWindow::initSketchWidgets() {
	//DebugDialog::debug("init sketch widgets");

	// all this belongs in viewLayer.xml
	m_breadboardGraphicsView = new BreadboardSketchWidget(ViewLayer::BreadboardView, this);
	initSketchWidget(m_breadboardGraphicsView);
	m_breadboardWidget = new SketchAreaWidget(m_breadboardGraphicsView,this);
	addTab(m_breadboardWidget, tr("Breadboard"));

	if (m_fileProgressDialog) {
		m_fileProgressDialog->setValue(11);
	}

	m_schematicGraphicsView = new SchematicSketchWidget(ViewLayer::SchematicView, this);
	initSketchWidget(m_schematicGraphicsView);
	m_schematicWidget = new SketchAreaWidget(m_schematicGraphicsView, this);
	addTab(m_schematicWidget, tr("Schematic"));

	if (m_fileProgressDialog) {
		m_fileProgressDialog->setValue(20);
	}

	m_pcbGraphicsView = new PCBSketchWidget(ViewLayer::PCBView, this);
	initSketchWidget(m_pcbGraphicsView);
	m_pcbWidget = new SketchAreaWidget(m_pcbGraphicsView, this);
	addTab(m_pcbWidget, tr("PCB"));

	if (m_fileProgressDialog) {
		m_fileProgressDialog->setValue(29);
	}
}

void MainWindow::initMenus() {
	// This is the magic translation that changes all the shortcut text on the menu items
	// to the native language instead of "Ctrl", so the German menu items will now read "Strg"
	// You don't actually have to translate every menu item in the .ts file, you can just leave it as "Ctrl".
	QShortcut::tr("Ctrl", "for naming shortcut keys on menu items");
	QShortcut::tr("Alt", "for naming shortcut keys on menu items");
	QShortcut::tr("Shift", "for naming shortcut keys on menu items");
	QShortcut::tr("Meta", "for naming shortcut keys on menu items");

	//DebugDialog::debug("create menus");

    createActions();
    createMenus();

	//DebugDialog::debug("create toolbars");

    createStatusBar();

	//DebugDialog::debug("after creating status bar");

	if (m_fileProgressDialog) {
		m_fileProgressDialog->setValue(91);
	}
}


				   

void MainWindow::showNavigator() {
	m_navigatorDock->setFloating(false);
}

void MainWindow::initSketchWidget(SketchWidget * sketchWidget) {
	sketchWidget->setSketchModel(m_sketchModel);
	sketchWidget->setReferenceModel(m_referenceModel);
	sketchWidget->setUndoStack(m_undoStack);
	sketchWidget->setChainDrag(true);			// enable bend points
	sketchWidget->initGrid();
	sketchWidget->addViewLayers();
}

void MainWindow::connectPairs() {
	connectPair(m_breadboardGraphicsView, m_schematicGraphicsView);
	connectPair(m_breadboardGraphicsView, m_pcbGraphicsView);
	connectPair(m_schematicGraphicsView, m_breadboardGraphicsView);
	connectPair(m_schematicGraphicsView, m_pcbGraphicsView);
	connectPair(m_pcbGraphicsView, m_breadboardGraphicsView);
	connectPair(m_pcbGraphicsView, m_schematicGraphicsView);

	connect(m_pcbGraphicsView, SIGNAL(groundFillSignal()), this, SLOT(groundFill()));
	connect(m_pcbGraphicsView, SIGNAL(copperFillSignal()), this, SLOT(copperFill()));

    connect(m_pcbGraphicsView, SIGNAL(swapBoardImageSignal(SketchWidget *, ItemBase *, const QString &, const QString &, bool)),
                this, SLOT(swapBoardImageSlot(SketchWidget *, ItemBase *, const QString &, const QString &, bool)));


	connect(m_breadboardGraphicsView, SIGNAL(setActiveWireSignal(Wire *)), this, SLOT(setActiveWire(Wire *)));
	connect(m_schematicGraphicsView, SIGNAL(setActiveWireSignal(Wire *)), this, SLOT(setActiveWire(Wire *)));
	connect(m_pcbGraphicsView, SIGNAL(setActiveWireSignal(Wire *)), this, SLOT(setActiveWire(Wire *)));

	connect(m_breadboardGraphicsView, SIGNAL(setActiveConnectorItemSignal(ConnectorItem *)), this, SLOT(setActiveConnectorItem(ConnectorItem *)));
	connect(m_schematicGraphicsView, SIGNAL(setActiveConnectorItemSignal(ConnectorItem *)), this, SLOT(setActiveConnectorItem(ConnectorItem *)));
	connect(m_pcbGraphicsView, SIGNAL(setActiveConnectorItemSignal(ConnectorItem *)), this, SLOT(setActiveConnectorItem(ConnectorItem *)));

	bool succeeded = connect(m_pcbGraphicsView, SIGNAL(routingStatusSignal(SketchWidget *, const RoutingStatus &)),
						this, SLOT(routingStatusSlot(SketchWidget *, const RoutingStatus &)));
	succeeded =  succeeded && connect(m_schematicGraphicsView, SIGNAL(routingStatusSignal(SketchWidget *, const RoutingStatus &)),
						this, SLOT(routingStatusSlot(SketchWidget *, const RoutingStatus &)));
	succeeded =  succeeded && connect(m_breadboardGraphicsView, SIGNAL(routingStatusSignal(SketchWidget *, const RoutingStatus &)),
						this, SLOT(routingStatusSlot(SketchWidget *, const RoutingStatus &)));

	succeeded =  succeeded && connect(m_breadboardGraphicsView, SIGNAL(swapSignal(const QString &, const QString &, QMap<QString, QString> &, ItemBase *)), 
						this, SLOT(swapSelectedDelay(const QString &, const QString &, QMap<QString, QString> &, ItemBase *)));
	succeeded =  succeeded && connect(m_schematicGraphicsView, SIGNAL(swapSignal(const QString &, const QString &, QMap<QString, QString> &, ItemBase *)), 
						this, SLOT(swapSelectedDelay(const QString &, const QString &, QMap<QString, QString> &, ItemBase *)));
	succeeded =  succeeded && connect(m_pcbGraphicsView, SIGNAL(swapSignal(const QString &, const QString &, QMap<QString, QString> &, ItemBase *)), 
						this, SLOT(swapSelectedDelay(const QString &, const QString &, QMap<QString, QString> &, ItemBase *)));

	succeeded =  succeeded && connect(m_breadboardGraphicsView, SIGNAL(dropPasteSignal(SketchWidget *)), 
						this, SLOT(dropPaste(SketchWidget *)));
	succeeded =  succeeded && connect(m_schematicGraphicsView, SIGNAL(dropPasteSignal(SketchWidget *)), 
						this, SLOT(dropPaste(SketchWidget *)));
	succeeded =  succeeded && connect(m_pcbGraphicsView, SIGNAL(dropPasteSignal(SketchWidget *)), 
						this, SLOT(dropPaste(SketchWidget *)));
	
	succeeded =  succeeded && connect(m_pcbGraphicsView, SIGNAL(subSwapSignal(SketchWidget *, ItemBase *, const QString &, ViewLayer::ViewLayerPlacement, long &, QUndoCommand *)),
						this, SLOT(subSwapSlot(SketchWidget *, ItemBase *, const QString &, ViewLayer::ViewLayerPlacement, long &, QUndoCommand *)),
						Qt::DirectConnection);

	succeeded =  succeeded && connect(m_pcbGraphicsView, SIGNAL(firstTimeHelpHidden()), this, SLOT(firstTimeHelpHidden()));
	succeeded =  succeeded && connect(m_schematicGraphicsView, SIGNAL(firstTimeHelpHidden()), this, SLOT(firstTimeHelpHidden()));
	succeeded =  succeeded && connect(m_breadboardGraphicsView, SIGNAL(firstTimeHelpHidden()), this, SLOT(firstTimeHelpHidden()));

	succeeded =  succeeded && connect(m_pcbGraphicsView, SIGNAL(updateLayerMenuSignal()), this, SLOT(updateLayerMenuSlot()));
	succeeded =  succeeded && connect(m_pcbGraphicsView, SIGNAL(changeBoardLayersSignal(int, bool )), this, SLOT(changeBoardLayers(int, bool )));

	succeeded =  succeeded && connect(m_pcbGraphicsView, SIGNAL(boardDeletedSignal()), this, SLOT(boardDeletedSlot()));

	succeeded =  succeeded && connect(qApp, SIGNAL(spaceBarIsPressedSignal(bool)), m_breadboardGraphicsView, SLOT(spaceBarIsPressedSlot(bool)));
	succeeded =  succeeded && connect(qApp, SIGNAL(spaceBarIsPressedSignal(bool)), m_schematicGraphicsView, SLOT(spaceBarIsPressedSlot(bool)));
	succeeded =  succeeded && connect(qApp, SIGNAL(spaceBarIsPressedSignal(bool)), m_pcbGraphicsView, SLOT(spaceBarIsPressedSlot(bool)));

	succeeded =  succeeded && connect(m_pcbGraphicsView, SIGNAL(cursorLocationSignal(double, double)), this, SLOT(cursorLocationSlot(double, double)));
	succeeded =  succeeded && connect(m_breadboardGraphicsView, SIGNAL(cursorLocationSignal(double, double)), this, SLOT(cursorLocationSlot(double, double)));
	succeeded =  succeeded && connect(m_schematicGraphicsView, SIGNAL(cursorLocationSignal(double, double)), this, SLOT(cursorLocationSlot(double, double)));

	succeeded =  succeeded && connect(m_breadboardGraphicsView, SIGNAL(filenameIfSignal(QString &)), this, SLOT(filenameIfSlot(QString &)), Qt::DirectConnection);
	succeeded =  succeeded && connect(m_pcbGraphicsView, SIGNAL(filenameIfSignal(QString &)), this, SLOT(filenameIfSlot(QString &)), Qt::DirectConnection);
	succeeded =  succeeded && connect(m_schematicGraphicsView, SIGNAL(filenameIfSignal(QString &)), this, SLOT(filenameIfSlot(QString &)), Qt::DirectConnection);
	


    succeeded =  succeeded && connect(m_breadboardGraphicsView, SIGNAL(getDroppedItemViewLayerPlacementSignal(ModelPart *, ViewLayer::ViewLayerPlacement &)), 
                                        m_pcbGraphicsView, SLOT(getDroppedItemViewLayerPlacement(ModelPart *, ViewLayer::ViewLayerPlacement &)), 
                                        Qt::DirectConnection);
    succeeded =  succeeded && connect(m_schematicGraphicsView, SIGNAL(getDroppedItemViewLayerPlacementSignal(ModelPart *, ViewLayer::ViewLayerPlacement &)), 
                                        m_pcbGraphicsView, SLOT(getDroppedItemViewLayerPlacement(ModelPart *, ViewLayer::ViewLayerPlacement &)), 
                                        Qt::DirectConnection);


    if (!succeeded) {
		DebugDialog::debug("connectPair failed");
	}
}

void MainWindow::connectPair(SketchWidget * signaller, SketchWidget * slotter)
{

	bool succeeded = connect(signaller, SIGNAL(itemAddedSignal(ModelPart *, ItemBase *, ViewLayer::ViewLayerPlacement, const ViewGeometry &, long, SketchWidget *)),
							 slotter, SLOT(itemAddedSlot(ModelPart *, ItemBase *, ViewLayer::ViewLayerPlacement, const ViewGeometry &, long, SketchWidget *)));

	succeeded = succeeded && connect(signaller, SIGNAL(itemDeletedSignal(long)),
									 slotter, SLOT(itemDeletedSlot(long)),
									 Qt::DirectConnection);

	succeeded = succeeded && connect(signaller, SIGNAL(clearSelectionSignal()),
									 slotter, SLOT(clearSelectionSlot()));

	succeeded = succeeded && connect(signaller, SIGNAL(itemSelectedSignal(long, bool)),
									 slotter, SLOT(itemSelectedSlot(long, bool)));
	succeeded = succeeded && connect(signaller, SIGNAL(selectAllItemsSignal(bool, bool)),
									 slotter, SLOT(selectAllItems(bool, bool)));
	succeeded = succeeded && connect(signaller, SIGNAL(wireDisconnectedSignal(long, QString)),
									 slotter, SLOT(wireDisconnectedSlot(long,  QString)));
	succeeded = succeeded && connect(signaller, SIGNAL(wireConnectedSignal(long,  QString, long,  QString)),
									 slotter, SLOT(wireConnectedSlot(long, QString, long, QString)));
	succeeded = succeeded && connect(signaller, SIGNAL(changeConnectionSignal(long,  QString, long,  QString, ViewLayer::ViewLayerPlacement, bool, bool)),
									 slotter, SLOT(changeConnectionSlot(long, QString, long, QString, ViewLayer::ViewLayerPlacement, bool, bool)));
	succeeded = succeeded && connect(signaller, SIGNAL(copyBoundingRectsSignal(QHash<QString, QRectF> &)),
													   slotter, SLOT(copyBoundingRectsSlot(QHash<QString, QRectF> &)),
									 Qt::DirectConnection);

	succeeded = succeeded && connect(signaller, SIGNAL(cleanUpWiresSignal(CleanUpWiresCommand *)),
									 slotter, SLOT(cleanUpWiresSlot(CleanUpWiresCommand *)) );

	succeeded = succeeded && connect(signaller, SIGNAL(cleanupRatsnestsSignal(bool)),
									 slotter, SLOT(cleanupRatsnests(bool)) );

	succeeded = succeeded && connect(signaller, SIGNAL(checkStickySignal(long, bool, bool, CheckStickyCommand *)),
									 slotter, SLOT(checkSticky(long, bool, bool, CheckStickyCommand *)) );

	succeeded = succeeded && connect(signaller, SIGNAL(disconnectAllSignal(QList<ConnectorItem *>, QHash<ItemBase *, SketchWidget *> &, QUndoCommand *)),
									 slotter, SLOT(disconnectAllSlot(QList<ConnectorItem *>, QHash<ItemBase *, SketchWidget *> &, QUndoCommand *)),
									 Qt::DirectConnection);
	succeeded = succeeded && connect(signaller, SIGNAL(setResistanceSignal(long, QString, QString, bool)),
									 slotter, SLOT(setResistance(long, QString, QString, bool)));
	succeeded = succeeded && connect(signaller, SIGNAL(makeDeleteItemCommandPrepSignal(ItemBase *, bool , QUndoCommand * )),
									 slotter, SLOT(makeDeleteItemCommandPrepSlot(ItemBase * , bool , QUndoCommand * )));
	succeeded = succeeded && connect(signaller, SIGNAL(makeDeleteItemCommandFinalSignal(ItemBase *, bool , QUndoCommand * )),
									 slotter, SLOT(makeDeleteItemCommandFinalSlot(ItemBase * , bool , QUndoCommand * )));

	succeeded = succeeded && connect(signaller, SIGNAL(setPropSignal(long,  const QString &,  const QString &, bool, bool)),
									 slotter, SLOT(setProp(long,  const QString &,  const QString &, bool, bool)));

	succeeded = succeeded && connect(signaller, SIGNAL(setInstanceTitleSignal(long, const QString &, const QString &, bool, bool )),
									 slotter, SLOT(setInstanceTitle(long, const QString &, const QString &, bool, bool )));

	succeeded = succeeded && connect(signaller, SIGNAL(setVoltageSignal(double, bool )),
									 slotter, SLOT(setVoltage(double, bool )));

	succeeded = succeeded && connect(signaller, SIGNAL(showLabelFirstTimeSignal(long, bool, bool )),
									 slotter, SLOT(showLabelFirstTime(long, bool, bool )));

	succeeded = succeeded && connect(signaller, SIGNAL(changeBoardLayersSignal(int, bool )),
									 slotter, SLOT(changeBoardLayers(int, bool )));

	succeeded = succeeded && connect(signaller, SIGNAL(deleteTracesSignal(QSet<ItemBase *> &, QHash<ItemBase *, SketchWidget *> &, QList<long> &, bool, QUndoCommand *)),
									 slotter, SLOT(deleteTracesSlot(QSet<ItemBase *> &, QHash<ItemBase *, SketchWidget *> &, QList<long> &, bool, QUndoCommand *)),
									 Qt::DirectConnection);

	succeeded = succeeded && connect(signaller, SIGNAL(ratsnestConnectSignal(long, const QString &, bool, bool)),
									 slotter, SLOT(ratsnestConnect(long, const QString &, bool, bool )),
									 Qt::DirectConnection);


	succeeded = succeeded && connect(signaller, SIGNAL(updatePartLabelInstanceTitleSignal(long)),
									 slotter, SLOT(updatePartLabelInstanceTitleSlot(long)));

	succeeded = succeeded && connect(signaller, SIGNAL(changePinLabelsSignal(ItemBase *, bool)),
									 slotter, SLOT(changePinLabelsSlot(ItemBase *, bool)));

	succeeded = succeeded && connect(signaller, SIGNAL(collectRatsnestSignal(QList<SketchWidget *> &)),
									 slotter, SLOT(collectRatsnestSlot(QList<SketchWidget *> &)),
									 Qt::DirectConnection);

	succeeded = succeeded && connect(signaller, SIGNAL(removeRatsnestSignal(QList<struct ConnectorEdge *> &, QUndoCommand *)),
									 slotter, SLOT(removeRatsnestSlot(QList<struct ConnectorEdge *> &, QUndoCommand *)),
									 Qt::DirectConnection);

	succeeded = succeeded && connect(signaller, SIGNAL(canConnectSignal(Wire *, ItemBase *, bool &)),
									 slotter, SLOT(canConnect(Wire *, ItemBase *, bool &)),
									 Qt::DirectConnection);

	succeeded = succeeded && connect(signaller, SIGNAL(swapStartSignal(SwapThing &, bool)),
									 slotter, SLOT(swapStart(SwapThing &, bool)),
									 Qt::DirectConnection);

	succeeded = succeeded && connect(signaller, SIGNAL(packItemsSignal(int, const QList<long> &, QUndoCommand *, bool)),
									 slotter, SLOT(packItems(int, const QList<long> &, QUndoCommand *, bool)));

	succeeded = succeeded && connect(signaller, SIGNAL(addSubpartSignal(long, long, bool)), slotter, SLOT(addSubpart(long, long, bool)));

	if (!succeeded) {
		DebugDialog::debug("connectPair failed");
	}

}

void MainWindow::setCurrentFile(const QString &filename, bool addToRecent, bool setAsLastOpened) {
	setFileName(filename);

	if(setAsLastOpened) {
		QSettings settings;
		settings.setValue("lastOpenSketch",filename);
	}

	updateRaiseWindowAction();
	setTitle();

	if(addToRecent) {
		QSettings settings;
		QStringList files = settings.value("recentFileList").toStringList();
		files.removeAll(filename);
		files.prepend(filename);
		while (files.size() > MaxRecentFiles)
			files.removeLast();

		settings.setValue("recentFileList", files);
	}

    foreach (QWidget *widget, QApplication::topLevelWidgets()) {
        MainWindow *mainWin = qobject_cast<MainWindow *>(widget);
        if (mainWin)
            mainWin->updateRecentFileActions();
    }
}


void MainWindow::createZoomOptions(SketchAreaWidget* parent) {

    connect(parent->contentView(), SIGNAL(zoomChanged(double)), this, SLOT(updateZoomSlider(double)));
    connect(parent->contentView(), SIGNAL(zoomOutOfRange(double)), this, SLOT(updateZoomOptionsNoMatterWhat(double)));
}

ExpandingLabel * MainWindow::createRoutingStatusLabel(SketchAreaWidget * parent) {
	ExpandingLabel * routingStatusLabel = new ExpandingLabel(m_pcbWidget);

	connect(routingStatusLabel, SIGNAL(mousePressSignal(QMouseEvent*)), this, SLOT(routingStatusLabelMousePress(QMouseEvent*)));
	connect(routingStatusLabel, SIGNAL(mouseReleaseSignal(QMouseEvent*)), this, SLOT(routingStatusLabelMouseRelease(QMouseEvent*)));

	routingStatusLabel->setTextInteractionFlags(Qt::NoTextInteraction);
	routingStatusLabel->setCursor(Qt::WhatsThisCursor);
	routingStatusLabel->viewport()->setCursor(Qt::WhatsThisCursor);

	routingStatusLabel->setObjectName(SketchAreaWidget::RoutingStateLabelName);
	parent->setRoutingStatusLabel(routingStatusLabel);
	RoutingStatus routingStatus;
	routingStatus.zero();
	routingStatusSlot(qobject_cast<SketchWidget *>(parent->contentView()), routingStatus);
	return routingStatusLabel;
}

SketchToolButton *MainWindow::createRotateButton(SketchAreaWidget *parent) {
	QList<QAction*> rotateMenuActions;
	rotateMenuActions << m_rotate45ccwAct << m_rotate90ccwAct << m_rotate180Act << m_rotate90cwAct << m_rotate45cwAct;
	SketchToolButton * rotateButton = new SketchToolButton("Rotate",parent, rotateMenuActions);
	rotateButton->setDefaultAction(m_rotate90ccwAct);
	rotateButton->setText(tr("Rotate"));

	m_rotateButtons << rotateButton;
	return rotateButton;
}

SketchToolButton *MainWindow::createShareButton(SketchAreaWidget *parent) {
	SketchToolButton *shareButton = new SketchToolButton("Share",parent, m_shareOnlineAct);
	shareButton->setText(tr("Share"));
	shareButton->setEnabledIcon();					// seems to need this to display button icon first time
	return shareButton;
}

SketchToolButton *MainWindow::createFlipButton(SketchAreaWidget *parent) {
	QList<QAction*> flipMenuActions;
	flipMenuActions << m_flipHorizontalAct << m_flipVerticalAct;
	SketchToolButton *flipButton = new SketchToolButton("Flip",parent, flipMenuActions);
	flipButton->setText(tr("Flip"));

	m_flipButtons << flipButton;
	return flipButton;
}

SketchToolButton *MainWindow::createAutorouteButton(SketchAreaWidget *parent) {
	SketchToolButton *autorouteButton = new SketchToolButton("Autoroute",parent, m_newAutorouteAct);
	autorouteButton->setText(tr("Autoroute"));
	autorouteButton->setEnabledIcon();					// seems to need this to display button icon first time

	return autorouteButton;
}

SketchToolButton *MainWindow::createOrderFabButton(SketchAreaWidget *parent) {
	SketchToolButton *orderFabButton = new SketchToolButton("Order",parent, m_orderFabAct);
	orderFabButton->setText(tr("Order PCB"));
	orderFabButton->setEnabledIcon();					// seems to need this to display button icon first time

	return orderFabButton;
}

QWidget *MainWindow::createActiveLayerButton(SketchAreaWidget *parent) 
{
	QList<QAction *> actions;
	actions << m_activeLayerBothAct << m_activeLayerBottomAct << m_activeLayerTopAct;

	m_activeLayerButtonWidget = new QStackedWidget;
	m_activeLayerButtonWidget->setMaximumWidth(90);

	SketchToolButton * button = new SketchToolButton("ActiveLayer", parent, actions);
	button->setDefaultAction(m_activeLayerBottomAct);
	button->setText(tr("Both Layers"));
	m_activeLayerButtonWidget->addWidget(button);

	button = new SketchToolButton("ActiveLayerB", parent, actions);
	button->setDefaultAction(m_activeLayerTopAct);
	button->setText(tr("Bottom Layer"));
	m_activeLayerButtonWidget->addWidget(button);

	button = new SketchToolButton("ActiveLayerT", parent, actions);
	button->setDefaultAction(m_activeLayerBothAct);
	button->setText(tr("Top Layer"));
	m_activeLayerButtonWidget->addWidget(button);

	return m_activeLayerButtonWidget;
}

QWidget *MainWindow::createViewFromButton(SketchAreaWidget *parent) 
{
	QList<QAction *> actions;
	actions << m_viewFromAboveAct << m_viewFromBelowAct;

	m_viewFromButtonWidget = new QStackedWidget;
	m_viewFromButtonWidget->setMaximumWidth(90);

	SketchToolButton * button = new SketchToolButton("ViewFromT", parent, actions);
	button->setDefaultAction(m_viewFromBelowAct);
	button->setText(tr("View from Above"));
    button->setEnabledIcon();					// seems to need this to display button icon first time

	m_viewFromButtonWidget->addWidget(button);

	button = new SketchToolButton("ViewFromB", parent, actions);
	button->setDefaultAction(m_viewFromAboveAct);
	button->setText(tr("View from Below"));
    button->setEnabledIcon();					// seems to need this to display button icon first time
	m_viewFromButtonWidget->addWidget(button);

	return m_viewFromButtonWidget;
}

SketchToolButton *MainWindow::createNoteButton(SketchAreaWidget *parent) {
	SketchToolButton *noteButton = new SketchToolButton("Notes",parent, m_addNoteAct);
	noteButton->setText(tr("Add a note"));
	noteButton->setEnabledIcon();					// seems to need this to display button icon first time
	return noteButton;
}

SketchToolButton *MainWindow::createExportEtchableButton(SketchAreaWidget *parent) {
	QList<QAction*> actions;
	actions << m_exportEtchablePdfAct << m_exportEtchableSvgAct << m_exportGerberAct;
	SketchToolButton *exportEtchableButton = new SketchToolButton("Diy",parent, actions);
	exportEtchableButton->setDefaultAction(m_exportEtchablePdfAct);
	exportEtchableButton->setText(tr("Export for PCB"));
	exportEtchableButton->setEnabledIcon();				// seems to need this to display button icon first time
	return exportEtchableButton;
}

QWidget *MainWindow::createToolbarSpacer(SketchAreaWidget *parent) {
	QFrame *toolbarSpacer = new QFrame(parent);
	QHBoxLayout *spacerLayout = new QHBoxLayout(toolbarSpacer);
	spacerLayout->addSpacerItem(new QSpacerItem(0,0,QSizePolicy::Expanding));

	return toolbarSpacer;
}

QList<QWidget*> MainWindow::getButtonsForView(ViewLayer::ViewID viewId) {
	QList<QWidget*> retval;
	SketchAreaWidget *parent;
	switch(viewId) {
		case ViewLayer::BreadboardView: parent = m_breadboardWidget; break;
		case ViewLayer::SchematicView: parent = m_schematicWidget; break;
		case ViewLayer::PCBView: parent = m_pcbWidget; break;
		default: return retval;
	}
	retval << createShareButton(parent);

	switch(viewId) {
		case ViewLayer::BreadboardView:
		case ViewLayer::SchematicView:
			retval << createNoteButton(parent);
		default: 
			break;
	}
	
	retval << createRotateButton(parent);
	switch (viewId) {
		case ViewLayer::BreadboardView:
			retval << createFlipButton(parent); 
			break;
		case ViewLayer::SchematicView:
			retval << createFlipButton(parent) << createToolbarSpacer(parent) << createAutorouteButton(parent);
			break;
		case ViewLayer::PCBView:
			retval << SketchAreaWidget::separator(parent) 
                << createViewFromButton(parent)
				<< createActiveLayerButton(parent) 
				<< createAutorouteButton(parent) 
				<< createExportEtchableButton(parent);
			if (m_orderFabEnabled) {
                SketchToolButton * orderFabButton = createOrderFabButton(parent);
				retval << orderFabButton;
                connect(orderFabButton, SIGNAL(entered()), this, SLOT(orderFabHoverEnter()));
                connect(orderFabButton, SIGNAL(left()), this, SLOT(orderFabHoverLeave()));
			}


			break;
		default:
			break;
	}

	retval << createRoutingStatusLabel(parent);
	return retval;
}

void MainWindow::updateZoomSlider(double zoom) {
	m_zoomSlider->setValue(zoom);
}

SketchAreaWidget *MainWindow::currentSketchArea() {
	return dynamic_cast<SketchAreaWidget*>(m_currentGraphicsView->parent());
}

void MainWindow::updateZoomOptionsNoMatterWhat(double zoom) {
	m_zoomSlider->setValue(zoom);
}

void MainWindow::updateViewZoom(double newZoom) {
	m_comboboxChanged = true;
	if(m_currentGraphicsView) m_currentGraphicsView->absoluteZoom(newZoom);
}


void MainWindow::createStatusBar()
{
    m_statusBar->showMessage(tr("Ready"));
}

void MainWindow::tabWidget_currentChanged(int index) {
	SketchAreaWidget * widgetParent = dynamic_cast<SketchAreaWidget *>(currentTabWidget());
	if (widgetParent == NULL) return;

	m_currentWidget = widgetParent;

	if (m_locationLabel) {
		m_locationLabel->setText("");
	}

	QStatusBar *sb = statusBar();
	connect(sb, SIGNAL(messageChanged(const QString &)), this, SLOT(showStatusMessage(const QString &)));
	widgetParent->addStatusBar(m_statusBar);
	if(sb != m_statusBar) sb->hide();

	if (m_breadboardGraphicsView) m_breadboardGraphicsView->setCurrent(false);
	if (m_schematicGraphicsView) m_schematicGraphicsView->setCurrent(false);
	if (m_pcbGraphicsView) m_pcbGraphicsView->setCurrent(false);

	SketchWidget *widget = qobject_cast<SketchWidget *>(widgetParent->contentView());

	if(m_currentGraphicsView) {
		m_currentGraphicsView->saveZoom(m_zoomSlider->value());
		disconnect(
			m_currentGraphicsView,
			SIGNAL(selectionChangedSignal()),
			this,
			SLOT(updateTransformationActions())
		);
	}
	m_currentGraphicsView = widget;

    if (m_programView) {
        hideShowProgramMenu();
    }

	hideShowTraceMenu();

    if (m_showBreadboardAct) {
	    QList<QAction *> actions;
	    actions << m_showBreadboardAct << m_showSchematicAct << m_showPCBAct;
        if (m_programView) actions << m_showProgramAct;
	    setActionsIcons(index, actions);
    }

	if (widget == NULL) return;

	m_zoomSlider->setValue(m_currentGraphicsView->retrieveZoom());

	connect(
		m_currentGraphicsView,					// don't connect directly to the scene here, connect to the widget's signal
		SIGNAL(selectionChangedSignal()),
		this,
		SLOT(updateTransformationActions())
	);

	updateActiveLayerButtons();

	m_currentGraphicsView->setCurrent(true);

	// !!!!!! hack alert  !!!!!!!  
	// this item update loop seems to deal with a qt update bug:
	// if one view is visible and you change something in another view, 
	// the change might not appear when you switch views until you move the item in question
	foreach(QGraphicsItem * item, m_currentGraphicsView->items()) {
		item->update();
	}

	updateLayerMenu(true);
	updateTraceMenu();
	updateTransformationActions();

	setTitle();

	// triggers a signal to the navigator widget
    if (m_navigators.count() > index) {
	    m_navigators[index]->miniViewMousePressedSlot();
    }
	emit viewSwitched(index);

    if (m_showInViewHelpAct) {
	    if (m_helper == NULL) {
		    m_showInViewHelpAct->setChecked(false);
	    }
	    else {
		    m_showInViewHelpAct->setChecked(m_helper->helpVisible(currentTabIndex()));
	    }
    }

    if (m_infoView) {
	    m_currentGraphicsView->updateInfoView();
    }

	// update issue with 4.5.1?: is this still valid (4.6.x?)
	m_currentGraphicsView->updateConnectors();

}

void MainWindow::setActionsIcons(int index, QList<QAction *> & actions) {
	for (int i = 0; i < actions.count(); i++) {
		actions[i]->setIcon(index == i ? m_dotIcon : m_emptyIcon);
	}
}

void MainWindow::closeEvent(QCloseEvent *event) {
	if (m_dontClose) {
		event->ignore();
		return;
	}

	if (m_programWindow) {
		m_programWindow->close();
		if (m_programWindow->isVisible()) {
			event->ignore();
			return;
		}
	}

	if (!m_closeSilently) {
		bool whatWithAliens = whatToDoWithAlienFiles();
		bool discard;
		if(!beforeClosing(true, discard) || !whatWithAliens ||!m_binManager->beforeClosing()) {
			event->ignore();
			return;
		}

		if(whatWithAliens && m_binManager->hasAlienParts()) {
			m_binManager->createIfMyPartsNotExists();
		}
	}

	//DebugDialog::debug(QString("top level windows: %1").arg(QApplication::topLevelWidgets().size()));
	/*
	foreach (QWidget * widget, QApplication::topLevelWidgets()) {
		QMenu * menu = qobject_cast<QMenu *>(widget);
		if (menu != NULL) {
			continue;				// QMenus are always top level widgets, even if they have parents...
		}
		DebugDialog::debug(QString("top level widget %1 %2 %3")
			.arg(widget->metaObject()->className())
			.arg(widget->windowTitle())
			.arg(widget->toolTip())
			);
	}
	*/

	m_closing = true;

	int count = 0;
	foreach (QWidget *widget, QApplication::topLevelWidgets()) {
		if (widget == this) continue;
		if (qobject_cast<QMainWindow *>(widget) == NULL) continue;

		count++;
	}

	if (count == 0) {
		DebugDialog::closeDebug();
	}

	QSettings settings;
	settings.setValue(m_settingsPrefix + "state",saveState());
	settings.setValue(m_settingsPrefix + "geometry",saveGeometry());

	QMainWindow::closeEvent(event);
}

bool MainWindow::whatToDoWithAlienFiles() {
	if (m_alienFiles.size() > 0) {
        QString basename = QFileInfo(m_fwFilename).fileName();
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, tr("Save %1").arg(basename),
						 m_alienPartsMsg
						 .arg(basename),
						 QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
		// TODO: translate button text
		if (reply == QMessageBox::Yes) {
			return true;
		} else if (reply == QMessageBox::No) {
			foreach(QString pathToRemove, m_alienFiles) {
				QFile::remove(pathToRemove);
			}
			m_alienFiles.clear();
			recoverBackupedFiles();

			emit alienPartsDismissed();
			return true;
		}
		else {
			return false;
		}
	} else {
		return true;
	}
}

void MainWindow::acceptAlienFiles() {
	m_alienFiles.clear();
}

bool MainWindow::eventFilter(QObject *object, QEvent *event) {
	if (object == this &&
		(event->type() == QEvent::KeyPress
		// || event->type() == QEvent::KeyRelease
		|| event->type() == QEvent::ShortcutOverride))
	{
		//DebugDialog::debug(QString("event filter %1").arg(event->type()) );
		updatePartMenu();
		updateTraceMenu();

		// On the mac, the first time the delete key is pressed, to be used as a shortcut for QAction m_deleteAct,
		// for some reason, the enabling of the m_deleteAct in UpdateEditMenu doesn't "take" until the next time the event loop is processed
		// Thereafter, the delete key works as it should.
		// So this call to processEvents() makes sure m_deleteAct is enabled.
		ProcessEventBlocker::processEvents();
	}

	return QMainWindow::eventFilter(object, event);
}

const QString MainWindow::untitledFileName() {
	return UntitledSketchName;
}

int &MainWindow::untitledFileCount() {
	return UntitledSketchIndex;
}

const QString MainWindow::fileExtension() {
	return FritzingBundleExtension;
}

const QString MainWindow::defaultSaveFolder() {
	return FolderUtils::openSaveFolder();
}

bool MainWindow::undoStackIsEmpty() {
	return m_undoStack->count() == 0;
}

void MainWindow::setInfoViewOnHover(bool infoViewOnHover) {
	m_breadboardGraphicsView->setInfoViewOnHover(infoViewOnHover);
	m_schematicGraphicsView->setInfoViewOnHover(infoViewOnHover);
	m_pcbGraphicsView->setInfoViewOnHover(infoViewOnHover);

	m_binManager->setInfoViewOnHover(infoViewOnHover);
}

void MainWindow::loadBundledSketch(const QString &fileName, bool addToRecent, bool setAsLastOpened, bool checkObsolete) {

    QString error;
	if(!FolderUtils::unzipTo(fileName, m_fzzFolder, error)) {
		FMessageBox::warning(
			this,
			tr("Fritzing"),
			tr("Unable to open '%1': %2").arg(fileName).arg(error)
		);

		return;
	}

	QDir dir(m_fzzFolder);
    QString binFileName = dir.absoluteFilePath(QFileInfo(BinManager::TempPartsBinTemplateLocation).fileName());
    m_binManager->setTempPartsBinLocation(binFileName);
    FolderUtils::copyBin(binFileName, BinManager::TempPartsBinTemplateLocation);

	QStringList namefilters;
	namefilters << "*"+FritzingSketchExtension;
	QFileInfoList entryInfoList = dir.entryInfoList(namefilters);
	if (entryInfoList.count() == 0) {
		FMessageBox::warning(
			this,
			tr("Fritzing"),
			tr("No Sketch found in '%1'").arg(fileName)
		);

		return;
	}

	QFileInfo sketchInfo = entryInfoList[0];

	QString sketchName = dir.absoluteFilePath(sketchInfo.fileName());

    namefilters.clear();
    namefilters << "*" + FritzingPartExtension;
    entryInfoList = dir.entryInfoList(namefilters);

    namefilters.clear();
	namefilters << "*.svg";
	QFileInfoList svgEntryInfoList = dir.entryInfoList(namefilters);

    m_addedToTemp = false;

    QList<MissingSvgInfo> missing;
    QList<ModelPart *> missingModelParts;

    foreach (QFileInfo fzpInfo, entryInfoList) {
        QFile file(dir.absoluteFilePath(fzpInfo.fileName()));
        if (!file.open(QFile::ReadOnly)) {
            DebugDialog::debug(QString("unable to open %1: %2").arg(file.fileName()));
            continue;
        }

        // TODO: could be more efficient by using a streamreader
        QString fzp = file.readAll();
        file.close();

        QString moduleID = TextUtils::parseForModuleID(fzp);
        if (moduleID.isEmpty()) {
            DebugDialog::debug("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            DebugDialog::debug(QString("unable to find module id in %1: %2").arg(file.fileName()).arg(fzp));
            DebugDialog::debug("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            continue;
        }

        ModelPart * mp = m_referenceModel->retrieveModelPart(moduleID);
        if (mp == NULL) {
            QDomDocument doc;
            if (!doc.setContent(fzp)) {
                DebugDialog::debug(QString("unable to parse fzp in %1: %2").arg(file.fileName()).arg(fzp));
                continue;
            }

            mp = copyToPartsFolder(fzpInfo, false, PartFactory::folderPath(), "contrib");
            if (mp == NULL) {
                DebugDialog::debug(QString("unable to create model part in %1: %2").arg(file.fileName()).arg(fzp));
                continue;
            }

            QDomElement root = doc.documentElement();
	        QDomElement views = root.firstChildElement("views");
	        QDomElement view = views.firstChildElement();
            while (!view.isNull()) {
                QDomElement layers = view.firstChildElement("layers");
                QString path = layers.attribute("image", "");
                if (!path.isEmpty()) {
                    bool copied = copySvg(path, svgEntryInfoList);
                    if (!copied) {
                        DebugDialog::debug(QString("missing svg %1").arg(path));
                        MissingSvgInfo msi;
                        msi.equal = false;
                        msi.modelPart = mp;
                        missingModelParts << mp;
                        msi.requestedPath = path;
                        ViewLayer::ViewID viewID = ViewLayer::idFromXmlName(view.tagName());
                        QDomElement connectors = root.firstChildElement("connectors");
                        QDomElement connector = connectors.firstChildElement("connector");
                        while (!connector.isNull()) {
                            QString id, terminalID;
                            ViewLayer::getConnectorSvgIDs(connector, viewID, id, terminalID);
                            if (!id.isEmpty()) {
                                msi.connectorSvgIds.append(id);
                            }
                            connector = connector.nextSiblingElement("connector");
                        }
                        missing << msi;
                    }
                }
                view = view.nextSiblingElement();
            }

        	mp->setFzz(true);
        }

        if (!missingModelParts.contains(mp)) {
		    m_binManager->addToTempPartsBin(mp);
            m_addedToTemp = true;
        }
    }

    qSort(missing.begin(), missing.end(), byConnectorCount);
    foreach (MissingSvgInfo msi, missing) {
        if (msi.equal) {
            // two or more parts have the same number of connectors--so we can't figure out how to assign them
            continue;
        }

        int slash = msi.requestedPath.indexOf("/");
        QString suffix = msi.requestedPath.mid(slash + 1);
        QString prefix = msi.requestedPath.left(slash);
        for (int jx = svgEntryInfoList.count() - 1; jx >= 0; jx--) {
            QFileInfo svgInfo = svgEntryInfoList.at(jx);
            if (!svgInfo.fileName().contains(prefix, Qt::CaseInsensitive)) continue;

            QFile svgfile(svgInfo.absoluteFilePath());
            QDomDocument svgDoc;
            if (!svgDoc.setContent(&svgfile)) continue;

            QList<QDomElement> elements;
            QDomElement root = svgDoc.documentElement();
            TextUtils::findElementsWithAttribute(root, "id", elements);
            if (elements.count() < msi.connectorSvgIds.count()) continue;

            QStringList ids;
            foreach (QDomElement element, elements) {
                ids << element.attribute("id");
            }

            bool allGood = true;
            foreach (QString id, msi.connectorSvgIds) {
                if (!ids.contains(id)) {
                    allGood = false;
                    break;
                }
            }

            if (!allGood) continue;

            QString destPath = copyToSvgFolder(svgInfo, false, PartFactory::folderPath(), "contrib");   // copy file with original name
            if (!destPath.isEmpty()) {
                QFileInfo destInfo(destPath);
                QFile file(destPath);
                DebugDialog::debug(QString("found missing %1").arg(destPath));
                FolderUtils::slamCopy(file, destInfo.absoluteDir().absoluteFilePath(suffix));         // make another copy that has the name used in the fzp file
                svgEntryInfoList.removeAt(jx);
                break;
            }
        }
    }

    foreach (ModelPart * mp, missingModelParts) {
		m_binManager->addToTempPartsBin(mp);
        m_addedToTemp = true;
    }

	if (!m_addedToTemp) {
		m_binManager->hideTempPartsBin();
	}

	// the bundled itself
	this->mainLoad(sketchName, "", checkObsolete);
	setCurrentFile(fileName, addToRecent, setAsLastOpened);
}

bool MainWindow::copySvg(const QString & path, QFileInfoList & svgEntryInfoList) 
{
    int slash = path.indexOf("/");
    QString subpath = path.mid(slash + 1);
    bool gotOne = false;
    for (int jx = svgEntryInfoList.count() - 1; jx >= 0; jx--) {
        QFileInfo svgInfo = svgEntryInfoList.at(jx);
        if (svgInfo.fileName().contains(subpath)) {
            copyToSvgFolder(svgInfo, false, PartFactory::folderPath(), "contrib");
            svgEntryInfoList.removeAt(jx);
            // jrc 30 oct 2012: not sure why we can't just return at this point--can there be other matching files?
            gotOne = true;
        }
    }

    if (gotOne) return true;

    // deal with a bug in which all the svg files exist in the fzz but the fz file points to the wrong name
    // most of the time it's just a GUID difference

    DebugDialog::debug(QString("svg matching fz path %1 not found").arg(path));
    int guidix = GuidMatcher.lastIndexIn(subpath);
    if (guidix < 0) return false;

    QString originalGuid = GuidMatcher.cap(0);
    QString tryPath = subpath;
    tryPath.replace(guidix, originalGuid.length(), "%%%%");
    for (int jx = svgEntryInfoList.count() - 1; jx >= 0; jx--) {
        QFileInfo svgInfo = svgEntryInfoList.at(jx);
        QString tempPath = svgInfo.fileName();
        guidix = GuidMatcher.lastIndexIn(tempPath);
        if (guidix < 0) continue;

        tempPath.replace(guidix, GuidMatcher.cap(0).length(), "%%%%");
        if (!tempPath.contains(tryPath)) continue;

        QString destPath = copyToSvgFolder(svgInfo, false, PartFactory::folderPath(), "contrib");
        if (!destPath.isEmpty()) {
            QFile file(destPath);
            guidix = GuidMatcher.lastIndexIn(destPath);
            destPath.replace(guidix, GuidMatcher.cap(0).length(), originalGuid);
            FolderUtils::slamCopy(file, destPath);
            DebugDialog::debug(QString("found matching svg %1").arg(destPath));
            svgEntryInfoList.removeAt(jx);
            return true;
        }
    }

    return false;

}


bool MainWindow::loadBundledNonAtomicEntity(const QString &fileName, Bundler* bundler, bool addToBin, bool dontAsk) {
	QDir destFolder = QDir::temp();

	FolderUtils::createFolderAnCdIntoIt(destFolder, TextUtils::getRandText());
	QString unzipDirPath = destFolder.path();

    QString error;
	if(!FolderUtils::unzipTo(fileName, unzipDirPath, error)) {
		FMessageBox::warning(
			this,
			tr("Fritzing"),
			tr("Unable to open shareable '%1': %2").arg(fileName).arg(error)
		);

		// gotta return now, or loadBundledSketchAux will crash
		return false;
	}

	QDir unzipDir(unzipDirPath);

	if (bundler->preloadBundledAux(unzipDir, dontAsk)) {
		QList<ModelPart*> mps = moveToPartsFolder(unzipDir,this,addToBin,true,FolderUtils::getUserDataStorePath("parts"), "contrib", false);
		// the bundled itself
		bundler->loadBundledAux(unzipDir,mps);
	}

	FolderUtils::rmdir(unzipDirPath);

    return true;
}

/*
void MainWindow::loadBundledPartFromWeb() {
	QMainWindow *mw = new QMainWindow();
	QString url = "http://localhost:8081/parts_gen/choose";
	QWebView *view = new QWebView(mw);
	mw->setCentralWidget(view);
	view->setUrl(QUrl(url));
	mw->show();
	mw->raise();
}
*/

ModelPart* MainWindow::loadBundledPart(const QString &fileName, bool addToBin) {
	QDir destFolder = QDir::temp();

	FolderUtils::createFolderAnCdIntoIt(destFolder, TextUtils::getRandText());
	QString unzipDirPath = destFolder.path();

    QString error;
	if(!FolderUtils::unzipTo(fileName, unzipDirPath, error)) {
		FMessageBox::warning(
			this,
			tr("Fritzing"),
			tr("Unable to open shareable part '%1': %2").arg(fileName).arg(error)
		);
		return NULL;
	}

	QDir unzipDir(unzipDirPath);

    QList<ModelPart*> mps;
    try {
	    mps = moveToPartsFolder(unzipDir, this, addToBin, true, FolderUtils::getUserDataStorePath("parts"), "user", true);
    }
    catch (const QString & msg) {
		FMessageBox::warning(
			this,
			tr("Fritzing"),
			msg
		);
        return NULL;
    }

	if (mps.count() != 1) {
		// if this fails, that means that the bundled was wrong
		FMessageBox::warning(
			this,
			tr("Fritzing"),
			tr("Unable to load part from '%1'").arg(fileName)
		);
		return NULL;
	}

	FolderUtils::rmdir(unzipDirPath);

	return mps[0];
}

void MainWindow::saveBundledPart(const QString &moduleId) {
	QString modIdToExport;
	ModelPart* mp;

	if(moduleId.isEmpty()) {
		if (m_currentGraphicsView == NULL) return;
		PaletteItem *selectedPart = m_currentGraphicsView->getSelectedPart();
		mp = selectedPart->modelPart();
		modIdToExport = mp->moduleID();
	} else {
		modIdToExport = moduleId;
		mp = m_referenceModel->retrieveModelPart(moduleId);
	}
	QString partTitle = mp->title();

	QString fileExt;
	QString path = defaultSaveFolder()+"/"+partTitle+FritzingBundledPartExtension;
	QString bundledFileName = FolderUtils::getSaveFileName(
			this,
			tr("Specify a file name"),
			path,
			tr("Fritzing Part (*%1)").arg(FritzingBundledPartExtension),
			&fileExt
		  );

	if (bundledFileName.isEmpty()) return; // Cancel pressed

	if(!alreadyHasExtension(bundledFileName, FritzingBundledPartExtension)) {
		bundledFileName += FritzingBundledPartExtension;
	}

	QDir destFolder = QDir::temp();

	FolderUtils::createFolderAnCdIntoIt(destFolder, TextUtils::getRandText());
	QString dirToRemove = destFolder.path();

	QString aux = QFileInfo(bundledFileName).fileName();
	QString destPartPath = // remove the last "z" from the extension
			destFolder.path()+"/"+aux.left(aux.size()-1);
	DebugDialog::debug("saving part temporarily to "+destPartPath);

	//bool wasModified = isWindowModified();
	//QString prevFileName = m_fileName;
	//saveAsAux(destPartPath);
	//m_fileName = prevFileName;
	//setWindowModified(wasModified);
	setTitle();

	saveBundledAux(mp, destFolder);

    QStringList skipSuffixes;
	if(!FolderUtils::createZipAndSaveTo(destFolder, bundledFileName, skipSuffixes)) {
		FMessageBox::warning(
			this,
			tr("Fritzing"),
			tr("Unable to export %1 to shareable sketch").arg(bundledFileName)
		);
	}

	FolderUtils::rmdir(dirToRemove);
}

QStringList MainWindow::saveBundledAux(ModelPart *mp, const QDir &destFolder) {
	QStringList names;
	QString partPath = mp->path();
	QFile file(partPath);
	QString fn = ZIP_PART + QFileInfo(partPath).fileName();
	names << fn;
    FolderUtils::slamCopy(file, destFolder.path()+"/"+fn);

	QList<ViewLayer::ViewID> viewIDs;
	viewIDs << ViewLayer::IconView << ViewLayer::BreadboardView << ViewLayer::SchematicView << ViewLayer::PCBView;
	foreach (ViewLayer::ViewID viewID, viewIDs) {
		QString basename = mp->hasBaseNameFor(viewID);
		if (basename.isEmpty()) continue;

		QString filename = PartFactory::getSvgFilename(mp, basename, true, true);
		if (filename.isEmpty()) continue;

		QFile file(filename);
		basename.replace("/", ".");
		QString fn = ZIP_SVG + basename;
		names << fn;
        FolderUtils::slamCopy(file, destFolder.path()+"/"+fn);
	}

	return names;
}

QList<ModelPart*> MainWindow::moveToPartsFolder(QDir &unzipDir, MainWindow* mw, bool addToBin, bool addToAlien, const QString & prefixFolder, const QString &destFolder, bool importingSinglePart) {
	QStringList namefilters;
	QList<ModelPart*> retval;

	if (mw == NULL) {
		throw tr("MainWindow::moveToPartsFolder mainwindow missing");
	}

    namefilters.clear();
	namefilters << ZIP_PART+"*"; 
    QList<QFileInfo> partEntryInfoList = unzipDir.entryInfoList(namefilters);

    if (importingSinglePart && partEntryInfoList.count() > 0) {
        // TODO use a stream reader
        QFile file(partEntryInfoList[0].absoluteFilePath());
        file.open(QFile::ReadOnly);
        QString fzp = file.readAll();
        file.close();
        QString moduleID = TextUtils::parseForModuleID(fzp);
        if (!moduleID.isEmpty() && m_referenceModel->retrieveModelPart(moduleID) != NULL) {
            throw tr("There is already a part with id '%1' loaded into Fritzing.").arg(moduleID);
        }
    }


	namefilters.clear();
    namefilters << ZIP_SVG+"*";
	foreach(QFileInfo file, unzipDir.entryInfoList(namefilters)) { // svg files
		//DebugDialog::debug("unzip svg " + file.absoluteFilePath());
		mw->copyToSvgFolder(file, addToAlien, prefixFolder, destFolder);
	}


	foreach(QFileInfo file, partEntryInfoList) { // part files
		//DebugDialog::debug("unzip part " + file.absoluteFilePath());
        ModelPart * mp = mw->copyToPartsFolder(file, addToAlien, prefixFolder, destFolder);
		retval << mp;
        if (addToBin) {
            // should only be here when adding single new part
		    m_binManager->addToMyParts(mp);
	    }
	}



	return retval;
}

QString MainWindow::copyToSvgFolder(const QFileInfo& file, bool addToAlien, const QString & prefixFolder, const QString &destFolder) {
	QFile svgfile(file.filePath());
	// let's make sure that we remove just the suffix
	QString fileName = file.fileName().remove(QRegExp("^"+ZIP_SVG));
	QString viewFolder = fileName.left(fileName.indexOf("."));
	fileName.remove(0, viewFolder.length() + 1);

	QString destFilePath =
		prefixFolder+"/svg/"+destFolder+"/"+viewFolder+"/"+fileName;

	backupExistingFileIfExists(destFilePath);
	if(FolderUtils::slamCopy(svgfile, destFilePath)) {
		if (addToAlien) {
			m_alienFiles << destFilePath;
		}
        return destFilePath;
	}

    return "";
}

ModelPart* MainWindow::copyToPartsFolder(const QFileInfo& file, bool addToAlien, const QString & prefixFolder, const QString &destFolder) {
	QFile partfile(file.filePath());
	// let's make sure that we remove just the suffix
	QString destFilePath =
		prefixFolder+"/"+destFolder+"/"+file.fileName().remove(QRegExp("^"+ZIP_PART));

	backupExistingFileIfExists(destFilePath);
	if(FolderUtils::slamCopy(partfile, destFilePath)) {
		if (addToAlien) {
			m_alienFiles << destFilePath;
			m_alienPartsMsg = tr("Do you want to keep the imported parts?");
		}
	}
	ModelPart *mp = m_referenceModel->loadPart(destFilePath, true);
	mp->setAlien(true);

	return mp;
}

void MainWindow::binSaved(bool hasPartsFromBundled) {
	if(hasPartsFromBundled) {
		// the bin will need those parts, so just keep them
		m_alienFiles.clear();
		resetTempFolder();
	}
}

#undef ZIP_PART
#undef ZIP_SVG


void MainWindow::backupExistingFileIfExists(const QString &destFilePath) {
	if(QFileInfo(destFilePath).exists()) {
		if(m_tempDir.path() == ".") {
			m_tempDir = QDir::temp();
			FolderUtils::createFolderAnCdIntoIt(m_tempDir, TextUtils::getRandText());
			DebugDialog::debug("debug folder for overwritten files: "+m_tempDir.path());
		}

		QString fileBackupName = QFileInfo(destFilePath).fileName();
		m_filesReplacedByAlienOnes << destFilePath;
		QFile file(destFilePath);
		bool alreadyExists = file.exists();
		FolderUtils::slamCopy(file, m_tempDir.path()+"/"+fileBackupName);

		if(alreadyExists) {
			file.remove(destFilePath);
		}
	}
}

void MainWindow::recoverBackupedFiles() {
	foreach(QString originalFilePath, m_filesReplacedByAlienOnes) {
		QFile file(m_tempDir.path()+"/"+QFileInfo(originalFilePath).fileName());
		if(file.exists(originalFilePath)) {
			file.remove();
		}
		FolderUtils::slamCopy(file, originalFilePath);
	}
	resetTempFolder();
}

void MainWindow::resetTempFolder() {
	if(m_tempDir.path() != ".") {
		FolderUtils::rmdir(m_tempDir);
		m_tempDir = QDir::temp();
	}
	m_filesReplacedByAlienOnes.clear();
}

void MainWindow::routingStatusSlot(SketchWidget * sketchWidget, const RoutingStatus & routingStatus) {
	m_routingStatus = routingStatus;
	QString theText;
	if (routingStatus.m_netCount == 0) {
		theText = tr("No connections to route");
	} else if (routingStatus.m_netCount == routingStatus.m_netRoutedCount) {
		if (routingStatus.m_jumperItemCount == 0) {
			theText = tr("Routing completed");
		}
		else {
			theText = tr("Routing completed using %n jumper part(s)", "", routingStatus.m_jumperItemCount);
		}
	} else {
		theText = tr("%1 of %2 nets routed - %n connector(s) still to be routed", "", routingStatus.m_connectorsLeftToRoute)
			.arg(routingStatus.m_netRoutedCount)
			.arg(routingStatus.m_netCount);
	}

	dynamic_cast<SketchAreaWidget *>(sketchWidget->parent())->routingStatusLabel()->setLabelText(theText);

	updateTraceMenu();
}

void MainWindow::applyReadOnlyChange(bool isReadOnly) {
	Q_UNUSED(isReadOnly);
	//m_saveAct->setDisabled(isReadOnly);
}

void MainWindow::currentNavigatorChanged(MiniViewContainer * miniView)
{
	int index = m_navigators.indexOf(miniView);
	if (index < 0) return;

	int oldIndex = currentTabIndex();
	if (oldIndex == index) return;

	setCurrentTabIndex(index);
}

void MainWindow::viewSwitchedTo(int viewIndex) {
	setCurrentTabIndex(viewIndex);
}

const QString MainWindow::fritzingTitle() {
	if (m_currentGraphicsView == NULL) {
		return FritzingWindow::fritzingTitle();
	}

	QString fritzing = FritzingWindow::fritzingTitle();
	return tr("%1 - [%2]").arg(fritzing).arg(m_currentGraphicsView->viewName());
}

QAction *MainWindow::raiseWindowAction() {
	return m_raiseWindowAct;
}

void MainWindow::raiseAndActivate() {
	if(isMinimized()) {
		showNormal();
	}
	raise();
	QTimer::singleShot(20, this, SLOT(activateWindowAux()));
}

void MainWindow::activateWindowAux() {
	activateWindow();
}

void MainWindow::updateRaiseWindowAction() {
	QString actionText;
	QFileInfo fileInfo(m_fwFilename);
	if(fileInfo.exists()) {
		int lastSlashIdx = m_fwFilename.lastIndexOf("/");
		int beforeLastSlashIdx = m_fwFilename.left(lastSlashIdx).lastIndexOf("/");
		actionText = beforeLastSlashIdx > -1 && lastSlashIdx > -1 ? "..." : "";
		actionText += m_fwFilename.right(m_fwFilename.size()-beforeLastSlashIdx-1);
	} else {
		actionText = m_fwFilename;
	}
	m_raiseWindowAct->setText(actionText);
	m_raiseWindowAct->setToolTip(m_fwFilename);
	m_raiseWindowAct->setStatusTip("raise \""+m_fwFilename+"\" window");
}

QSizeGrip *MainWindow::sizeGrip() {
	return m_sizeGrip;
}

QStatusBar *MainWindow::realStatusBar() {
	return m_statusBar;
}

void MainWindow::moveEvent(QMoveEvent * event) {
	FritzingWindow::moveEvent(event);
	emit mainWindowMoved(this);
}

bool MainWindow::event(QEvent * e) {
	switch (e->type()) {
		case QEvent::WindowActivate:
			emit changeActivationSignal(true, this);
			break;
		case QEvent::WindowDeactivate:
			emit changeActivationSignal(false, this);
			break;
		default:
			break;
	}
	return FritzingWindow::event(e);
}

void MainWindow::resizeEvent(QResizeEvent * event) {
    if (m_sizeGrip) {
	    m_sizeGrip->rearrange();
    }
	FritzingWindow::resizeEvent(event);
}

void MainWindow::showInViewHelp() {
	//delete m_helper;
	if (m_helper == NULL) {
		m_helper = new Helper(this, true);
		return;
	}

	bool toggle = !m_helper->helpVisible(currentTabIndex());
	showAllFirstTimeHelp(toggle);

	/*
	m_helper->toggleHelpVisibility(currentTabIndex());
	*/

	m_showInViewHelpAct->setChecked(m_helper->helpVisible(currentTabIndex()));
}


void MainWindow::showAllFirstTimeHelp(bool show) {
	if (m_helper) {
		for (int i = 0; i < 3; i++) {
			m_helper->setHelpVisibility(i, show);
		}
	}
	m_showInViewHelpAct->setChecked(show);
}

void MainWindow::enableCheckUpdates(bool enabled)
{
	if (m_checkForUpdatesAct != NULL) {
		m_checkForUpdatesAct->setEnabled(enabled);
	}
}


void MainWindow::swapSelectedDelay(const QString & family, const QString & prop, QMap<QString, QString> & currPropsMap, ItemBase * itemBase) 
{
	m_swapTimer.stop();
	m_swapTimer.setAll(family, prop, currPropsMap, itemBase);
	m_swapTimer.start();
}

void MainWindow::swapSelectedTimeout()
{
	if (sender() == &m_swapTimer) {
		QMap<QString, QString> map =  m_swapTimer.propsMap();
		swapSelectedMap(m_swapTimer.family(), m_swapTimer.prop(), map, m_swapTimer.itemBase());
	}
}

void MainWindow::swapSelectedMap(const QString & family, const QString & prop, QMap<QString, QString> & currPropsMap, ItemBase * itemBase) 
{
	if (itemBase == NULL) return;

	QString generatedModuleID = currPropsMap.value("moduleID");

	if (generatedModuleID.isEmpty()) {
        if (prop.compare("layer") == 0) {
	        if (family.compare("logo") == 0 || family.compare("pad") == 0 || family.contains("blocker", Qt::CaseInsensitive)) {
		        QString value = Board::convertToXmlName(currPropsMap.value(prop));
		        if (value.contains("copper1") && m_currentGraphicsView->boardLayers() == 1) {
			        QMessageBox::warning(
				        this,
				        tr("No copper top layer"),
				        tr("The copper top (copper 1) layer is not available on a one-sided board.  Please switch the board to double-sided or choose the copper bottom (copper 0) layer.")
			        );
			        return;
		        }
                // use the xml name
                currPropsMap.insert(prop, value);
	        }
            else if (itemBase->itemType() == ModelPart::Wire) {
                // assume this option is disabled for a one-sided board, so we would not get here?
                m_pcbGraphicsView->changeTraceLayer(itemBase, false, NULL);
                return;
            }
        }
    }

	if (generatedModuleID.isEmpty()) {
		if (family.compare("Prototyping Board", Qt::CaseInsensitive) == 0) {
			if (prop.compare("size", Qt::CaseInsensitive) == 0 || prop.compare("type", Qt::CaseInsensitive) == 0) {
				QString size = currPropsMap.value("size");
				QString type = currPropsMap.value("type");
				if (type.compare("perfboard", Qt::CaseInsensitive) == 0) {
					generatedModuleID = Perfboard::genModuleID(currPropsMap);
				}
				else if (type.compare("stripboard", Qt::CaseInsensitive) == 0) {
					generatedModuleID = Stripboard::genModuleID(currPropsMap);
				}
			}
		}
	}

    bool swapLayer = false;
    ViewLayer::ViewLayerPlacement newViewLayerPlacement = ViewLayer::UnknownPlacement;
    if (prop.compare("layer") == 0) {
        if (itemBase->modelPart()->flippedSMD() || itemBase->itemType() == ModelPart::Part) {
            ItemBase * viewItem = itemBase->modelPart()->viewItem(ViewLayer::PCBView);
            if (viewItem) {
                ViewLayer::ViewLayerPlacement vlp = (currPropsMap.value(prop) == ItemBase::TranslatedPropertyNames.value("bottom") ? ViewLayer::NewBottom : ViewLayer::NewTop);
                if (viewItem->viewLayerPlacement() != newViewLayerPlacement) {
                    swapLayer = true;
                    newViewLayerPlacement = vlp;
                }
            }
        }
    }

	if (!generatedModuleID.isEmpty()) {
		ModelPart * modelPart = m_referenceModel->retrieveModelPart(generatedModuleID);
		if (modelPart == NULL) {
			if (!m_referenceModel->genFZP(generatedModuleID, m_referenceModel)) {
				return;
			}
		}

		swapSelectedAux(itemBase->layerKinChief(), generatedModuleID, swapLayer, newViewLayerPlacement, currPropsMap);
		return;
	}

    if (swapLayer) {
        swapSelectedAux(itemBase->layerKinChief(), itemBase->moduleID(), true, newViewLayerPlacement, currPropsMap);
        return;
    }

	if ((prop.compare("package", Qt::CaseSensitive) != 0) && swapSpecial(prop, currPropsMap)) {
		return;
	}

	foreach (QString key, currPropsMap.keys()) {
		QString value = currPropsMap.value(key);
		m_referenceModel->recordProperty(key, value);
	}

	QString moduleID = m_referenceModel->retrieveModuleIdWith(family, prop, true);
	bool exactMatch = m_referenceModel->lastWasExactMatch();

	if (moduleID.isEmpty()) {
		QMessageBox::information(
			this,
			tr("Sorry!"),
			tr(
			 "No part with those characteristics.\n"
			 "We're working to avoid this message, and only let you choose between properties that do exist")
		);
		return;
	}



	itemBase = itemBase->layerKinChief();

	if(!exactMatch) {
		AutoCloseMessageBox::showMessage(this, tr("No exactly matching part found; Fritzing chose the closest match."));
	}

	swapSelectedAux(itemBase, moduleID, false, ViewLayer::UnknownPlacement, currPropsMap);
}

bool MainWindow::swapSpecial(const QString & theProp, QMap<QString, QString> & currPropsMap) {
	ItemBase * itemBase = m_infoView->currentItem();
	QString pinSpacing, resistance;
    int layers = 0;

	foreach (QString key, currPropsMap.keys()) {
		if (key.compare("layers", Qt::CaseInsensitive) == 0) {
			if (!Board::isBoard(itemBase)) continue;

			QString value = currPropsMap.value(key, "");
			if (value.compare(Board::OneLayerTranslated) == 0) {
				layers = 1;
			}
			else if (value.compare(Board::TwoLayersTranslated) == 0) {
				layers = 2;
			}
        }

		if (key.compare("resistance", Qt::CaseInsensitive) == 0) {
			resistance = currPropsMap.value(key);
			continue;
		}
		if (key.compare("pin spacing", Qt::CaseInsensitive) == 0) {
			pinSpacing = currPropsMap.value(key);
			continue;
		}
	}

    if (layers != 0) {
        currPropsMap.insert("layers", QString::number(layers));
        if (theProp.compare("layers") == 0) {
            QString msg = (layers == 1) ? tr("Change to single layer pcb") : tr("Change to two layer pcb");
            swapLayers(itemBase, layers, msg, SketchWidget::PropChangeDelay);
            return true;
        }
	}

	if (!resistance.isEmpty() || !pinSpacing.isEmpty()) {
		if (theProp.contains("band", Qt::CaseInsensitive)) {
			// swap 4band for 5band or vice versa.
			return false;
		}

		Resistor * resistor = qobject_cast<Resistor *>(itemBase);
		if (resistor != NULL) {
			m_currentGraphicsView->setResistance(resistance, pinSpacing);
			return true;
		}
	}

	return false;
}

void MainWindow::swapLayers(ItemBase * itemBase, int layers, const QString & msg, int delay) {
    QUndoCommand* parentCommand = new QUndoCommand(msg);
	new CleanUpWiresCommand(m_breadboardGraphicsView, CleanUpWiresCommand::UndoOnly, parentCommand);
	new CleanUpRatsnestsCommand(m_breadboardGraphicsView, CleanUpWiresCommand::UndoOnly, parentCommand);
    m_pcbGraphicsView->swapLayers(itemBase, layers, parentCommand);
	// need to defer execution so the content of the info view doesn't change during an event that started in the info view
	m_undoStack->waitPush(parentCommand, delay);
}

void MainWindow::swapSelectedAux(ItemBase * itemBase, const QString & moduleID, bool useViewLayerPlacement, ViewLayer::ViewLayerPlacement overrideViewLayerPlacement,  QMap<QString, QString> & propsMap) {

	QUndoCommand* parentCommand = new QUndoCommand(tr("Swapped %1 with module %2").arg(itemBase->instanceTitle()).arg(moduleID));
	new CleanUpWiresCommand(m_breadboardGraphicsView, CleanUpWiresCommand::UndoOnly, parentCommand);
	new CleanUpRatsnestsCommand(m_breadboardGraphicsView, CleanUpWiresCommand::UndoOnly, parentCommand);

    ViewLayer::ViewLayerPlacement viewLayerPlacement = itemBase->viewLayerPlacement();
    ModelPart * modelPart = m_referenceModel->retrieveModelPart(moduleID);
    if (m_pcbGraphicsView->boardLayers() == 2) {
        if (modelPart->flippedSMD()) {
            //viewLayerPlacement = m_pcbGraphicsView->dropOnBottom() ? ViewLayer::NewBottom : ViewLayer::NewTop;
            if (useViewLayerPlacement) viewLayerPlacement = overrideViewLayerPlacement;
        }
        else if (modelPart->itemType() == ModelPart::Part) {
            //viewLayerPlacement = m_pcbGraphicsView->dropOnBottom() ? ViewLayer::NewBottom : ViewLayer::NewTop;
            if (useViewLayerPlacement) viewLayerPlacement = overrideViewLayerPlacement;
        }
    }
    else {
        if (modelPart->flippedSMD()) {
            viewLayerPlacement = ViewLayer::NewBottom;
        }
        else if (modelPart->itemType() == ModelPart::Part) {
            //viewLayerPlacement = m_pcbGraphicsView->dropOnBottom() ? ViewLayer::NewBottom : ViewLayer::NewTop;
            if (useViewLayerPlacement) viewLayerPlacement = overrideViewLayerPlacement;
        }
    }

	swapSelectedAuxAux(itemBase, moduleID, viewLayerPlacement, propsMap, parentCommand);
    
	// need to defer execution so the content of the info view doesn't change during an event that started in the info view
	m_undoStack->waitPush(parentCommand, SketchWidget::PropChangeDelay);

}

void MainWindow::swapBoardImageSlot(SketchWidget * sketchWidget, ItemBase * itemBase, const QString & filename, const QString & moduleID, bool addName) {

	QUndoCommand* parentCommand = new QUndoCommand(tr("Change image to %2").arg(filename));
    QMap<QString, QString> propsMap;
	long newID = swapSelectedAuxAux(itemBase, moduleID, itemBase->viewLayerPlacement(), propsMap, parentCommand);

    LoadLogoImageCommand * cmd = new LoadLogoImageCommand(sketchWidget, newID, "", QSizeF(0,0), filename, filename, addName, parentCommand);
    cmd->setRedoOnly();
   
	// need to defer execution so the content of the info view doesn't change during an event that started in the info view
	m_undoStack->waitPush(parentCommand, SketchWidget::PropChangeDelay);
}


void MainWindow::subSwapSlot(SketchWidget * sketchWidget, ItemBase * itemBase, const QString & newModuleID, ViewLayer::ViewLayerPlacement viewLayerPlacement, long & newID, QUndoCommand * parentCommand) {
	Q_UNUSED(sketchWidget);
    QMap<QString, QString> propsMap;
	newID = swapSelectedAuxAux(itemBase, newModuleID, viewLayerPlacement, propsMap, parentCommand);
}

long MainWindow::swapSelectedAuxAux(ItemBase * itemBase, const QString & moduleID,  ViewLayer::ViewLayerPlacement viewLayerPlacement, QMap<QString, QString> & propsMap, QUndoCommand * parentCommand) 
{
	long modelIndex = ModelPart::nextIndex();

	QList<SketchWidget *> sketchWidgets;

    // master view must go last, since it creates the delete command, and possibly has all the local props
    switch (itemBase->viewID()) {
        case ViewLayer::SchematicView:
		    sketchWidgets << m_pcbGraphicsView << m_breadboardGraphicsView << m_schematicGraphicsView;
            break;
        case ViewLayer::PCBView:
            sketchWidgets << m_schematicGraphicsView << m_breadboardGraphicsView << m_pcbGraphicsView;
            break;
        default:
            sketchWidgets << m_schematicGraphicsView << m_pcbGraphicsView << m_breadboardGraphicsView;
            break;
	}

    SwapThing swapThing;
    swapThing.firstTime = true;
    swapThing.itemBase = itemBase;
    swapThing.newModelIndex = modelIndex;
    swapThing.newModuleID = moduleID;
    swapThing.viewLayerPlacement = viewLayerPlacement;
    swapThing.parentCommand = parentCommand;
    swapThing.propsMap = propsMap;

    long newID = 0;
    for (int i = 0; i < 3; i++) {
        long tempID = sketchWidgets[i]->setUpSwap(swapThing, i == 2);
        if (newID == 0 && tempID != 0) newID = tempID;
    }

	// TODO:  z-order?

	return newID;
}

void MainWindow::svgMissingLayer(const QString & layername, const QString & path) {
	QMessageBox::warning(
		this,
		tr("Fritzing"),
		tr("Svg %1 is missing a '%2' layer. "
			"For more information on how to create a custom board shape, "
			"see the tutorial at <a href='http://fritzing.org/learning/tutorials/designing-pcb/pcb-custom-shape/'>http://fritzing.org/learning/tutorials/designing-pcb/pcb-custom-shape/</a>.")
		.arg(path)
		.arg(layername)
	);
}

void MainWindow::addDefaultParts() {
	if (m_pcbGraphicsView == NULL) return;

	m_pcbGraphicsView->addDefaultParts();
	m_breadboardGraphicsView->addDefaultParts();
	m_schematicGraphicsView->addDefaultParts();
}

MainWindow * MainWindow::newMainWindow(ReferenceModel *referenceModel, const QString & displayPath, bool showProgress, bool lockFiles) {
    MainWindow * mw = new MainWindow(referenceModel, NULL);
	if (showProgress) {
		mw->showFileProgressDialog(displayPath);
	}

    mw->init(referenceModel, lockFiles);

	return mw;
}

void  MainWindow::clearFileProgressDialog() {
	if (m_fileProgressDialog) {
		m_fileProgressDialog->close();
		delete m_fileProgressDialog;
		m_fileProgressDialog = NULL;
	}
}

void MainWindow::setFileProgressPath(const QString & path)
{
	if (m_fileProgressDialog) m_fileProgressDialog->setMessage(tr("loading %1").arg(path));
}

FileProgressDialog * MainWindow::fileProgressDialog()
{
	return m_fileProgressDialog;
}

void MainWindow::showFileProgressDialog(const QString & path) {
    m_fileProgressDialog = new FileProgressDialog(tr("Loading..."), 200, this);
	m_fileProgressDialog->setBinLoadingChunk(50);
	if (!path.isEmpty()) {
		setFileProgressPath(QFileInfo(path).fileName());
	}
	else {
		setFileProgressPath(tr("new sketch"));
	}
}

const QString &MainWindow::selectedModuleID() {
	if(m_currentGraphicsView) {
		return m_currentGraphicsView->selectedModuleID();
	} else {
		return ___emptyString___;
	}
}

void MainWindow::redrawSketch() {
    QList<ConnectorItem *> visited;
	foreach (QGraphicsItem * item, m_currentGraphicsView->scene()->items()) {
		item->update();
		ConnectorItem * c = dynamic_cast<ConnectorItem *>(item);
		if (c != NULL) {
			c->restoreColor(visited);
		}
	}
}

void MainWindow::statusMessage(QString message, int timeout) {
	QStatusBar * sb = realStatusBar();
	if (sb != NULL) {
		sb->showMessage(message, timeout);
	}
}

void MainWindow::dropPaste(SketchWidget * sketchWidget) {
	Q_UNUSED(sketchWidget);
	pasteInPlace();
}

void MainWindow::updateLayerMenuSlot() {
	updateLayerMenu(true);
}

bool MainWindow::save() {
	bool result = FritzingWindow::save();
	if (result) {
		QSettings settings;
		settings.setValue("lastOpenSketch", m_fwFilename);
	}
	return result;
}

bool MainWindow::saveAs() {
	bool result = false;
	if (m_fwFilename.endsWith(FritzingSketchExtension)) {
		result = FritzingWindow::saveAs(m_fwFilename + 'z', false);
	}
	else {
		result = FritzingWindow::saveAs();
	}
	if (result) {
		QSettings settings;
		settings.setValue("lastOpenSketch", m_fwFilename);
	}
	return result;
}

void MainWindow::changeBoardLayers(int layers, bool doEmit) {
	Q_UNUSED(doEmit);
	Q_UNUSED(layers);
	updateActiveLayerButtons();
	m_currentGraphicsView->updateConnectors();
}

void MainWindow::updateActiveLayerButtons() {
    if (m_activeLayerButtonWidget != NULL) {
	    int index = activeLayerIndex();
	    bool enabled = index >= 0;

	    m_activeLayerButtonWidget->setCurrentIndex(index);
	    m_activeLayerButtonWidget->setVisible(enabled);

	    m_activeLayerBothAct->setEnabled(enabled);
	    m_activeLayerBottomAct->setEnabled(enabled);
	    m_activeLayerTopAct->setEnabled(enabled);

	    QList<QAction *> actions;
	    actions << m_activeLayerBothAct << m_activeLayerBottomAct << m_activeLayerTopAct;
	    setActionsIcons(index, actions);
    }

    if (m_viewFromButtonWidget != NULL) {
        if (m_pcbGraphicsView) {
            bool viewFromBelow = m_pcbGraphicsView->viewFromBelow();
            int index = (viewFromBelow ? 1 : 0);
	        m_viewFromButtonWidget->setCurrentIndex(index);
	        m_viewFromButtonWidget->setVisible(true);
            m_viewFromBelowToggleAct->setChecked(viewFromBelow);

	        m_viewFromBelowToggleAct->setEnabled(true);
	        m_viewFromBelowAct->setEnabled(true);
	        m_viewFromAboveAct->setEnabled(true);
        }
    }
}

int MainWindow::activeLayerIndex() 
{
	if (m_currentGraphicsView == NULL) return -1;

	if (m_currentGraphicsView->boardLayers() == 2 || activeLayerWidgetAlwaysOn()) {
		bool copper0Visible = m_currentGraphicsView->layerIsActive(ViewLayer::Copper0);
		bool copper1Visible = m_currentGraphicsView->layerIsActive(ViewLayer::Copper1);
		if (copper0Visible && copper1Visible) {
			return 0;
		}
		else if (copper1Visible) {
			return 2;
		}
		else if (copper0Visible) {
			return 1;
		}
	}

	return -1;
}

bool MainWindow::activeLayerWidgetAlwaysOn() {
	return false;
}

/**
 * A slot for saving a copy of the current sketch to a temp location.
 * This should be called every X minutes as well as just before certain
 * events, such as saves, part imports, file export/printing. This relies
 * on the m_autosaveNeeded variable and the undoStack being dirty for
 * an autosave to be attempted.
 */
void  MainWindow::backupSketch() {
	if (ProcessEventBlocker::isProcessing()) {
		// don't want to autosave during autorouting, for example
		return;
	}

    if (m_autosaveNeeded && !m_undoStack->isClean()) {
        m_autosaveNeeded = false;			// clear this now in case the save takes a really long time

        DebugDialog::debug(QString("%1 autosaved as %2").arg(m_fwFilename).arg(m_backupFileNameAndPath));
        statusBar()->showMessage(tr("Backing up '%1'").arg(m_fwFilename), 2000);
		ProcessEventBlocker::processEvents();
		m_backingUp = true;
		connectStartSave(true);
		m_sketchModel->save(m_backupFileNameAndPath, false);
		connectStartSave(false);
		m_backingUp = false;
    }
}

/**
 * This function is used to trigger an autosave at the next autosave
 * timer event. It is connected to the QUndoStack::indexChanged(int)
 * signal so that any change to the undo stack triggers autosaves.
 * This function can be called independent of this signal and
 * still work properly.
 */
void MainWindow::autosaveNeeded(int index) {
    Q_UNUSED(index);
    //DebugDialog::debug(QString("Triggering autosave"));
    m_autosaveNeeded = true;
}

/**
 * delete the backup file when the undostack is clean.
 */
void MainWindow::undoStackCleanChanged(bool isClean) {
    // DebugDialog::debug(QString("Clean status changed to %1").arg(isClean));
    if (isClean) {
        QFile::remove(m_backupFileNameAndPath);
    }
}

void MainWindow::setAutosavePeriod(int minutes) {
	setAutosave(minutes, AutosaveEnabled);
}

void MainWindow::setAutosaveEnabled(bool enabled) {
	setAutosave(AutosaveTimeoutMinutes, enabled);
}

void MainWindow::setAutosave(int minutes, bool enabled) {
	AutosaveTimeoutMinutes = minutes;
	AutosaveEnabled = enabled;
	foreach (QWidget * widget, QApplication::topLevelWidgets()) {
		MainWindow * mainWindow = qobject_cast<MainWindow *>(widget);
		if (mainWindow == NULL) continue;

		mainWindow->m_autosaveTimer.stop();
		if (qobject_cast<PEMainWindow *>(widget)) {
			continue;
		}

		if (AutosaveEnabled) {
			// is there a way to get the current timer offset so that all the timers aren't running in sync?
			// or just add some random time...
			mainWindow->m_autosaveTimer.start(AutosaveTimeoutMinutes * 60 * 1000);
		}
	}
}

bool MainWindow::hasLinkedProgramFiles(const QString & filename, QStringList & linkedProgramFiles) 
{
	QFile file(filename);
	file.open(QFile::ReadOnly);
	QXmlStreamReader xml(&file);
    xml.setNamespaceProcessing(false);

	bool done = false;
	while (!xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::StartElement:
			if (xml.name().toString().compare("program") == 0) {
				linkedProgramFiles.append(xml.readElementText());
				break;
			}
			if (xml.name().toString().compare("views") == 0) {
				done = true;
				break;
			}
			if (xml.name().toString().compare("instances") == 0) {
				done = true;
				break;
			}
		default:
			break;
		}

		if (done) break;
	}

	return linkedProgramFiles.count() > 0;
}

QString MainWindow::getExtensionString() {
	return tr("Fritzing (*%1)").arg(fileExtension());
}

QStringList MainWindow::getExtensions() {
	QStringList extensions;
	extensions.append(fileExtension());
	return extensions;
}

void MainWindow::firstTimeHelpHidden() {
	m_showInViewHelpAct->setChecked(false);
}

void MainWindow::routingStatusLabelMousePress(QMouseEvent* event) {
	routingStatusLabelMouse(event, true);
}

void MainWindow::routingStatusLabelMouseRelease(QMouseEvent* event) {
	routingStatusLabelMouse(event, false);
}

void MainWindow::routingStatusLabelMouse(QMouseEvent*, bool show) {
	if (show) DebugDialog::debug("-------");

	QSet<ConnectorItem *> toShow;
	foreach (QGraphicsItem * item, m_currentGraphicsView->scene()->items()) {
		VirtualWire * vw = dynamic_cast<VirtualWire *>(item);
		if (vw == NULL) continue;

		foreach (ConnectorItem * connectorItem, vw->connector0()->connectedToItems()) {
			toShow.insert(connectorItem);
		}
		foreach (ConnectorItem * connectorItem, vw->connector1()->connectedToItems()) {
			toShow.insert(connectorItem);
		}
	}
	QList<ConnectorItem *> visited;
	foreach (ConnectorItem * connectorItem, toShow) {
		if (show) {
			DebugDialog::debug(QString("unrouted %1 %2 %3 %4")
				.arg(connectorItem->attachedToInstanceTitle())
				.arg(connectorItem->attachedToID())
				.arg(connectorItem->attachedTo()->title())
				.arg(connectorItem->connectorSharedName()));
		}

		if (connectorItem->isActive() && connectorItem->isVisible() && !connectorItem->hidden() && !connectorItem->layerHidden()) {
			connectorItem->showEqualPotential(show, visited);
		}
		else {
			connectorItem = connectorItem->getCrossLayerConnectorItem();
			if (connectorItem) connectorItem->showEqualPotential(show, visited);
		}
	}

    if (!show && toShow.count() == 0) {
            QMessageBox::information(this, tr("Unrouted connections"), 
            tr("There are no unrouted connections in this view."));
    }
}

void MainWindow::setReportMissingModules(bool b) {
	if (m_sketchModel) {
		m_sketchModel->setReportMissingModules(b);
	}
}

void MainWindow::boardDeletedSlot() 
{
	activeLayerBottom();
}

void MainWindow::cursorLocationSlot(double xinches, double yinches)
{
	if (m_locationLabel) {
		QString units;
		double x, y;

		m_locationLabel->setProperty("location", QSizeF(xinches, xinches));

		if (m_locationLabelUnits.compare("mm") == 0) {
			units = "mm";
			x = xinches * 25.4;
			y = yinches * 25.4;
		}
		else if (m_locationLabelUnits.compare("px") == 0) {
			units = "px";
			x = xinches * GraphicsUtils::SVGDPI;
			y = yinches * GraphicsUtils::SVGDPI;
		}
		else {
			units = "in";
			x = xinches;
			y = yinches;
		}

		m_locationLabel->setText(tr("%1 %2 %3")
			.arg(x, 0, 'f', 3)
			.arg(y, 0, 'f', 3)
			.arg(units) );
	}
}

void MainWindow::locationLabelClicked()
{
	if (m_locationLabelUnits.compare("mm") == 0) {
		m_locationLabelUnits = "px";
	}
	else if (m_locationLabelUnits.compare("px") == 0) {
		m_locationLabelUnits = "in";
	}
	else if (m_locationLabelUnits.compare("in") == 0) {
		m_locationLabelUnits = "mm";
	}
	else {
		m_locationLabelUnits = "in";
	}

	if (m_locationLabel) {
		QVariant variant =  m_locationLabel->property("location");
		if (variant.isValid()) {
			QSizeF size = variant.toSizeF();
			cursorLocationSlot(size.width(), size.height());
		}
		else {
			cursorLocationSlot(0, 0);
		}
	}
		
	QSettings settings;
	settings.setValue("LocationInches", QVariant(m_locationLabelUnits));
}

void MainWindow::filenameIfSlot(QString & filename)
{
	filename = QFileInfo(fileName()).fileName();
}

QList<SketchWidget *> MainWindow::sketchWidgets() 
{
	QList<SketchWidget *> list;
	list << m_breadboardGraphicsView << m_schematicGraphicsView << m_pcbGraphicsView;
	return list;
}

void MainWindow::setCloseSilently(bool cs)
{
	m_closeSilently = cs;
}

PCBSketchWidget * MainWindow::pcbView() {
	return m_pcbGraphicsView;
}

void MainWindow::noBackup()
{
	m_autosaveTimer.stop();
}

void MainWindow::hideTempPartsBin() {
	if (m_binManager) m_binManager->hideTempPartsBin();
}

void MainWindow::setActiveWire(Wire * wire) {
	m_activeWire = wire;
}

void MainWindow::setActiveConnectorItem(ConnectorItem * connectorItem) {
	m_activeConnectorItem = connectorItem;
}

const QString & MainWindow::fritzingVersion() {
	if (m_sketchModel) return m_sketchModel->fritzingVersion();

	return ___emptyString___;
}


void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData* mimeData = event->mimeData();
 
    if (mimeData->hasUrls()) {
        QStringList pathList;
        QList<QUrl> urlList = mimeData->urls();
 
        // extract the local paths of the files
        for (int i = 0; i < urlList.size() && i < 32; ++i) {
            QString fn = urlList.at(i).toLocalFile();
            foreach (QString ext, fritzingExtensions()) {
                if (fn.endsWith(ext)) {
                    event->acceptProposedAction();
                    return;
                }
            }

            if (fn.endsWith("txt")) {
                QFile file(fn);
                if (file.open(QFile::ReadOnly)) {
                    QTextStream stream(&file);
                    while (!stream.atEnd()) {
                        QString line = stream.readLine().trimmed();
                        foreach (QString ext, fritzingExtensions()) {
                            if (line.endsWith(ext)) {
                                event->acceptProposedAction();
                                return;
                            }
                        }
                    }
                }
            }
        }
    }

}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData* mimeData = event->mimeData();
 
    if (mimeData->hasUrls()) {
        QStringList pathList;
        QList<QUrl> urlList = mimeData->urls();
 
        // extract the local paths of the files
        for (int i = 0; i < urlList.size() && i < 32; ++i) {
            mainLoadAux(urlList.at(i).toLocalFile());
        }

        return;
    }
}

bool MainWindow::hasAnyAlien() {
    return m_addedToTemp;
}

void MainWindow::initStyleSheet() 
{
    QString suffix = getStyleSheetSuffix();
	QFile styleSheet(QString(":/resources/styles/%1.qss").arg(suffix));
    if (!styleSheet.open(QIODevice::ReadOnly)) {
		DebugDialog::debug(QString("Unable to open :/resources/styles/%1.qss").arg(suffix));
	} else {
		QString platformDependantStyle = "";
		QString platformDependantStylePath;
#ifdef Q_WS_X11
		if(style()->metaObject()->className()==QString("OxygenStyle")) {
			QFile oxygenStyleSheet(QString(":/resources/styles/linux-kde-oxygen-%1.qss").arg(suffix));
			if(oxygenStyleSheet.open(QIODevice::ReadOnly)) {
				platformDependantStyle += oxygenStyleSheet.readAll();
			}
		}
		platformDependantStylePath = QString(":/resources/styles/linux-%1.qss").arg(suffix);
#endif

#ifdef Q_WS_MAC
		platformDependantStylePath = QString(":/resources/styles/mac-%1.qss").arg(suffix);
#endif

#ifdef Q_WS_WIN
		platformDependantStylePath = QString(":/resources/styles/win-%1.qss").arg(suffix);
#endif

		QFile platformDependantStyleSheet(platformDependantStylePath);
		if(platformDependantStyleSheet.open(QIODevice::ReadOnly)) {
			platformDependantStyle += platformDependantStyleSheet.readAll();
		}
		setStyleSheet(styleSheet.readAll()+platformDependantStyle);
	}
}

QString MainWindow::getStyleSheetSuffix() {
    return "fritzing";
}

void MainWindow::addToMyParts(ModelPart * modelPart)
{
    if (modelPart != NULL) m_binManager->addToMyParts(modelPart);
}

bool MainWindow::anyUsePart(const QString & moduleID) {
    foreach (QWidget *widget, QApplication::topLevelWidgets()) {
        MainWindow *mainWindow = qobject_cast<MainWindow *>(widget);
		if (mainWindow == NULL) continue;
			    
        if (mainWindow->usesPart(moduleID)) {
            return true;
        }
    }

    return false;
}

bool MainWindow::usesPart(const QString & moduleID) {
    if (m_currentGraphicsView == NULL) return false;

    foreach (QGraphicsItem * item, m_currentGraphicsView->scene()->items()) {
        ItemBase * itemBase = dynamic_cast<ItemBase *>(item);
        if (itemBase != NULL && itemBase->moduleID().compare(moduleID) == 0) {
            return true;
        }
    }

    return false;
}

bool MainWindow::updateParts(const QString & moduleID, QUndoCommand * parentCommand) {
    if (m_currentGraphicsView == NULL) return false;

    QSet<ItemBase *> itemBases;
    foreach (QGraphicsItem * item, m_currentGraphicsView->scene()->items()) {
        ItemBase * itemBase = dynamic_cast<ItemBase *>(item);
        if (itemBase == NULL) continue;       
        if (itemBase->moduleID().compare(moduleID) != 0) continue;

        itemBases.insert(itemBase->layerKinChief());
    }

    QMap<QString, QString> propsMap;
    foreach (ItemBase * itemBase, itemBases) {
        swapSelectedAuxAux(itemBase, moduleID, itemBase->viewLayerPlacement(), propsMap, parentCommand);
    }

    return true;
}

void MainWindow::showStatusMessage(const QString & message)
{
    if (sender() == m_statusBar) {
        return;
    }

    if (message == m_statusBar->currentMessage()) {
        return;
    }

    //DebugDialog::debug("show message " + message);
    m_statusBar->blockSignals(true);
    m_statusBar->showMessage(message);
    m_statusBar->blockSignals(false);
}

void MainWindow::updatePartsBin(const QString & moduleID) {
	m_binManager->reloadPart(moduleID);
}

bool MainWindow::hasCustomBoardShape() {
	if (m_pcbGraphicsView == NULL) return false;

	return m_pcbGraphicsView->hasCustomBoardShape();
}

void MainWindow::selectPartsWithModuleID(ModelPart * modelPart) {
    if (m_currentGraphicsView == NULL) return;

    m_currentGraphicsView->selectItemsWithModuleID(modelPart);
}

void MainWindow::addToSketch(QList<ModelPart *> & modelParts) {
    if (m_currentGraphicsView == NULL) return;

    m_currentGraphicsView->addToSketch(modelParts);
}

void MainWindow::initProgrammingWidget() {
    return; // not yet implemented

    m_programView = new ProgramWindow(this);

	connect(m_programView, SIGNAL(linkToProgramFile(const QString &, const QString &, const QString &, bool, bool)), 
			this, SLOT(linkToProgramFile(const QString &, const QString &, const QString &, bool, bool)));

	m_programView->setup();

	SketchAreaWidget * sketchAreaWidget = new SketchAreaWidget(m_programView, this);
	addTab(sketchAreaWidget, tr("Code"));
}

void MainWindow::orderFabHoverEnter() {
    if (!QuoteDialog::quoteSucceeded()) return;
    if (m_rolloverQuoteDialog && m_rolloverQuoteDialog->isVisible()) return;

    QTimer::singleShot(1, this, SLOT(fireQuote()));
}

void MainWindow::fireQuote() {
    if (!QuoteDialog::quoteSucceeded()) return;

    m_rolloverQuoteDialog = m_pcbGraphicsView->quoteDialog(m_pcbWidget);
    if (m_rolloverQuoteDialog == NULL) return;

    //DebugDialog::debug("enter fab button");
    //QWidget * toolbar = m_pcbWidget->toolbar();
    //QRect r = toolbar->geometry();
    //QPointF p = toolbar->parentWidget()->mapToGlobal(r.topLeft());

	Qt::WindowFlags flags = m_rolloverQuoteDialog->windowFlags();
    flags = Qt::Window | Qt::Dialog | Qt::FramelessWindowHint;
	m_rolloverQuoteDialog->setWindowFlags(flags);
    m_rolloverQuoteDialog->show();

    // seems to require setting position after show()

    QRect t = m_pcbWidget->toolbar()->geometry();
    QPoint gt = m_pcbWidget->toolbar()->parentWidget()->mapToGlobal(t.topLeft());
    QRect q = m_rolloverQuoteDialog->geometry();

    // I don't understand why--perhaps due to the windowFlags--but q is already in global coordinates
    q.moveBottom(gt.y() - 20);   
    m_rolloverQuoteDialog->setGeometry(q);
}

void MainWindow::orderFabHoverLeave() {
    //DebugDialog::debug("leave fab button");
    if (m_rolloverQuoteDialog) {
        m_rolloverQuoteDialog->hide();
    }
}

