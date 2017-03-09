
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
// Python wrapper for SingleCellView classes
//==============================================================================

#pragma once

//==============================================================================

#include "PythonQtPythonInclude.h"

//==============================================================================

#include <QList>
#include <QObject>

//==============================================================================

namespace OpenCOR {

//==============================================================================

namespace DataStore {
    class DataStoreVariable;
}

namespace SingleCellView {
    class SingleCellViewSimulation;
    class SingleCellViewSimulationData;
    class SingleCellViewSimulationResults;
    class SingleCellViewSimulationWidget;
};

//==============================================================================

namespace PythonWrapper {

//==============================================================================

class PythonWrapperSingleCellView : public QObject
{
    Q_OBJECT

public:
    void initialise(PyObject *pModule);

private:
    static PyObject * OpenCOR_simulation(PyObject *self,  PyObject *args);
    static PyObject * OpenCOR_simulations(PyObject *self, PyObject *args);

public slots:
    bool run(OpenCOR::SingleCellView::SingleCellViewSimulation *pSingleCellViewSimulation);
    OpenCOR::SingleCellView::SingleCellViewSimulationWidget * widget(OpenCOR::SingleCellView::SingleCellViewSimulation *pSingleCellViewSimulation) const;

    PyObject * constants(OpenCOR::SingleCellView::SingleCellViewSimulationData *pSingleCellViewSimulationData) const;
    PyObject * rates(OpenCOR::SingleCellView::SingleCellViewSimulationData *pSingleCellViewSimulationData) const;
    PyObject * states(OpenCOR::SingleCellView::SingleCellViewSimulationData *pSingleCellViewSimulationData) const;
    PyObject * algebraic(OpenCOR::SingleCellView::SingleCellViewSimulationData *pSingleCellViewSimulationData) const;
    PyObject * condVar(OpenCOR::SingleCellView::SingleCellViewSimulationData *pSingleCellViewSimulationData) const;

    OpenCOR::DataStore::DataStoreVariable * points(
        OpenCOR::SingleCellView::SingleCellViewSimulationResults *pSingleCellViewSimulationResults) const;
    QList<OpenCOR::DataStore::DataStoreVariable *> constants(
        OpenCOR::SingleCellView::SingleCellViewSimulationResults *pSingleCellViewSimulationResults) const;
    QList<OpenCOR::DataStore::DataStoreVariable *> rates(
        OpenCOR::SingleCellView::SingleCellViewSimulationResults *pSingleCellViewSimulationResults) const;
    QList<OpenCOR::DataStore::DataStoreVariable *> states(
        OpenCOR::SingleCellView::SingleCellViewSimulationResults *pSingleCellViewSimulationResults) const;
    QList<OpenCOR::DataStore::DataStoreVariable *> algebraic(
        OpenCOR::SingleCellView::SingleCellViewSimulationResults *pSingleCellViewSimulationResults) const;
};

//==============================================================================

}   // namespace PythonWrapper
}   // namespace OpenCOR

//==============================================================================
// End of file
//==============================================================================
