/*!****************************************************************************************
 * \file oas_exporter.cpp
 * \brief Native OASIS export from a ROOM Database.
 *****************************************************************************************/

#include "oas_exporter.h"

#include "oas_writer.h"

namespace room {

void OasExporter::exportFile(const Database &db, const std::string &oasPath) const
{
    m_warnings.clear();
    m_errors.clear();

    OasWriter writer(oasPath);
    writer.exportDatabase(db);
    m_errors.insert(m_errors.end(), writer.errors().begin(), writer.errors().end());
}

} // namespace room
