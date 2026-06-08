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

    Database importFile(const std::string &oasPath) const;

    const std::vector<std::string> &warnings() const { return warnings_; }
    const std::vector<std::string> &errors() const { return errors_; }

private:
    Options options_;
    mutable std::vector<std::string> warnings_;
    mutable std::vector<std::string> errors_;
};

} // namespace core
