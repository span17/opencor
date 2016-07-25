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
// BioSignalML data store plugin
//==============================================================================

#include "biosignalmldatastoredata.h"
#include "biosignalmldatastoreexporter.h"
#include "biosignalmldatastoreplugin.h"
#include "corecliutils.h"
#include "coreguiutils.h"
#include "datastoredialog.h"

//==============================================================================

#include <QApplication>
#include <QMainWindow>
#include <QSettings>

//==============================================================================

namespace OpenCOR {
namespace BioSignalMLDataStore {

//==============================================================================

PLUGININFO_FUNC BioSignalMLDataStorePluginInfo()
{
    Descriptions descriptions;

    descriptions.insert("en", QString::fromUtf8("a BioSignalML specific data store plugin."));
    descriptions.insert("fr", QString::fromUtf8("une extension de magasin de données spécifique à BioSignalML."));

    return new PluginInfo("Data Store", true, false,
                          QStringList() << "BioSignalMLAPI" << "DataStore",
                          descriptions);
}

//==============================================================================
// I18n interface
//==============================================================================

void BioSignalMLDataStorePlugin::retranslateUi()
{
    // We don't handle this interface...
    // Note: even though we don't handle this interface, we still want to
    //       support it since some other aspects of our plugin are
    //       multilingual...
}

//==============================================================================
// Data store interface
//==============================================================================

QString BioSignalMLDataStorePlugin::dataStoreName() const
{
    // Return the name of the data store

    return "BioSignalML";
}

//==============================================================================

DataStore::DataStoreData * BioSignalMLDataStorePlugin::getData(const QString &pFileName,
                                                               DataStore::DataStore *pDataStore) const
{
    // Ask which data should be exported, as well as some other information

    DataStore::DataStoreDialog dataStoreDialog(pDataStore, Core::mainWindow());

    if (dataStoreDialog.exec()) {
        // Now that we have the information we need, we can ask for the name of
        // the BioSignalML file where to do the export

        QString biosignalmlFilter = QObject::tr("BioSignalML File")+" (*.biosignalml)";
        QString fileName = Core::getSaveFileName(QObject::tr("Export To BioSignalML"),
                                                 Core::newFileName(pFileName, QObject::tr("Data"), false, "biosignalml"),
                                                 QStringList() << biosignalmlFilter,
                                                 &biosignalmlFilter);

        if (!fileName.isEmpty())
            return new BiosignalmlDataStoreData(fileName, dataStoreDialog.selectedData());
    }

    return 0;
}

//==============================================================================

DataStore::DataStoreExporter * BioSignalMLDataStorePlugin::dataStoreExporterInstance(const QString &pFileName,
                                                                                     DataStore::DataStore *pDataStore,
                                                                                     DataStore::DataStoreData *pDataStoreData) const
{
    // Return an instance of our BioSignalML data store exporter

    return new BiosignalmlDataStoreExporter(pFileName, pDataStore, pDataStoreData);
}

//==============================================================================

}   // namespace BioSignalMLDataStore
}   // namespace OpenCOR

//==============================================================================
// End of file
//==============================================================================
