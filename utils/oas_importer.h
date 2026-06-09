#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace core {

// OAS -> CORE import. File I/O currently uses KLayout to decode/encode OAS geometry;
// data is stored in the native CORE Database via GdsImporter/GdsExporter internally.

class OasImporter {
public:
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
