#pragma once

#include "block.h"
#include "types.h"

namespace core {

class CellContent {
public:
    CellContent(ViewType viewType, double dbuPerMicron = 1000.0);

    ViewType viewType() const { return viewType_; }
    void setViewType(ViewType type) { viewType_ = type; }

    double dbuPerMicron() const { return dbuPerMicron_; }
    void setDbuPerMicron(double value) { dbuPerMicron_ = value; }

    std::vector<Property> &properties() { return properties_; }
    const std::vector<Property> &properties() const { return properties_; }

    Block &block() { return block_; }
    const Block &block() const { return block_; }

private:
    ViewType viewType_;
    double dbuPerMicron_;
    std::vector<Property> properties_;
    Block block_;
};

} // namespace core
