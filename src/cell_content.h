#pragma once

#include "block.h"
#include "types.h"

#include <vector>

namespace cdb {

class CellContent {
public:
    CellContent(ViewType viewType, double dbuPerMicron = 1000.0);

    ViewType viewType() const { return viewType_; }
    void setViewType(ViewType type) { viewType_ = type; }

    double dbuPerMicron() const { return dbuPerMicron_; }
    void setDbuPerMicron(double value) { dbuPerMicron_ = value; }

    std::vector<LayerSpec> &layers() { return layers_; }
    const std::vector<LayerSpec> &layers() const { return layers_; }

    std::vector<Property> &properties() { return properties_; }
    const std::vector<Property> &properties() const { return properties_; }

    Block &block() { return block_; }
    const Block &block() const { return block_; }

private:
    ViewType viewType_;
    double dbuPerMicron_;
    std::vector<LayerSpec> layers_;
    std::vector<Property> properties_;
    Block block_;
};

} // namespace cdb
