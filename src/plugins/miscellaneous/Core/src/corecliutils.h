/*******************************************************************************

Copyright (C) The University of Auckland

OpenCOR is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

OpenCOR is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*******************************************************************************/

//==============================================================================
// Core CLI utilities
//==============================================================================

#pragma once

//==============================================================================

#include "coreglobal.h"
#include "plugin.h"
#include "plugininfo.h"

//==============================================================================

#include <QAbstractMessageHandler>
#include <QByteArray>
#include <QCoreApplication>
#include <QDomDocument>
#include <QSourceLocation>
#include <QSslError>
#include <QUrl>

//==============================================================================

namespace OpenCOR {
namespace Core {

//==============================================================================

#include "corecliutils.h.inl"

//==============================================================================

}   // namespace Core
}   // namespace OpenCOR

//==============================================================================

typedef QList<bool> QBoolList;
typedef QList<int> QIntList;

//==============================================================================

QBoolList CORE_EXPORT qVariantListToBoolList(const QVariantList &pVariantList);
QVariantList CORE_EXPORT qBoolListToVariantList(const QBoolList &pBoolList);

QIntList CORE_EXPORT qVariantListToIntList(const QVariantList &pVariantList);
QVariantList CORE_EXPORT qIntListToVariantList(const QIntList &pIntList);

//==============================================================================

class QNetworkReply;

//==============================================================================

namespace OpenCOR {
namespace Core {

//==============================================================================

class CORE_EXPORT DummyMessageHandler : public QAbstractMessageHandler
{
    Q_OBJECT

protected:
    virtual void handleMessage(QtMsgType pType, const QString &pDescription,
                               const QUrl &pIdentifier,
                               const QSourceLocation &pSourceLocation);
};

//==============================================================================
// Note: both cliutils.h and corecliutils.h must specifically define
//       SynchronousFileDownloader. To have it in corecliutils.h.inl is NOT good
//       enough since the MOC won't pick it up...

class SynchronousFileDownloader : public QObject
{
    Q_OBJECT

public:
    bool download(const QString &pUrl, QByteArray &pContents,
                  QString *pErrorMessage) const;

private slots:
    void networkAccessManagerSslErrors(QNetworkReply *pNetworkReply,
                                       const QList<QSslError> &pSslErrors);
};

//==============================================================================

qulonglong CORE_EXPORT totalMemory();
qulonglong CORE_EXPORT freeMemory();

QString CORE_EXPORT digitGroupNumber(const QString &pNumber);

QString CORE_EXPORT sizeAsString(const double &pSize,
                                 const int &pPrecision = 1);

QString CORE_EXPORT sha1(const QByteArray &pByteArray);
QString CORE_EXPORT sha1(const QString &pString);

void CORE_EXPORT stringPositionAsLineColumn(const QString &pString,
                                            const QString &pEol,
                                            const int &pPosition, int &pLine,
                                            int &pColumn);
void CORE_EXPORT stringLineColumnAsPosition(const QString &pString,
                                            const QString &pEol,
                                            const int &pLine,
                                            const int &pColumn, int &pPosition);

void CORE_EXPORT * globalInstance(const QString &pObjectName,
                                  void *pDefaultGlobalInstance = 0);

QString CORE_EXPORT activeDirectory();
void CORE_EXPORT setActiveDirectory(const QString &pDirName);

bool CORE_EXPORT isDirectory(const QString &pDirName);
bool CORE_EXPORT isEmptyDirectory(const QString &pDirName);

void CORE_EXPORT doNothing(const int &pMax);

void CORE_EXPORT checkFileNameOrUrl(const QString &pInFileNameOrUrl,
                                    bool &pOutIsLocalFile,
                                    QString &pOutFileNameOrUrl);

QString CORE_EXPORT formatXml(const QString &pXml);

QString CORE_EXPORT cleanContentMathml(const QString &pContentMathml);
QString CORE_EXPORT cleanPresentationMathml(const QString &pPresentationMathml);

QString CORE_EXPORT newFileName(const QString &pFileName,
                                const QString &pExtra,
                                const bool &pBefore,
                                const QString &pFileExtension);
QString CORE_EXPORT newFileName(const QString &pFileName,
                                const QString &pExtra,
                                const bool &pBefore);
QString CORE_EXPORT newFileName(const QString &pFileName,
                                const QString &pFileExtension);

bool CORE_EXPORT validXml(const QString &pXml, const QString &pSchema);
bool CORE_EXPORT validXmlFile(const QString &pXmlFileName,
                              const QString &pSchemaFileName);

QByteArray CORE_EXPORT serialiseDomDocument(const QDomDocument &pDomDocument);

//==============================================================================

}   // namespace Core
}   // namespace OpenCOR

//==============================================================================
// End of file
//==============================================================================
