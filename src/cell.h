#pragma once

#include "cell_content.h"
#include "types.h"

#include <string>
#include <vector>

namespace cdb {

class Cell {
public:
    explicit Cell(std::string name);

    const std::string &name() const { return name_; }

    std::vector<Property> &properties() { return properties_; }
    const std::vector<Property> &properties() const { return properties_; }

    std::vector<CellContent> &contents() { return contents_; }
    const std::vector<CellContent> &contents() const { return contents_; }

    CellContent *findContent(ViewType type);
    const CellContent *findContent(ViewType type) const;
    CellContent &getOrCreateContent(ViewType type, double dbuPerMicron = 1000.0);

private:
    std::string name_;
    std::vector<Property> properties_;
    std::vector<CellContent> contents_;
};

} // namespace cdb
