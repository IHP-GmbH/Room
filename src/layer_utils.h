#pragma once

#include "block.h"
#include "cell_content.h"
#include "enums.h"
#include "layer_spec.h"
#include "lib.h"

#include <cstdint>
#include <vector>

namespace room {

std::vector<LayerSpec> collectLayersForBlock(const Block &block, const std::vector<LayerSpec> &libLayers);

const std::vector<LayerSpec> &resolveViewLayers(const CellContent &content, const Lib &lib);

std::vector<LayerSpec> layersForSerialization(const CellContent &content, const std::vector<LayerSpec> &libLayers);

std::uint32_t findOrAddViewLayer(CellContent &content,
                                 const LayerSpec &spec,
                                 const std::vector<LayerSpec> &libLayers);

LayerPurpose gdsShapePurpose(Shape::Type shapeType);

void mergeLayerPurpose(LayerSpec &layer, LayerPurpose purpose);

} // namespace room
