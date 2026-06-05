#pragma once

#include "database.h"

#include <ostream>
#include <string>

namespace core {

class TextDumper {
public:
    void dump(const Database &db, std::ostream &out) const;
    void dumpToFile(const Database &db, const std::string &path) const;
};

} // namespace core
