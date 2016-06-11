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
// BioSignalML data store exporter class
//==============================================================================

#ifndef BioSignalMLDATASTOREEXPORTER_H
#define BioSignalMLDATASTOREEXPORTER_H

//==============================================================================

#include "biosignalmldatastoreglobal.h"
#include "biosignalmldatastoresavedialog.h"
#include "datastoreinterface.h"

//==============================================================================

#include <QMainWindow>

//==============================================================================

namespace OpenCOR {
namespace BioSignalMLDataStore {

//==============================================================================

class BioSignalMLDATASTORE_EXPORT BioSignalMLExporter : public DataStore::DataStoreExporter
{
public:
    BioSignalMLExporter(const QString &pId = QString());
    virtual void execute(const QString &pFileName,
                         DataStore::DataStore *pDataStore) const;

private:
    BioSignalMLSaveDialog *mSaveDialog;
};

//==============================================================================

}   // namespace BioSignalMLDataStore
}   // namespace OpenCOR

//==============================================================================

#endif

//==============================================================================
// End of file
//==============================================================================
