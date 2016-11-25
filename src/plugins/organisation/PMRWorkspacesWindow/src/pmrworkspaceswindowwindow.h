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
// PMR Workspaces window
//==============================================================================

#pragma once

//==============================================================================

#include "organisationwidget.h"

//==============================================================================

namespace Ui {
    class PmrWorkspacesWindowWindow;
}

//==============================================================================

class QGraphicsColorizeEffect;

//==============================================================================

namespace OpenCOR {

//==============================================================================

namespace PMRSupport {
    class PmrWebService;
}   // namespace PMRSupport

//==============================================================================

namespace PMRWorkspacesWindow {

//==============================================================================

class PmrWorkspacesWindowWidget;

//==============================================================================

class PmrWorkspacesWindowWindow : public Core::OrganisationWidget
{
    Q_OBJECT

public:
    explicit PmrWorkspacesWindowWindow(QWidget *pParent);
    ~PmrWorkspacesWindowWindow();

    virtual void retranslateUi();

    virtual void loadSettings(QSettings *pSettings);
    virtual void saveSettings(QSettings *pSettings) const;

    void fileRenamed(const QString &pOldFileName, const QString &pNewFileName);
    void fileReloaded(const QString &pFileName);

protected:
    virtual void resizeEvent(QResizeEvent *pEvent);

private:
    Ui::PmrWorkspacesWindowWindow *mGui;

    PMRSupport::PmrWebService *mPmrWebService;
    PmrWorkspacesWindowWidget *mPmrWorkspacesWindowWidget;

    bool mAuthenticated;

    QGraphicsColorizeEffect *mColorizeEffect;

    void retranslateActionPmr();

private slots:
    void on_actionNew_triggered();
    void on_actionReload_triggered();
    void on_actionPmr_triggered();

    void busy(const bool &pBusy);

    void showInformation(const QString &pMessage);
    void showWarning(const QString &pMessage);
    void showError(const QString &pMessage);

    void updateGui();

    void retrieveWorkspaces(const bool &pVisible);
};

//==============================================================================

}   // namespace PMRWorkspacesWindow
}   // namespace OpenCOR

//==============================================================================
// End of file
//==============================================================================