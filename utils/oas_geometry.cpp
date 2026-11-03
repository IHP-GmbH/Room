#include "oas_geometry.h"

#include "cell.h"
#include "layer_utils.h"

#include <cstdlib>

namespace room {

OasImportContext::OasImportContext(Database &db, std::string libName, double defaultDbuPerMicron)
    : m_db(db), m_dbuPerMicron(defaultDbuPerMicron)
{
    m_db.setGenerator("ROOM OasImporter");
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

void OasImportContext::renameCell(const std::string &from, const std::string &to)
{
    if (from.empty() || from == to) {
        return;
    }
    if (Cell *cell = m_db.lib().findCell(from)) {
        m_db.lib().renameCell(*cell, to);
    }
    for (Cell &cell : m_db.lib().cells()) {
        if (CellContent *content = cell.findContent(ViewType::Layout)) {
            for (Instance &inst : content->block().instances()) {
                if (inst.cellName() == from) {
                    inst.setCellName(to);
                }
            }
        }
    }
    if (m_currentCellName == from) {
        m_currentCellName = to;
        syncCurrentCell();
    }
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

void OasImportContext::addPlacement(const std::string &cellName, std::int64_t x, std::int64_t y,
                                     std::uint32_t columns, std::uint32_t rows, std::int64_t stepX, std::int64_t stepY,
                                     Orient orient, std::int64_t colY, std::int64_t rowX,
                                     const std::vector<Point> *offsets)
{
    if (m_currentCellName.empty() || cellName.empty()) {
        return;
    }
    m_db.lib().getOrCreateCell(cellName);
    syncCurrentCell();
    Transform transform;
    transform.x = x;
    transform.y = y;
    transform.orient = orient;
    Instance &instance = m_currentBlock->instances().emplace_back(cellName, transform);
    if (offsets != nullptr && offsets->size() > 1) {
        instance.setOffsets(*offsets);
        return;
    }
    instance.setArray(columns, rows, stepX, stepY, colY, rowX);
}

void OasImportContext::addRectangle(std::int64_t x, std::int64_t y, std::uint64_t w, std::uint64_t h,
                                    std::uint32_t columns, std::uint32_t rows, std::int64_t stepX, std::int64_t stepY,
                                    const std::vector<Point> *copies, std::int64_t colY, std::int64_t rowX)
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
    if (copies != nullptr && copies->size() > 1) {
        rect.copies = *copies;
    } else if (columns > 1 || rows > 1) {
        rect.columns = columns == 0 ? 1 : columns;
        rect.rows = rows == 0 ? 1 : rows;
        rect.stepX = stepX;
        rect.stepY = stepY;
        rect.colY = colY;
        rect.rowX = rowX;
    }
    m_currentBlock->shapes().emplace_back(rect);
}

void OasImportContext::addPolygon(const std::vector<Point> &points,
                                  std::uint32_t columns, std::uint32_t rows, std::int64_t stepX, std::int64_t stepY,
                                  const std::vector<Point> *copies, std::int64_t colY, std::int64_t rowX)
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
    if (copies != nullptr && copies->size() > 1) {
        poly.copies = *copies;
    } else if (columns > 1 || rows > 1) {
        poly.columns = columns == 0 ? 1 : columns;
        poly.rows = rows == 0 ? 1 : rows;
        poly.stepX = stepX;
        poly.stepY = stepY;
        poly.colY = colY;
        poly.rowX = rowX;
    }
    m_currentBlock->shapes().emplace_back(poly);
}

void OasImportContext::addPath(const std::vector<Point> &points, std::uint32_t width,
                               std::uint32_t columns, std::uint32_t rows, std::int64_t stepX, std::int64_t stepY,
                               std::int32_t bgnExt, std::int32_t endExt, bool round,
                               const std::vector<Point> *copies, std::int64_t colY, std::int64_t rowX)
{
    syncCurrentCell();
    if (m_currentBlock == nullptr || points.empty() || (!round && points.size() < 2)) {
        return;
    }
    const std::uint16_t layerNum = static_cast<std::uint16_t>(m_modalLayer);
    const std::uint16_t dataType = static_cast<std::uint16_t>(m_modalDatatype);
    const std::uint32_t layerId = ensureViewLayer(layerNum, dataType, LayerPurpose::Wire);

    Shape::PathData path;
    path.layerId = layerId;
    path.points = points;
    path.width = width;
    path.bgnExt = bgnExt;
    path.endExt = endExt;
    path.round = round;
    if (copies != nullptr && copies->size() > 1) {
        path.copies = *copies;
    } else if (columns > 1 || rows > 1) {
        path.columns = columns == 0 ? 1 : columns;
        path.rows = rows == 0 ? 1 : rows;
        path.stepX = stepX;
        path.stepY = stepY;
        path.colY = colY;
        path.rowX = rowX;
    }
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

void OasImportContext::resolveTextPlaceholders(const std::vector<std::string> &names)
{
    const std::string prefix = "\x01oastext:";
    for (Cell &cell : m_db.lib().cells()) {
        if (CellContent *content = cell.findContent(ViewType::Layout)) {
            for (Shape &shape : content->block().shapes()) {
                const Shape::TextData *text = shape.text();
                if (text == nullptr || text->text.compare(0, prefix.size(), prefix) != 0) {
                    continue;
                }
                const std::uint64_t rn = std::strtoull(text->text.c_str() + prefix.size(), nullptr, 10);
                if (rn >= names.size() || names[static_cast<std::size_t>(rn)].empty() ||
                    names[static_cast<std::size_t>(rn)] == text->text) {
                    continue;
                }
                Shape::TextData updated = *text;
                updated.text = names[static_cast<std::size_t>(rn)];
                std::vector<Property> props = shape.properties();
                shape = Shape(std::move(updated));
                shape.properties() = std::move(props);
            }
        }
    }
}

void OasImportContext::finalize()
{
    m_db.lib().recomputeAllBBoxes(ViewType::Layout);
    m_db.lib().refreshIndex(ViewType::Layout);
}

} // namespace room
