#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace core {

class QucsImporter {
public:
    struct Options {
        std::string libName = "qucs_import";
        std::string cellName; // empty = derive from .sch file name
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
