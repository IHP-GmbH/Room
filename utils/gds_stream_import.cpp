#include "gds_stream_import.h"

#include "enums.h"
#include "varint_codec.h"

#include <compact.capnp.h>
#include <database.capnp.h>
#include <dm.capnp.h>

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/io.h>
#include <kj/std/iostream.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <utility>

namespace room {
namespace {

constexpr std::uint16_t GDS_UNITS = 0x0305;
constexpr std::uint16_t GDS_BGNSTR = 0x0502;
constexpr std::uint16_t GDS_STRNAME = 0x0606;
constexpr std::uint16_t GDS_ENDSTR = 0x0700;
constexpr std::uint16_t GDS_BOUNDARY = 0x0800;
constexpr std::uint16_t GDS_PATH = 0x0900;
constexpr std::uint16_t GDS_SREF = 0x0A00;
constexpr std::uint16_t GDS_AREF = 0x0B00;
constexpr std::uint16_t GDS_TEXT = 0x0C00;
constexpr std::uint16_t GDS_LAYER = 0x0D02;
constexpr std::uint16_t GDS_DATATYPE = 0x0E02;
constexpr std::uint16_t GDS_WIDTH = 0x0F03;
constexpr std::uint16_t GDS_XY = 0x1003;
constexpr std::uint16_t GDS_ENDEL = 0x1100;
constexpr std::uint16_t GDS_SNAME = 0x1206;
constexpr std::uint16_t GDS_COLROW = 0x1302;
constexpr std::uint16_t GDS_TEXTTYPE = 0x1602;
constexpr std::uint16_t GDS_STRING = 0x1906;
constexpr std::uint16_t GDS_STRANS = 0x1A01;
constexpr std::uint16_t GDS_MAG = 0x1B05;
constexpr std::uint16_t GDS_ANGLE = 0x1C05;
constexpr std::uint16_t GDS_PATHTYPE = 0x2102;
constexpr std::uint16_t GDS_BOX = 0x2D00;
constexpr std::uint16_t GDS_BOXTYPE = 0x2E02;
constexpr std::uint16_t GDS_BGNEXTN = 0x3003;
constexpr std::uint16_t GDS_ENDEXTN = 0x3103;

std::uint16_t be16(const std::uint8_t *p)
{
    return (static_cast<std::uint16_t>(p[0]) << 8) | p[1];
}

std::int32_t be32(const std::uint8_t *p)
{
    return (static_cast<std::int32_t>(p[0]) << 24) | (static_cast<std::int32_t>(p[1]) << 16) |
           (static_cast<std::int32_t>(p[2]) << 8) | static_cast<std::int32_t>(p[3]);
}

double readGdsReal(const std::uint8_t *p)
{
    const std::uint8_t sign = (p[0] & 0x80) ? 1 : 0;
    const int exp = static_cast<int>(p[0] & 0x7F) - 64;
    double mant = 0.0;
    for (int i = 1; i < 8; ++i) {
        mant += static_cast<double>(p[i]) * std::pow(256.0, -i);
    }
    const double value = mant * std::pow(16.0, exp);
    return sign ? -value : value;
}

std::string decodeGdsString(const std::uint8_t *payload, int len)
{
    while (len > 0 && payload[len - 1] == 0) {
        --len;
    }
    return std::string(reinterpret_cast<const char *>(payload), static_cast<std::size_t>(len));
}

schema::Orient toSchemaOrient(Orient orient)
{
    switch (orient) {
    case Orient::R0: return schema::Orient::R0;
    case Orient::R90: return schema::Orient::R90;
    case Orient::R180: return schema::Orient::R180;
    case Orient::R270: return schema::Orient::R270;
    case Orient::MY: return schema::Orient::MY;
    case Orient::MX: return schema::Orient::MX;
    case Orient::MX90: return schema::Orient::MX90;
    case Orient::MY90: return schema::Orient::MY90;
    }
    return schema::Orient::R0;
}

Orient orientFromAngle(double degrees, bool reflect)
{
    int quadrant = static_cast<int>(std::lround(degrees / 90.0)) % 4;
    if (quadrant < 0) {
        quadrant += 4;
    }
    if (reflect) {
        switch (quadrant) {
        case 0: return Orient::MX;
        case 1: return Orient::MX90;
        case 2: return Orient::MY;
        default: return Orient::MY90;
        }
    }
    switch (quadrant) {
        case 0: return Orient::R0;
        case 1: return Orient::R90;
        case 2: return Orient::R180;
        default: return Orient::R270;
    }
}

struct Point32 {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

enum class ElemKind { None, Boundary, Path, Text, Sref, Aref };

struct Elem {
    ElemKind kind = ElemKind::None;
    std::uint16_t layer = 0;
    std::uint16_t dataType = 0;
    std::uint32_t width = 0;
    std::int32_t signedWidth = 0;
    std::int32_t bgnExt = 0;
    std::int32_t endExt = 0;
    bool hasBgnExt = false;
    bool hasEndExt = false;
    std::uint16_t pathType = 0;
    std::vector<Point32> points;
    std::string text;
    std::string sname;
    std::uint16_t strans = 0;
    double mag = 1.0;
    double angle = 0.0;
    bool hasAngle = false;
    std::uint16_t columns = 1;
    std::uint16_t rows = 1;
};

struct LayerAcc {
    std::uint16_t layer = 0;
    std::uint16_t dataType = 0;
    std::vector<std::int64_t> rects;
    std::vector<std::uint32_t> polyCounts;
    std::vector<std::int64_t> polyDeltas;
    std::vector<std::uint32_t> pathWidths;
    std::vector<std::uint32_t> pathCounts;
    std::vector<std::int64_t> pathDeltas;
    std::vector<std::int32_t> pathBgnExt;
    std::vector<std::int32_t> pathEndExt;
    std::vector<std::uint8_t> pathRound;
    std::vector<std::int64_t> textX;
    std::vector<std::int64_t> textY;
    std::vector<std::uint32_t> textHeights;
    std::vector<std::string> texts;
};

struct InstAcc {
    std::uint32_t nameId = 0;
    std::int64_t x = 0;
    std::int64_t y = 0;
    Orient orient = Orient::R0;
    double mag = 1.0;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::int64_t stepX = 0;
    std::int64_t stepY = 0;
};

struct CellAcc {
    std::string name;
    std::unordered_map<std::uint32_t, std::size_t> layerOf;
    std::vector<LayerAcc> layers;
    std::vector<InstAcc> instances;
    bool bboxValid = false;
    std::int64_t llx = 0;
    std::int64_t lly = 0;
    std::int64_t urx = 0;
    std::int64_t ury = 0;
    std::uint64_t elements = 0;
};

struct LibAcc {
    std::vector<std::string> names;
    std::unordered_map<std::string, std::uint32_t> nameOf;
    std::vector<CellAcc> cells;
    double dbuPerMicron = 1000.0;
    GdsStreamStats stats;
};

std::uint32_t internName(LibAcc &lib, const std::string &name)
{
    const auto it = lib.nameOf.find(name);
    if (it != lib.nameOf.end()) {
        return it->second;
    }
    const auto id = static_cast<std::uint32_t>(lib.names.size());
    lib.names.push_back(name);
    lib.nameOf.emplace(lib.names.back(), id);
    return id;
}

void expandBBox(CellAcc &cell, std::int64_t x, std::int64_t y)
{
    if (!cell.bboxValid) {
        cell.llx = cell.urx = x;
        cell.lly = cell.ury = y;
        cell.bboxValid = true;
        return;
    }
    if (x < cell.llx) cell.llx = x;
    if (y < cell.lly) cell.lly = y;
    if (x > cell.urx) cell.urx = x;
    if (y > cell.ury) cell.ury = y;
}

LayerAcc &layerAcc(CellAcc &cell, std::uint16_t layer, std::uint16_t dataType)
{
    const std::uint32_t key = (static_cast<std::uint32_t>(layer) << 16) | dataType;
    const auto it = cell.layerOf.find(key);
    if (it != cell.layerOf.end()) {
        return cell.layers[it->second];
    }
    const std::size_t index = cell.layers.size();
    cell.layerOf.emplace(key, index);
    LayerAcc acc;
    acc.layer = layer;
    acc.dataType = dataType;
    cell.layers.push_back(std::move(acc));
    return cell.layers.back();
}

bool asRect(const std::vector<Point32> &points, std::int64_t &llx, std::int64_t &lly, std::int64_t &urx, std::int64_t &ury)
{
    std::size_t n = points.size();
    if (n >= 2 && points.front().x == points.back().x && points.front().y == points.back().y) {
        --n;
    }
    if (n != 4) {
        return false;
    }
    llx = urx = points[0].x;
    lly = ury = points[0].y;
    for (std::size_t i = 1; i < n; ++i) {
        llx = std::min<std::int64_t>(llx, points[i].x);
        lly = std::min<std::int64_t>(lly, points[i].y);
        urx = std::max<std::int64_t>(urx, points[i].x);
        ury = std::max<std::int64_t>(ury, points[i].y);
    }
    if (llx == urx || lly == ury) {
        return false;
    }
    bool sawLowerLeft = false;
    bool sawLowerRight = false;
    bool sawUpperLeft = false;
    bool sawUpperRight = false;
    for (std::size_t i = 0; i < n; ++i) {
        const Point32 &here = points[i];
        const Point32 &next = points[(i + 1) % n];
        if (here.x != next.x && here.y != next.y) {
            return false;
        }
        const bool onLeft = here.x == llx;
        const bool onRight = here.x == urx;
        const bool onBottom = here.y == lly;
        const bool onTop = here.y == ury;
        if ((!onLeft && !onRight) || (!onBottom && !onTop)) {
            return false;
        }
        sawLowerLeft = sawLowerLeft || (onLeft && onBottom);
        sawLowerRight = sawLowerRight || (onRight && onBottom);
        sawUpperLeft = sawUpperLeft || (onLeft && onTop);
        sawUpperRight = sawUpperRight || (onRight && onTop);
    }
    return sawLowerLeft && sawLowerRight && sawUpperLeft && sawUpperRight;
}

void appendDeltas(std::vector<std::int64_t> &out, const std::vector<Point32> &points, std::size_t n)
{
    if (n == 0) {
        return;
    }
    out.push_back(points[0].x);
    out.push_back(points[0].y);
    for (std::size_t i = 1; i < n; ++i) {
        out.push_back(static_cast<std::int64_t>(points[i].x) - points[i - 1].x);
        out.push_back(static_cast<std::int64_t>(points[i].y) - points[i - 1].y);
    }
}

void commitElem(LibAcc &lib, CellAcc &cell, Elem &elem)
{
    if (elem.kind == ElemKind::None) {
        return;
    }
    if (elem.kind == ElemKind::Boundary) {
        std::size_t n = elem.points.size();
        if (n >= 2 && elem.points.front().x == elem.points.back().x && elem.points.front().y == elem.points.back().y) {
            --n;
        }
        if (n >= 3) {
            LayerAcc &layer = layerAcc(cell, elem.layer, elem.dataType);
            std::int64_t llx = 0, lly = 0, urx = 0, ury = 0;
            if (asRect(elem.points, llx, lly, urx, ury)) {
                layer.rects.push_back(llx);
                layer.rects.push_back(lly);
                layer.rects.push_back(urx);
                layer.rects.push_back(ury);
                ++lib.stats.rects;
            } else {
                layer.polyCounts.push_back(static_cast<std::uint32_t>(n));
                appendDeltas(layer.polyDeltas, elem.points, n);
                ++lib.stats.polygons;
            }
            for (std::size_t i = 0; i < n; ++i) {
                expandBBox(cell, elem.points[i].x, elem.points[i].y);
            }
            ++cell.elements;
        }
    } else if (elem.kind == ElemKind::Path) {
        if (!elem.points.empty()) {
            std::uint16_t type = elem.pathType;
            if (type != 0 && type != 1 && type != 2 && type != 4) {
                type = 0;
            }
            const std::int32_t bgnExt = elem.hasBgnExt ? elem.bgnExt
                                                       : ((type == 1 || type == 2) ? elem.signedWidth / 2 : 0);
            const std::int32_t endExt = elem.hasEndExt ? elem.endExt
                                                       : ((type == 1 || type == 2) ? elem.signedWidth / 2 : 0);
            LayerAcc &layer = layerAcc(cell, elem.layer, elem.dataType);
            layer.pathWidths.push_back(elem.width);
            layer.pathCounts.push_back(static_cast<std::uint32_t>(elem.points.size()));
            layer.pathBgnExt.push_back(bgnExt);
            layer.pathEndExt.push_back(endExt);
            layer.pathRound.push_back(type == 1 ? 1 : 0);
            appendDeltas(layer.pathDeltas, elem.points, elem.points.size());
            for (const Point32 &pt : elem.points) {
                expandBBox(cell, pt.x, pt.y);
            }
            ++lib.stats.paths;
            ++cell.elements;
        }
    } else if (elem.kind == ElemKind::Text) {
        LayerAcc &layer = layerAcc(cell, elem.layer, elem.dataType);
        const std::int64_t x = elem.points.empty() ? 0 : elem.points.front().x;
        const std::int64_t y = elem.points.empty() ? 0 : elem.points.front().y;
        layer.textX.push_back(x);
        layer.textY.push_back(y);
        layer.textHeights.push_back(elem.width);
        layer.texts.push_back(std::move(elem.text));
        expandBBox(cell, x, y);
        ++lib.stats.texts;
        ++cell.elements;
    } else if (elem.kind == ElemKind::Sref || elem.kind == ElemKind::Aref) {
        InstAcc inst;
        inst.nameId = internName(lib, elem.sname);
        if (!elem.points.empty()) {
            inst.x = elem.points.front().x;
            inst.y = elem.points.front().y;
            expandBBox(cell, inst.x, inst.y);
        }
        inst.orient = orientFromAngle(elem.angle, (elem.strans & 0x8000) != 0);
        inst.mag = elem.mag == 0.0 ? 1.0 : elem.mag;
        if (elem.kind == ElemKind::Aref) {
            inst.columns = elem.columns == 0 ? 1 : elem.columns;
            inst.rows = elem.rows == 0 ? 1 : elem.rows;
            const std::int64_t colDx = (elem.points.size() >= 2 && inst.columns > 1)
                                           ? (static_cast<std::int64_t>(elem.points[1].x) - inst.x) / (inst.columns - 1)
                                           : 0;
            const std::int64_t colDy = (elem.points.size() >= 2 && inst.columns > 1)
                                           ? (static_cast<std::int64_t>(elem.points[1].y) - inst.y) / (inst.columns - 1)
                                           : 0;
            const std::int64_t rowDx = (elem.points.size() >= 3 && inst.rows > 1)
                                           ? (static_cast<std::int64_t>(elem.points[2].x) - inst.x) / (inst.rows - 1)
                                           : 0;
            const std::int64_t rowDy = (elem.points.size() >= 3 && inst.rows > 1)
                                           ? (static_cast<std::int64_t>(elem.points[2].y) - inst.y) / (inst.rows - 1)
                                           : 0;
            inst.stepX = colDx != 0 ? colDx : rowDx;
            inst.stepY = rowDy != 0 ? rowDy : colDy;
            ++lib.stats.arefs;
        } else {
            ++lib.stats.srefs;
        }
        cell.instances.push_back(inst);
        ++cell.elements;
    }
    elem = Elem{};
}

struct Wh {
    std::int64_t w = 0;
    std::int64_t h = 0;
    bool operator==(const Wh &other) const { return w == other.w && h == other.h; }
};

struct WhHash {
    std::size_t operator()(const Wh &k) const
    {
        return std::hash<std::int64_t>{}(k.w) ^ (std::hash<std::int64_t>{}(k.h) << 1);
    }
};

void writeLayer(schema::CompactLayerShapes::Builder builder, LayerAcc &layer, std::uint32_t layerId)
{
    builder.setLayerId(layerId);

    std::unordered_map<Wh, std::vector<std::pair<std::int64_t, std::int64_t>>, WhHash> groups;
    std::vector<std::int64_t> singles;
    for (std::size_t i = 0; i + 3 < layer.rects.size(); i += 4) {
        const std::int64_t llx = layer.rects[i];
        const std::int64_t lly = layer.rects[i + 1];
        const std::int64_t urx = layer.rects[i + 2];
        const std::int64_t ury = layer.rects[i + 3];
        groups[Wh{urx - llx, ury - lly}].push_back({llx, lly});
    }
    layer.rects.clear();
    layer.rects.shrink_to_fit();

    std::vector<std::pair<Wh, std::vector<std::pair<std::int64_t, std::int64_t>>>> grouped;
    grouped.reserve(groups.size());
    for (auto &entry : groups) {
        if (entry.second.size() < 2) {
            for (const auto &pt : entry.second) {
                singles.push_back(pt.first);
                singles.push_back(pt.second);
                singles.push_back(pt.first + entry.first.w);
                singles.push_back(pt.second + entry.first.h);
            }
        } else {
            grouped.push_back(std::move(entry));
        }
    }
    groups.clear();

    auto rects = builder.initRectCoords(singles.size());
    for (std::size_t i = 0; i < singles.size(); ++i) {
        rects.set(i, singles[i]);
    }
    auto groupBuilders = builder.initRectGroups(grouped.size());
    for (std::size_t i = 0; i < grouped.size(); ++i) {
        auto gb = groupBuilders[i];
        gb.setWidth(grouped[i].first.w);
        gb.setHeight(grouped[i].first.h);
        auto llx = gb.initLlx(grouped[i].second.size());
        auto lly = gb.initLly(grouped[i].second.size());
        for (std::size_t j = 0; j < grouped[i].second.size(); ++j) {
            llx.set(j, grouped[i].second[j].first);
            lly.set(j, grouped[i].second[j].second);
        }
    }

    auto polyCounts = builder.initPolygonVertexCounts(layer.polyCounts.size());
    for (std::size_t i = 0; i < layer.polyCounts.size(); ++i) {
        polyCounts.set(i, layer.polyCounts[i]);
    }
    layer.polyCounts.clear();
    if (!layer.polyDeltas.empty()) {
        const std::vector<std::uint8_t> packed = encodeVarint64(layer.polyDeltas);
        layer.polyDeltas.clear();
        layer.polyDeltas.shrink_to_fit();
        builder.setPolygonDeltasPacked(capnp::Data::Reader(packed.data(), packed.size()));
    }

    auto pathWidths = builder.initPathWidths(layer.pathWidths.size());
    for (std::size_t i = 0; i < layer.pathWidths.size(); ++i) {
        pathWidths.set(i, layer.pathWidths[i]);
    }
    auto pathCounts = builder.initPathVertexCounts(layer.pathCounts.size());
    for (std::size_t i = 0; i < layer.pathCounts.size(); ++i) {
        pathCounts.set(i, layer.pathCounts[i]);
    }
    layer.pathWidths.clear();
    layer.pathCounts.clear();
    if (!layer.pathDeltas.empty()) {
        const std::vector<std::uint8_t> packed = encodeVarint64(layer.pathDeltas);
        layer.pathDeltas.clear();
        layer.pathDeltas.shrink_to_fit();
        builder.setPathDeltasPacked(capnp::Data::Reader(packed.data(), packed.size()));
    }
    bool customEnds = false;
    for (std::size_t i = 0; i < layer.pathBgnExt.size(); ++i) {
        if (layer.pathBgnExt[i] != 0 || layer.pathEndExt[i] != 0 || layer.pathRound[i] != 0) {
            customEnds = true;
            break;
        }
    }
    if (customEnds) {
        auto bgnExt = builder.initPathBgnExt(layer.pathBgnExt.size());
        auto endExt = builder.initPathEndExt(layer.pathEndExt.size());
        auto round = builder.initPathRound(layer.pathRound.size());
        for (std::size_t i = 0; i < layer.pathBgnExt.size(); ++i) {
            bgnExt.set(i, layer.pathBgnExt[i]);
            endExt.set(i, layer.pathEndExt[i]);
            round.set(i, layer.pathRound[i] != 0);
        }
    }
    layer.pathBgnExt.clear();
    layer.pathEndExt.clear();
    layer.pathRound.clear();

    auto textX = builder.initTextX(layer.textX.size());
    auto textY = builder.initTextY(layer.textY.size());
    auto textH = builder.initTextHeights(layer.textHeights.size());
    auto texts = builder.initTexts(layer.texts.size());
    for (std::size_t i = 0; i < layer.texts.size(); ++i) {
        textX.set(i, layer.textX[i]);
        textY.set(i, layer.textY[i]);
        textH.set(i, i < layer.textHeights.size() ? layer.textHeights[i] : 0);
        texts.set(i, layer.texts[i]);
    }
    layer.texts.clear();
}

void writeRoom(const std::string &roomPath, const std::string &libName, LibAcc &lib)
{
    capnp::MallocMessageBuilder message;
    auto root = message.initRoot<schema::Database>();
    root.setVersion("1.0");
    root.setGenerator("ROOM gds_stream_import");
    root.setTechnology("");
    auto summary = root.initSummary();
    summary.setView(schema::ViewType::LAYOUT);
    summary.setCellCount(static_cast<std::uint32_t>(lib.cells.size()));
    if (!lib.cells.empty()) {
        summary.setPrimaryCell(lib.cells.front().name);
    }

    auto libBuilder = root.initLib();
    libBuilder.setName(libName);

    std::vector<std::pair<std::uint16_t, std::uint16_t>> libLayers;
    std::unordered_map<std::uint32_t, std::size_t> libLayerOf;
    for (const CellAcc &cell : lib.cells) {
        for (const LayerAcc &layer : cell.layers) {
            const std::uint32_t key = (static_cast<std::uint32_t>(layer.layer) << 16) | layer.dataType;
            if (libLayerOf.emplace(key, libLayers.size()).second) {
                libLayers.push_back({layer.layer, layer.dataType});
            }
        }
    }
    auto layers = libBuilder.initLayers(libLayers.size());
    for (std::size_t i = 0; i < libLayers.size(); ++i) {
        layers[i].setLayerNum(libLayers[i].first);
        layers[i].setDataType(libLayers[i].second);
        layers[i].setName("L" + std::to_string(libLayers[i].first) + "D" + std::to_string(libLayers[i].second));
        layers[i].setPurpose(schema::LayerPurpose::DRAWING);
    }

    auto cells = libBuilder.initCells(lib.cells.size());
    std::uint64_t placementCount = 0;
    for (std::size_t ci = 0; ci < lib.cells.size(); ++ci) {
        CellAcc &cell = lib.cells[ci];
        auto cellBuilder = cells[ci];
        cellBuilder.setName(cell.name);
        auto contents = cellBuilder.initContents(1);
        auto content = contents[0];
        content.setViewType(schema::ViewType::LAYOUT);
        content.setDbuPerMicron(lib.dbuPerMicron);
        auto layout = content.initPayload().initLayout();
        auto viewLayers = layout.initLayers(cell.layers.size());
        for (std::size_t i = 0; i < cell.layers.size(); ++i) {
            viewLayers[i].setLayerNum(cell.layers[i].layer);
            viewLayers[i].setDataType(cell.layers[i].dataType);
            viewLayers[i].setName("L" + std::to_string(cell.layers[i].layer) + "D" + std::to_string(cell.layers[i].dataType));
            viewLayers[i].setPurpose(schema::LayerPurpose::DRAWING);
        }
        auto block = layout.initBlock();
        block.initShapes(0);
        block.initInstances(0);
        block.initNets(0);
        auto bbox = block.initBbox();
        bbox.setLlx(cell.bboxValid ? cell.llx : 0);
        bbox.setLly(cell.bboxValid ? cell.lly : 0);
        bbox.setUrx(cell.bboxValid ? cell.urx : 0);
        bbox.setUry(cell.bboxValid ? cell.ury : 0);

        auto compact = layout.initCompact();
        auto layerBuilders = compact.initLayerShapes(cell.layers.size());
        for (std::size_t i = 0; i < cell.layers.size(); ++i) {
            writeLayer(layerBuilders[i], cell.layers[i], static_cast<std::uint32_t>(i));
        }
        cell.layers.clear();
        cell.layers.shrink_to_fit();

        auto instances = compact.initInstances(cell.instances.size());
        placementCount += cell.instances.size();
        for (std::size_t i = 0; i < cell.instances.size(); ++i) {
            const InstAcc &inst = cell.instances[i];
            auto ib = instances[i];
            ib.setCellName(inst.nameId < lib.names.size() ? lib.names[inst.nameId] : std::string());
            auto transform = ib.initTransform();
            transform.setX(inst.x);
            transform.setY(inst.y);
            transform.setOrient(toSchemaOrient(inst.orient));
            transform.setMag(inst.mag);
            ib.setColumns(inst.columns);
            ib.setRows(inst.rows);
            ib.setStepX(inst.stepX);
            ib.setStepY(inst.stepY);
        }
        cell.instances.clear();
        cell.instances.shrink_to_fit();
        if (cell.elements >= 1000000) {
            std::cout << "wrote " << cell.name << " elements=" << cell.elements << std::endl;
        }
    }

    auto index = libBuilder.initIndex();
    index.setPlacementCount(placementCount);
    index.initTopCells(0);
    index.initEntries(0);

    std::ofstream out(roomPath, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot open file for writing: " + roomPath);
    }
    kj::std::StdOutputStream kjOut(out);
    capnp::writeMessage(kjOut, message);
}

bool readAll(const std::string &path, std::vector<char> &buffer, std::string &error)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "Cannot open file: " + path;
        return false;
    }
    const std::streamoff fileSize = in.tellg();
    if (fileSize < 0) {
        error = "Cannot read file size: " + path;
        return false;
    }
    in.seekg(0, std::ios::beg);
    buffer.resize(static_cast<std::size_t>(fileSize));
    std::size_t got = 0;
    constexpr std::size_t kChunk = 256u * 1024u * 1024u;
    while (got < buffer.size()) {
        const std::size_t n = std::min(kChunk, buffer.size() - got);
        in.read(buffer.data() + got, static_cast<std::streamsize>(n));
        const std::size_t nread = static_cast<std::size_t>(in.gcount());
        got += nread;
        if (nread < n) {
            break;
        }
    }
    if (got != buffer.size()) {
        error = "Failed to read file: " + path;
        return false;
    }
    return true;
}

} // namespace

GdsStreamImportResult importGdsStreamToRoom(const std::string &gdsPath, const std::string &roomPath, const std::string &libName)
{
    GdsStreamImportResult result;
    std::ifstream in(gdsPath, std::ios::binary);
    if (!in) {
        result.errors.push_back("Cannot open GDS file: " + gdsPath);
        return result;
    }

    LibAcc lib;
    Elem elem;
    bool inCell = false;
    std::vector<std::uint8_t> hold;
    std::vector<char> chunk(8 * 1024 * 1024);

    auto handle = [&](std::uint16_t recType, const std::uint8_t *payload, int payloadLen) {
        switch (recType) {
        case GDS_UNITS:
            if (payloadLen >= 16) {
                const double userPerDbu = readGdsReal(payload);
                const double metersPerUser = readGdsReal(payload + 8);
                const double metersPerDbu = userPerDbu * metersPerUser;
                if (metersPerDbu > 0.0) {
                    lib.dbuPerMicron = 1e-6 / metersPerDbu;
                }
            }
            break;
        case GDS_BGNSTR:
            if (inCell) {
                commitElem(lib, lib.cells.back(), elem);
                if (lib.cells.back().name.empty()) {
                    lib.cells.back().name = "cell_" + std::to_string(lib.cells.size());
                }
                ++lib.stats.cells;
                inCell = false;
            }
            lib.cells.emplace_back();
            inCell = true;
            break;
        case GDS_STRNAME:
            if (inCell) {
                lib.cells.back().name = decodeGdsString(payload, payloadLen);
            }
            break;
        case GDS_ENDSTR:
            if (inCell) {
                commitElem(lib, lib.cells.back(), elem);
                if (lib.cells.back().name.empty()) {
                    lib.cells.back().name = "cell_" + std::to_string(lib.cells.size());
                }
                ++lib.stats.cells;
                if (lib.stats.cells % 2000 == 0) {
                    std::cout << "imported cells " << lib.stats.cells << std::endl;
                }
                inCell = false;
            }
            break;
        case GDS_BOUNDARY:
        case GDS_BOX:
            if (inCell) {
                commitElem(lib, lib.cells.back(), elem);
                elem.kind = ElemKind::Boundary;
            }
            break;
        case GDS_PATH:
            if (inCell) {
                commitElem(lib, lib.cells.back(), elem);
                elem.kind = ElemKind::Path;
            }
            break;
        case GDS_TEXT:
            if (inCell) {
                commitElem(lib, lib.cells.back(), elem);
                elem.kind = ElemKind::Text;
            }
            break;
        case GDS_SREF:
            if (inCell) {
                commitElem(lib, lib.cells.back(), elem);
                elem.kind = ElemKind::Sref;
            }
            break;
        case GDS_AREF:
            if (inCell) {
                commitElem(lib, lib.cells.back(), elem);
                elem.kind = ElemKind::Aref;
            }
            break;
        case GDS_LAYER:
            if (payloadLen >= 2) {
                elem.layer = be16(payload);
            }
            break;
        case GDS_DATATYPE:
        case GDS_TEXTTYPE:
        case GDS_BOXTYPE:
            if (payloadLen >= 2) {
                elem.dataType = be16(payload);
            }
            break;
        case GDS_PATHTYPE:
            if (payloadLen >= 2) {
                elem.pathType = be16(payload);
            }
            break;
        case GDS_WIDTH:
            if (payloadLen >= 4) {
                elem.signedWidth = be32(payload);
                elem.width = static_cast<std::uint32_t>(elem.signedWidth);
            } else if (payloadLen >= 2) {
                elem.signedWidth = static_cast<std::int32_t>(be16(payload));
                elem.width = static_cast<std::uint32_t>(elem.signedWidth);
            }
            break;
        case GDS_BGNEXTN:
            if (payloadLen >= 4) {
                elem.bgnExt = be32(payload);
                elem.hasBgnExt = true;
            }
            break;
        case GDS_ENDEXTN:
            if (payloadLen >= 4) {
                elem.endExt = be32(payload);
                elem.hasEndExt = true;
            }
            break;
        case GDS_XY:
            elem.points.clear();
            for (int i = 0; i + 8 <= payloadLen; i += 8) {
                elem.points.push_back(Point32{be32(payload + i), be32(payload + i + 4)});
            }
            break;
        case GDS_SNAME:
            elem.sname = decodeGdsString(payload, payloadLen);
            break;
        case GDS_COLROW:
            if (payloadLen >= 4) {
                elem.columns = be16(payload);
                elem.rows = be16(payload + 2);
            }
            break;
        case GDS_STRING:
            elem.text = decodeGdsString(payload, payloadLen);
            break;
        case GDS_STRANS:
            if (payloadLen >= 2) {
                elem.strans = be16(payload);
            }
            break;
        case GDS_MAG:
            if (payloadLen >= 8) {
                elem.mag = readGdsReal(payload);
            }
            break;
        case GDS_ANGLE:
            if (payloadLen >= 8) {
                elem.angle = readGdsReal(payload);
                elem.hasAngle = true;
            }
            break;
        case GDS_ENDEL:
            if (inCell) {
                commitElem(lib, lib.cells.back(), elem);
            }
            break;
        default:
            break;
        }
    };

    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const std::size_t nread = static_cast<std::size_t>(in.gcount());
        if (nread == 0) {
            break;
        }
        hold.insert(hold.end(), reinterpret_cast<std::uint8_t *>(chunk.data()),
                    reinterpret_cast<std::uint8_t *>(chunk.data()) + nread);
        std::size_t i = 0;
        while (i + 4 <= hold.size()) {
            const unsigned len = (unsigned(hold[i]) << 8) | hold[i + 1];
            const unsigned recType = (unsigned(hold[i + 2]) << 8) | hold[i + 3];
            if (len < 4 || (len & 1) || i + len > hold.size()) {
                break;
            }
            handle(static_cast<std::uint16_t>(recType), hold.data() + i + 4, static_cast<int>(len - 4));
            i += len;
        }
        if (i == 0 && hold.size() > 1024 * 1024) {
            result.errors.push_back("Malformed GDS record");
            return result;
        }
        hold.erase(hold.begin(), hold.begin() + static_cast<std::ptrdiff_t>(i));
    }
    if (inCell) {
        commitElem(lib, lib.cells.back(), elem);
        ++lib.stats.cells;
    }

    try {
        writeRoom(roomPath, libName, lib);
    } catch (const std::exception &ex) {
        result.errors.push_back(ex.what());
        return result;
    }
    result.stats = lib.stats;
    return result;
}

GdsStreamStats readCompactRoomStats(const std::string &roomPath)
{
    std::vector<char> buffer;
    std::string error;
    if (!readAll(roomPath, buffer, error)) {
        throw std::runtime_error(error);
    }
    kj::ArrayInputStream input(kj::arrayPtr(reinterpret_cast<const kj::byte *>(buffer.data()), buffer.size()));
    capnp::ReaderOptions options;
    options.traversalLimitInWords = buffer.size() / sizeof(capnp::word) + (1024 * 1024);
    capnp::InputStreamMessageReader reader(input, options);
    auto root = reader.getRoot<schema::Database>();
    GdsStreamStats stats;
    const auto cells = root.getLib().getCells();
    stats.cells = cells.size();
    for (const auto cell : cells) {
        for (const auto content : cell.getContents()) {
            if (!content.getPayload().isLayout()) {
                continue;
            }
            const auto compact = content.getPayload().getLayout().getCompact();
            for (const auto layer : compact.getLayerShapes()) {
                stats.rects += layer.getRectCoords().size() / 4;
                for (const auto group : layer.getRectGroups()) {
                    stats.rects += group.getLlx().size();
                }
                stats.polygons += layer.getPolygonVertexCounts().size();
                stats.paths += layer.getPathVertexCounts().size();
                stats.texts += layer.getTexts().size();
            }
            for (const auto inst : compact.getInstances()) {
                if (inst.getColumns() > 1 || inst.getRows() > 1) {
                    ++stats.arefs;
                } else {
                    ++stats.srefs;
                }
            }
        }
    }
    return stats;
}

} // namespace room
