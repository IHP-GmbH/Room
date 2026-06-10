#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief The QucsImporter class reads Qucs schematic (.sch) files into a CORE Database.
 *****************************************************************************************/
class QucsImporter {
public:
    /*! \brief Import options for library and cell naming. */
    struct Options {
        std::string libName = "qucs_import";
        std::string cellName; /*!< Empty = derive cell name from .sch file name. */
    };

    QucsImporter();
    explicit QucsImporter(const Options &options);

    Database                                            importFile(const std::string &schPath) const;

    const std::vector<std::string> &                    warnings() const { return m_warnings; }
    const std::vector<std::string> &                    errors() const { return m_errors; }

private:
    Options                                             m_options;
    mutable std::vector<std::string>                    m_warnings;
    mutable std::vector<std::string>                    m_errors;
};

} // namespace core
