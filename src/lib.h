#pragma once

#include "cell.h"
#include "types.h"

#include <string>
#include <vector>

namespace core {

class Lib {
public:
    explicit Lib(std::string name);

    const std::string &name() const { return name_; }

    std::vector<Property> &properties() { return properties_; }
    const std::vector<Property> &properties() const { return properties_; }

    std::vector<Cell> &cells() { return cells_; }
    const std::vector<Cell> &cells() const { return cells_; }

    Cell *findCell(const std::string &name);
    const Cell *findCell(const std::string &name) const;
    Cell &getOrCreateCell(const std::string &name);

private:
    std::string name_;
    std::vector<Property> properties_;
    std::vector<Cell> cells_;
};

} // namespace core
