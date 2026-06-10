#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief The QucsExporter class writes a schematic cell from a CORE Database to a .sch file.
 *****************************************************************************************/
class QucsExporter {
public:
    /*! \brief Export options such as Qucs version string in the output header. */
    struct Options {
        std::string qucsVersion = "0.0.19";
    };

    QucsExporter();
    explicit QucsExporter(const Options &options);

    void                                                exportCell(const Database &db, const std::string &cellName,
                                                                 const std::string &schPath) const;

    const std::vector<std::string> &                    warnings() const { return m_warnings; }
    const std::vector<std::string> &                    errors() const { return m_errors; }

private:
    Options                                             m_options;
    mutable std::vector<std::string>                    m_warnings;
    mutable std::vector<std::string>                    m_errors;
};

} // namespace core
