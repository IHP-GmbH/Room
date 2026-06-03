#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace cdb {

class QucsImporter {
public:
    struct Options {
        std::string libName = "qucs_import";
        std::string cellName; // empty = derive from .sch file name
    };

    QucsImporter();
    explicit QucsImporter(const Options &options);

    Database importFile(const std::string &schPath) const;

    const std::vector<std::string> &warnings() const { return warnings_; }
    const std::vector<std::string> &errors() const { return errors_; }

private:
    Options options_;
    mutable std::vector<std::string> warnings_;
    mutable std::vector<std::string> errors_;
};

} // namespace cdb
