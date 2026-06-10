#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief The OasImporter class reads OASIS layout files into a CORE Database.
 *
 * Currently decodes OAS via a temporary GDS conversion using KLayout, then uses GdsImporter.
 *****************************************************************************************/
class OasImporter {
public:
    /*! \brief Import options for library name and DBU scale. */
    struct Options {
        std::string libName = "oas_import";
        double defaultDbuPerMicron = 1000.0;
    };

    OasImporter();
    explicit OasImporter(const Options &options);

    Database                                            importFile(const std::string &oasPath) const;

    const std::vector<std::string> &                    warnings() const { return m_warnings; }
    const std::vector<std::string> &                    errors() const { return m_errors; }

private:
    Options                                             m_options;
    mutable std::vector<std::string>                    m_warnings;
    mutable std::vector<std::string>                    m_errors;
};

} // namespace core
