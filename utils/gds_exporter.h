#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief The GdsExporter class writes a CORE Database to a GDSII layout file.
 *****************************************************************************************/
class GdsExporter {
public:
    void                                                exportFile(const Database &db, const std::string &gdsPath) const;

    const std::vector<std::string> &                    warnings() const { return m_warnings; }
    const std::vector<std::string> &                    errors() const { return m_errors; }

private:
    mutable std::vector<std::string>                    m_warnings;
    mutable std::vector<std::string>                    m_errors;
};

} // namespace core
