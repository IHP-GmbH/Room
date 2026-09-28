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
    std::vector<Property> properties;
};

struct RectGroupSpec {
    std::int64_t width = 0;
    std::int64_t height = 0;
    std::vector<std::int64_t> llx;
    std::vector<std::int64_t> lly;
    std::vector<std::vector<Property>> propertiesPerPlacement;
};

struct PolygonRepeatSpec {
    std::uint32_t vertexCount = 0;
    std::vector<std::int64_t> deltas;
    std::vector<std::int64_t> originX;
    std::vector<std::int64_t> originY;
    std::vector<Property> properties;
};

struct PathRepeatSpec {
    std::uint32_t width = 0;
    std::uint32_t vertexCount = 0;
    std::vector<std::int64_t> deltas;
    std::vector<std::int64_t> originX;
    std::vector<std::int64_t> originY;
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

void optimizePaths(std::vector<std::tuple<std::uint32_t, std::vector<Point>, std::vector<Property>>> &paths,
                   LayerBucket &bucket)
{
    struct Entry {
        std::uint32_t width = 0;
        std::vector<Point> points;
        std::vector<Property> properties;
        std::vector<std::int64_t> deltas;
    };

    std::unordered_map<DeltaKey, std::vector<Entry>, DeltaKeyHash> groups;
    for (auto &path : paths) {
        Entry entry;
        entry.width = std::get<0>(path);
        entry.points = std::move(std::get<1>(path));
        entry.properties = std::move(std::get<2>(path));
        appendDeltaEncoded(entry.deltas, entry.points);
        DeltaKey key{entry.deltas};
        groups[key].push_back(std::move(entry));
    }
    paths.clear();

    for (auto &entry : groups) {
        auto &items = entry.second;
        if (items.size() >= 2) {
            bool sameWidth = true;
            bool sameProps = true;
            for (std::size_t i = 1; i < items.size(); ++i) {
                if (items[i].width != items[0].width) {
                    sameWidth = false;
                }
                if (!propertiesEqual(items[0].properties, items[i].properties)) {
                    sameProps = false;
                }
            }
            if (sameWidth && sameProps) {
                PathRepeatSpec repeat;
                repeat.width = items[0].width;
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
    std::map<std::uint32_t, std::vector<std::tuple<std::uint32_t, std::vector<Point>, std::vector<Property>>>>
        pendingPaths;

    for (const Shape &shape : block.shapes()) {
        switch (shape.type()) {
        case Shape::Type::Rect: {
            const auto &rect = *shape.rect();
            LayerBucket &bucket = buckets[rect.layerId];
            bucket.layerId = rect.layerId;
            pendingRects[rect.layerId].push_back(
                RawRect{rect.box.llx, rect.box.lly, rect.box.urx, rect.box.ury, shape.properties()});
            break;
        }
        case Shape::Type::Polygon: {
            const auto &polygon = *shape.polygon();
            LayerBucket &bucket = buckets[polygon.layerId];
            bucket.layerId = polygon.layerId;
            pendingPolygons[polygon.layerId].emplace_back(polygon.points, shape.properties());
            break;
        }
        case Shape::Type::Path: {
            const auto &path = *shape.path();
            LayerBucket &bucket = buckets[path.layerId];
            bucket.layerId = path.layerId;
            pendingPaths[path.layerId].emplace_back(path.width, path.points, shape.properties());
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
            pendingPaths[arc.layerId].emplace_back(arc.width, points, props);
            break;
        }
        }
    }

    for (auto &entry : buckets) {
        const std::uint32_t layerId = entry.first;
        LayerBucket &bucket = entry.second;
        optimizeRectangles(pendingRects[layerId], bucket);
        for (const RectArraySpec &arraySpec : bucket.rectArrays) {
            const std::size_t placementCount =
                static_cast<std::size_t>(arraySpec.columns) * static_cast<std::size_t>(arraySpec.rows);
            for (std::size_t i = 0; i < placementCount; ++i) {
                appendShapeProperties(bucket, arraySpec.properties);
            }
        }
        for (const RectGroupSpec &groupSpec : bucket.rectGroups) {
            for (const std::vector<Property> &props : groupSpec.propertiesPerPlacement) {
                appendShapeProperties(bucket, props);
            }
        }
        optimizePolygons(pendingPolygons[layerId], bucket);
        for (const PolygonRepeatSpec &repeat : bucket.polygonRepeats) {
            for (std::size_t i = 0; i < repeat.originX.size(); ++i) {
                appendShapeProperties(bucket, repeat.properties);
            }
        }
        optimizePaths(pendingPaths[layerId], bucket);
        for (const PathRepeatSpec &repeat : bucket.pathRepeats) {
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
    }

    auto groups = builder.initRectGroups(bucket.rectGroups.size());
    for (std::size_t i = 0; i < bucket.rectGroups.size(); ++i) {
        const RectGroupSpec &spec = bucket.rectGroups[i];
        auto gb = groups[i];
        gb.setWidth(spec.width);
        gb.setHeight(spec.height);
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

    auto pathRepeats = builder.initPathRepeats(bucket.pathRepeats.size());
    for (std::size_t i = 0; i < bucket.pathRepeats.size(); ++i) {
        const PathRepeatSpec &spec = bucket.pathRepeats[i];
        auto pb = pathRepeats[i];
        pb.setWidth(spec.width);
        pb.setVertexCount(spec.vertexCount);
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

void readLayerBucket(schema::CompactLayerShapes::Reader reader, Block &block)
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

    const auto rectCoords = reader.getRectCoords();
    for (std::size_t i = 0; i + 3 < rectCoords.size(); i += 4) {
        Shape::RectData data;
        data.layerId = layerId;
        data.box = Box{rectCoords[i], rectCoords[i + 1], rectCoords[i + 2], rectCoords[i + 3]};
        Shape shape(data);
        assignShapeProperties(shape);
        block.shapes().push_back(std::move(shape));
    }

    for (const auto array : reader.getRectArrays()) {
        for (std::uint32_t row = 0; row < array.getRows(); ++row) {
            for (std::uint32_t col = 0; col < array.getColumns(); ++col) {
                const std::int64_t llx = array.getOriginLlx() + static_cast<std::int64_t>(col) * array.getStepX();
                const std::int64_t lly = array.getOriginLly() + static_cast<std::int64_t>(row) * array.getStepY();
                Shape::RectData data;
                data.layerId = layerId;
                data.box = Box{llx, lly, llx + array.getWidth(), lly + array.getHeight()};
                Shape shape(data);
                assignShapeProperties(shape);
                block.shapes().push_back(std::move(shape));
            }
        }
    }

    for (const auto group : reader.getRectGroups()) {
        const auto llxList = group.getLlx();
        const auto llyList = group.getLly();
        const std::size_t count = llxList.size();
        for (std::size_t i = 0; i < count; ++i) {
            Shape::RectData data;
            data.layerId = layerId;
            data.box = Box{llxList[i], llyList[i], llxList[i] + group.getWidth(), llyList[i] + group.getHeight()};
            Shape shape(data);
            assignShapeProperties(shape);
            block.shapes().push_back(std::move(shape));
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
        block.shapes().push_back(std::move(shape));
        polygonCursor += needed;
    }

    for (const auto repeat : reader.getPolygonRepeats()) {
        std::vector<std::int64_t> deltas;
        for (const auto value : repeat.getDeltas()) {
            deltas.push_back(value);
        }
        const auto originsX = repeat.getOriginX();
        const auto originsY = repeat.getOriginY();
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
            assignShapeProperties(shape);
            block.shapes().push_back(std::move(shape));
        }
    }

    const auto pathWidths = reader.getPathWidths();
    const auto pathCounts = reader.getPathVertexCounts();
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
        data.points = decodeDeltaEncoded(slice, count);
        Shape shape(data);
        assignShapeProperties(shape);
        block.shapes().push_back(std::move(shape));
        pathCursor += needed;
    }

    for (const auto repeat : reader.getPathRepeats()) {
        std::vector<std::int64_t> deltas;
        for (const auto value : repeat.getDeltas()) {
            deltas.push_back(value);
        }
        const auto originsX = repeat.getOriginX();
        const auto originsY = repeat.getOriginY();
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
            data.points = decodeDeltaEncoded(slice, repeat.getVertexCount());
            Shape shape(data);
            assignShapeProperties(shape);
            block.shapes().push_back(std::move(shape));
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
        block.shapes().push_back(std::move(shape));
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
        readLayerBucket(layer, block);
    }

    for (const auto inst : reader.getInstances()) {
        Transform transform;
        const auto tr = inst.getTransform();
        transform.x = tr.getX();
        transform.y = tr.getY();
        transform.orient = fromSchemaOrient(tr.getOrient());
        transform.mag = tr.getMag();
        Instance instance(inst.getCellName().cStr(), transform);
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
