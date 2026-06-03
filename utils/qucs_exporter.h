#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace cdb {

class QucsExporter {
public:
    struct Options {
        std::string qucsVersion = "0.0.19";
    };

    QucsExporter();
    explicit QucsExporter(const Options &options);

    void exportCell(const Database &db, const std::string &cellName, const std::string &schPath) const;

    const std::vector<std::string> &warnings() const { return warnings_; }
    const std::vector<std::string> &errors() const { return errors_; }

private:
    Options options_;
    mutable std::vector<std::string> warnings_;
    mutable std::vector<std::string> errors_;
};

} // namespace cdb
