#pragma once

#include "database.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief Exports schematic/symbol cells from a CORE Database to native Xschem files.
 *****************************************************************************************/
class XschemExporter {
public:
    void                                                exportCell(const Database &db, const std::string &cellName,
                                                                     const std::string &outputPath) const;
    void                                                exportCell(const Database &db, const std::string &cellName,
                                                                     std::ostream &out) const;
    std::string                                         exportCellToString(const Database &db, const std::string &cellName) const;
    std::size_t                                         exportAll(const Database &db, const std::string &outputDir) const;

    const std::vector<std::string> &                    warnings() const { return m_warnings; }
    const std::vector<std::string> &                    errors() const { return m_errors; }

private:
    mutable std::vector<std::string>                    m_warnings;
    mutable std::vector<std::string>                    m_errors;
};

} // namespace core
