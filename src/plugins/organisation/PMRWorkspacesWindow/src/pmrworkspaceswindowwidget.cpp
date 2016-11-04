/*******************************************************************************

Copyright The University of Auckland

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*******************************************************************************/

//==============================================================================
// PMR Workspaces window widget
//==============================================================================

#include "coreguiutils.h"
#include "pmrwebservice.h"
#include "pmrworkspace.h"
#include "pmrworkspacefilenode.h"
#include "pmrworkspacemanager.h"
#include "pmrworkspaceswindowcommit.h"
#include "pmrworkspaceswindowwidget.h"

//==============================================================================

#include <QApplication>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QHelpEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMutableMapIterator>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QWebElement>
#include <QWebFrame>

//==============================================================================

#include "git2/remote.h"
#include "git2/repository.h"

//==============================================================================

namespace OpenCOR {
namespace PMRWorkspacesWindow {

//==============================================================================

static const auto AboutAction           = QStringLiteral("about");
static const auto CloneAction           = QStringLiteral("clone");
static const auto CommitAction          = QStringLiteral("commit");
static const auto RefreshAction         = QStringLiteral("refresh");
static const auto StageAction           = QStringLiteral("action");
static const auto UnstageAction         = QStringLiteral("unstage");
static const auto SynchronizeAction     = QStringLiteral("synchronize");
static const auto SynchronizePushAction = QStringLiteral("synchronizePush");
static const auto SynchronizePullAction = QStringLiteral("synchronizePull");

//==============================================================================

static const auto AboutIcon           = QStringLiteral(":/oxygen/actions/help-about.png");
static const auto CloneIcon           = QStringLiteral(":/oxygen/places/folder-downloads.png");
static const auto CommitIcon          = QStringLiteral(":/oxygen/actions/view-task.png");
static const auto FolderIcon          = QStringLiteral(":/oxygen/places/folder.png");
static const auto FolderOpenIcon      = QStringLiteral(":/oxygen/places/folder-open.png");
static const auto OwnedIcon           = QStringLiteral(":/oxygen/places/folder-favorites.png");
static const auto RefreshIcon         = QStringLiteral(":/oxygen/actions/view-refresh.png");
static const auto StageIcon           = QStringLiteral(":/oxygen/actions/dialog-ok-apply.png");
static const auto UnstageIcon         = QStringLiteral(":/oxygen/actions/dialog-cancel.png");
static const auto SynchronizeIcon     = QStringLiteral(":/PMRWorkspacesWindow/icons/synchronize.png");
static const auto SynchronizePushIcon = QStringLiteral(":/PMRWorkspacesWindow/icons/synchronize-push.png");
static const auto SynchronizePullIcon = QStringLiteral(":/PMRWorkspacesWindow/icons/synchronize-pull.png");

//==============================================================================

PmrWorkspacesWindowWidget::PmrWorkspacesWindowWidget(PMRSupport::PmrWebService *pPmrWebService,
                                                     QWidget *pParent) :
    WebViewerWidget::WebViewerWidget(pParent),
    Core::CommonWidget(this),
    mPmrWebService(pPmrWebService),
    mWorkspaceManager(PMRSupport::PmrWorkspaceManager::instance()),
    mWorkspaceFolderUrls(QMap<QString, QString>()),
    mUrlFolderNameMines(QMap<QString, QPair<QString, bool>>()),
    mCurrentWorkspaceUrl(QString()),
    mExpandedItems(QSet<QString>()),
    mSelectedItem(QString()),
    mRowAnchor(0),
    mItemAnchors(QMap<QString, int>())
{
    // Prevent objects from being dropped on us

    setAcceptDrops(false);

    // We track the mouse ourselves

    setMouseTracking(true);

    // Connections to handle responses from PMR

    connect(mPmrWebService, SIGNAL(workspaceCloned(PMRSupport::PmrWorkspace *)),
            this, SLOT(workspaceCloned(PMRSupport::PmrWorkspace *)));
    connect(mPmrWebService, SIGNAL(workspaceCreated(const QString &)),
            this, SLOT(workspaceCreated(const QString &)));
    connect(mPmrWebService, SIGNAL(workspaceSynchronized(PMRSupport::PmrWorkspace *)),
            this, SLOT(workspaceSynchronized(PMRSupport::PmrWorkspace *)));

    // Handle workspace cloned signals from other plugins

    connect(mWorkspaceManager, SIGNAL(workspaceCloned(PMRSupport::PmrWorkspace *)),
            this, SLOT(workspaceCloned(PMRSupport::PmrWorkspace *)));

    // Retrieve the HTML template

    QString fileContents;
    Core::readFileContentsFromFile(":/PMRWorkspacesWindow/output.html", fileContents);

    mTemplate = fileContents.arg(Core::iconDataUri(CloneIcon, 16, 16),
                                 Core::iconDataUri(CommitIcon, 16, 16))
                            .arg(Core::iconDataUri(FolderIcon, 16, 16),
                                 Core::iconDataUri(FolderOpenIcon, 16, 16))
                            .arg(Core::iconDataUri(OwnedIcon, 16, 16))
                            .arg(Core::iconDataUri(StageIcon, 16, 16),
                                 Core::iconDataUri(UnstageIcon, 16, 16))
                            .arg(Core::iconDataUri(SynchronizeIcon, 16, 16),
                                 Core::iconDataUri(SynchronizePushIcon, 16, 16),
                                 Core::iconDataUri(SynchronizePullIcon, 16, 16))
                            .arg("%1");

    // Create our timer for refreshing the current workspace

    mTimer = new QTimer(this);

    // A connection to handle the timing out of our timer

    connect(mTimer, SIGNAL(timeout()),
            this, SLOT(refreshCurrentWorkspace()));

    // Keep track of when OpenCOR gets/loses the focus
    // Note: the focusWindowChanged() signal comes from QGuiApplication, so we
    //       need to check whether we are running the console version of OpenCOR
    //       or its GUI version...

    if (dynamic_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(qApp, SIGNAL(focusWindowChanged(QWindow *)),
                this, SLOT(focusWindowChanged()));
    }
}

//==============================================================================

PmrWorkspacesWindowWidget::~PmrWorkspacesWindowWidget()
{
    // Delete some internal objects

    delete mTimer;
}

//==============================================================================

QSize PmrWorkspacesWindowWidget::sizeHint() const
{
    // Suggest a default size for our PMR workspaces widget
    // Note: this is critical if we want a docked widget, with a PMR workspaces
    //        widget on it, to have a decent size when docked to the main window.

    return defaultSize(0.15);
}

//==============================================================================

static const auto SettingsWorkspaces       = QStringLiteral("Workspaces");
static const auto SettingsFolders          = QStringLiteral("Folders");
static const auto SettingsExpandedItems    = QStringLiteral("ExpandedItems");
static const auto SettingsSelectedItem     = QStringLiteral("SelectedItem");
static const auto SettingsCurrentWorkspace = QStringLiteral("CurrentWorkspace");

//==============================================================================

void PmrWorkspacesWindowWidget::loadSettings(QSettings *pSettings)
{
    pSettings->beginGroup(SettingsWorkspaces);
        // Retrieve the current workspace url, if any

        mCurrentWorkspaceUrl = pSettings->value(SettingsCurrentWorkspace).toString();

        // Retrieve the names of folders containing cloned workspaces

        QStringList folders = pSettings->value(SettingsFolders).toStringList();
        foreach (QString folder, folders) {
            if (!folder.isEmpty()) {
                QString url = addWorkspaceFolder(folder);

                // Ensure only the current workspace is expanded

                if (url == mCurrentWorkspaceUrl)
                    mExpandedItems.insert(mCurrentWorkspaceUrl);
                else if (mExpandedItems.contains(url))
                    mExpandedItems.remove(url);
            }
        }

        // Retrieve the names of expanded workspaces and folders

        mExpandedItems = pSettings->value(SettingsExpandedItems).toStringList().toSet();

        // Retrieve the currently selected item, if any

        mSelectedItem = pSettings->value(SettingsSelectedItem).toString();
    pSettings->endGroup();
}

//==============================================================================

void PmrWorkspacesWindowWidget::saveSettings(QSettings *pSettings) const
{
    pSettings->remove(SettingsWorkspaces);
    pSettings->beginGroup(SettingsWorkspaces);

        // Keep track of the names of folders containing cloned workspaces

        pSettings->setValue(SettingsFolders, QVariant(mWorkspaceFolderUrls.keys()));

        // Keep track of the names of expanded workspaces and folders

        pSettings->setValue(SettingsExpandedItems, QVariant(mExpandedItems.toList()));

        // Keep track of the currently selected item

        pSettings->setValue(SettingsSelectedItem, mSelectedItem);

        // Keep track of the current workspace url

        pSettings->setValue(SettingsCurrentWorkspace, mCurrentWorkspaceUrl);

    pSettings->endGroup();
}

//==============================================================================

void PmrWorkspacesWindowWidget::addWorkspace(PMRSupport::PmrWorkspace *pWorkspace,
                                             const bool &pOwned)
{
    QString folder = pWorkspace->path();
    QString url = pWorkspace->url();

    if (!mWorkspaceFolderUrls.contains(folder)) {
        if (mUrlFolderNameMines.contains(url)) {
            duplicateCloneMessage(url, mUrlFolderNameMines.value(url).first, folder);
        } else {
            mWorkspaceFolderUrls.insert(folder, url);
            mUrlFolderNameMines.insert(url, QPair<QString, bool>(folder, pOwned));
            mWorkspaceManager->addWorkspace(pWorkspace);
        }
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::duplicateCloneMessage(const QString &pUrl,
                                                      const QString &pPath1,
                                                      const QString &pPath2)
{
    emit warning(QString("Workspace '%1' is cloned into both '%2' and '%3'").arg(pUrl, pPath1, pPath2));
}

//==============================================================================

const QString PmrWorkspacesWindowWidget::addWorkspaceFolder(const QString &pFolder)
{
    if (!mWorkspaceFolderUrls.contains(pFolder)) {
        // Retrieve the workspace url (i.e. remote.origin.url) for the given
        // folder

        QString res = QString();

        git_repository *gitRepository = 0;
        QByteArray folderByteArray = pFolder.toUtf8();

        if (!git_repository_open(&gitRepository, folderByteArray.constData())) {
            git_strarray remotes;

            if (!git_remote_list(&remotes, gitRepository)) {
                for (int i = 0; i < (int)remotes.count; i++) {
                    char *name = remotes.strings[i];

                    if (!strcmp(name, "origin")) {
                        git_remote *remote = 0;

                        if (!git_remote_lookup(&remote, gitRepository, name)) {
                            const char *remoteUrl = git_remote_url(remote);

                            if (remoteUrl)
                                res = QString(remoteUrl);
                        }
                    }
                }
            }

            git_repository_free(gitRepository);
        }

        if (!res.isEmpty()) {
            if (mUrlFolderNameMines.contains(res)) {
                duplicateCloneMessage(res, mUrlFolderNameMines.value(res).first, pFolder);
            } else {
                mWorkspaceFolderUrls.insert(pFolder, res);
                mUrlFolderNameMines.insert(res, QPair<QString, bool>(pFolder, false));
            }
        }
        return res;
    } else {
        return mWorkspaceFolderUrls.value(pFolder);
    }
}

//==============================================================================

const QString PmrWorkspacesWindowWidget::actionHtml(const StringPairs &pActions)
{
    QStringList actions = QStringList();

    foreach (const StringPair &action, pActions)
        actions << QString("<a href=\"%1|%2\"><img class=\"button noHover %1\"></a>").arg(action.first, action.second);

    return actions.join("");
}

//==============================================================================

QString PmrWorkspacesWindowWidget::containerHtml(const QString &pClass,
                                                 const QString &pIcon,
                                                 const QString &pId,
                                                 const QString &pName,
                                                 const QString &pStatus,
                                                 const StringPairs &pActionList)
{
    static const QString html = "<tr class=\"%1\" id=\"%2\">\n"
                                "  <td class=\"icon\"><a id=\"a_%7\">%3</a></td>\n"
                                "  <td class=\"name\">%4</td>\n"
                                "  <td class=\"status\">%5</td>\n"
                                "  <td class=\"action\">%6</td>\n"
                                "</tr>\n";

    const QString iconHtml = QString("<img class=\"%1\">").arg(pIcon);
    const QString rowClass = pClass + ((pId == mSelectedItem || pId == mCurrentWorkspaceUrl)?" selected":"");

    // Use an anchor element to allow us to set the scroll position at a row

    ++mRowAnchor;
    mItemAnchors.insert(pId, mRowAnchor);

    return html.arg(rowClass, pId, iconHtml, pName, pStatus, actionHtml(pActionList)).arg(mRowAnchor);
}

//==============================================================================

QStringList PmrWorkspacesWindowWidget::fileStatusActionHtml(const QString &pPath,
                                                            const PMRSupport::CharPair &pGitStatus)
{
    static const QString statusHtml = "<span class=\"istatus\">%1</span><span class=\"wstatus\">%2</span>";

    StringPairs actionList = StringPairs();

    if (pGitStatus.second != ' ')
        actionList << StringPair(StageAction, pPath);
    else if (pGitStatus.first != ' ')
        actionList << StringPair(UnstageAction, pPath);

    return QStringList() << statusHtml.arg(pGitStatus.first).arg(pGitStatus.second)
                         << actionHtml(actionList);
}

//==============================================================================

QStringList PmrWorkspacesWindowWidget::fileStatusActionHtml(const PMRSupport::PmrWorkspace *pWorkspace,
                                                            const QString &pPath)
{
    return fileStatusActionHtml(pPath, pWorkspace->gitFileStatus(pPath));
}

//==============================================================================

QStringList PmrWorkspacesWindowWidget::fileStatusActionHtml(const PMRSupport::PmrWorkspaceFileNode *pFileNode)
{
    return fileStatusActionHtml(pFileNode->fullName(), pFileNode->status());
}

//==============================================================================

QString PmrWorkspacesWindowWidget::fileHtml(const PMRSupport::PmrWorkspaceFileNode *pFileNode)
{
    static const QString html = "<tr class=\"file%1\" id=\"%2\">\n"
                                "  <td colspan=\"2\" class=\"name\"><a id=\"a_%6\" href=\"%2\">%3</a></td>\n"
                                "  <td class=\"status\">%4</td>\n"
                                "  <td class=\"action\">%5</td>\n"
                                "</tr>\n";
    QString path = pFileNode->fullName();

    ++mRow;

    QString rowClass = (mRow % 2)?QString():" even";

    if (path == mSelectedItem)
        rowClass += " selected";

    QStringList statusActionHtml = fileStatusActionHtml(pFileNode);

    // Use an anchor element to allow us to set the scroll position at a row

    ++mRowAnchor;

    mItemAnchors.insert(path, mRowAnchor);

    return html.arg(rowClass, path, pFileNode->shortName(), statusActionHtml[0], statusActionHtml[1]).arg(mRowAnchor);
}

//==============================================================================

QString PmrWorkspacesWindowWidget::emptyContentsHtml()
{
    static const QString html = "<tr></tr>\n";

    return html;
}

//==============================================================================

QString PmrWorkspacesWindowWidget::contentsHtml(const PMRSupport::PmrWorkspaceFileNode *pFileNode,
                                                const bool &pHidden)
{
    static const QString html = "<tr class=\"contents%1\">\n"
                                "  <td></td>\n"
                                "  <td colspan=\"3\">\n"
                                "    <table>\n"
                                "      <tr class=\"spacing\">\n"
                                "        <td class=\"icon\"></td>\n"
                                "        <td class=\"name\"></td>\n"
                                "        <td class=\"status\"></td>\n"
                                "        <td class=\"action\"></td>\n"
                                "      </tr>\n"
                                "      %2\n"
                                "    </table>\n"
                                "  </td>\n"
                                "</tr>\n";

    if (pFileNode) {
        QStringList itemHtml;

        foreach(PMRSupport::PmrWorkspaceFileNode *fileNode, pFileNode->children()) {
            if (fileNode->hasChildren())
                itemHtml << folderHtml(fileNode);
            else
                itemHtml << fileHtml(fileNode);
        }

        return html.arg(pHidden?" hidden":"", itemHtml.join("\n"));
    } else {
        return emptyContentsHtml();
    }
}

//==============================================================================

QStringList PmrWorkspacesWindowWidget::workspaceHtml(const PMRSupport::PmrWorkspace *pWorkspace)
{
    QString url = pWorkspace->url();
    QString name = pWorkspace->name();
    QString path = pWorkspace->path();
    QString icon = pWorkspace->isOwned()?"owned":"folder";
    QString status = QString();
    StringPairs actionList = StringPairs();
    PMRSupport::PmrWorkspace::WorkspaceStatus workspaceStatus = pWorkspace->gitWorkspaceStatus();

    if (path.isEmpty()) {
        actionList << StringPair(CloneAction, url);
    } else if (workspaceStatus & PMRSupport::PmrWorkspace::StatusConflict) {
        // Indicate that there are merge conflicts

        status = "C";
    } else {
        // Indicate whether there are unstaged files

        if (workspaceStatus & PMRSupport::PmrWorkspace::StatusUnstaged)
            status = "?";

        // Indicate whether files are staged for commit

        if (workspaceStatus & PMRSupport::PmrWorkspace::StatusCommit)
            actionList << StringPair(CommitAction, url);

        // Show synchronisation button, with different styles to
        // indicate exactly what it will do

        if (pWorkspace->isOwned()) {
            if (workspaceStatus & PMRSupport::PmrWorkspace::StatusAhead)
                actionList << StringPair(SynchronizePushAction, url);
            else
                actionList << StringPair(SynchronizeAction, url);
        } else {
            actionList << StringPair(SynchronizePullAction, url);
        }
    }

    mRow = 0;
    QStringList html = QStringList(containerHtml("workspace", icon, url, name, status, actionList));

    if (!path.isEmpty())
        html << contentsHtml(pWorkspace->rootFileNode(), !mExpandedItems.contains(url));
    else
        html << emptyContentsHtml();

    return html;
}

//==============================================================================

QStringList PmrWorkspacesWindowWidget::folderHtml(const PMRSupport::PmrWorkspaceFileNode *pFileNode)
{
    QString fullname = pFileNode->fullName();
    QString icon = mExpandedItems.contains(fullname)?"open":"folder";

    ++mRow;

    QStringList html = QStringList(containerHtml((mRow % 2)?"folder":"folder even",
                                                 icon, fullname, pFileNode->shortName(), "",
                                                 StringPairs()));

    html << contentsHtml(pFileNode, !mExpandedItems.contains(fullname));

    return html;
}

//==============================================================================

QWebElement PmrWorkspacesWindowWidget::parentWorkspaceElement(const QWebElement &pRowElement)
{
    QWebElement workspaceElement = pRowElement;

    // Find parent workspace

    while (   !workspaceElement.isNull()
           && !(   !workspaceElement.tagName().compare("TR")
                &&  workspaceElement.hasClass("workspace"))) {
        workspaceElement = workspaceElement.parent();

        if (workspaceElement.hasClass("contents"))
            workspaceElement = workspaceElement.previousSibling();
    }

    return workspaceElement;
}

//==============================================================================

void PmrWorkspacesWindowWidget::setCurrentWorkspaceUrl(const QString &pUrl)
{
    if (pUrl != mCurrentWorkspaceUrl) {

        // Close the current workspace if we are selecting a different one

        if (!mCurrentWorkspaceUrl.isEmpty()) {
            QWebElement workspaceContents = page()->mainFrame()->documentElement().findFirst(QString("tr.workspace[id=\"%1\"] + tr").arg(mCurrentWorkspaceUrl));

            if (!workspaceContents.isNull()) {
                workspaceContents.previousSibling().removeClass("selected");
                workspaceContents.addClass("hidden");
                mExpandedItems.remove(mCurrentWorkspaceUrl);
            }
        }

        // Set the new current workspace and ensure it is expanded when displayed

        mCurrentWorkspaceUrl = pUrl;
        mExpandedItems.insert(mCurrentWorkspaceUrl);

        // Set the active directory to the workspace

        PMRSupport::PmrWorkspace *workspace = mWorkspaceManager->workspace(pUrl);

        if (workspace->isLocal())
            Core::setActiveDirectory(workspace->path());
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::startStopTimer()
{
    // Start our timer if OpenCOR is active and we have a current workspace, or stop
    // it if either OpenCOR is not active or we no longer have have a current workspace.
    // Note: If we are to start our timer, then we refresh the workspace first since
    //       waiting one second may seem long to a user.

    if (Core::opencorActive() && !mCurrentWorkspaceUrl.isEmpty() && !mTimer->isActive()) {
        disconnect(qApp, SIGNAL(focusWindowChanged(QWindow *)),
                   this, SLOT(focusWindowChanged()));

        /* TODO: To avoid redrawing on every timed refresh we could check if
           workspace->modified() (or have refreshStatus() return true if
           modifications) where the PmrWorkspace class compares (via SHA?)
           current and new status.
        */
        refreshCurrentWorkspace();

        connect(qApp, SIGNAL(focusWindowChanged(QWindow *)),
                this, SLOT(focusWindowChanged()));

        mTimer->start(1000);
    } else if ((!Core::opencorActive() || mCurrentWorkspaceUrl.isEmpty()) && mTimer->isActive()) {
        mTimer->stop();
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::focusWindowChanged()
{
    // Start/stop our timer

    startStopTimer();
}

//==============================================================================

void PmrWorkspacesWindowWidget::refreshCurrentWorkspace()
{
    refreshWorkspace(mCurrentWorkspaceUrl);
}

//==============================================================================

void PmrWorkspacesWindowWidget::setSelected(QWebElement pNewSelectedRow)
{
    QString id = pNewSelectedRow.attribute("id");

    if (!id.isEmpty() && id != mSelectedItem) {

        QWebElement trElement = page()->mainFrame()->documentElement().findFirst(
                                    QString("tr.selected[id=\"%1\"]").arg(mSelectedItem));
        if (!trElement.isNull()) trElement.removeClass("selected");

        pNewSelectedRow.addClass("selected");
        mSelectedItem = id;
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::scrollToSelected()
{
    // Position the frame so that the selected line is shown

    if (!mSelectedItem.isEmpty() && mItemAnchors.contains(mSelectedItem)) {
        // Position two rows before the selected item to show some context.

        page()->mainFrame()->scrollToAnchor(QString("a_%1").arg(mItemAnchors.value(mSelectedItem) - 2));
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::expandHtmlTree(const QString &pId)
{
    QWebElement trElement = page()->mainFrame()->documentElement().findFirst(
                                QString("tr[id=\"%1\"] + tr").arg(pId));
    if (!trElement.isNull()) {
        if (mExpandedItems.contains(pId)) {
            trElement.addClass("hidden");
            mExpandedItems.remove(pId);
        } else if (trElement.hasClass("contents")) {
            trElement.removeClass("hidden");
            mExpandedItems.insert(pId);
        }
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::clearWorkspaces()
{
    setHtml(mTemplate.arg(QString()));
}

//==============================================================================

void PmrWorkspacesWindowWidget::displayWorkspaces()
{
    PMRSupport::PmrWorkspaces workspaces = mWorkspaceManager->workspaces();

    // We want the HTML table in name order

    std::sort(workspaces.begin(), workspaces.end(), PMRSupport::PmrWorkspace::compare);

    // Reset our row anchors

    mRowAnchor = 0;
    mItemAnchors.clear();

    // Finally generate and emit HTML

    QStringList html;

    foreach (PMRSupport::PmrWorkspace *workspace, workspaces)
        html << workspaceHtml(workspace);

    setHtml(mTemplate.arg(html.join("\n")));
}

//==============================================================================

void PmrWorkspacesWindowWidget::mouseMoveEvent(QMouseEvent *pEvent)
{
    QWebElement webElement = page()->mainFrame()->hitTestContent(pEvent->pos()).element();

    while (   !webElement.isNull()
           &&  webElement.tagName().compare("A")
           &&  webElement.tagName().compare("TR")
           && !webElement.hasClass("status")
           && !webElement.hasClass("istatus")
           && !webElement.hasClass("wstatus")) {
        webElement = webElement.parent();
    }

    QString toolTip = QString();
    QCursor mouseCursor = QCursor(Qt::ArrowCursor);

    if (!webElement.isNull()) {
        if (webElement.tagName() == "A" && !webElement.attribute("href").isEmpty()) {
            QString action = webElement.attribute("href").split("|")[0];

            if (!action.compare(StageAction)) {
                toolTip = tr("Stage");
            } else if (!action.compare(UnstageAction)) {
                toolTip = tr("Unstage");
            } else if (webElement.parent().parent().hasClass("file")) {
                toolTip = tr("Open file");

                mouseCursor = QCursor(Qt::PointingHandCursor);
            } else if (!action.compare(CloneAction)) {
                toolTip = tr("Clone workspace");
            } else if (   !action.compare(SynchronizeAction)
                       || !action.compare(SynchronizePullAction)) {
                toolTip = tr("Synchronise with PMR");
            } else if (!action.compare(SynchronizePushAction)) {
                toolTip = tr("Synchronise and upload to PMR");
            } else if (!action.compare(CommitAction)) {
                toolTip = tr("Commit staged changes");
            }
        } else if (   webElement.hasClass("status")
                   || webElement.hasClass("istatus")
                   || webElement.hasClass("wstatus")) {
            QString statusChar = webElement.toPlainText().trimmed();

            if (webElement.hasClass("status")) {
                if (!statusChar.compare("C"))
                    toolTip = tr("Workspace has merge conflicts");
            } else {
                if (!statusChar.compare("A"))
                    toolTip = tr("File has been added");
                else if (!statusChar.compare("C"))
                    toolTip = tr("File has merge conflicts");
                else if (!statusChar.compare("D"))
                    toolTip = tr("File has been deleted");
                else if (!statusChar.compare("M"))
                    toolTip = tr("File has been modified");
            }
        }

        if (toolTip.isEmpty()) {
            QString link = webElement.attribute("id");

            if (webElement.hasClass("workspace")) {
                PMRSupport::PmrWorkspace *workspace = mWorkspaceManager->workspace(link);

                toolTip = QString("%1\n%2").arg(workspace->url(), workspace->path());
            }
        }
    }

    setCursor(mouseCursor);
    QToolTip::showText(mapToGlobal(pEvent->pos()), toolTip, this, QRect());
}

//==============================================================================

void PmrWorkspacesWindowWidget::mousePressEvent(QMouseEvent *pEvent)
{
    if (pEvent->button() == Qt::LeftButton) {
        // Find the containing row and highlight it

        QWebElement trElement = page()->mainFrame()->hitTestContent(pEvent->pos()).element();

        while (!trElement.isNull() && trElement.tagName().compare("TR"))
            trElement = trElement.parent();

        // Select the row that's been clicked in before doing anything else

        QString rowLink = trElement.attribute("id");

        if (!trElement.hasClass("workspace"))
            setSelected(trElement);

        QWebElement aElement = page()->mainFrame()->hitTestContent(pEvent->pos()).element();

        while (   !aElement.isNull()
               &&  aElement.tagName().compare("A")
               && aElement.tagName().compare("TR")) {
            aElement = aElement.parent();
        }

        // Check if an anchor element has been clicked

        QString aLink = aElement.attribute("href");

        if (   !aElement.isNull() && !aElement.tagName().compare("A")
            && !aLink.isEmpty()) {
            if (aElement.toPlainText().isEmpty() && aLink.contains("|")) {
                QStringList linkList = aLink.split("|");
                QString action = linkList[0];
                QString linkUrl = linkList[1];

                if (!action.compare(CloneAction)) {
                    cloneWorkspace(linkUrl);
                } else if (!action.compare(CommitAction)) {
                    commitWorkspace(linkUrl);
                } else if (   !action.compare(SynchronizeAction)
                           || !action.compare(SynchronizePullAction)) {
                    synchronizeWorkspace(linkUrl, false);
                } else if (!action.compare(SynchronizePushAction)) {
                    synchronizeWorkspace(linkUrl);
                } else if (!action.compare(StageAction) || !action.compare(UnstageAction)) {
                    QWebElement workspaceElement = parentWorkspaceElement(trElement);

                    if (!workspaceElement.isNull()) {
                        // Stage/unstage the file

                        PMRSupport::PmrWorkspace *workspace = mWorkspaceManager->workspace(workspaceElement.attribute("id"));
                        workspace->stageFile(linkUrl, (!action.compare(StageAction)));

                        // Now update the file's status and action

                        QStringList statusActionHtml = fileStatusActionHtml(workspace, linkUrl);
                        QWebElement statusElement = trElement.findFirst("td.status");

                        if (!statusElement.isNull())
                            statusElement.setInnerXml(statusActionHtml[0]);

                        QWebElement actionElement = trElement.findFirst("td.action");

                        if (!actionElement.isNull())
                            actionElement.setInnerXml(statusActionHtml[1]);

                        // And update the workspace's header icons

                        refreshWorkspace(workspace->url());
                    }
               }
            } else if (!aLink.isEmpty()) {
                // Text link clicked, e.g. to open a file

                emit openFileRequested(aLink);
            }
        } else if (trElement.hasClass("workspace")) {
            if (rowLink == mCurrentWorkspaceUrl)
                expandHtmlTree(rowLink);
            else
                setCurrentWorkspaceUrl(rowLink);

            if (mExpandedItems.contains(rowLink))
                refreshWorkspace(rowLink);
        } else if (trElement.hasClass("folder")) {
            expandHtmlTree(rowLink);
        }
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::mouseDoubleClickEvent(QMouseEvent *pEvent)
{
    Q_UNUSED(pEvent);

    // We handle this event ourselves so default event processing
    // doesn't do anything (such as follow a link or open a file).
}

//==============================================================================

void PmrWorkspacesWindowWidget::contextMenuEvent(QContextMenuEvent *pEvent)
{
    QMenu *menu = new QMenu(this);
    const QPoint &pos = pEvent->pos();
    QWebElement trElement = page()->mainFrame()->hitTestContent(pos).element();

    while (!trElement.isNull() && trElement.tagName().compare("TR"))
        trElement = trElement.parent();

    if (!trElement.isNull() && trElement.hasClass("workspace")) {
        QString elementId = trElement.attribute("id");

        if (!trElement.findFirst("img.clone").isNull()) {
            QAction *action = new QAction(QIcon(CloneIcon), tr("Clone workspace"), this);

            action->setData(QString(CloneAction+"|%1").arg(elementId));

            menu->addAction(action);
        } else {
            if (!trElement.findFirst("img.commit").isNull()) {
                QAction *action = new QAction(QIcon(CommitIcon), tr("Commit staged changes"), this);

                action->setData(QString(CommitAction+"|%1").arg(elementId));

                menu->addAction(action);
            }

            if (!trElement.findFirst("img.synchronizePull").isNull()) {
                QAction *action = new QAction(QIcon(SynchronizePullIcon), tr("Synchronise with PMR"), this);

                action->setData(QString(SynchronizePullAction+"|%1").arg(elementId));

                menu->addAction(action);
            }

            if (!trElement.findFirst("img.synchronize").isNull()) {
                QAction *action = new QAction(QIcon(SynchronizeIcon), tr("Synchronise with PMR"), this);

                action->setData(QString(SynchronizeAction+"|%1").arg(elementId));

                menu->addAction(action);
            } else if (!trElement.findFirst("img.synchronizePush").isNull()) {
                QAction *action = new QAction(QIcon(SynchronizePushIcon), tr("Synchronise and upload to PMR"), this);

                action->setData(QString(SynchronizePushAction+"|%1").arg(elementId));

                menu->addAction(action);
            }
        }

        menu->addSeparator();

        QAction *refreshAction = new QAction(QIcon(RefreshIcon), tr("Refresh display"), this);

        refreshAction->setData(QString(RefreshAction+"|%1").arg(elementId));

        menu->addAction(refreshAction);

        PMRSupport::PmrWorkspace *workspace = mWorkspaceManager->workspace(elementId);

        if (workspace) {
            QAction *action = new QAction(tr("View in PMR..."), this);

            action->setData(QString("pmr|%1").arg(workspace->url()));

            menu->addAction(action);

            if (workspace->isLocal()) {
                action = new QAction(tr("Show containing folder..."), this);

                action->setData(QString("show|%1").arg(workspace->path()));

                menu->addAction(action);
            }
        }

        menu->addSeparator();

        QAction *aboutAction = new QAction(QIcon(AboutIcon), tr("About the workspace"), this);

        aboutAction->setData(QString(AboutAction+"|%1").arg(elementId));

        menu->addAction(aboutAction);
    }

    if (!menu->isEmpty()) {
        QAction *item = menu->exec(mapToGlobal(pos));

        if (item) {
            QStringList linkList = item->data().toString().split("|");
            QString action = linkList[0];
            QString linkId = linkList[1];

            if (!action.compare(AboutAction)) {
                aboutWorkspace(linkId);
            } else if (!action.compare(CloneAction)) {
                cloneWorkspace(linkId);
            } else if (!action.compare(RefreshAction)) {
                refreshWorkspace(linkId);
            } else if (   !action.compare(SynchronizePullAction)
                       || !action.compare(SynchronizeAction)) {
                synchronizeWorkspace(linkId, false);
            } else if (!action.compare(CommitAction)) {
                commitWorkspace(linkId);
            } else if (!action.compare(SynchronizePushAction)) {
                synchronizeWorkspace(linkId);
            } else if (!action.compare("pmr")) {
                QDesktopServices::openUrl(linkId);
            } else if (!action.compare("show")) {
                showInGraphicalShell(linkId);
            }
        }
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::showInGraphicalShell(const QString &pPath)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(pPath));
}

//==============================================================================

void PmrWorkspacesWindowWidget::initialiseWorkspaceWidget(const PMRSupport::PmrWorkspaces &pWorkspaces)
{
    // First clear existing workspaces from the manager

    mWorkspaceManager->clearWorkspaces();

    // We now reconcile URLs of my-workspaces (on PMR) with those from workspace
    // folders. In doing so folders/URLs that don't correspond to an actual PMR
    // workspace are pruned from the relevant maps.

    // First clear the owned flag from the list of URLs with workspace folders

    QMutableMapIterator<QString, QPair<QString, bool>> urlsIterator(mUrlFolderNameMines);
    while (urlsIterator.hasNext()) {
        urlsIterator.next();
        urlsIterator.setValue(QPair<QString, bool>(urlsIterator.value().first, false));
    }

    foreach (PMRSupport::PmrWorkspace *workspace, pWorkspaces) {
        // Remember our workspace so we can find it by URL

        QString url = workspace->url();

        mWorkspaceManager->addWorkspace(workspace);

        // Check if we know its folder and flag it is ours

        if (mUrlFolderNameMines.contains(url)) {
            QString path = mUrlFolderNameMines.value(url).first;

            mUrlFolderNameMines.insert(url, QPair<QString, bool>(path, true));
            workspace->setPath(path);
            workspace->open();
        }
    }

    // Now check the workspace folders that aren't from my-workspaces (on PMR)

    urlsIterator.toFront();
    while (urlsIterator.hasNext()) {
        urlsIterator.next();

        if (!urlsIterator.value().second) {
            QString url = urlsIterator.key();
            PMRSupport::PmrWorkspace *workspace = mPmrWebService->workspace(url);

            if (workspace) {
                mWorkspaceManager->addWorkspace(workspace);
                workspace->setPath(urlsIterator.value().first);
                workspace->open();
            } else {
                mWorkspaceFolderUrls.remove(urlsIterator.value().first);
                urlsIterator.remove();
            }
        }
    }

    // Display the list of workspaces

    displayWorkspaces();

    // And scroll to the current selected item

    scrollToSelected();
}

//==============================================================================

void PmrWorkspacesWindowWidget::aboutWorkspace(const QString &pUrl)
{
    QWebElement workspaceElement = page()->mainFrame()->documentElement().findFirst(QString("tr.workspace[id=\"%1\"]").arg(pUrl));

    if (!workspaceElement.isNull()) {
        PMRSupport::PmrWorkspace *workspace = mWorkspaceManager->workspace(workspaceElement.attribute("id"));

        if (workspace) {
            QStringList workspaceInformation = QStringList() << workspace->name();

            if (!workspace->description().isEmpty())
                workspaceInformation << workspace->description();

            if (!workspace->owner().isEmpty())
                workspaceInformation << tr("Owner: %1").arg(workspace->owner());

            workspaceInformation << tr("PMR: <a href=\"%1\">%1</a>").arg(workspace->url());

            if (workspace->isLocal())
                workspaceInformation << tr("Path: <a href=\"file://%1/\">%1</a>").arg(workspace->path());

            emit information(workspaceInformation.join("<br></br><br></br>"));
        }
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::cloneWorkspace(const QString &pUrl)
{
    PMRSupport::PmrWorkspace *workspace = mWorkspaceManager->workspace(pUrl);

    if (workspace && !workspace->isLocal()) {
        QString dirName = PMRSupport::PmrWebService::getEmptyDirectory();

        if (!dirName.isEmpty()) {
            // Create the folder for the new workspace

            QDir workspaceFolder = QDir(dirName);

            if (!workspaceFolder.exists())
                workspaceFolder.mkpath(".");

            mPmrWebService->requestWorkspaceClone(workspace, dirName);
        }
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::commitWorkspace(const QString &pUrl)
{
    PMRSupport::PmrWorkspace *workspace = mWorkspaceManager->workspace(pUrl);

    if (workspace && workspace->isLocal()) {
        if (workspace->isMerging()) {
            workspace->commitMerge();
        } else {
            PmrWorkspacesWindowCommit *commitDialog = new PmrWorkspacesWindowCommit(workspace->stagedFiles(), Core::mainWindow());

            if (commitDialog->exec() == QDialog::Accepted)
                workspace->commit(commitDialog->message());
        }

        refreshWorkspace(workspace->url());
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::refreshWorkspace(const QString &pUrl)
{
    QWebElement workspaceElement = page()->mainFrame()->documentElement().findFirst(
                                       QString("tr.workspace[id=\"%1\"]").arg(pUrl));
    if (!workspaceElement.isNull()) {
        PMRSupport::PmrWorkspace *workspace = mWorkspaceManager->workspace(workspaceElement.attribute("id"));

        if (workspace) {
            // We have a valid workspace so refresh its status

            workspace->refreshStatus();

            // And replace the header and content rows

            QStringList htmlRows = workspaceHtml(workspace);

            // Using workspaceElement.nextSibling() directly doesn't update Xml

            QWebElement contentsElement = workspaceElement.nextSibling();

            workspaceElement.setOuterXml(htmlRows[0]);
            contentsElement.setOuterXml(htmlRows[1]);
        }
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::refreshWorkspaceFile(const QString &pPath)
{
    QWebElement fileElement = page()->mainFrame()->documentElement().findFirst(QString("tr.file[id=\"%1\"]").arg(pPath));
    QWebElement workspaceElement = parentWorkspaceElement(fileElement);

    if (!workspaceElement.isNull()) {
        PMRSupport::PmrWorkspace *workspace = mWorkspaceManager->workspace(workspaceElement.attribute("id"));

        if (workspace) {
            // We have a valid workspace so update file's status

            QStringList statusActionHtml = fileStatusActionHtml(workspace, pPath);

            QWebElement statusElement = fileElement.findFirst("td.status");

            if (!statusElement.isNull())
                statusElement.setInnerXml(statusActionHtml[0]);

            QWebElement actionElement = fileElement.findFirst("td.action");

            if (!actionElement.isNull())
                actionElement.setInnerXml(statusActionHtml[1]);
        }
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::refreshWorkspaces()
{
    // initialiseWorkspaceWidget() will be called when list received.

    mPmrWebService->requestWorkspaces();
}

//==============================================================================

void PmrWorkspacesWindowWidget::synchronizeWorkspace(const QString &pUrl,
                                                     const bool &pPush)
{
    PMRSupport::PmrWorkspace *workspace = mWorkspaceManager->workspace(pUrl);

    if (workspace && workspace->isLocal())
        mPmrWebService->requestWorkspaceSynchronize(workspace, pPush);
}

//==============================================================================

void PmrWorkspacesWindowWidget::workspaceCreated(const QString &pUrl)
{
    Q_UNUSED(pUrl);

    // Ignore as workspaceCloned() will be called next
}

//==============================================================================

void PmrWorkspacesWindowWidget::workspaceCloned(PMRSupport::PmrWorkspace *pWorkspace)
{
    if (pWorkspace) {
        QString url = pWorkspace->url();

        // Ensure our widget knows about the workspace

        if (!mUrlFolderNameMines.contains(url))
            addWorkspace(pWorkspace);

        // Close display of current workspace and set the cloned one current

        setCurrentWorkspaceUrl(url);

        // Redisplay with workspace expanded and selected

        pWorkspace->open();
        displayWorkspaces();
        scrollToSelected();
    }
}

//==============================================================================

void PmrWorkspacesWindowWidget::workspaceSynchronized(PMRSupport::PmrWorkspace *pWorkspace)
{
    if (pWorkspace) {
        refreshWorkspace(pWorkspace->url());
    }
}

//==============================================================================

}   // namespace PMRWorkspacesWindow
}   // namespace OpenCOR

//==============================================================================
// End of file
//==============================================================================