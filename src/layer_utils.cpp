#include "layer_utils.h"

#include "shape.h"

#include <unordered_set>

namespace core {
namespace {

std::uint32_t shapeLayerId(const Shape &shape)
{
    switch (shape.type()) {
    case Shape::Type::Rect:
        if (const Shape::RectData *rect = shape.rect()) {
            return rect->layerId;
        }
        break;
    case Shape::Type::Polygon:
        if (const Shape::PolygonData *poly = shape.polygon()) {
            return poly->layerId;
        }
        break;
    case Shape::Type::Path:
        if (const Shape::PathData *path = shape.path()) {
            return path->layerId;
        }
        break;
    case Shape::Type::Text:
        if (const Shape::TextData *text = shape.text()) {
            return text->layerId;
        }
        break;
    }
    return 0;
}

const LayerSpec *libLayerAt(const std::vector<LayerSpec> &libLayers, std::uint32_t layerId)
{
    if (layerId >= libLayers.size()) {
        return nullptr;
    }
    return &libLayers[layerId];
}

} // namespace

std::vector<LayerSpec> collectLayersForBlock(const Block &block, const std::vector<LayerSpec> &libLayers)
{
    std::vector<LayerSpec> layers;
    std::unordered_set<std::uint32_t> seen;
    for (const Shape &shape : block.shapes()) {
        const std::uint32_t layerId = shapeLayerId(shape);
        if (!seen.insert(layerId).second) {
            continue;
        }
        if (const LayerSpec *spec = libLayerAt(libLayers, layerId)) {
            layers.push_back(*spec);
        }
    }
    return layers;
}

const std::vector<LayerSpec> &resolveViewLayers(const CellContent &content, const Lib &lib)
{
    if (!content.layers().empty()) {
        return content.layers();
    }
    return lib.layers();
}

std::vector<LayerSpec> layersForSerialization(const CellContent &content,
                                              const std::vector<LayerSpec> &libLayers)
{
    if (!content.layers().empty()) {
        return content.layers();
    }
    return collectLayersForBlock(content.block(), libLayers);
}

std::uint32_t findOrAddViewLayer(CellContent &content,
                                 const LayerSpec &spec,
                                 const std::vector<LayerSpec> &libLayers)
{
    auto &viewLayers = content.layers();
    for (std::size_t i = 0; i < viewLayers.size(); ++i) {
        if (viewLayers[i].layerNum == spec.layerNum && viewLayers[i].dataType == spec.dataType) {
            mergeLayerPurpose(viewLayers[i], spec.purpose);
            if (viewLayers[i].name.empty() && !spec.name.empty()) {
                viewLayers[i].name = spec.name;
            }
            return static_cast<std::uint32_t>(i);
        }
    }

    LayerSpec layer = spec;
    if (layer.name.empty()) {
        for (const LayerSpec &libLayer : libLayers) {
            if (libLayer.layerNum == spec.layerNum && libLayer.dataType == spec.dataType) {
                layer.name = libLayer.name;
                break;
            }
        }
    }
    viewLayers.push_back(layer);
    return static_cast<std::uint32_t>(viewLayers.size() - 1);
}

LayerPurpose gdsShapePurpose(Shape::Type shapeType)
{
    switch (shapeType) {
    case Shape::Type::Text:
        return LayerPurpose::Label;
    case Shape::Type::Path:
        return LayerPurpose::Wire;
    case Shape::Type::Polygon:
    case Shape::Type::Rect:
        return LayerPurpose::Boundary;
    }
    return LayerPurpose::Drawing;
}

void mergeLayerPurpose(LayerSpec &layer, LayerPurpose purpose)
{
    if (layer.purpose == LayerPurpose::Drawing && purpose != LayerPurpose::Drawing) {
        layer.purpose = purpose;
    }
}

} // namespace core
