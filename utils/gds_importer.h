#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace core {

class GdsImporter {
public:
    struct Options {
        std::string libName = "gds_import";
        double defaultDbuPerMicron = 1000.0;
    };

    GdsImporter();
    explicit GdsImporter(const Options &options);

    Database importFile(const std::string &gdsPath) const;

    const std::vector<std::string> &warnings() const { return warnings_; }
    const std::vector<std::string> &errors() const { return errors_; }

private:
    Options options_;
    mutable std::vector<std::string> warnings_;
    mutable std::vector<std::string> errors_;
};

} // namespace core
