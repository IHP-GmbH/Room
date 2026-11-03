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
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
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
            lib.stats.arefMembers += static_cast<std::uint64_t>(inst.columns) *
                                     static_cast<std::uint64_t>(inst.rows);
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

struct OrthoGrid {
    std::int64_t x0 = 0;
    std::int64_t y0 = 0;
    std::int64_t dx = 0;
    std::int64_t dy = 0;
    std::uint32_t columns = 1;
    std::uint32_t rows = 1;
};

struct WrittenGrids {
    std::uint64_t rectArrays = 0;
    std::uint64_t rectArrayMembers = 0;
    std::uint64_t polygonRepeats = 0;
    std::uint64_t polygonRepeatMembers = 0;
    std::uint64_t pathRepeats = 0;
    std::uint64_t pathRepeatMembers = 0;
};

using PointList = std::vector<std::pair<std::int64_t, std::int64_t>>;

void extractOrthogonalGrids(PointList points, std::vector<OrthoGrid> &grids, PointList &rest)
{
    grids.clear();
    rest.clear();
    if (points.empty()) {
        return;
    }
    std::sort(points.begin(), points.end(), [](const auto &a, const auto &b) {
        if (a.second != b.second) {
            return a.second < b.second;
        }
        return a.first < b.first;
    });

    struct Run {
        std::int64_t x0 = 0;
        std::int64_t y = 0;
        std::int64_t dx = 0;
        std::uint32_t n = 1;
    };
    std::vector<Run> runs;
    std::size_t row = 0;
    while (row < points.size()) {
        std::size_t rowEnd = row + 1;
        while (rowEnd < points.size() && points[rowEnd].second == points[row].second) {
            ++rowEnd;
        }
        std::size_t cursor = row;
        while (cursor < rowEnd) {
            std::size_t end = cursor + 1;
            std::int64_t step = 0;
            if (end < rowEnd && points[end].first != points[cursor].first) {
                step = points[end].first - points[cursor].first;
                ++end;
                while (end < rowEnd && points[end].first - points[end - 1].first == step) {
                    ++end;
                }
            }
            const std::uint64_t count = static_cast<std::uint64_t>(end - cursor);
            runs.push_back(Run{points[cursor].first, points[cursor].second, count > 1 ? step : 0,
                               static_cast<std::uint32_t>(count)});
            cursor = end;
        }
        row = rowEnd;
    }

    std::sort(runs.begin(), runs.end(), [](const Run &a, const Run &b) {
        if (a.x0 != b.x0) {
            return a.x0 < b.x0;
        }
        if (a.dx != b.dx) {
            return a.dx < b.dx;
        }
        if (a.n != b.n) {
            return a.n < b.n;
        }
        return a.y < b.y;
    });

    std::size_t band = 0;
    while (band < runs.size()) {
        std::size_t bandEnd = band + 1;
        while (bandEnd < runs.size() && runs[bandEnd].x0 == runs[band].x0 && runs[bandEnd].dx == runs[band].dx &&
               runs[bandEnd].n == runs[band].n) {
            ++bandEnd;
        }
        std::size_t cursor = band;
        while (cursor < bandEnd) {
            std::size_t end = cursor + 1;
            std::int64_t dy = 0;
            if (end < bandEnd && runs[end].y != runs[cursor].y) {
                dy = runs[end].y - runs[cursor].y;
                ++end;
                while (end < bandEnd && runs[end].y - runs[end - 1].y == dy) {
                    ++end;
                }
            }
            const std::uint32_t rows = static_cast<std::uint32_t>(end - cursor);
            const std::uint32_t columns = runs[cursor].n;
            if (columns > 1 || rows > 1) {
                grids.push_back(OrthoGrid{runs[cursor].x0, runs[cursor].y, columns > 1 ? runs[cursor].dx : 0,
                                          rows > 1 ? dy : 0, columns, rows});
            } else {
                rest.emplace_back(runs[cursor].x0, runs[cursor].y);
            }
            cursor = end;
        }
        band = bandEnd;
    }

    PointList rebuilt = rest;
    rebuilt.reserve(points.size());
    for (const OrthoGrid &grid : grids) {
        for (std::uint32_t r = 0; r < grid.rows; ++r) {
            for (std::uint32_t c = 0; c < grid.columns; ++c) {
                rebuilt.emplace_back(grid.x0 + static_cast<std::int64_t>(c) * grid.dx,
                                     grid.y0 + static_cast<std::int64_t>(r) * grid.dy);
            }
        }
    }
    std::sort(points.begin(), points.end());
    std::sort(rebuilt.begin(), rebuilt.end());
    if (points != rebuilt) {
        throw std::runtime_error("ROOM grid pack did not preserve points");
    }
}

PointList gridPoints(const OrthoGrid &grid)
{
    PointList points;
    points.reserve(static_cast<std::size_t>(grid.columns) * grid.rows);
    for (std::uint32_t r = 0; r < grid.rows; ++r) {
        for (std::uint32_t c = 0; c < grid.columns; ++c) {
            points.emplace_back(grid.x0 + static_cast<std::int64_t>(c) * grid.dx,
                                grid.y0 + static_cast<std::int64_t>(r) * grid.dy);
        }
    }
    return points;
}

struct InstKey {
    std::uint32_t nameId = 0;
    std::uint8_t orient = 0;
    std::uint64_t magBits = 0;
    bool operator==(const InstKey &other) const
    {
        return nameId == other.nameId && orient == other.orient && magBits == other.magBits;
    }
};

struct InstKeyHash {
    std::size_t operator()(const InstKey &key) const
    {
        std::size_t h = key.nameId;
        h ^= static_cast<std::size_t>(key.orient) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(key.magBits) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(key.magBits >> 32) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

std::uint64_t doubleBits(double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void packSingleInstances(std::vector<InstAcc> &instances)
{
    std::unordered_map<InstKey, PointList, InstKeyHash> singles;
    std::unordered_map<InstKey, InstAcc, InstKeyHash> prototype;
    std::vector<InstAcc> kept;
    kept.reserve(instances.size());
    for (const InstAcc &inst : instances) {
        if (inst.columns > 1 || inst.rows > 1) {
            kept.push_back(inst);
            continue;
        }
        const InstKey key{inst.nameId, static_cast<std::uint8_t>(inst.orient), doubleBits(inst.mag)};
        if (prototype.find(key) == prototype.end()) {
            prototype.emplace(key, inst);
        }
        singles[key].emplace_back(inst.x, inst.y);
    }

    for (auto &entry : singles) {
        std::vector<OrthoGrid> grids;
        PointList rest;
        extractOrthogonalGrids(std::move(entry.second), grids, rest);
        const InstAcc &base = prototype.at(entry.first);
        for (const OrthoGrid &grid : grids) {
            InstAcc inst = base;
            inst.x = grid.x0;
            inst.y = grid.y0;
            inst.columns = grid.columns;
            inst.rows = grid.rows;
            inst.stepX = grid.dx;
            inst.stepY = grid.dy;
            kept.push_back(inst);
        }
        for (const auto &pt : rest) {
            InstAcc inst = base;
            inst.x = pt.first;
            inst.y = pt.second;
            inst.columns = 0;
            inst.rows = 0;
            inst.stepX = 0;
            inst.stepY = 0;
            kept.push_back(inst);
        }
    }
    instances.swap(kept);
}

struct SpanRef {
    std::size_t deltaBegin = 0;
    std::uint32_t vertices = 0;
    std::uint32_t width = 0;
    std::int32_t bgnExt = 0;
    std::int32_t endExt = 0;
    std::uint8_t round = 0;
};

struct RepeatDraft {
    std::uint32_t width = 0;
    std::uint32_t vertices = 0;
    std::int32_t bgnExt = 0;
    std::int32_t endExt = 0;
    bool round = false;
    std::vector<std::int64_t> deltas;
    std::vector<std::int64_t> originX;
    std::vector<std::int64_t> originY;
};

std::uint64_t mixHash(std::uint64_t hash, std::uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

std::uint64_t spanHash(const std::vector<std::int64_t> &deltas, const SpanRef &span)
{
    std::uint64_t hash = 14695981039346656037ull;
    hash = mixHash(hash, span.width);
    hash = mixHash(hash, span.vertices);
    hash = mixHash(hash, static_cast<std::uint32_t>(span.bgnExt));
    hash = mixHash(hash, static_cast<std::uint32_t>(span.endExt));
    hash = mixHash(hash, span.round);
    const std::size_t n = static_cast<std::size_t>(span.vertices) * 2;
    for (std::size_t i = 2; i < n; ++i) {
        hash = mixHash(hash, static_cast<std::uint64_t>(deltas[span.deltaBegin + i]));
    }
    return hash;
}

bool sameRelative(const std::vector<std::int64_t> &deltas, const SpanRef &a, const SpanRef &b)
{
    if (a.vertices != b.vertices || a.width != b.width || a.bgnExt != b.bgnExt || a.endExt != b.endExt ||
        a.round != b.round) {
        return false;
    }
    const std::size_t n = static_cast<std::size_t>(a.vertices) * 2;
    for (std::size_t i = 2; i < n; ++i) {
        if (deltas[a.deltaBegin + i] != deltas[b.deltaBegin + i]) {
            return false;
        }
    }
    return true;
}

void pushRepeat(std::vector<RepeatDraft> &repeats, const std::vector<std::int64_t> &deltas, const SpanRef &proto,
                const PointList &origins)
{
    if (origins.size() < 2) {
        return;
    }
    RepeatDraft repeat;
    repeat.width = proto.width;
    repeat.vertices = proto.vertices;
    repeat.bgnExt = proto.bgnExt;
    repeat.endExt = proto.endExt;
    repeat.round = proto.round != 0;
    const std::size_t n = static_cast<std::size_t>(proto.vertices) * 2;
    repeat.deltas.assign(deltas.begin() + static_cast<std::ptrdiff_t>(proto.deltaBegin),
                         deltas.begin() + static_cast<std::ptrdiff_t>(proto.deltaBegin + n));
    repeat.originX.reserve(origins.size());
    repeat.originY.reserve(origins.size());
    for (const auto &pt : origins) {
        repeat.originX.push_back(pt.first);
        repeat.originY.push_back(pt.second);
    }
    repeats.push_back(std::move(repeat));
}

bool compactTranslatedCopies(std::vector<std::int64_t> &deltaStream, std::vector<std::uint32_t> *widths,
                             std::vector<std::uint32_t> &counts, std::vector<std::int32_t> *bgn,
                             std::vector<std::int32_t> *endExt, std::vector<std::uint8_t> *round,
                             std::vector<RepeatDraft> &repeats)
{
    if (counts.empty()) {
        return true;
    }
    std::vector<SpanRef> spans;
    spans.reserve(counts.size());
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < counts.size(); ++i) {
        const std::uint32_t vertices = counts[i];
        const std::size_t needed = static_cast<std::size_t>(vertices) * 2;
        if (cursor + needed > deltaStream.size()) {
            return false;
        }
        SpanRef span;
        span.deltaBegin = cursor;
        span.vertices = vertices;
        span.width = widths != nullptr && i < widths->size() ? (*widths)[i] : 0;
        span.bgnExt = bgn != nullptr && i < bgn->size() ? (*bgn)[i] : 0;
        span.endExt = endExt != nullptr && i < endExt->size() ? (*endExt)[i] : 0;
        span.round = round != nullptr && i < round->size() ? (*round)[i] : 0;
        spans.push_back(span);
        cursor += needed;
    }
    if (cursor != deltaStream.size()) {
        return false;
    }

    struct HashIx {
        std::uint64_t hash = 0;
        std::uint32_t index = 0;
    };
    std::vector<HashIx> order;
    std::vector<std::size_t> singles;
    order.reserve(spans.size());
    for (std::size_t i = 0; i < spans.size(); ++i) {
        if (spans[i].vertices < 1) {
            singles.push_back(i);
            continue;
        }
        order.push_back(HashIx{spanHash(deltaStream, spans[i]), static_cast<std::uint32_t>(i)});
    }
    std::sort(order.begin(), order.end(), [](const HashIx &a, const HashIx &b) {
        if (a.hash != b.hash) {
            return a.hash < b.hash;
        }
        return a.index < b.index;
    });

    std::vector<RepeatDraft> found;
    std::vector<std::size_t> synthesized;
    std::vector<std::int64_t> synthesizedX;
    std::vector<std::int64_t> synthesizedY;
    std::vector<std::uint32_t> synthesizedProto;
    std::size_t accounted = singles.size();
    std::size_t band = 0;
    while (band < order.size()) {
        std::size_t bandEnd = band + 1;
        while (bandEnd < order.size() && order[bandEnd].hash == order[band].hash) {
            ++bandEnd;
        }
        std::vector<std::vector<std::uint32_t>> classes;
        for (std::size_t i = band; i < bandEnd; ++i) {
            const std::uint32_t spanIndex = order[i].index;
            bool placed = false;
            if (classes.size() < 32) {
                for (auto &cls : classes) {
                    if (sameRelative(deltaStream, spans[cls.front()], spans[spanIndex])) {
                        cls.push_back(spanIndex);
                        placed = true;
                        break;
                    }
                }
            }
            if (!placed) {
                if (classes.size() < 32) {
                    classes.push_back({spanIndex});
                } else {
                    singles.push_back(spanIndex);
                    ++accounted;
                }
            }
        }
        for (const auto &cls : classes) {
            accounted += cls.size();
            if (cls.size() < 2) {
                singles.push_back(cls.front());
                continue;
            }
            PointList origins;
            origins.reserve(cls.size());
            for (const std::uint32_t spanIndex : cls) {
                const SpanRef &span = spans[spanIndex];
                origins.emplace_back(deltaStream[span.deltaBegin], deltaStream[span.deltaBegin + 1]);
            }
            std::vector<OrthoGrid> grids;
            PointList rest;
            extractOrthogonalGrids(std::move(origins), grids, rest);
            const SpanRef &proto = spans[cls.front()];
            for (const OrthoGrid &grid : grids) {
                pushRepeat(found, deltaStream, proto, gridPoints(grid));
            }
            if (rest.size() >= 2) {
                pushRepeat(found, deltaStream, proto, rest);
            } else if (rest.size() == 1) {
                synthesized.push_back(cls.front());
                synthesizedX.push_back(rest.front().first);
                synthesizedY.push_back(rest.front().second);
                synthesizedProto.push_back(cls.front());
            }
        }
        band = bandEnd;
    }
    if (accounted != spans.size()) {
        throw std::runtime_error("ROOM repeat pack dropped a shape");
    }
    if (found.empty()) {
        return true;
    }

    std::vector<std::int64_t> newDeltas;
    std::vector<std::uint32_t> newWidths;
    std::vector<std::uint32_t> newCounts;
    std::vector<std::int32_t> newBgn;
    std::vector<std::int32_t> newEnd;
    std::vector<std::uint8_t> newRound;
    const bool keepWidth = widths != nullptr;
    const bool keepEnds = bgn != nullptr;
    auto appendStream = [&](const SpanRef &span, std::int64_t x, std::int64_t y, bool replaceOrigin) {
        const std::size_t n = static_cast<std::size_t>(span.vertices) * 2;
        const std::size_t at = newDeltas.size();
        newDeltas.insert(newDeltas.end(), deltaStream.begin() + static_cast<std::ptrdiff_t>(span.deltaBegin),
                         deltaStream.begin() + static_cast<std::ptrdiff_t>(span.deltaBegin + n));
        if (replaceOrigin && n >= 2) {
            newDeltas[at] = x;
            newDeltas[at + 1] = y;
        }
        newCounts.push_back(span.vertices);
        if (keepWidth) {
            newWidths.push_back(span.width);
        }
        if (keepEnds) {
            newBgn.push_back(span.bgnExt);
            newEnd.push_back(span.endExt);
            newRound.push_back(span.round);
        }
    };
    for (const std::size_t index : singles) {
        appendStream(spans[index], 0, 0, false);
    }
    for (std::size_t i = 0; i < synthesized.size(); ++i) {
        appendStream(spans[synthesizedProto[i]], synthesizedX[i], synthesizedY[i], true);
    }

    deltaStream.swap(newDeltas);
    counts.swap(newCounts);
    if (keepWidth) {
        widths->swap(newWidths);
    }
    if (keepEnds) {
        bgn->swap(newBgn);
        endExt->swap(newEnd);
        round->swap(newRound);
    }
    repeats.swap(found);
    return true;
}

void writeLayer(schema::CompactLayerShapes::Builder builder, LayerAcc &layer, std::uint32_t layerId,
                WrittenGrids &written)
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

    struct RectArrayAcc {
        std::int64_t llx = 0;
        std::int64_t lly = 0;
        std::int64_t w = 0;
        std::int64_t h = 0;
        std::int64_t dx = 0;
        std::int64_t dy = 0;
        std::uint32_t columns = 1;
        std::uint32_t rows = 1;
    };
    std::vector<RectArrayAcc> arrays;
    std::vector<std::pair<Wh, PointList>> grouped;
    grouped.reserve(groups.size());
    for (auto &entry : groups) {
        std::vector<OrthoGrid> grids;
        PointList rest;
        extractOrthogonalGrids(std::move(entry.second), grids, rest);
        for (const OrthoGrid &grid : grids) {
            arrays.push_back(RectArrayAcc{grid.x0, grid.y0, entry.first.w, entry.first.h, grid.dx, grid.dy,
                                          grid.columns, grid.rows});
            written.rectArrays += 1;
            written.rectArrayMembers += static_cast<std::uint64_t>(grid.columns) * grid.rows;
        }
        if (rest.size() >= 2) {
            grouped.push_back({entry.first, std::move(rest)});
        } else {
            for (const auto &pt : rest) {
                singles.push_back(pt.first);
                singles.push_back(pt.second);
                singles.push_back(pt.first + entry.first.w);
                singles.push_back(pt.second + entry.first.h);
            }
        }
    }
    groups.clear();

    auto rects = builder.initRectCoords(singles.size());
    for (std::size_t i = 0; i < singles.size(); ++i) {
        rects.set(i, singles[i]);
    }
    auto arrayBuilders = builder.initRectArrays(arrays.size());
    for (std::size_t i = 0; i < arrays.size(); ++i) {
        auto ab = arrayBuilders[i];
        ab.setOriginLlx(arrays[i].llx);
        ab.setOriginLly(arrays[i].lly);
        ab.setWidth(arrays[i].w);
        ab.setHeight(arrays[i].h);
        ab.setColumns(arrays[i].columns);
        ab.setRows(arrays[i].rows);
        ab.setStepX(arrays[i].dx);
        ab.setStepY(arrays[i].dy);
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

    std::vector<RepeatDraft> polygonRepeats;
    compactTranslatedCopies(layer.polyDeltas, nullptr, layer.polyCounts, nullptr, nullptr, nullptr, polygonRepeats);
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
    auto polyRepeatBuilders = builder.initPolygonRepeats(polygonRepeats.size());
    for (std::size_t i = 0; i < polygonRepeats.size(); ++i) {
        const RepeatDraft &repeat = polygonRepeats[i];
        auto pb = polyRepeatBuilders[i];
        pb.setVertexCount(repeat.vertices);
        auto deltas = pb.initDeltas(repeat.deltas.size());
        for (std::size_t j = 0; j < repeat.deltas.size(); ++j) {
            deltas.set(j, repeat.deltas[j]);
        }
        auto ox = pb.initOriginX(repeat.originX.size());
        auto oy = pb.initOriginY(repeat.originY.size());
        for (std::size_t j = 0; j < repeat.originX.size(); ++j) {
            ox.set(j, repeat.originX[j]);
            oy.set(j, repeat.originY[j]);
        }
        written.polygonRepeats += 1;
        written.polygonRepeatMembers += repeat.originX.size();
    }
    polygonRepeats.clear();

    std::vector<RepeatDraft> pathRepeats;
    compactTranslatedCopies(layer.pathDeltas, &layer.pathWidths, layer.pathCounts, &layer.pathBgnExt,
                            &layer.pathEndExt, &layer.pathRound, pathRepeats);
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
    auto pathRepeatBuilders = builder.initPathRepeats(pathRepeats.size());
    for (std::size_t i = 0; i < pathRepeats.size(); ++i) {
        const RepeatDraft &repeat = pathRepeats[i];
        auto pb = pathRepeatBuilders[i];
        pb.setWidth(repeat.width);
        pb.setVertexCount(repeat.vertices);
        pb.setBgnExt(repeat.bgnExt);
        pb.setEndExt(repeat.endExt);
        pb.setRound(repeat.round);
        auto deltas = pb.initDeltas(repeat.deltas.size());
        for (std::size_t j = 0; j < repeat.deltas.size(); ++j) {
            deltas.set(j, repeat.deltas[j]);
        }
        auto ox = pb.initOriginX(repeat.originX.size());
        auto oy = pb.initOriginY(repeat.originY.size());
        for (std::size_t j = 0; j < repeat.originX.size(); ++j) {
            ox.set(j, repeat.originX[j]);
            oy.set(j, repeat.originY[j]);
        }
        written.pathRepeats += 1;
        written.pathRepeatMembers += repeat.originX.size();
    }

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
    WrittenGrids written;
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
            writeLayer(layerBuilders[i], cell.layers[i], static_cast<std::uint32_t>(i), written);
        }
        cell.layers.clear();
        cell.layers.shrink_to_fit();

        packSingleInstances(cell.instances);
        auto instances = compact.initInstances(cell.instances.size());
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
            std::uint64_t members = 1;
            if (inst.columns > 1 || inst.rows > 1) {
                const std::uint64_t columns = inst.columns == 0 ? 1 : inst.columns;
                const std::uint64_t rows = inst.rows == 0 ? 1 : inst.rows;
                members = columns * rows;
            }
            placementCount += members;
        }
        cell.instances.clear();
        cell.instances.shrink_to_fit();
    }
    std::cout << "stored rect-arrays=" << written.rectArrays << " members=" << written.rectArrayMembers
              << " polygon-repeats=" << written.polygonRepeats << " members=" << written.polygonRepeatMembers
              << " path-repeats=" << written.pathRepeats << " members=" << written.pathRepeatMembers
              << " placements=" << placementCount << std::endl;

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
                for (const auto array : layer.getRectArrays()) {
                    const std::uint64_t columns = array.getColumns() == 0 ? 1 : array.getColumns();
                    const std::uint64_t rows = array.getRows() == 0 ? 1 : array.getRows();
                    stats.rects += columns * rows;
                }
                stats.polygons += layer.getPolygonVertexCounts().size();
                for (const auto repeat : layer.getPolygonRepeats()) {
                    stats.polygons += repeat.getOriginX().size();
                }
                stats.paths += layer.getPathVertexCounts().size();
                for (const auto repeat : layer.getPathRepeats()) {
                    stats.paths += repeat.getOriginX().size();
                }
                stats.texts += layer.getTexts().size();
            }
            for (const auto inst : compact.getInstances()) {
                if (inst.getColumns() > 1 || inst.getRows() > 1) {
                    ++stats.arefs;
                    const std::uint64_t columns = inst.getColumns() == 0 ? 1 : inst.getColumns();
                    const std::uint64_t rows = inst.getRows() == 0 ? 1 : inst.getRows();
                    stats.arefMembers += columns * rows;
                } else {
                    ++stats.srefs;
                }
            }
        }
    }
    return stats;
}

} // namespace room
