/*!****************************************************************************************
 * \file compact_codec.cpp
 * \brief Encoder and decoder for layer-grouped CompactBlock geometry.
 *****************************************************************************************/

#include "compact_codec.h"

#include "enums.h"
#include "varint_codec.h"

#include <design.capnp.h>
#include <dm.capnp.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace room {
namespace {

schema::Orient toSchemaOrient(Orient o)
{
    switch (o) {
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

Orient fromSchemaOrient(schema::Orient o)
{
    switch (o) {
    case schema::Orient::R0: return Orient::R0;
    case schema::Orient::R90: return Orient::R90;
    case schema::Orient::R180: return Orient::R180;
    case schema::Orient::R270: return Orient::R270;
    case schema::Orient::MY: return Orient::MY;
    case schema::Orient::MX: return Orient::MX;
    case schema::Orient::MX90: return Orient::MX90;
    case schema::Orient::MY90: return Orient::MY90;
    }
    return Orient::R0;
}

schema::Net::SigType toSchemaSigType(SigType type)
{
    switch (type) {
    case SigType::Signal: return schema::Net::SigType::SIGNAL;
    case SigType::Power: return schema::Net::SigType::POWER;
    case SigType::Ground: return schema::Net::SigType::GROUND;
    case SigType::Clock: return schema::Net::SigType::CLOCK;
    }
    return schema::Net::SigType::SIGNAL;
}

SigType fromSchemaSigType(schema::Net::SigType type)
{
    switch (type) {
    case schema::Net::SigType::SIGNAL: return SigType::Signal;
    case schema::Net::SigType::POWER: return SigType::Power;
    case schema::Net::SigType::GROUND: return SigType::Ground;
    case schema::Net::SigType::CLOCK: return SigType::Clock;
    }
    return SigType::Signal;
}

struct RawRect {
    std::int64_t llx = 0;
    std::int64_t lly = 0;
    std::int64_t urx = 0;
    std::int64_t ury = 0;
    std::vector<Property> properties;
};

struct RectArraySpec {
    std::int64_t originLlx = 0;
    std::int64_t originLly = 0;
    std::int64_t width = 0;
    std::int64_t height = 0;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::int64_t stepX = 0;
    std::int64_t stepY = 0;
    std::int64_t colY = 0;
    std::int64_t rowX = 0;
    std::vector<Property> properties;
    bool propertiesStored = false;
    bool memberProperties = true;
};

struct RectGroupSpec {
    std::int64_t width = 0;
    std::int64_t height = 0;
    std::vector<std::int64_t> llx;
    std::vector<std::int64_t> lly;
    std::vector<std::vector<Property>> propertiesPerPlacement;
    bool memberProperties = true;
};

struct PolygonRepeatSpec {
    std::uint32_t vertexCount = 0;
    std::vector<std::int64_t> deltas;
    std::vector<std::int64_t> originX;
    std::vector<std::int64_t> originY;
    std::vector<Property> properties;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::int64_t stepX = 0;
    std::int64_t stepY = 0;
    std::int64_t colY = 0;
    std::int64_t rowX = 0;
    bool memberProperties = true;
};

struct PathRepeatSpec {
    std::uint32_t width = 0;
    std::uint32_t vertexCount = 0;
    std::int32_t bgnExt = 0;
    std::int32_t endExt = 0;
    bool round = false;
    std::vector<std::int64_t> deltas;
    std::vector<std::int64_t> originX;
    std::vector<std::int64_t> originY;
    std::vector<Property> properties;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::int64_t stepX = 0;
    std::int64_t stepY = 0;
    std::int64_t colY = 0;
    std::int64_t rowX = 0;
    bool memberProperties = true;
};

struct PendingPath {
    std::uint32_t width = 0;
    std::int32_t bgnExt = 0;
    std::int32_t endExt = 0;
    bool round = false;
    std::vector<Point> points;
    std::vector<Property> properties;
};

struct LayerBucket {
    std::uint32_t layerId = 0;
    std::vector<std::int64_t> rectCoords;
    std::vector<std::uint32_t> polygonVertexCounts;
    std::vector<std::int64_t> polygonDeltas;
    std::vector<std::uint32_t> pathWidths;
    std::vector<std::uint32_t> pathVertexCounts;
    std::vector<std::int64_t> pathDeltas;
    std::vector<std::int32_t> pathBgnExt;
    std::vector<std::int32_t> pathEndExt;
    std::vector<std::uint8_t> pathRound;
    std::vector<std::int64_t> textX;
    std::vector<std::int64_t> textY;
    std::vector<std::uint32_t> textHeights;
    std::vector<std::string> texts;
    std::vector<std::uint32_t> shapePropertyCounts;
    std::vector<Property> shapeProperties;
    std::vector<RectArraySpec> rectArrays;
    std::vector<RectGroupSpec> rectGroups;
    std::vector<PolygonRepeatSpec> polygonRepeats;
    std::vector<PathRepeatSpec> pathRepeats;
};

void appendShapeProperties(LayerBucket &bucket, const std::vector<Property> &properties)
{
    bucket.shapePropertyCounts.push_back(static_cast<std::uint32_t>(properties.size()));
    bucket.shapeProperties.insert(bucket.shapeProperties.end(), properties.begin(), properties.end());
}

void appendDeltaEncoded(std::vector<std::int64_t> &out, const std::vector<Point> &points)
{
    if (points.empty()) {
        return;
    }
    out.push_back(points[0].x);
    out.push_back(points[0].y);
    for (std::size_t i = 1; i < points.size(); ++i) {
        out.push_back(points[i].x - points[i - 1].x);
        out.push_back(points[i].y - points[i - 1].y);
    }
}

std::vector<Point> decodeDeltaEncoded(const std::vector<std::int64_t> &deltas, std::size_t vertexCount)
{
    std::vector<Point> points;
    if (vertexCount == 0 || deltas.size() < 2) {
        return points;
    }
    points.reserve(vertexCount);
    points.push_back({deltas[0], deltas[1]});
    std::size_t cursor = 2;
    for (std::size_t i = 1; i < vertexCount; ++i) {
        if (cursor + 1 >= deltas.size()) {
            break;
        }
        const Point prev = points.back();
        points.push_back({prev.x + deltas[cursor], prev.y + deltas[cursor + 1]});
        cursor += 2;
    }
    return points;
}

bool propertiesEqual(const std::vector<Property> &a, const std::vector<Property> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name || a[i].value != b[i].value) {
            return false;
        }
    }
    return true;
}

std::string propertyKey(const std::vector<Property> &properties)
{
    std::string key;
    for (const Property &prop : properties) {
        key.push_back('\1');
        key += prop.name;
        key.push_back('\2');
        key += prop.value;
    }
    return key;
}

std::int64_t rectWidth(const RawRect &rect)
{
    return rect.urx - rect.llx;
}

std::int64_t rectHeight(const RawRect &rect)
{
    return rect.ury - rect.lly;
}

bool tryExtractRectArray(std::vector<RawRect> &rects, RectArraySpec &arrayOut)
{
    if (rects.size() < 4) {
        return false;
    }

    const std::int64_t width = rectWidth(rects[0]);
    const std::int64_t height = rectHeight(rects[0]);
    const std::vector<Property> refProps = rects[0].properties;

    std::set<std::int64_t> xs;
    std::set<std::int64_t> ys;
    for (const RawRect &rect : rects) {
        if (rectWidth(rect) != width || rectHeight(rect) != height) {
            return false;
        }
        if (!propertiesEqual(rect.properties, refProps)) {
            return false;
        }
        xs.insert(rect.llx);
        ys.insert(rect.lly);
    }

    const std::size_t columns = xs.size();
    const std::size_t rows = ys.size();
    if (columns < 2 || rows < 2 || columns * rows != rects.size()) {
        return false;
    }

    std::vector<std::int64_t> xVals(xs.begin(), xs.end());
    std::vector<std::int64_t> yVals(ys.begin(), ys.end());
    const std::int64_t stepX = xVals[1] - xVals[0];
    const std::int64_t stepY = yVals[1] - yVals[0];
    for (std::size_t i = 1; i + 1 < xVals.size(); ++i) {
        if (xVals[i + 1] - xVals[i] != stepX) {
            return false;
        }
    }
    for (std::size_t i = 1; i + 1 < yVals.size(); ++i) {
        if (yVals[i + 1] - yVals[i] != stepY) {
            return false;
        }
    }

    for (const RawRect &rect : rects) {
        const std::int64_t col = (rect.llx - xVals[0]) / stepX;
        const std::int64_t row = (rect.lly - yVals[0]) / stepY;
        if (xVals[0] + col * stepX != rect.llx || yVals[0] + row * stepY != rect.lly) {
            return false;
        }
    }

    arrayOut.originLlx = xVals[0];
    arrayOut.originLly = yVals[0];
    arrayOut.width = width;
    arrayOut.height = height;
    arrayOut.columns = static_cast<std::uint32_t>(columns);
    arrayOut.rows = static_cast<std::uint32_t>(rows);
    arrayOut.stepX = stepX;
    arrayOut.stepY = stepY;
    arrayOut.properties = refProps;
    rects.clear();
    return true;
}

void optimizeRectangles(std::vector<RawRect> &rects, LayerBucket &bucket)
{
    using SizeKey = std::pair<std::int64_t, std::int64_t>;
    std::map<SizeKey, std::vector<RawRect>> bySize;

    for (RawRect &rect : rects) {
        bySize[{rectWidth(rect), rectHeight(rect)}].push_back(std::move(rect));
    }
    rects.clear();

    for (auto &entry : bySize) {
        auto &group = entry.second;
        while (group.size() >= 4) {
            RectArraySpec arraySpec;
            if (tryExtractRectArray(group, arraySpec)) {
                bucket.rectArrays.push_back(std::move(arraySpec));
                continue;
            }
            break;
        }

        std::map<std::string, std::vector<RawRect>> byProps;
        for (RawRect &rect : group) {
            byProps[propertyKey(rect.properties)].push_back(std::move(rect));
        }

        for (auto &propEntry : byProps) {
            auto &sameProps = propEntry.second;
            if (sameProps.size() >= 2) {
                RectGroupSpec groupSpec;
                groupSpec.width = entry.first.first;
                groupSpec.height = entry.first.second;
                groupSpec.llx.reserve(sameProps.size());
                groupSpec.lly.reserve(sameProps.size());
                for (const RawRect &rect : sameProps) {
                    groupSpec.llx.push_back(rect.llx);
                    groupSpec.lly.push_back(rect.lly);
                }
                groupSpec.propertiesPerPlacement.assign(sameProps.size(), sameProps.front().properties);
                bucket.rectGroups.push_back(std::move(groupSpec));
            } else {
                for (RawRect &rect : sameProps) {
                    rects.push_back(std::move(rect));
                }
            }
        }
    }

    for (const RawRect &rect : rects) {
        bucket.rectCoords.push_back(rect.llx);
        bucket.rectCoords.push_back(rect.lly);
        bucket.rectCoords.push_back(rect.urx);
        bucket.rectCoords.push_back(rect.ury);
        appendShapeProperties(bucket, rect.properties);
    }
    rects.clear();
}

struct DeltaKey {
    std::vector<std::int64_t> deltas;

    bool operator==(const DeltaKey &other) const { return deltas == other.deltas; }
};

struct DeltaKeyHash {
    std::size_t operator()(const DeltaKey &key) const
    {
        std::size_t hash = 0;
        for (const std::int64_t value : key.deltas) {
            hash ^= static_cast<std::size_t>(value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

void optimizePolygons(std::vector<std::pair<std::vector<Point>, std::vector<Property>>> &polygons,
                      LayerBucket &bucket)
{
    struct Entry {
        std::vector<Point> points;
        std::vector<Property> properties;
        std::vector<std::int64_t> deltas;
    };

    std::unordered_map<DeltaKey, std::vector<Entry>, DeltaKeyHash> groups;
    for (auto &polygon : polygons) {
        Entry entry;
        entry.points = std::move(polygon.first);
        entry.properties = std::move(polygon.second);
        appendDeltaEncoded(entry.deltas, entry.points);
        DeltaKey key{entry.deltas};
        groups[key].push_back(std::move(entry));
    }
    polygons.clear();

    for (auto &entry : groups) {
        auto &items = entry.second;
        if (items.size() >= 2) {
            bool sameProps = true;
            for (std::size_t i = 1; i < items.size(); ++i) {
                if (!propertiesEqual(items[0].properties, items[i].properties)) {
                    sameProps = false;
                    break;
                }
            }
            if (sameProps) {
                PolygonRepeatSpec repeat;
                repeat.vertexCount = static_cast<std::uint32_t>(items[0].points.size());
                repeat.deltas = entry.first.deltas;
                repeat.properties = items[0].properties;
                for (const Entry &item : items) {
                    repeat.originX.push_back(item.points[0].x);
                    repeat.originY.push_back(item.points[0].y);
                }
                bucket.polygonRepeats.push_back(std::move(repeat));
                continue;
            }
        }

        for (Entry &item : items) {
            bucket.polygonVertexCounts.push_back(static_cast<std::uint32_t>(item.points.size()));
            bucket.polygonDeltas.insert(bucket.polygonDeltas.end(), item.deltas.begin(), item.deltas.end());
            appendShapeProperties(bucket, item.properties);
        }
    }
}

void optimizePaths(std::vector<PendingPath> &paths, LayerBucket &bucket)
{
    struct Entry {
        std::uint32_t width = 0;
        std::int32_t bgnExt = 0;
        std::int32_t endExt = 0;
        bool round = false;
        std::vector<Point> points;
        std::vector<Property> properties;
        std::vector<std::int64_t> deltas;
    };

    std::unordered_map<DeltaKey, std::vector<Entry>, DeltaKeyHash> groups;
    for (auto &path : paths) {
        Entry entry;
        entry.width = path.width;
        entry.bgnExt = path.bgnExt;
        entry.endExt = path.endExt;
        entry.round = path.round;
        entry.points = std::move(path.points);
        entry.properties = std::move(path.properties);
        appendDeltaEncoded(entry.deltas, entry.points);
        DeltaKey key{entry.deltas};
        groups[key].push_back(std::move(entry));
    }
    paths.clear();

    for (auto &entry : groups) {
        auto &items = entry.second;
        if (items.size() >= 2) {
            bool sameWidth = true;
            bool sameEnds = true;
            bool sameProps = true;
            for (std::size_t i = 1; i < items.size(); ++i) {
                if (items[i].width != items[0].width) {
                    sameWidth = false;
                }
                if (items[i].bgnExt != items[0].bgnExt || items[i].endExt != items[0].endExt ||
                    items[i].round != items[0].round) {
                    sameEnds = false;
                }
                if (!propertiesEqual(items[0].properties, items[i].properties)) {
                    sameProps = false;
                }
            }
            if (sameWidth && sameEnds && sameProps) {
                PathRepeatSpec repeat;
                repeat.width = items[0].width;
                repeat.bgnExt = items[0].bgnExt;
                repeat.endExt = items[0].endExt;
                repeat.round = items[0].round;
                repeat.vertexCount = static_cast<std::uint32_t>(items[0].points.size());
                repeat.deltas = entry.first.deltas;
                repeat.properties = items[0].properties;
                for (const Entry &item : items) {
                    repeat.originX.push_back(item.points[0].x);
                    repeat.originY.push_back(item.points[0].y);
                }
                bucket.pathRepeats.push_back(std::move(repeat));
                continue;
            }
        }

        for (Entry &item : items) {
            bucket.pathWidths.push_back(item.width);
            bucket.pathBgnExt.push_back(item.bgnExt);
            bucket.pathEndExt.push_back(item.endExt);
            bucket.pathRound.push_back(item.round ? 1 : 0);
            bucket.pathVertexCounts.push_back(static_cast<std::uint32_t>(item.points.size()));
            bucket.pathDeltas.insert(bucket.pathDeltas.end(), item.deltas.begin(), item.deltas.end());
            appendShapeProperties(bucket, item.properties);
        }
    }
}

std::map<std::uint32_t, LayerBucket> bucketShapes(const Block &block)
{
    std::map<std::uint32_t, LayerBucket> buckets;
    std::map<std::uint32_t, std::vector<RawRect>> pendingRects;
    std::map<std::uint32_t, std::vector<std::pair<std::vector<Point>, std::vector<Property>>>> pendingPolygons;
    std::map<std::uint32_t, std::vector<PendingPath>> pendingPaths;

    for (const Shape &shape : block.shapes()) {
        switch (shape.type()) {
        case Shape::Type::Rect: {
            const auto &rect = *shape.rect();
            LayerBucket &bucket = buckets[rect.layerId];
            bucket.layerId = rect.layerId;
            if (rect.copies.size() > 1) {
                RectGroupSpec group;
                group.width = rect.box.urx - rect.box.llx;
                group.height = rect.box.ury - rect.box.lly;
                group.memberProperties = !shape.properties().empty();
                group.llx.reserve(rect.copies.size());
                group.lly.reserve(rect.copies.size());
                for (const Point &off : rect.copies) {
                    group.llx.push_back(rect.box.llx + off.x);
                    group.lly.push_back(rect.box.lly + off.y);
                }
                if (group.memberProperties) {
                    group.propertiesPerPlacement.assign(rect.copies.size(), shape.properties());
                }
                bucket.rectGroups.push_back(std::move(group));
                break;
            }
            if (rect.columns > 1 || rect.rows > 1) {
                RectArraySpec spec;
                spec.originLlx = rect.box.llx;
                spec.originLly = rect.box.lly;
                spec.width = rect.box.urx - rect.box.llx;
                spec.height = rect.box.ury - rect.box.lly;
                spec.columns = rect.columns;
                spec.rows = rect.rows;
                spec.stepX = rect.stepX;
                spec.stepY = rect.stepY;
                spec.colY = rect.colY;
                spec.rowX = rect.rowX;
                spec.properties = shape.properties();
                spec.propertiesStored = true;
                spec.memberProperties = !shape.properties().empty();
                bucket.rectArrays.push_back(std::move(spec));
                if (spec.memberProperties) {
                    appendShapeProperties(bucket, shape.properties());
                }
                break;
            }
            pendingRects[rect.layerId].push_back(
                RawRect{rect.box.llx, rect.box.lly, rect.box.urx, rect.box.ury, shape.properties()});
            break;
        }
        case Shape::Type::Polygon: {
            const auto &polygon = *shape.polygon();
            LayerBucket &bucket = buckets[polygon.layerId];
            bucket.layerId = polygon.layerId;
            if (polygon.copies.size() > 1 && !polygon.points.empty()) {
                PolygonRepeatSpec repeat;
                repeat.vertexCount = static_cast<std::uint32_t>(polygon.points.size());
                appendDeltaEncoded(repeat.deltas, polygon.points);
                repeat.properties = shape.properties();
                const std::int64_t originX = polygon.points.front().x;
                const std::int64_t originY = polygon.points.front().y;
                repeat.originX.reserve(polygon.copies.size());
                repeat.originY.reserve(polygon.copies.size());
                for (const Point &off : polygon.copies) {
                    repeat.originX.push_back(originX + off.x);
                    repeat.originY.push_back(originY + off.y);
                }
                repeat.memberProperties = !shape.properties().empty();
                bucket.polygonRepeats.push_back(std::move(repeat));
                break;
            }
            if ((polygon.columns > 1 || polygon.rows > 1) && !polygon.points.empty() &&
                shape.properties().empty()) {
                PolygonRepeatSpec repeat;
                repeat.vertexCount = static_cast<std::uint32_t>(polygon.points.size());
                appendDeltaEncoded(repeat.deltas, polygon.points);
                repeat.columns = polygon.columns == 0 ? 1 : polygon.columns;
                repeat.rows = polygon.rows == 0 ? 1 : polygon.rows;
                repeat.stepX = polygon.stepX;
                repeat.stepY = polygon.stepY;
                repeat.colY = polygon.colY;
                repeat.rowX = polygon.rowX;
                repeat.originX.push_back(polygon.points.front().x);
                repeat.originY.push_back(polygon.points.front().y);
                repeat.memberProperties = false;
                bucket.polygonRepeats.push_back(std::move(repeat));
                break;
            }
            pendingPolygons[polygon.layerId].emplace_back(polygon.points, shape.properties());
            break;
        }
        case Shape::Type::Path: {
            const auto &path = *shape.path();
            LayerBucket &bucket = buckets[path.layerId];
            bucket.layerId = path.layerId;
            if (path.copies.size() > 1 && !path.points.empty()) {
                PathRepeatSpec repeat;
                repeat.width = path.width;
                repeat.bgnExt = path.bgnExt;
                repeat.endExt = path.endExt;
                repeat.round = path.round;
                repeat.vertexCount = static_cast<std::uint32_t>(path.points.size());
                appendDeltaEncoded(repeat.deltas, path.points);
                repeat.properties = shape.properties();
                const std::int64_t originX = path.points.front().x;
                const std::int64_t originY = path.points.front().y;
                repeat.originX.reserve(path.copies.size());
                repeat.originY.reserve(path.copies.size());
                for (const Point &off : path.copies) {
                    repeat.originX.push_back(originX + off.x);
                    repeat.originY.push_back(originY + off.y);
                }
                repeat.memberProperties = !shape.properties().empty();
                bucket.pathRepeats.push_back(std::move(repeat));
                break;
            }
            if ((path.columns > 1 || path.rows > 1) && !path.points.empty() && shape.properties().empty()) {
                PathRepeatSpec repeat;
                repeat.width = path.width;
                repeat.bgnExt = path.bgnExt;
                repeat.endExt = path.endExt;
                repeat.round = path.round;
                repeat.vertexCount = static_cast<std::uint32_t>(path.points.size());
                appendDeltaEncoded(repeat.deltas, path.points);
                repeat.columns = path.columns == 0 ? 1 : path.columns;
                repeat.rows = path.rows == 0 ? 1 : path.rows;
                repeat.stepX = path.stepX;
                repeat.stepY = path.stepY;
                repeat.colY = path.colY;
                repeat.rowX = path.rowX;
                repeat.originX.push_back(path.points.front().x);
                repeat.originY.push_back(path.points.front().y);
                repeat.memberProperties = false;
                bucket.pathRepeats.push_back(std::move(repeat));
                break;
            }
            if ((path.columns > 1 || path.rows > 1) && !path.points.empty()) {
                const std::uint32_t columns = path.columns == 0 ? 1 : path.columns;
                const std::uint32_t rows = path.rows == 0 ? 1 : path.rows;
                const std::uint64_t members = static_cast<std::uint64_t>(columns) * rows;
                if (members > 0) {
                PathRepeatSpec repeat;
                repeat.width = path.width;
                repeat.bgnExt = path.bgnExt;
                repeat.endExt = path.endExt;
                repeat.round = path.round;
                repeat.vertexCount = static_cast<std::uint32_t>(path.points.size());
                appendDeltaEncoded(repeat.deltas, path.points);
                repeat.properties = shape.properties();
                const std::int64_t originX = path.points.front().x;
                const std::int64_t originY = path.points.front().y;
                repeat.originX.reserve(static_cast<std::size_t>(columns) * rows);
                repeat.originY.reserve(static_cast<std::size_t>(columns) * rows);
                for (std::uint32_t row = 0; row < rows; ++row) {
                    for (std::uint32_t col = 0; col < columns; ++col) {
                        repeat.originX.push_back(originX + static_cast<std::int64_t>(col) * path.stepX);
                        repeat.originY.push_back(originY + static_cast<std::int64_t>(row) * path.stepY);
                    }
                }
                bucket.pathRepeats.push_back(std::move(repeat));
                break;
                }
            }
            PendingPath pending;
            pending.width = path.width;
            pending.bgnExt = path.bgnExt;
            pending.endExt = path.endExt;
            pending.round = path.round;
            pending.points = path.points;
            pending.properties = shape.properties();
            pendingPaths[path.layerId].push_back(std::move(pending));
            break;
        }
        case Shape::Type::Text: {
            const auto &text = *shape.text();
            LayerBucket &bucket = buckets[text.layerId];
            bucket.layerId = text.layerId;
            bucket.textX.push_back(text.position.x);
            bucket.textY.push_back(text.position.y);
            bucket.textHeights.push_back(text.height);
            bucket.texts.push_back(text.text);
            appendShapeProperties(bucket, shape.properties());
            break;
        }
        case Shape::Type::Arc: {
            const auto &arc = *shape.arc();
            LayerBucket &bucket = buckets[arc.layerId];
            bucket.layerId = arc.layerId;
            std::vector<Point> points;
            constexpr int segments = 32;
            // Xschem arc record stores start angle and span (not absolute end angle).
            const double start = arc.startAngle * 3.141592653589793 / 180.0;
            const double span = arc.endAngle * 3.141592653589793 / 180.0;
            const double cx = static_cast<double>(arc.center.x) / 1000.0;
            const double cy = static_cast<double>(arc.center.y) / 1000.0;
            for (int i = 0; i <= segments; ++i) {
                const double angle = start + span * static_cast<double>(i) / static_cast<double>(segments);
                const double x = cx + arc.radius * std::cos(angle);
                const double y = cy + arc.radius * std::sin(angle);
                points.push_back(
                    Point{static_cast<std::int64_t>(std::llround(x * 1000.0)), static_cast<std::int64_t>(std::llround(y * 1000.0))});
            }
            std::vector<Property> props = shape.properties();
            props.push_back({"geometry", "arc"});
            props.push_back({"arc.centerX", std::to_string(arc.center.x)});
            props.push_back({"arc.centerY", std::to_string(arc.center.y)});
            props.push_back({"arc.radius", std::to_string(arc.radius)});
            props.push_back({"arc.startAngle", std::to_string(arc.startAngle)});
            props.push_back({"arc.endAngle", std::to_string(arc.endAngle)});
            PendingPath pending;
            pending.width = arc.width;
            pending.points = std::move(points);
            pending.properties = std::move(props);
            pendingPaths[arc.layerId].push_back(std::move(pending));
            break;
        }
        }
    }

    for (auto &entry : buckets) {
        const std::uint32_t layerId = entry.first;
        LayerBucket &bucket = entry.second;
        optimizeRectangles(pendingRects[layerId], bucket);
        for (const RectArraySpec &arraySpec : bucket.rectArrays) {
            if (arraySpec.propertiesStored || !arraySpec.memberProperties) {
                continue;
            }
            const std::size_t placementCount =
                static_cast<std::size_t>(arraySpec.columns) * static_cast<std::size_t>(arraySpec.rows);
            for (std::size_t i = 0; i < placementCount; ++i) {
                appendShapeProperties(bucket, arraySpec.properties);
            }
        }
        for (const RectGroupSpec &groupSpec : bucket.rectGroups) {
            if (!groupSpec.memberProperties) {
                continue;
            }
            for (const std::vector<Property> &props : groupSpec.propertiesPerPlacement) {
                appendShapeProperties(bucket, props);
            }
        }
        optimizePolygons(pendingPolygons[layerId], bucket);
        for (const PolygonRepeatSpec &repeat : bucket.polygonRepeats) {
            if (!repeat.memberProperties) {
                continue;
            }
            for (std::size_t i = 0; i < repeat.originX.size(); ++i) {
                appendShapeProperties(bucket, repeat.properties);
            }
        }
        optimizePaths(pendingPaths[layerId], bucket);
        for (const PathRepeatSpec &repeat : bucket.pathRepeats) {
            if (!repeat.memberProperties) {
                continue;
            }
            for (std::size_t i = 0; i < repeat.originX.size(); ++i) {
                appendShapeProperties(bucket, repeat.properties);
            }
        }
    }

    return buckets;
}

std::vector<std::int64_t> loadPackedOrList(capnp::Data::Reader packed, capnp::List<std::int64_t>::Reader list)
{
    if (packed.size() > 0) {
        return decodeVarint64(reinterpret_cast<const std::uint8_t *>(packed.begin()), packed.size());
    }
    std::vector<std::int64_t> out;
    out.reserve(list.size());
    for (const auto value : list) {
        out.push_back(value);
    }
    return out;
}

void writePackedDeltas(schema::CompactLayerShapes::Builder builder,
                       const std::vector<std::int64_t> &deltas,
                       bool isPolygon)
{
    if (deltas.empty()) {
        return;
    }
    const std::vector<std::uint8_t> packed = encodeVarint64(deltas);
    const std::size_t listBytes = deltas.size() * sizeof(std::int64_t);
    if (packed.size() < listBytes) {
        if (isPolygon) {
            builder.setPolygonDeltasPacked(capnp::Data::Reader(packed.data(), packed.size()));
        } else {
            builder.setPathDeltasPacked(capnp::Data::Reader(packed.data(), packed.size()));
        }
        return;
    }
    if (isPolygon) {
        auto list = builder.initPolygonDeltas(deltas.size());
        for (std::size_t i = 0; i < deltas.size(); ++i) {
            list.set(i, deltas[i]);
        }
    } else {
        auto list = builder.initPathDeltas(deltas.size());
        for (std::size_t i = 0; i < deltas.size(); ++i) {
            list.set(i, deltas[i]);
        }
    }
}

void writeLayerBucket(schema::CompactLayerShapes::Builder builder, const LayerBucket &bucket)
{
    builder.setLayerId(bucket.layerId);

    auto rects = builder.initRectCoords(bucket.rectCoords.size());
    for (std::size_t i = 0; i < bucket.rectCoords.size(); ++i) {
        rects.set(i, bucket.rectCoords[i]);
    }

    auto arrays = builder.initRectArrays(bucket.rectArrays.size());
    for (std::size_t i = 0; i < bucket.rectArrays.size(); ++i) {
        const RectArraySpec &spec = bucket.rectArrays[i];
        auto ab = arrays[i];
        ab.setOriginLlx(spec.originLlx);
        ab.setOriginLly(spec.originLly);
        ab.setWidth(spec.width);
        ab.setHeight(spec.height);
        ab.setColumns(spec.columns);
        ab.setRows(spec.rows);
        ab.setStepX(spec.stepX);
        ab.setStepY(spec.stepY);
        ab.setColY(spec.colY);
        ab.setRowX(spec.rowX);
        ab.setMemberProperties(spec.memberProperties);
    }

    auto groups = builder.initRectGroups(bucket.rectGroups.size());
    for (std::size_t i = 0; i < bucket.rectGroups.size(); ++i) {
        const RectGroupSpec &spec = bucket.rectGroups[i];
        auto gb = groups[i];
        gb.setWidth(spec.width);
        gb.setHeight(spec.height);
        gb.setMemberProperties(spec.memberProperties);
        auto llx = gb.initLlx(spec.llx.size());
        auto lly = gb.initLly(spec.lly.size());
        for (std::size_t j = 0; j < spec.llx.size(); ++j) {
            llx.set(j, spec.llx[j]);
            lly.set(j, spec.lly[j]);
        }
    }

    auto polygonCounts = builder.initPolygonVertexCounts(bucket.polygonVertexCounts.size());
    for (std::size_t i = 0; i < bucket.polygonVertexCounts.size(); ++i) {
        polygonCounts.set(i, bucket.polygonVertexCounts[i]);
    }
    writePackedDeltas(builder, bucket.polygonDeltas, true);

    auto polyRepeats = builder.initPolygonRepeats(bucket.polygonRepeats.size());
    for (std::size_t i = 0; i < bucket.polygonRepeats.size(); ++i) {
        const PolygonRepeatSpec &spec = bucket.polygonRepeats[i];
        auto pb = polyRepeats[i];
        pb.setVertexCount(spec.vertexCount);
        pb.setColumns(spec.columns);
        pb.setRows(spec.rows);
        pb.setStepX(spec.stepX);
        pb.setStepY(spec.stepY);
        pb.setColY(spec.colY);
        pb.setRowX(spec.rowX);
        pb.setMemberProperties(spec.memberProperties);
        auto deltas = pb.initDeltas(spec.deltas.size());
        for (std::size_t j = 0; j < spec.deltas.size(); ++j) {
            deltas.set(j, spec.deltas[j]);
        }
        auto ox = pb.initOriginX(spec.originX.size());
        auto oy = pb.initOriginY(spec.originY.size());
        for (std::size_t j = 0; j < spec.originX.size(); ++j) {
            ox.set(j, spec.originX[j]);
            oy.set(j, spec.originY[j]);
        }
    }

    auto pathWidths = builder.initPathWidths(bucket.pathWidths.size());
    for (std::size_t i = 0; i < bucket.pathWidths.size(); ++i) {
        pathWidths.set(i, bucket.pathWidths[i]);
    }
    auto pathCounts = builder.initPathVertexCounts(bucket.pathVertexCounts.size());
    for (std::size_t i = 0; i < bucket.pathVertexCounts.size(); ++i) {
        pathCounts.set(i, bucket.pathVertexCounts[i]);
    }
    writePackedDeltas(builder, bucket.pathDeltas, false);

    bool customEnds = false;
    for (std::size_t i = 0; i < bucket.pathWidths.size(); ++i) {
        const std::int32_t bgn = i < bucket.pathBgnExt.size() ? bucket.pathBgnExt[i] : 0;
        const std::int32_t end = i < bucket.pathEndExt.size() ? bucket.pathEndExt[i] : 0;
        const bool round = i < bucket.pathRound.size() && bucket.pathRound[i] != 0;
        if (bgn != 0 || end != 0 || round) {
            customEnds = true;
            break;
        }
    }
    if (customEnds) {
        auto bgnExt = builder.initPathBgnExt(bucket.pathWidths.size());
        auto endExt = builder.initPathEndExt(bucket.pathWidths.size());
        auto round = builder.initPathRound(bucket.pathWidths.size());
        for (std::size_t i = 0; i < bucket.pathWidths.size(); ++i) {
            bgnExt.set(i, i < bucket.pathBgnExt.size() ? bucket.pathBgnExt[i] : 0);
            endExt.set(i, i < bucket.pathEndExt.size() ? bucket.pathEndExt[i] : 0);
            round.set(i, i < bucket.pathRound.size() && bucket.pathRound[i] != 0);
        }
    }

    auto pathRepeats = builder.initPathRepeats(bucket.pathRepeats.size());
    for (std::size_t i = 0; i < bucket.pathRepeats.size(); ++i) {
        const PathRepeatSpec &spec = bucket.pathRepeats[i];
        auto pb = pathRepeats[i];
        pb.setWidth(spec.width);
        pb.setVertexCount(spec.vertexCount);
        pb.setBgnExt(spec.bgnExt);
        pb.setEndExt(spec.endExt);
        pb.setRound(spec.round);
        pb.setColumns(spec.columns);
        pb.setRows(spec.rows);
        pb.setStepX(spec.stepX);
        pb.setStepY(spec.stepY);
        pb.setColY(spec.colY);
        pb.setRowX(spec.rowX);
        pb.setMemberProperties(spec.memberProperties);
        auto deltas = pb.initDeltas(spec.deltas.size());
        for (std::size_t j = 0; j < spec.deltas.size(); ++j) {
            deltas.set(j, spec.deltas[j]);
        }
        auto ox = pb.initOriginX(spec.originX.size());
        auto oy = pb.initOriginY(spec.originY.size());
        for (std::size_t j = 0; j < spec.originX.size(); ++j) {
            ox.set(j, spec.originX[j]);
            oy.set(j, spec.originY[j]);
        }
    }

    auto textX = builder.initTextX(bucket.textX.size());
    auto textY = builder.initTextY(bucket.textY.size());
    auto textHeights = builder.initTextHeights(bucket.textHeights.size());
    auto texts = builder.initTexts(bucket.texts.size());
    for (std::size_t i = 0; i < bucket.texts.size(); ++i) {
        textX.set(i, bucket.textX[i]);
        textY.set(i, bucket.textY[i]);
        textHeights.set(i, bucket.textHeights[i]);
        texts.set(i, bucket.texts[i]);
    }

    auto propCounts = builder.initShapePropertyCounts(bucket.shapePropertyCounts.size());
    for (std::size_t i = 0; i < bucket.shapePropertyCounts.size(); ++i) {
        propCounts.set(i, bucket.shapePropertyCounts[i]);
    }
    auto props = builder.initShapeProperties(bucket.shapeProperties.size());
    for (std::size_t i = 0; i < bucket.shapeProperties.size(); ++i) {
        props[i].setName(bucket.shapeProperties[i].name);
        props[i].setValue(bucket.shapeProperties[i].value);
    }
}

template <typename Emit>
void readLayerBucket(schema::CompactLayerShapes::Reader reader, Emit &&emit,
                     const CompactRepeatHooks *hooks = nullptr)
{
    const std::uint32_t layerId = reader.getLayerId();
    const auto propCounts = reader.getShapePropertyCounts();
    const auto props = reader.getShapeProperties();
    std::size_t shapePropertyIndex = 0;
    std::size_t propertyCursor = 0;

    auto assignShapeProperties = [&](Shape &shape) {
        const std::uint32_t count =
            shapePropertyIndex < propCounts.size() ? propCounts[shapePropertyIndex++] : 0;
        for (std::uint32_t i = 0; i < count && propertyCursor < props.size(); ++i, ++propertyCursor) {
            const auto prop = props[propertyCursor];
            shape.properties().push_back({prop.getName().cStr(), prop.getValue().cStr()});
        }
    };

    auto membersHaveProperties = [&](std::uint64_t count) -> bool {
        std::uint64_t left = count;
        for (std::size_t idx = shapePropertyIndex; idx < propCounts.size() && left > 0; ++idx, --left) {
            if (propCounts[idx] != 0) {
                return true;
            }
        }
        return false;
    };

    auto skipMemberProperties = [&](std::uint64_t count) {
        std::uint64_t left = count;
        while (shapePropertyIndex < propCounts.size() && left > 0) {
            propertyCursor += propCounts[shapePropertyIndex++];
            --left;
        }
    };

    const auto rectCoords = reader.getRectCoords();
    for (std::size_t i = 0; i + 3 < rectCoords.size(); i += 4) {
        Shape::RectData data;
        data.layerId = layerId;
        data.box = Box{rectCoords[i], rectCoords[i + 1], rectCoords[i + 2], rectCoords[i + 3]};
        Shape shape(data);
        assignShapeProperties(shape);
        emit(std::move(shape));
    }

    for (const auto array : reader.getRectArrays()) {
        const std::uint64_t columns = array.getColumns();
        const std::uint64_t rows = array.getRows();
        const std::uint64_t count = columns * rows;
        const bool perMember = array.getMemberProperties();
        if (count > 1 && hooks && hooks->rectArray && (!perMember || !membersHaveProperties(count))) {
            CompactRectArrayRef ref;
            ref.layerId = layerId;
            ref.originLlx = array.getOriginLlx();
            ref.originLly = array.getOriginLly();
            ref.width = array.getWidth();
            ref.height = array.getHeight();
            ref.columns = array.getColumns();
            ref.rows = array.getRows();
            ref.stepX = array.getStepX();
            ref.stepY = array.getStepY();
            ref.colY = array.getColY();
            ref.rowX = array.getRowX();
            hooks->rectArray(ref);
            if (perMember) {
                skipMemberProperties(count);
            }
            continue;
        }
        for (std::uint32_t row = 0; row < array.getRows(); ++row) {
            for (std::uint32_t col = 0; col < array.getColumns(); ++col) {
                const std::int64_t llx = array.getOriginLlx() + static_cast<std::int64_t>(col) * array.getStepX() +
                                         static_cast<std::int64_t>(row) * array.getRowX();
                const std::int64_t lly = array.getOriginLly() + static_cast<std::int64_t>(col) * array.getColY() +
                                         static_cast<std::int64_t>(row) * array.getStepY();
                Shape::RectData data;
                data.layerId = layerId;
                data.box = Box{llx, lly, llx + array.getWidth(), lly + array.getHeight()};
                Shape shape(data);
                if (perMember) {
                    assignShapeProperties(shape);
                }
                emit(std::move(shape));
            }
        }
    }

    for (const auto group : reader.getRectGroups()) {
        const auto llxList = group.getLlx();
        const auto llyList = group.getLly();
        const std::size_t count = llxList.size();
        const bool perMember = group.getMemberProperties();
        if (count > 1 && llxList.size() == llyList.size() && hooks && hooks->rectGroup &&
            (!perMember || !membersHaveProperties(count))) {
            CompactRectGroupRef ref;
            ref.layerId = layerId;
            ref.width = group.getWidth();
            ref.height = group.getHeight();
            ref.llx.reserve(count);
            ref.lly.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                ref.llx.push_back(llxList[i]);
                ref.lly.push_back(llyList[i]);
            }
            hooks->rectGroup(ref);
            if (perMember) {
                skipMemberProperties(count);
            }
            continue;
        }
        for (std::size_t i = 0; i < count; ++i) {
            Shape::RectData data;
            data.layerId = layerId;
            data.box = Box{llxList[i], llyList[i], llxList[i] + group.getWidth(), llyList[i] + group.getHeight()};
            Shape shape(data);
            if (perMember) {
                assignShapeProperties(shape);
            }
            emit(std::move(shape));
        }
    }

    const auto polygonCounts = reader.getPolygonVertexCounts();
    std::vector<std::int64_t> polygonStream =
        loadPackedOrList(reader.getPolygonDeltasPacked(), reader.getPolygonDeltas());
    std::size_t polygonCursor = 0;
    for (const auto vertexCount : polygonCounts) {
        const std::size_t count = vertexCount;
        const std::size_t needed = count == 0 ? 0 : 2 + (count - 1) * 2;
        if (polygonCursor + needed > polygonStream.size()) {
            break;
        }
        std::vector<std::int64_t> slice(polygonStream.begin() + static_cast<std::ptrdiff_t>(polygonCursor),
                                        polygonStream.begin()
                                            + static_cast<std::ptrdiff_t>(polygonCursor + needed));
        Shape::PolygonData data;
        data.layerId = layerId;
        data.points = decodeDeltaEncoded(slice, count);
        Shape shape(data);
        assignShapeProperties(shape);
        emit(std::move(shape));
        polygonCursor += needed;
    }

    for (const auto repeat : reader.getPolygonRepeats()) {
        std::vector<std::int64_t> deltas;
        for (const auto value : repeat.getDeltas()) {
            deltas.push_back(value);
        }
        const auto originsX = repeat.getOriginX();
        const auto originsY = repeat.getOriginY();
        const bool perMember = repeat.getMemberProperties();
        const bool grid = repeat.getColumns() > 1 || repeat.getRows() > 1;
        const std::uint64_t gridCount =
            static_cast<std::uint64_t>(repeat.getColumns()) * static_cast<std::uint64_t>(repeat.getRows());
        if (grid && originsX.size() != 0 && originsX.size() == originsY.size() && hooks && hooks->polygonRepeat &&
            (!perMember || !membersHaveProperties(gridCount))) {
            std::vector<std::int64_t> slice = deltas;
            if (!slice.empty()) {
                slice[0] = originsX[0];
            }
            if (slice.size() > 1) {
                slice[1] = originsY[0];
            }
            CompactPolygonRepeatRef ref;
            ref.layerId = layerId;
            ref.points = decodeDeltaEncoded(slice, repeat.getVertexCount());
            ref.originX.push_back(originsX[0]);
            ref.originY.push_back(originsY[0]);
            ref.columns = repeat.getColumns();
            ref.rows = repeat.getRows();
            ref.stepX = repeat.getStepX();
            ref.stepY = repeat.getStepY();
            ref.colY = repeat.getColY();
            ref.rowX = repeat.getRowX();
            if (ref.points.size() >= 3) {
                hooks->polygonRepeat(ref);
                if (perMember) {
                    skipMemberProperties(gridCount);
                }
                continue;
            }
        }
        if (!grid && originsX.size() > 1 && originsX.size() == originsY.size() && hooks && hooks->polygonRepeat &&
            (!perMember || !membersHaveProperties(originsX.size()))) {
            std::vector<std::int64_t> slice = deltas;
            if (!slice.empty()) {
                slice[0] = originsX[0];
            }
            if (slice.size() > 1) {
                slice[1] = originsY[0];
            }
            CompactPolygonRepeatRef ref;
            ref.layerId = layerId;
            ref.points = decodeDeltaEncoded(slice, repeat.getVertexCount());
            ref.originX.reserve(originsX.size());
            ref.originY.reserve(originsY.size());
            for (const auto value : originsX) {
                ref.originX.push_back(value);
            }
            for (const auto value : originsY) {
                ref.originY.push_back(value);
            }
            if (ref.points.size() >= 3) {
                hooks->polygonRepeat(ref);
                if (perMember) {
                    skipMemberProperties(originsX.size());
                }
                continue;
            }
        }
        if (grid && originsX.size() != 0) {
            for (std::uint32_t row = 0; row < repeat.getRows(); ++row) {
                for (std::uint32_t col = 0; col < repeat.getColumns(); ++col) {
                    std::vector<std::int64_t> slice = deltas;
                    if (!slice.empty()) {
                        slice[0] = originsX[0] + static_cast<std::int64_t>(col) * repeat.getStepX() +
                                   static_cast<std::int64_t>(row) * repeat.getRowX();
                    }
                    if (slice.size() > 1) {
                        slice[1] = originsY[0] + static_cast<std::int64_t>(col) * repeat.getColY() +
                                   static_cast<std::int64_t>(row) * repeat.getStepY();
                    }
                    Shape::PolygonData data;
                    data.layerId = layerId;
                    data.points = decodeDeltaEncoded(slice, repeat.getVertexCount());
                    Shape shape(data);
                    if (perMember) {
                        assignShapeProperties(shape);
                    }
                    emit(std::move(shape));
                }
            }
            continue;
        }
        for (std::size_t i = 0; i < originsX.size(); ++i) {
            std::vector<std::int64_t> slice = deltas;
            if (!slice.empty()) {
                slice[0] = originsX[i];
            }
            if (slice.size() > 1) {
                slice[1] = originsY[i];
            }
            Shape::PolygonData data;
            data.layerId = layerId;
            data.points = decodeDeltaEncoded(slice, repeat.getVertexCount());
            Shape shape(data);
            if (perMember) {
                assignShapeProperties(shape);
            }
            emit(std::move(shape));
        }
    }

    const auto pathWidths = reader.getPathWidths();
    const auto pathCounts = reader.getPathVertexCounts();
    const auto pathBgn = reader.getPathBgnExt();
    const auto pathEnd = reader.getPathEndExt();
    const auto pathRound = reader.getPathRound();
    std::vector<std::int64_t> pathStream = loadPackedOrList(reader.getPathDeltasPacked(), reader.getPathDeltas());
    std::size_t pathCursor = 0;
    for (std::size_t pathIndex = 0; pathIndex < pathCounts.size(); ++pathIndex) {
        const std::size_t count = pathCounts[pathIndex];
        const std::size_t needed = count == 0 ? 0 : 2 + (count - 1) * 2;
        if (pathCursor + needed > pathStream.size()) {
            break;
        }
        std::vector<std::int64_t> slice(pathStream.begin() + static_cast<std::ptrdiff_t>(pathCursor),
                                        pathStream.begin() + static_cast<std::ptrdiff_t>(pathCursor + needed));
        Shape::PathData data;
        data.layerId = layerId;
        data.width = pathIndex < pathWidths.size() ? pathWidths[pathIndex] : 0;
        data.bgnExt = pathIndex < pathBgn.size() ? pathBgn[pathIndex] : 0;
        data.endExt = pathIndex < pathEnd.size() ? pathEnd[pathIndex] : 0;
        data.round = pathIndex < pathRound.size() && pathRound[pathIndex];
        data.points = decodeDeltaEncoded(slice, count);
        Shape shape(data);
        assignShapeProperties(shape);
        emit(std::move(shape));
        pathCursor += needed;
    }

    for (const auto repeat : reader.getPathRepeats()) {
        std::vector<std::int64_t> deltas;
        for (const auto value : repeat.getDeltas()) {
            deltas.push_back(value);
        }
        const auto originsX = repeat.getOriginX();
        const auto originsY = repeat.getOriginY();
        const bool perMember = repeat.getMemberProperties();
        const bool grid = repeat.getColumns() > 1 || repeat.getRows() > 1;
        const std::uint64_t gridCount =
            static_cast<std::uint64_t>(repeat.getColumns()) * static_cast<std::uint64_t>(repeat.getRows());
        auto fillPath = [&](CompactPathRepeatRef &ref, std::int64_t originX, std::int64_t originY) {
            std::vector<std::int64_t> slice = deltas;
            if (!slice.empty()) {
                slice[0] = originX;
            }
            if (slice.size() > 1) {
                slice[1] = originY;
            }
            ref.layerId = layerId;
            ref.width = repeat.getWidth();
            ref.bgnExt = repeat.getBgnExt();
            ref.endExt = repeat.getEndExt();
            ref.round = repeat.getRound();
            ref.points = decodeDeltaEncoded(slice, repeat.getVertexCount());
        };
        if (grid && originsX.size() != 0 && originsX.size() == originsY.size() && hooks && hooks->pathRepeat &&
            (!perMember || !membersHaveProperties(gridCount))) {
            CompactPathRepeatRef ref;
            fillPath(ref, originsX[0], originsY[0]);
            ref.originX.push_back(originsX[0]);
            ref.originY.push_back(originsY[0]);
            ref.columns = repeat.getColumns();
            ref.rows = repeat.getRows();
            ref.stepX = repeat.getStepX();
            ref.stepY = repeat.getStepY();
            ref.colY = repeat.getColY();
            ref.rowX = repeat.getRowX();
            if (!ref.points.empty()) {
                hooks->pathRepeat(ref);
                if (perMember) {
                    skipMemberProperties(gridCount);
                }
                continue;
            }
        }
        if (!grid && originsX.size() > 1 && originsX.size() == originsY.size() && hooks && hooks->pathRepeat &&
            (!perMember || !membersHaveProperties(originsX.size()))) {
            CompactPathRepeatRef ref;
            fillPath(ref, originsX[0], originsY[0]);
            ref.originX.reserve(originsX.size());
            ref.originY.reserve(originsY.size());
            for (const auto value : originsX) {
                ref.originX.push_back(value);
            }
            for (const auto value : originsY) {
                ref.originY.push_back(value);
            }
            if (!ref.points.empty()) {
                hooks->pathRepeat(ref);
                if (perMember) {
                    skipMemberProperties(originsX.size());
                }
                continue;
            }
        }
        if (grid && originsX.size() != 0) {
            for (std::uint32_t row = 0; row < repeat.getRows(); ++row) {
                for (std::uint32_t col = 0; col < repeat.getColumns(); ++col) {
                    const std::int64_t ox = originsX[0] + static_cast<std::int64_t>(col) * repeat.getStepX() +
                                            static_cast<std::int64_t>(row) * repeat.getRowX();
                    const std::int64_t oy = originsY[0] + static_cast<std::int64_t>(col) * repeat.getColY() +
                                            static_cast<std::int64_t>(row) * repeat.getStepY();
                    Shape::PathData data;
                    data.layerId = layerId;
                    data.width = repeat.getWidth();
                    data.bgnExt = repeat.getBgnExt();
                    data.endExt = repeat.getEndExt();
                    data.round = repeat.getRound();
                    std::vector<std::int64_t> slice = deltas;
                    if (!slice.empty()) {
                        slice[0] = ox;
                    }
                    if (slice.size() > 1) {
                        slice[1] = oy;
                    }
                    data.points = decodeDeltaEncoded(slice, repeat.getVertexCount());
                    Shape shape(data);
                    if (perMember) {
                        assignShapeProperties(shape);
                    }
                    emit(std::move(shape));
                }
            }
            continue;
        }
        for (std::size_t i = 0; i < originsX.size(); ++i) {
            std::vector<std::int64_t> slice = deltas;
            if (!slice.empty()) {
                slice[0] = originsX[i];
            }
            if (slice.size() > 1) {
                slice[1] = originsY[i];
            }
            Shape::PathData data;
            data.layerId = layerId;
            data.width = repeat.getWidth();
            data.bgnExt = repeat.getBgnExt();
            data.endExt = repeat.getEndExt();
            data.round = repeat.getRound();
            data.points = decodeDeltaEncoded(slice, repeat.getVertexCount());
            Shape shape(data);
            if (perMember) {
                assignShapeProperties(shape);
            }
            emit(std::move(shape));
        }
    }

    const auto textX = reader.getTextX();
    const auto textY = reader.getTextY();
    const auto textHeights = reader.getTextHeights();
    const auto texts = reader.getTexts();
    const std::size_t textCount = texts.size();
    for (std::size_t i = 0; i < textCount; ++i) {
        Shape::TextData data;
        data.layerId = layerId;
        data.position.x = i < textX.size() ? textX[i] : 0;
        data.position.y = i < textY.size() ? textY[i] : 0;
        data.height = i < textHeights.size() ? textHeights[i] : 0;
        data.text = texts[i].cStr();
        Shape shape(data);
        assignShapeProperties(shape);
        emit(std::move(shape));
    }
}

} // namespace

void writeCompactBlock(schema::CompactBlock::Builder builder, const Block &block)
{
    const auto buckets = bucketShapes(block);
    auto layers = builder.initLayerShapes(buckets.size());
    std::size_t layerIndex = 0;
    for (const auto &entry : buckets) {
        writeLayerBucket(layers[layerIndex++], entry.second);
    }

    auto instances = builder.initInstances(block.instances().size());
    for (std::size_t i = 0; i < block.instances().size(); ++i) {
        const auto &inst = block.instances()[i];
        auto ib = instances[i];
        ib.setCellName(inst.cellName());
        auto transform = ib.initTransform();
        transform.setX(inst.transform().x);
        transform.setY(inst.transform().y);
        transform.setOrient(toSchemaOrient(inst.transform().orient));
        transform.setMag(inst.transform().mag);
        if (inst.columns() > 1 || inst.rows() > 1) {
            ib.setColumns(inst.columns());
            ib.setRows(inst.rows());
            ib.setStepX(inst.stepX());
            ib.setStepY(inst.stepY());
            ib.setColY(inst.colY());
            ib.setRowX(inst.rowX());
        }
        if (inst.offsets().size() > 1) {
            auto ox = ib.initOffsetX(inst.offsets().size());
            auto oy = ib.initOffsetY(inst.offsets().size());
            for (std::size_t j = 0; j < inst.offsets().size(); ++j) {
                ox.set(j, inst.offsets()[j].x);
                oy.set(j, inst.offsets()[j].y);
            }
        }
        auto props = ib.initProperties(inst.properties().size());
        for (std::size_t pi = 0; pi < inst.properties().size(); ++pi) {
            props[pi].setName(inst.properties()[pi].name);
            props[pi].setValue(inst.properties()[pi].value);
        }
    }

    auto nets = builder.initNets(block.nets().size());
    for (std::size_t i = 0; i < block.nets().size(); ++i) {
        const auto &net = block.nets()[i];
        auto nb = nets[i];
        nb.setName(net.name());
        nb.setSigType(toSchemaSigType(net.sigType()));
        auto terms = nb.initTerms(net.terms().size());
        for (std::size_t j = 0; j < net.terms().size(); ++j) {
            terms[j].setName(net.terms()[j].name());
            terms[j].setLayerId(net.terms()[j].layerId());
            auto position = terms[j].initPosition();
            position.setX(net.terms()[j].position().x);
            position.setY(net.terms()[j].position().y);
        }
    }

    auto bbox = builder.initBbox();
    bbox.setLlx(block.bbox().llx);
    bbox.setLly(block.bbox().lly);
    bbox.setUrx(block.bbox().urx);
    bbox.setUry(block.bbox().ury);
}

Block readCompactBlock(schema::CompactBlock::Reader reader)
{
    Block block;
    for (const auto layer : reader.getLayerShapes()) {
        readLayerBucket(layer, [&](Shape shape) { block.shapes().push_back(std::move(shape)); });
    }

    for (const auto inst : reader.getInstances()) {
        Transform transform;
        const auto tr = inst.getTransform();
        transform.x = tr.getX();
        transform.y = tr.getY();
        transform.orient = fromSchemaOrient(tr.getOrient());
        transform.mag = tr.getMag();
        Instance instance(inst.getCellName().cStr(), transform);
        if (inst.getColumns() > 1 || inst.getRows() > 1) {
            instance.setArray(inst.getColumns(), inst.getRows(), inst.getStepX(), inst.getStepY(),
                               inst.getColY(), inst.getRowX());
        }
        const auto offsetX = inst.getOffsetX();
        const auto offsetY = inst.getOffsetY();
        if (offsetX.size() > 1 && offsetX.size() == offsetY.size()) {
            std::vector<Point> offsets;
            offsets.reserve(offsetX.size());
            for (std::size_t j = 0; j < offsetX.size(); ++j) {
                offsets.push_back(Point{offsetX[j], offsetY[j]});
            }
            instance.setOffsets(std::move(offsets));
        }
        for (const auto prop : inst.getProperties()) {
            instance.properties().push_back({prop.getName().cStr(), prop.getValue().cStr()});
        }
        block.instances().push_back(std::move(instance));
    }

    for (const auto net : reader.getNets()) {
        Net n(net.getName().cStr(), fromSchemaSigType(net.getSigType()));
        for (const auto term : net.getTerms()) {
            const auto position = term.getPosition();
            n.terms().emplace_back(term.getName().cStr(), term.getLayerId(),
                                   Point{position.getX(), position.getY()});
        }
        block.nets().push_back(std::move(n));
    }

    block.recomputeBBox();
    return block;
}

void visitCompactShapes(schema::CompactBlock::Reader reader, const std::function<void(Shape)> &emit,
                        const CompactRepeatHooks *hooks)
{
    for (const auto layer : reader.getLayerShapes()) {
        readLayerBucket(layer, [&](Shape shape) { emit(std::move(shape)); }, hooks);
    }
}

bool compactBlockHasGeometry(schema::CompactBlock::Reader reader)
{
    if (reader.getLayerShapes().size() > 0) {
        return true;
    }
    if (reader.getInstances().size() > 0) {
        return true;
    }
    if (reader.getNets().size() > 0) {
        return true;
    }
    return false;
}

} // namespace room
