/*******************************************************************************

Licensed to the OpenCOR team under one or more contributor license agreements.
See the NOTICE.txt file distributed with this work for additional information
regarding copyright ownership. The OpenCOR team licenses this file to you under
the Apache License, Version 2.0 (the "License"); you may not use this file
except in compliance with the License. You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.

*******************************************************************************/

//==============================================================================
// Main window
//==============================================================================

#include "centralwidget.h"
#include "checkforupdateswindow.h"
#include "cliutils.h"
#include "coreinterface.h"
#include "guiinterface.h"
#include "guiutils.h"
#include "i18ninterface.h"
#include "mainwindow.h"
#include "plugininterface.h"
#include "pluginmanager.h"
#include "pluginswindow.h"
#include "preferenceswindow.h"
#include "viewinterface.h"
#include "windowinterface.h"
#include "windowwidget.h"

//==============================================================================

#include "ui_mainwindow.h"

//==============================================================================

#ifdef Q_OS_WIN
    #include <windows.h>
#endif

//==============================================================================

#include <Qt>

//==============================================================================

#include <QAction>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QRect>
#include <QSettings>
#include <QShortcut>
#include <QUrl>

//==============================================================================

#ifdef Q_OS_WIN
    #include <shlobj.h>
#endif

//==============================================================================

#ifdef Q_OS_MAC
    #include <CoreServices/CoreServices.h>
#endif

//==============================================================================

namespace OpenCOR {

//==============================================================================

static const auto EnglishLocale = QStringLiteral("en");
static const auto FrenchLocale  = QStringLiteral("fr");

//==============================================================================

MainWindow::MainWindow(const QString &pApplicationDate) :
    QMainWindow(),
    mGui(new Ui::MainWindow),
    mApplicationDate(pApplicationDate),
    mFullyLoaded(false),
    mShuttingDown(false),
    mFileNamesOrOpencorUrls(QStringList()),
    mLoadedPluginPlugins(Plugins()),
    mLoadedI18nPlugins(Plugins()),
    mLoadedGuiPlugins(Plugins()),
    mLoadedWindowPlugins(Plugins()),
    mRawLocale(QString()),
    mMenus(QMap<QString, QMenu *>()),
    mFileNewMenu(0),
    mViewWindowsMenu(0),
    mViewSeparator(0),
    mViewPlugin(0),
    mDockedWindowsVisible(true),
    mDockedWindowsState(QByteArray())
{
    // Make sure that OpenCOR can handle a file opening request (from the
    // operating system), as well as a message sent by another instance of
    // itself

    QObject::connect(qApp, SIGNAL(fileOpenRequest(const QString &)),
                     this, SLOT(openFileOrHandleUrl(const QString &)));
    QObject::connect(qApp, SIGNAL(messageReceived(const QString &, QObject *)),
                     this, SLOT(handleMessage(const QString &)));

    // Handle OpenCOR URLs
    // Note: we should, through our GuiApplication class (see main.cpp), be able
    //       to handle OpenCOR URLs (not least because we make sure that the
    //       OpenCOR URL scheme is set; see the call to
    //       registerOpencorUrlScheme() below), but our URL handler ensures that
    //       it will work whether the OpenCOR URL scheme is set or not (in case
    //       it can't be set on a given platform)...

    QDesktopServices::setUrlHandler("opencor", this, "handleUrl");

    // Register our OpenCOR URL scheme

    registerOpencorUrlScheme();

    // Create our settings object

    mSettings = new QSettings();

    // Create our plugin manager (which will automatically load our various
    // plugins)

    mPluginManager = new PluginManager(qApp);

    // Retrieve some categories of plugins

    foreach (Plugin *plugin, mPluginManager->loadedPlugins()) {
        if (qobject_cast<PluginInterface *>(plugin->instance()))
            mLoadedPluginPlugins << plugin;

        if (qobject_cast<I18nInterface *>(plugin->instance()))
            mLoadedI18nPlugins << plugin;

        if (qobject_cast<GuiInterface *>(plugin->instance()))
            mLoadedGuiPlugins << plugin;

        if (qobject_cast<WindowInterface *>(plugin->instance()))
            mLoadedWindowPlugins << plugin;
    }

    // Set up the GUI
    // Note: the application icon (which needs to be set for Linux, but not for
    //       Windows or OS X, since it's set through CMake in those cases (see
    //       CMakeLists.txt)) is set within the GUI file. This being said, it's
    //       good to have it set for all three platforms, since it can then be
    //       used in, for example, the about box...

    mGui->setupUi(this);
//---GRY--- DISABLED UNTIL WE ACTUALLY SUPPORT USER PREFERENCES...
showEnableAction(mGui->actionPreferences, false);

    // Set the role of some of our menu items, so that OS X can move them into
    // the application menu

    mGui->actionQuit->setMenuRole(QAction::QuitRole);
    mGui->actionPreferences->setMenuRole(QAction::PreferencesRole);
    mGui->actionAbout->setMenuRole(QAction::AboutRole);
    mGui->actionCheckForUpdates->setMenuRole(QAction::ApplicationSpecificRole);

    // Title of our main window
    // Note: we don't set it in our .ui file since this will require
    //       'translating' it in other languages...

    setWindowTitle(qAppName());

    // Customise our docked windows action and handle it through a connection
    // Note #1: the reason for having several shortcuts is because one or
    //          several of them are bound to be already used on the target
    //          system...
    // Note #2: normally we would call setShortcuts() rather than setShortcut()
    //          and then manually creating several QShortcut objects, but it
    //          doesn't work (bug?)...

    mGui->actionDockedWindows->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_Space));

    connect(mGui->actionDockedWindows, SIGNAL(triggered(bool)),
            this, SLOT(showDockedWindows(const bool &)));

    new QShortcut(QKeySequence(Qt::META|Qt::Key_Space),
                  this, SLOT(toggleDockedWindows()));
    new QShortcut(QKeySequence(Qt::ALT|Qt::Key_Space),
                  this, SLOT(toggleDockedWindows()));
    new QShortcut(QKeySequence(Qt::CTRL|Qt::META|Qt::Key_Space),
                  this, SLOT(toggleDockedWindows()));
    new QShortcut(QKeySequence(Qt::CTRL|Qt::ALT|Qt::Key_Space),
                  this, SLOT(toggleDockedWindows()));
    new QShortcut(QKeySequence(Qt::META|Qt::ALT|Qt::Key_Space),
                  this, SLOT(toggleDockedWindows()));
    new QShortcut(QKeySequence(Qt::CTRL|Qt::META|Qt::ALT|Qt::Key_Space),
                  this, SLOT(toggleDockedWindows()));

    // A connection to handle the status bar

    connect(mGui->actionStatusBar, SIGNAL(triggered(bool)),
            mGui->statusBar, SLOT(setVisible(bool)));

    // Some connections to handle our various menu items

    connect(mGui->actionQuit, SIGNAL(triggered(bool)),
            this, SLOT(close()));
    connect(mGui->actionResetAll, SIGNAL(triggered(bool)),
            this, SLOT(resetAll()));

    // Set the shortcuts of some actions
    // Note: we do it here, so that we can use standard shortcuts (whenever
    //       possible)...

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    // Note: normally, we would be using QKeySequence::Quit, but we want to
    //       support both Alt+F4 and Ctrl+Q on Windows and Linux, and the
    //       default key sequence doesn't, so we set them ourselves...

    mGui->actionQuit->setShortcuts(QList<QKeySequence>()
                                       << QKeySequence(Qt::ALT|Qt::Key_F4)
                                       << QKeySequence(Qt::CTRL|Qt::Key_Q));
#elif defined(Q_OS_MAC)
    mGui->actionQuit->setShortcut(QKeySequence::Quit);
#else
    #error Unsupported platform
#endif

#ifdef Q_OS_MAC
    // A special shortcut to have OpenCOR minimised on OS X when pressing Cmd+M
    // Note: indeed, when pressing Cmd+M on OS X, the active application is
    //       expected to minimise itself, but it doesn't using Qt only...

    new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_M),
                  this, SLOT(showMinimized()));
#endif

    mGui->actionFullScreen->setShortcut(QKeySequence::FullScreen);

    // Initialise the plugin itself

    foreach (Plugin *plugin, mLoadedPluginPlugins)
        qobject_cast<PluginInterface *>(plugin->instance())->initializePlugin();

    // Initialise the plugin further by doing things that can only be done by
    // OpenCOR itself (e.g. set the central widget, create some menus)

    foreach (Plugin *plugin, mPluginManager->loadedPlugins())
        initializeGuiPlugin(plugin);

    // Keep track of the plugin's name in case we support
    // internationalisation

    foreach (Plugin *plugin, mLoadedI18nPlugins)
        qobject_cast<I18nInterface *>(plugin->instance())->setPluginName(plugin->name());

    // Let our various plugins know that all of them have been initialised
    // Note: this is important to do since the initialisation of a plugin is
    //       something that is done without knowing anything about other
    //       plugins. However, there may be things that require knowledge of
    //       what one or several other plugins are about, and this is something
    //       that can only be done once all the plugins have been initialised
    //       (e.g. the SingleCellSimulationView plugin needs to know which
    //       solvers, if any, are available to it)...

    foreach (Plugin *plugin, mLoadedPluginPlugins)
        qobject_cast<PluginInterface *>(plugin->instance())->pluginsInitialized(mPluginManager->loadedPlugins());

    // Keep track of the showing/hiding of the different window widgets

    foreach (Plugin *plugin, mLoadedWindowPlugins) {
        connect(qobject_cast<WindowInterface *>(plugin->instance())->windowWidget(), SIGNAL(visibilityChanged(bool)),
                this, SLOT(updateDockWidgetsVisibility()));
    }

    // Allow links, in message boxes, to be clickable using the mouse, but not
    // using the keyboard since it would allow messages to be selected, which we
    // don't want to allow at all

    qApp->setStyleSheet("QMessageBox {"
                        "    messagebox-text-interaction-flags: 4;"
                        "}");

    // Show/hide and enable/disable the windows action depending on whether
    // there are window widgets

    showEnableAction(mGui->actionDockedWindows, mLoadedWindowPlugins.count());

    // Retrieve the user settings from the previous session, if any

    loadSettings();

    // Initialise the checked state of our full screen action, since OpenCOR may
    // (re)start in full screen mode

    mGui->actionFullScreen->setChecked(isFullScreen());

    // We are done, so open/handle any file / OpenCOR URL there may be
    // Note: the way we open/handle those files / OpenCOR URLs ensures that we
    //       can still receive files / OpenCOR URLs to open/handle while we
    //       start opening/handling those that we have in stock, and this in the
    //       correct order...

    while (!mFileNamesOrOpencorUrls.isEmpty()) {
        openFileOrHandleUrl(mFileNamesOrOpencorUrls.first(), true);

        mFileNamesOrOpencorUrls.removeFirst();
    }

    mFullyLoaded = true;
}

//==============================================================================

MainWindow::~MainWindow()
{
    // Finalise our various plugins
    // Note: we only need to test for our default interface since we want to
    //       call the finalize method and this method is not overriden by any
    //       other interface...

    foreach (Plugin *plugin, mLoadedPluginPlugins)
        qobject_cast<PluginInterface *>(plugin->instance())->finalizePlugin();

    // Delete our central widget
    // Note: if we don't have one, then nothing will happen...

    delete centralWidget();

    // Delete some internal objects

    delete mPluginManager;
    delete mSettings;

    // Delete the GUI

    delete mGui;
}

//==============================================================================

void MainWindow::changeEvent(QEvent *pEvent)
{
    // Default handling of the event

    QMainWindow::changeEvent(pEvent);

    // Do a few more things for some changes

    if (   (pEvent->type() == QEvent::LocaleChange)
        && (mGui->actionSystem->isChecked())) {
        // The system's locale has changed, so update OpenCOR's locale in case
        // the user wants to use the system's locale

        setLocale();
#ifdef Q_OS_MAC
    } else if (pEvent->type() == QEvent::WindowStateChange) {
        // The window state has changed, so update the checked state of our full
        // screen action
        // Note: useful on OS X since there is a special full screen button in
        //       the main window's title bar...

        mGui->actionFullScreen->setChecked(isFullScreen());
#endif
    }
}

//==============================================================================

void MainWindow::closeEvent(QCloseEvent *pEvent)
{
    // Check with our Core plugin, if it has been loaded, that it's OK to close

    bool canClose = true;

    if (mPluginManager->corePlugin()) {
        canClose = qobject_cast<CoreInterface *>(mPluginManager->corePlugin()->instance())->canClose();
        // Note: if the Core plugin is loaded, then it means it supports the
        //       Core interface, so no need to check anything...
    }

    // Close ourselves, if possible

    if (canClose) {
        mShuttingDown = true;

        // Keep track of our default settings
        // Note: it must be done here, as opposed to the destructor, otherwise
        //       some settings (e.g. docked windows) won't be properly saved...

        saveSettings();

        // Accept the event

        pEvent->accept();
    } else {
        // Ignore the event, i.e. don't close ourselves

        pEvent->ignore();
    }
}

//==============================================================================

void MainWindow::registerOpencorUrlScheme()
{
    // Register our OpenCOR URL scheme

#if defined(Q_OS_WIN)
    QSettings settings("HKEY_CURRENT_USER\\Software\\Classes", QSettings::NativeFormat);
    QString applicationFileName = nativeCanonicalFileName(qApp->applicationFilePath());

    settings.setValue("opencor/Default", "URL:OpenCOR link");
    settings.setValue("opencor/Content Type", "application/x-opencor");
    settings.setValue("opencor/URL Protocol", "");
    settings.setValue("opencor/DefaultIcon/Default", "\""+applicationFileName+"\",1");
    settings.setValue("opencor/shell/Default", "open");
    settings.setValue("opencor/shell/open/command/Default", "\""+applicationFileName+"\" \"%1\"");

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, 0, 0);
#elif defined(Q_OS_LINUX)
//---GRY--- TO BE DONE...
#elif defined(Q_OS_MAC)
    LSSetDefaultHandlerForURLScheme(CFSTR("opencor"),
                                    CFBundleGetIdentifier(CFBundleGetMainBundle()));
#else
    #error Unsupported platform
#endif
}

//==============================================================================

void MainWindow::initializeGuiPlugin(Plugin *pPlugin)
{
    // Retrieve and apply the plugin's GUI settings, if any

    GuiInterface *guiInterface = qobject_cast<GuiInterface *>(pPlugin->instance());

    if (guiInterface) {
        // Add the menus to our menu bar or merge them to existing menus, if
        // needed
        // Note: we must do that in reverse order since we are inserting menus,
        //       as opposed to appending some...

        Gui::MenuIterator menuIter(guiInterface->guiMenus());

        menuIter.toBack();

        while (menuIter.hasPrevious()) {
            // Insert the menu in the right place

            Gui::Menu menu = menuIter.previous();

            QMenu *newMenu = menu.menu();
            QString newMenuName = newMenu->objectName();

            QMenu *oldMenu = mMenus.value(newMenuName);

            if (oldMenu && !menu.action()) {
                // A menu with the same name already exists, so add the contents
                // of the new menu to the existing one

                oldMenu->addSeparator();
                oldMenu->addActions(newMenu->actions());

                // Delete the new menu since we don't need it anymore

                delete newMenu;
            } else {
                // No menu with the same name already exists (or the menu
                // doesn't have a name), so add the new menu to our menu bar

                switch (menu.type()) {
                case Gui::Menu::View:
                    mGui->menuBar->insertAction(mGui->menuView->menuAction(),
                                                newMenu->menuAction());

                    break;
                default:
                    // Not a type in which we are interested, so do nothing

                    ;
                }

                // Keep track of the new menu, but only if it has a name

                if (newMenuName.size())
                    mMenus.insert(newMenuName, newMenu);
            }
        }

        // Add the actions/separators to our different menus
        // Note: as for the menus above, we must do it in reverse order since we
        //       are inserting actions, as opposed to appending some...

        Gui::MenuActionIterator menuActionIter(guiInterface->guiMenuActions());

        menuActionIter.toBack();

        while (menuActionIter.hasPrevious()) {
            // Insert the action/separator to the right menu, if any

            QMenu *menu = 0;
            Gui::MenuAction menuAction = menuActionIter.previous();

            if (menuAction.type() == Gui::MenuAction::File)
                menu = mGui->menuFile;
            else if (menuAction.type() == Gui::MenuAction::Tools)
                menu = mGui->menuTools;

            if (menu) {
                QAction *action = menuAction.action();

                if (action)
                    menu->insertAction(menu->actions().first(), action);
                else
                    menu->insertSeparator(menu->actions().first());
            }
        }

        // Make sure that our language menu item is first in our tools menu

        mGui->menuTools->insertSeparator(mGui->menuTools->actions().first());
        mGui->menuTools->insertAction(mGui->menuTools->actions().first(), mGui->menuLanguage->menuAction());

        // Add some sub-menus before some menu items

        foreach (const Gui::Menu &menu, guiInterface->guiMenus()) {
            // Insert the menu before a menu item / separator

            if (menu.action()) {
                switch (menu.type()) {
                case Gui::Menu::File:
                    mGui->menuFile->insertMenu(menu.action(), menu.menu());

                    break;
                default:
                    // Not a type in which we are interested, so do nothing

                    ;
                }
            }
        }

        // Add some actions to some sub-menus and keep track of them

        static QString pluginForFileNewMenu = QString();

        foreach (const Gui::MenuAction &menuAction, guiInterface->guiMenuActions()) {
            // Insert the action to the right menu

            switch (menuAction.type()) {
            case Gui::MenuAction::FileNew:
                // Check whether the File|New menu has been created and if not,
                // then create it

                if (!mFileNewMenu) {
                    // The menu doesn't already exist, so create it

                    mFileNewMenu = new QMenu(this);

                    mFileNewMenu->menuAction()->setIcon(QIcon(":/oxygen/mimetypes/application-x-zerosize.png"));

                    // Add the New menu to our File menu and add a separator
                    // after it

                    mGui->menuFile->insertMenu(mGui->menuFile->actions().first(),
                                               mFileNewMenu);
                    mGui->menuFile->insertSeparator(mGui->menuFile->actions()[1]);

                    pluginForFileNewMenu = pPlugin->name();
                } else if (pluginForFileNewMenu.compare(pPlugin->name())) {
                    // The File|New menu already exists, so add a separator to
                    // it so that previous menu items (from a different plugin)
                    // don't get mixed up with the new one

                    mFileNewMenu->addSeparator();

                    pluginForFileNewMenu = pPlugin->name();
                }

                mFileNewMenu->addAction(menuAction.action());

                break;
            default:
                // Not a type in which we are interested, so do nothing

                ;
            }
        }
    }

    // Set our central widget, in case we are dealing with the Core plugin

    if (pPlugin == mPluginManager->corePlugin()) {
        // We are dealing with the Core plugin, so set our central widget

        setCentralWidget(qobject_cast<CoreInterface *>(pPlugin->instance())->centralWidget());
        // Note: if the Core plugin is loaded, then it means it supports the
        //       Core interface, so no need to check anything...

        // Also keep track of GUI updates in our central widget

        connect(static_cast<Core::CentralWidget *>(centralWidget()), SIGNAL(guiUpdated(Plugin *, const QString &)),
                this, SLOT(updateGui(Plugin *, const QString &)));
    }

    // Add the plugin's window, in case we are dealing with a window plugin

    WindowInterface *windowInterface = qobject_cast<WindowInterface *>(pPlugin->instance());

    if (windowInterface) {
        // Dock the window to its default dock area

        addDockWidget(windowInterface->windowDefaultDockArea(), windowInterface->windowWidget());

        // Add an action to our menu to show/hide the window

        if (!pPlugin->name().compare("HelpWindow")) {
            // Special case of the help window

            mGui->menuHelp->insertAction(mGui->actionHomePage,
                                         windowInterface->windowAction());
            mGui->menuHelp->insertSeparator(mGui->actionHomePage);
        } else {
            // Update the View menu by adding the action to the
            // View|Windows menu

            updateViewWindowsMenu(windowInterface->windowAction());
        }

        // Connect the action to the window

        connect(windowInterface->windowAction(), SIGNAL(triggered(bool)),
                windowInterface->windowWidget(), SLOT(setVisible(bool)));
        connect(windowInterface->windowWidget()->toggleViewAction(), SIGNAL(toggled(bool)),
                windowInterface->windowAction(), SLOT(setChecked(bool)));
    }
}

//==============================================================================

static const auto SettingsGeometry             = QStringLiteral("Geometry");
static const auto SettingsState                = QStringLiteral("State");
static const auto SettingsDockedWindowsVisible = QStringLiteral("DockedWindowsVisible");
static const auto SettingsDockedWindowsState   = QStringLiteral("DockedWindowsState");
static const auto SettingsStatusBarVisible     = QStringLiteral("StatusBarVisible");

//==============================================================================

void MainWindow::loadSettings()
{
    // Retrieve the geometry and state of the main window

    if (   !restoreGeometry(mSettings->value(SettingsGeometry).toByteArray())
        || !restoreState(mSettings->value(SettingsState).toByteArray())) {
        // The geometry and/or state of the main window couldn't be retrieved,
        // so go with some default settings

        // Default size and position of the main window

        QRect desktopGeometry = qApp->desktop()->availableGeometry();
        int horizSpace = desktopGeometry.width()/13;
        int vertSpace  = desktopGeometry.height()/13;

        setGeometry(desktopGeometry.left()+horizSpace,
                    desktopGeometry.top()+vertSpace,
                    desktopGeometry.width()-2*horizSpace,
                    desktopGeometry.height()-2*vertSpace);
    }

    // Retrieve whether the docked windows are to be shown

    showDockedWindows(mSettings->value(SettingsDockedWindowsVisible, true).toBool(), true);

    // Retrieve the state of the docked windows

    mDockedWindowsState = mSettings->value(SettingsDockedWindowsState, QByteArray()).toByteArray();

    // Retrieve the settings of our various plugins

    foreach (Plugin *plugin, mLoadedPluginPlugins) {
        mSettings->beginGroup(SettingsPlugins);
            mSettings->beginGroup(plugin->name());
                qobject_cast<PluginInterface *>(plugin->instance())->loadSettings(mSettings);
            mSettings->endGroup();
        mSettings->endGroup();
    }

    // Let our core plugin know that all of the plugins have loaded their
    // settings
    // Note: this is similar to initializePlugin() vs. pluginsInitialized()...

    if (mPluginManager->corePlugin()) {
        qobject_cast<CoreInterface *>(mPluginManager->corePlugin()->instance())->settingsLoaded(mPluginManager->loadedPlugins());
        // Note: if the Core plugin is loaded, then it means it supports the
        //       Core interface, so no need to check anything...
    }

    // Remove the File menu when on OS X, should no plugins be loaded
    // Note: our File menu should only contain the Exit menu item, but on OS X
    //       that menu item gets automatically moved to the application menu...

#ifdef Q_OS_MAC
    mGui->menuFile->menuAction()->setVisible(mPluginManager->loadedPlugins().count());
#endif

    // Retrieve whether the status bar is to be shown

    bool statusBarVisible = mSettings->value(SettingsStatusBarVisible, true).toBool();

    mGui->statusBar->setVisible(statusBarVisible);
    mGui->actionStatusBar->setChecked(statusBarVisible);

    // Retrieve and set the language to be used by OpenCOR
    // Note #1: the setting is forced in order to account for locale-dependent
    //          initialisations (e.g. see CentralWidget::retranslateUi())...
    // Note #2: this must be done once all the settings have been loaded since
    //          some plugins may, as a result of the loading of settings, create
    //          widgets that need translating (e.g. graph panels get created in
    //          the SingleCellView plugin)...

    setLocale(OpenCOR::rawLocale(), true);
}

//==============================================================================

void MainWindow::saveSettings() const
{
    // Keep track of the geometry and state of the main window

    mSettings->setValue(SettingsGeometry, saveGeometry());
    mSettings->setValue(SettingsState, saveState());

    // Keep track of whether the docked windows are to be shown

    mSettings->setValue(SettingsDockedWindowsVisible, mDockedWindowsVisible);

    // Keep track of the state of the docked windows

    mSettings->setValue(SettingsDockedWindowsState, mDockedWindowsState);

    // Keep track of whether the status bar is to be shown

    mSettings->setValue(SettingsStatusBarVisible,
                        mGui->statusBar->isVisible());

    // Keep track of the settings of our various plugins

    foreach (Plugin *plugin, mLoadedPluginPlugins) {
        mSettings->beginGroup(SettingsPlugins);
            mSettings->beginGroup(plugin->name());
                qobject_cast<PluginInterface *>(plugin->instance())->saveSettings(mSettings);
            mSettings->endGroup();
        mSettings->endGroup();
    }
}

//==============================================================================

void MainWindow::setLocale(const QString &pRawLocale, const bool &pForceSetting)
{
    const QString systemLocale = QLocale::system().name().left(2);

    QString oldLocale = mRawLocale.isEmpty()?systemLocale:mRawLocale;
    QString newLocale = pRawLocale.isEmpty()?systemLocale:pRawLocale;

    // Keep track of the new locale, if needed

    if (pRawLocale.compare(mRawLocale) || pForceSetting) {
        mRawLocale = pRawLocale;

        // Also keep a copy of the new raw locale in our settings (so that the
        // new locale can be retrieved from plugins)

        OpenCOR::setRawLocale(mRawLocale);
    }

    // Check whether the new locale is different from the old one and if so,
    // then retranslate everything

    if (oldLocale.compare(newLocale) || pForceSetting) {
        // Specify the language to be used by OpenCOR

        QLocale::setDefault(QLocale(newLocale));

        qApp->removeTranslator(&mQtTranslator);
        mQtTranslator.load(":qt_"+newLocale);
        qApp->installTranslator(&mQtTranslator);

        qApp->removeTranslator(&mAppTranslator);
        mAppTranslator.load(":app_"+newLocale);
        qApp->installTranslator(&mAppTranslator);

        // Retranslate OpenCOR

        mGui->retranslateUi(this);

        // Retranslate some widgets that are not originally part of our user
        // interface

        if (mFileNewMenu)
            I18nInterface::retranslateMenu(mFileNewMenu, tr("New"));

        if (mViewWindowsMenu)
            I18nInterface::retranslateMenu(mViewWindowsMenu, tr("Windows"));

        // Update the locale of our various loaded plugins
        // Note: we must set the locale of all the plugins before we can safely
        //       retranslate them since a plugin may require another plugin to
        //       work properly...

        foreach (Plugin *plugin, mLoadedI18nPlugins)
            qobject_cast<I18nInterface *>(plugin->instance())->setLocale(newLocale);

        // Retranslate our various plugins

        foreach (Plugin *plugin, mLoadedI18nPlugins)
            qobject_cast<I18nInterface *>(plugin->instance())->retranslateUi();

        // Reorder our various View|Windows menu items, just in case

        reorderViewWindowsMenu();
    }

    // Update the checked menu item
    // Note: it has to be done every single time, since selecting a menu item
    //       will automatically toggle its checked status...

    mGui->actionSystem->setChecked(pRawLocale.isEmpty());

    mGui->actionEnglish->setChecked(!pRawLocale.compare(EnglishLocale));
    mGui->actionFrench->setChecked(!pRawLocale.compare(FrenchLocale));
}

//==============================================================================

void MainWindow::reorderViewWindowsMenu()
{
    // Reorder the View|Windows menu, should it exist
    // Note: this is useful after having added a new menu item or after having
    //       changed the locale

    if (mViewWindowsMenu) {
        // Retrieve the title of the menu items and keep track of their actions

        QStringList menuItemTitles;
        QMap<QString, QAction *> menuItemActions;

        foreach (QAction *menuItemAction, mViewWindowsMenu->actions()) {
            // Remove any "&" present in the menu item title, as well as replace
            // accentuated characters by non-accentuated ones, making the
            // sorting sensible

            QString menuItemTitle = menuItemAction->text().remove("&").normalized(QString::NormalizationForm_KD);

            for (int i = menuItemTitle.length()-1; i >= 0; --i) {
                if (menuItemTitle[i].category() == QChar::Mark_NonSpacing)
                    menuItemTitle.remove(i, 1);
            }

            // Keep track of the menu item title and the action to which it is
            // associated

            menuItemTitles << menuItemTitle;
            menuItemActions.insert(menuItemTitle, menuItemAction);
        }

        // Sort the menu items

        menuItemTitles.sort();

        // Add the menu items actions in the new order
        // Note: to use addAction will effectively 'move' the menu items to the
        //       end of the menu, so since we do it in the right order, we end
        //       up with the menu items being properly ordered...

        foreach (const QString &menuItemTitle, menuItemTitles)
            mViewWindowsMenu->addAction(menuItemActions.value(menuItemTitle));
    }
}

//==============================================================================

void MainWindow::updateViewWindowsMenu(QAction *pAction)
{
    // Check whether we need to insert a separator before the docked windows
    // menu item

    if (!mViewSeparator)
        mViewSeparator = mGui->menuView->insertSeparator(mGui->actionDockedWindows);

    // Check whether the View|Windows menu already exists and create it if not

    if (!mViewWindowsMenu) {
        // The View|Windows menu doesn't already exist, so create it

        mViewWindowsMenu = new QMenu(this);

        // Add the View|Windows menu to our View menu

        mGui->menuView->insertMenu(mViewSeparator, mViewWindowsMenu);
    }

    // At this stage, the View|Windows menu exist, so add the given action to
    // it

    mViewWindowsMenu->addAction(pAction);
}

//==============================================================================

bool MainWindow::shuttingDown() const
{
    // Return whether we are shutting down

    return mShuttingDown;
}

//==============================================================================

void MainWindow::showSelf()
{
    // Note: to show ourselves, one would normally use activateWindow() (and
    //       possibly raise()), but depending on the operating system it may or
    //       not bring OpenCOR to the foreground, so instead we do what follows,
    //       depending on the operating system...

#if defined(Q_OS_WIN)
    // Show ourselves the Windows way

    // Retrieve OpenCOR's window Id

    HWND mainWinId = reinterpret_cast<HWND>(winId());

    // Bring OpenCOR to the foreground

    DWORD foregroundThreadProcId = GetWindowThreadProcessId(GetForegroundWindow(), 0);
    DWORD mainThreadProcId = GetWindowThreadProcessId(mainWinId, 0);

    if (foregroundThreadProcId != mainThreadProcId) {
        // OpenCOR's thread process Id is not that of the foreground window, so
        // attach the foreground thread to OpenCOR's, set OpenCOR to the
        // foreground, and detach the foreground thread from OpenCOR's

        AttachThreadInput(foregroundThreadProcId, mainThreadProcId, true);

        SetForegroundWindow(mainWinId);
        SetFocus(mainWinId);

        AttachThreadInput(foregroundThreadProcId, mainThreadProcId, false);
    } else {
        // OpenCOR's thread process Id is that of the foreground window, so
        // just set OpenCOR to the foreground

        SetForegroundWindow(mainWinId);
    }

    // Show/restore OpenCOR, depending on its current state

    if (IsIconic(mainWinId))
        ShowWindow(mainWinId, SW_RESTORE);
    else
        ShowWindow(mainWinId, SW_SHOW);

    // Note: under Windows, to use activateWindow() will only highlight the
    //       application in the taskbar, since under Windows no application
    //       should be allowed to bring itself to the foreground when another
    //       application is already in the foreground. Fair enough, but it
    //       happens that, here, the user wants OpenCOR to be brought to the
    //       foreground, hence the above code to get the effect we are after...
#elif defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    // We are on Linux or OS X, so we can simply activate the window and raise
    // ourselves

    activateWindow();
    raise();
#else
    #error Unsupported platform
#endif
}

//==============================================================================

void MainWindow::handleArguments(const QString &pArguments)
{
    // Handle the arguments that were passed to OpenCOR by passing them to the
    // Core plugin, should it be loaded, but only if the argument is not an
    // OpenCOR URL

    QUrl url = pArguments;

    if (!url.scheme().compare("opencor"))
        handleUrl(url);
    else if (mPluginManager->corePlugin())
        qobject_cast<CoreInterface *>(mPluginManager->corePlugin()->instance())->handleArguments(pArguments.split("|"));
}

//==============================================================================

void MainWindow::openFileOrHandleUrl(const QString &pFileNameOrOpencorUrl,
                                     const bool &ForceOpeningOrHandling)
{
    // Handle the given file name or OpenCOR URL as if it was an argument, but
    // only if we are fully loaded otherwise we keep track of that file name or
    // OpenCOR URL
    // Note: indeed, if we are not fully loaded then a file will still be
    //       opened, but not selected, or a URL may be handled, but having
    //       OpenCOR to keep loading itself may mess things up (e.g. OpenCOR is
    //       not started and it was previously in Simulation mode, from there an
    //       OpenCOR URL to select the Editing mode is clicked, resuling in
    //       OpenCOR starting up, selecting the Editing mode, but then the
    //       Simulation mode will effectively be active even though not
    //       selected)...

    if (!ForceOpeningOrHandling && !mFullyLoaded)
        mFileNamesOrOpencorUrls << pFileNameOrOpencorUrl;
    else
        handleArguments(pFileNameOrOpencorUrl);
}

//==============================================================================

void MainWindow::handleUrl(const QUrl &pUrl)
{
    // Handle the action that was passed to OpenCOR

    QString actionName = pUrl.authority();

    if (!actionName.compare("openPluginsDialogBox", Qt::CaseInsensitive)) {
        // We want to open the Plugins dialog box

        on_actionPlugins_triggered();
    } else if (!actionName.compare("openAboutDialogBox", Qt::CaseInsensitive)) {
        // We want to open the About dialog box

        on_actionAbout_triggered();
    } else if (!actionName.compare("openFile", Qt::CaseInsensitive)) {
        // We want to open a file, so handle it as an argument that is passed to
        // OpenCOR
        // Note: the file name is contained in the path of the URL minus the
        //       leading forward slash. Indeed, an open file request will look
        //       like opencor://openFile//home/user/file...

        handleArguments(pUrl.path().remove(0, 1));
    } else if (!actionName.compare("openFiles", Qt::CaseInsensitive)) {
        // We want to open some files, so handle them as a series of arguments
        // that were passed to OpenCOR
        // Note: the file names are contained in the path of the URL minus the
        //       leading forward slash. Indeed, an open files request  will look
        //       like opencor://openFiles//home/user/file1|/home/user/file2...

        handleArguments(pUrl.path().remove(0, 1));
    } else {
        // We are dealing with an action that OpenCOR itself can't handle, but
        // maybe one of its loaded plugins can

        QString pluginName = actionName.split(".").first();

        foreach (Plugin *plugin, mLoadedPluginPlugins) {
            if (!plugin->name().compare(pluginName, Qt::CaseInsensitive)) {
                // This is an action for the current plugin, so forward the
                // action to it, should it support the Plugin interface

                PluginInterface *pluginInterface = qobject_cast<PluginInterface *>(plugin->instance());

                if (pluginInterface)
                    pluginInterface->handleUrl(pUrl);

                break;
            }
        }
    }
}

//==============================================================================

void MainWindow::handleMessage(const QString &pMessage)
{
    // We have just received a message, which means that the user tried to run
    // another instance of OpenCOR, which sent a message to this instance,
    // asking it to bring itself to the foreground and handling all the
    // arguments passed in the given message

    showSelf();

    handleArguments(pMessage);
}

//==============================================================================

void MainWindow::on_actionFullScreen_triggered()
{
    // Switch to / back from full screen mode

    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

//==============================================================================

void MainWindow::on_actionSystem_triggered()
{
    // Select the system's language as the language used by OpenCOR

    setLocale();
}

//==============================================================================

void MainWindow::on_actionEnglish_triggered()
{
    // Select English as the language used by OpenCOR

    setLocale(EnglishLocale);
}

//==============================================================================

void MainWindow::on_actionFrench_triggered()
{
    // Select French as the language used by OpenCOR

    setLocale(FrenchLocale);
}

//==============================================================================

void MainWindow::on_actionPlugins_triggered()
{
    if (mPluginManager->plugins().count()) {
        // There are some plugins, so we can show the plugins window

        PluginsWindow pluginsWindow(mPluginManager, this);

        mSettings->beginGroup(pluginsWindow.objectName());
            pluginsWindow.loadSettings(mSettings);
        mSettings->endGroup();

        pluginsWindow.exec();
        // Note: the execution of the plugins window may result in the saving of
        //       the application's settings, so for this to work we must ensure
        //       that any opened settings group has first been closed...

        mSettings->beginGroup(pluginsWindow.objectName());
            pluginsWindow.saveSettings(mSettings);
        mSettings->endGroup();

        // Restart OpenCOR (after having saved its settings) in case the user
        // asked for his/her plugin-related settings to be  applied

        if (pluginsWindow.result() == QMessageBox::Apply)
            restart(true);
    } else {
        QMessageBox::warning(this, tr("Plugins"),
                             tr("No plugin could be found."));
    }
}

//==============================================================================

void MainWindow::on_actionPreferences_triggered()
{
    // Show the preferences window

    PreferencesWindow preferencesWindow(this);

    preferencesWindow.exec();
}

//==============================================================================

void MainWindow::on_actionHomePage_triggered()
{
    // Look up OpenCOR's home page

    QDesktopServices::openUrl(HomePageUrl);
}

//==============================================================================

void MainWindow::on_actionCheckForUpdates_triggered()
{
    // Show the check for updates window

    CheckForUpdatesWindow checkForUpdatesWindow(mApplicationDate, this);

    mSettings->beginGroup(checkForUpdatesWindow.objectName());
        checkForUpdatesWindow.loadSettings(mSettings);
    mSettings->endGroup();

    checkForUpdatesWindow.exec();

    mSettings->beginGroup(checkForUpdatesWindow.objectName());
        checkForUpdatesWindow.saveSettings(mSettings);
    mSettings->endGroup();
}

//==============================================================================

void MainWindow::on_actionAbout_triggered()
{
    // Display some information about OpenCOR

    QMessageBox::about(this, tr("About"),
                       "<h1 align=center><strong>"+version()+"</strong></h1>"
                       "<h3 align=center><em>"+QSysInfo::prettyProductName()+"</em></h3>"
                       "<p align=center><em>"+copyright()+"</em></p>"
                       "<p>"+applicationDescription()+"</p>"
                       "<p>"+applicationBuildInformation()+"</p>");
}

//==============================================================================

void MainWindow::restart(const bool &pSaveSettings) const
{
    // Restart OpenCOR after saving its settings, if required
    // Note: the closeEvent method won't get called when exiting OpenCOR and
    //       this is exactly what we want in case we don't want to save its
    //       settings (e.g. when resetting all)

    if (pSaveSettings)
        saveSettings();

    qApp->exit(pSaveSettings?NormalRestart:CleanRestart);
}

//==============================================================================

void MainWindow::showEnableActions(const QList<QAction *> &pActions)
{
    // Show/enable or hide/disable the given actions, depending on whether they
    // correspond to a menu with visible/enabled or hidden/disabled actions,
    // respectively

    foreach (QAction *action, pActions) {
        QMenu *actionMenu = action->menu();

        if (actionMenu) {
            QList<QAction *> actionMenuActions = actionMenu->actions();

            showEnableActions(actionMenuActions);

            bool showEnable = false;

            foreach (QAction *actionMenuAction, actionMenuActions) {
                if (   !actionMenuAction->isSeparator()
                    &&  actionMenuAction->isVisible()) {
                    showEnable = true;

                    break;
                }
            }

            showEnableAction(action, showEnable);
        }
    }
}

//==============================================================================

void MainWindow::updateGui(Plugin *pViewPlugin, const QString &pFileName)
{
    // We come here as a result of our central widget having updated its GUI,
    // meaning that a new view or file has been selected, so we may need to
    // enable/disable and/or show/hide some menus/actions/etc.

    // Keep track of our view plugin

    mViewPlugin = pViewPlugin;

    // Let our different plugins know that the GUI has been updated
    // Note: this can be useful when a plugin (e.g. CellMLTools) offers some
    //       tools that may need to be enabled/disabled and shown/hidden,
    //       depending on which view plugin and/or file are currently active...

    foreach (Plugin *plugin, mLoadedGuiPlugins)
        qobject_cast<GuiInterface *>(plugin->instance())->updateGui(mViewPlugin, pFileName);

    // Go through our different menus and show/hide them, depending on whether
    // they have visible items

    showEnableActions(mGui->menuBar->actions());
}

//==============================================================================

void MainWindow::showDockedWindows(const bool &pShow,
                                   const bool &pInitialisation)
{
    // Show/hide the docked windows

    if (!pInitialisation) {
        if (!pShow)
            mDockedWindowsState = saveState();

        foreach (Plugin *plugin, mLoadedWindowPlugins) {
            WindowInterface *windowInterface = qobject_cast<WindowInterface *>(plugin->instance());

            if (!windowInterface->windowWidget()->isFloating())
                windowInterface->windowWidget()->setVisible(pShow);
        }

        if (pShow && !mDockedWindowsState.isEmpty())
            restoreState(mDockedWindowsState);
    }

    // Keep track of the docked windows visible state

    mDockedWindowsVisible = pShow;

    // Update the checked state of our docked windows action

    mGui->actionDockedWindows->setChecked(pShow);
}

//==============================================================================

void MainWindow::toggleDockedWindows()
{
    // Toggle the visible state of our docked windows

    showDockedWindows(!mGui->actionDockedWindows->isChecked());
}

//==============================================================================

void MainWindow::updateDockWidgetsVisibility()
{
    // Check whether at least one dockable window is visible

    mDockedWindowsVisible = false;

    foreach (Plugin *plugin, mLoadedWindowPlugins) {
        QDockWidget *dockWidget = qobject_cast<WindowInterface *>(plugin->instance())->windowWidget();

        if (!dockWidget->isFloating() && dockWidget->isVisible()) {
            mDockedWindowsVisible = true;

            break;
        }
    }

    // Update the checked state of our docked windows action

    mGui->actionDockedWindows->setChecked(mDockedWindowsVisible);
}

//==============================================================================

void MainWindow::resetAll()
{
    if (QMessageBox::question(this, qAppName(),
                              tr("You are about to reset <strong>all</strong> of your settings. Do you wish to proceed?"),
                              QMessageBox::Yes|QMessageBox::No,
                              QMessageBox::Yes) == QMessageBox::Yes ) {
        // Restart OpenCOR without first saving its settings

        restart(false);
    }
}

//==============================================================================

}   // namespace OpenCOR

//==============================================================================
// End of file
//==============================================================================
