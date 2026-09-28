#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace room {

/*!****************************************************************************************
 * \brief The OasExporter class writes a ROOM Database to an OASIS layout file.
 *
 * Exports strict-mode OASIS via OasWriter (KLayout-compatible START + native geometry).
 *****************************************************************************************/
class OasExporter {
public:
    void                                                exportFile(const Database &db, const std::string &oasPath) const;

    const std::vector<std::string> &                    warnings() const { return m_warnings; }
    const std::vector<std::string> &                    errors() const { return m_errors; }

private:
    mutable std::vector<std::string>                    m_warnings;
    mutable std::vector<std::string>                    m_errors;
};

} // namespace room
