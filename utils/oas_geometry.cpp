#include "oas_geometry.h"

#include "cell.h"
#include "layer_utils.h"

namespace core {

OasImportContext::OasImportContext(Database &db, std::string libName, double defaultDbuPerMicron)
    : m_db(db), m_dbuPerMicron(defaultDbuPerMicron)
{
    m_db.setGenerator("CORE OasImporter");
    m_db.lib() = Lib(std::move(libName));
}

void OasImportContext::setDbuPerMicron(double value)
{
    if (value > 0.0) {
        m_dbuPerMicron = value;
        if (m_currentContent != nullptr) {
            m_currentContent->setDbuPerMicron(m_dbuPerMicron);
        }
    }
}

void OasImportContext::setModalLayer(std::uint64_t layer, std::uint64_t datatype)
{
    m_modalLayer = layer;
    m_modalDatatype = datatype;
}

void OasImportContext::registerLayerRef(std::uint64_t ref, std::uint16_t layer, std::uint16_t datatype)
{
    m_layerRef[ref] = layer;
    m_modalLayer = layer;
    m_modalDatatype = datatype;
}

void OasImportContext::registerDatatypeRef(std::uint64_t ref, std::uint16_t datatype)
{
    m_datatypeRef[ref] = datatype;
    m_modalDatatype = datatype;
}

void OasImportContext::syncCurrentCell()
{
    if (m_currentCellName.empty()) {
        m_currentCell = nullptr;
        m_currentContent = nullptr;
        m_currentBlock = nullptr;
        return;
    }
    m_currentCell = &m_db.lib().getOrCreateCell(m_currentCellName);
    m_currentContent = &m_currentCell->getOrCreateContent(ViewType::Layout, m_dbuPerMicron);
    m_currentContent->setDbuPerMicron(m_dbuPerMicron);
    m_currentBlock = &m_currentContent->block();
}

void OasImportContext::beginCell(const std::string &name)
{
    m_currentCellName = name;
    syncCurrentCell();
}

std::uint32_t OasImportContext::ensureViewLayer(std::uint16_t layerNum,
                                                std::uint16_t dataType,
                                                LayerPurpose purpose)
{
    if (m_currentContent == nullptr) {
        return 0;
    }

    LayerSpec spec;
    spec.layerNum = layerNum;
    spec.dataType = dataType;
    spec.purpose = purpose;
    const std::uint32_t viewId = findOrAddViewLayer(*m_currentContent, spec, m_db.lib().layers());

    bool foundInLib = false;
    for (const LayerSpec &libLayer : m_db.lib().layers()) {
        if (libLayer.layerNum == layerNum && libLayer.dataType == dataType) {
            foundInLib = true;
            break;
        }
    }
    if (!foundInLib) {
        LayerSpec libSpec = spec;
        if (libSpec.name.empty()) {
            libSpec.name = "L" + std::to_string(layerNum) + "/D" + std::to_string(dataType);
        }
        m_db.lib().layers().push_back(libSpec);
    }

    return viewId;
}

LayerPurpose OasImportContext::rectanglePurpose() const
{
    return LayerPurpose::Boundary;
}

void OasImportContext::addPlacement(const std::string &cellName, std::int64_t x, std::int64_t y)
{
    if (m_currentCellName.empty() || cellName.empty()) {
        return;
    }
    m_db.lib().getOrCreateCell(cellName);
    syncCurrentCell();
    Transform transform;
    transform.x = x;
    transform.y = y;
    m_currentBlock->instances().emplace_back(cellName, transform);
}

void OasImportContext::addRectangle(std::int64_t x, std::int64_t y, std::uint64_t w, std::uint64_t h)
{
    syncCurrentCell();
    if (m_currentBlock == nullptr || w == 0 || h == 0) {
        return;
    }
    const std::uint16_t layerNum = static_cast<std::uint16_t>(m_modalLayer);
    const std::uint16_t dataType = static_cast<std::uint16_t>(m_modalDatatype);
    const std::uint32_t layerId = ensureViewLayer(layerNum, dataType, rectanglePurpose());

    Shape::RectData rect;
    rect.layerId = layerId;
    rect.box = Box{x, y, x + static_cast<std::int64_t>(w), y + static_cast<std::int64_t>(h)};
    m_currentBlock->shapes().emplace_back(rect);
}

void OasImportContext::addPolygon(const std::vector<Point> &points)
{
    syncCurrentCell();
    if (m_currentBlock == nullptr || points.size() < 3) {
        return;
    }
    const std::uint16_t layerNum = static_cast<std::uint16_t>(m_modalLayer);
    const std::uint16_t dataType = static_cast<std::uint16_t>(m_modalDatatype);
    const std::uint32_t layerId = ensureViewLayer(layerNum, dataType, LayerPurpose::Boundary);

    Shape::PolygonData poly;
    poly.layerId = layerId;
    poly.points = points;
    m_currentBlock->shapes().emplace_back(poly);
}

void OasImportContext::addPath(const std::vector<Point> &points, std::uint32_t width)
{
    syncCurrentCell();
    if (m_currentBlock == nullptr || points.size() < 2) {
        return;
    }
    const std::uint16_t layerNum = static_cast<std::uint16_t>(m_modalLayer);
    const std::uint16_t dataType = static_cast<std::uint16_t>(m_modalDatatype);
    const std::uint32_t layerId = ensureViewLayer(layerNum, dataType, LayerPurpose::Wire);

    Shape::PathData path;
    path.layerId = layerId;
    path.points = points;
    path.width = width;
    m_currentBlock->shapes().emplace_back(path);
}

void OasImportContext::addText(const std::string &text,
                               std::int64_t x,
                               std::int64_t y,
                               std::uint32_t height)
{
    syncCurrentCell();
    if (m_currentBlock == nullptr || text.empty()) {
        return;
    }
    const std::uint16_t layerNum = static_cast<std::uint16_t>(m_modalLayer);
    const std::uint16_t dataType = static_cast<std::uint16_t>(m_modalDatatype);
    const std::uint32_t layerId = ensureViewLayer(layerNum, dataType, LayerPurpose::Label);

    Shape::TextData textData;
    textData.layerId = layerId;
    textData.text = text;
    textData.position = Point{x, y};
    textData.height = height;
    m_currentBlock->shapes().emplace_back(textData);
}

void OasImportContext::finalize()
{
    m_db.lib().recomputeAllBBoxes(ViewType::Layout);
    m_db.lib().refreshIndex(ViewType::Layout);
}

} // namespace core
