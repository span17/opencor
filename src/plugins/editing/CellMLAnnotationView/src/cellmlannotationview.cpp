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
// CellML Annotation view
//==============================================================================

#include "cellmlannotationview.h"

//==============================================================================

namespace OpenCOR {
namespace CellMLAnnotationView {

//==============================================================================

QString resourceUrl(const QString &pResource)
{
    // Return the URL for the given resource

    return "http://identifiers.org/"+pResource+"/?redirect=true";
}

//==============================================================================

QString idUrl(const QString &pResource, const QString &pId)
{
    // Return the URL for the given resource

    return "http://identifiers.org/"+pResource+"/"+pId+"/?profile=most_reliable&redirect=true";
}

//==============================================================================

}   // namespace CellMLAnnotationView
}   // namespace OpenCOR

//==============================================================================
// End of file
//==============================================================================
