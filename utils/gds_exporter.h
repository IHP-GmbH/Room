#pragma once

#include "database.h"

#include <string>
#include <vector>

namespace core {

class GdsExporter {
public:
    void exportFile(const Database &db, const std::string &gdsPath) const;

    const std::vector<std::string> &warnings() const { return warnings_; }
    const std::vector<std::string> &errors() const { return errors_; }

private:
    mutable std::vector<std::string> warnings_;
    mutable std::vector<std::string> errors_;
};

} // namespace core
