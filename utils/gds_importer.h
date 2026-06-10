#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief The GdsImporter class reads GDSII layout files into a CORE Database.
 *****************************************************************************************/
class GdsImporter {
public:
    /*! \brief Import options for library name and DBU scale. */
    struct Options {
        std::string libName = "gds_import";
        double defaultDbuPerMicron = 1000.0;
    };

    GdsImporter();
    explicit GdsImporter(const Options &options);

    Database                                            importFile(const std::string &gdsPath) const;

    const std::vector<std::string> &                    warnings() const { return m_warnings; }
    const std::vector<std::string> &                    errors() const { return m_errors; }

private:
    Options                                             m_options;
    mutable std::vector<std::string>                    m_warnings;
    mutable std::vector<std::string>                    m_errors;
};

} // namespace core
