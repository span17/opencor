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
// Plugin interface
//==============================================================================

#ifdef INTERFACE_DEFINITION
    #define PURE = 0
#else
    #define PURE
#endif

    // Note: changes to this interface must result in pluginVersion() being
    //       updated (see [OpenCOR]/src/plugins/pluginInfo.cpp)...

    virtual bool definesInterfaces() PURE;

    virtual void initializePlugin() PURE;
    virtual void finalizePlugin() PURE;

    virtual void pluginsInitialized(const Plugins &pLoadedPlugins) PURE;

    virtual void loadSettings(QSettings *pSettings) PURE;
    virtual void saveSettings(QSettings *pSettings) const PURE;

    virtual void handleUrl(const QUrl &pUrl) PURE;

#undef PURE

//==============================================================================
// End of file
//==============================================================================
