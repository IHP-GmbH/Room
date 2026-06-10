/*!****************************************************************************************
 * \file compact_codec.cpp
 * \brief Encoder and decoder for layer-grouped CompactBlock geometry.
 *
 * Groups shapes by layer, delta-encodes polygon/path vertices, and stores per-shape
 * properties in parallel count/value lists.
 *****************************************************************************************/

#include "compact_codec.h"

#include "enums.h"

#include <design.capnp.h>
#include <dm.capnp.h>

#include <cstdint>
#include <map>
#include <vector>

namespace core {
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

std::map<std::uint32_t, LayerBucket> bucketShapes(const Block &block)
{
    std::map<std::uint32_t, LayerBucket> buckets;
    for (const Shape &shape : block.shapes()) {
        switch (shape.type()) {
        case Shape::Type::Rect: {
            const auto &rect = *shape.rect();
            LayerBucket &bucket = buckets[rect.layerId];
            bucket.layerId = rect.layerId;
            bucket.rectCoords.push_back(rect.box.llx);
            bucket.rectCoords.push_back(rect.box.lly);
            bucket.rectCoords.push_back(rect.box.urx);
            bucket.rectCoords.push_back(rect.box.ury);
            appendShapeProperties(bucket, shape.properties());
            break;
        }
        case Shape::Type::Polygon: {
            const auto &polygon = *shape.polygon();
            LayerBucket &bucket = buckets[polygon.layerId];
            bucket.layerId = polygon.layerId;
            bucket.polygonVertexCounts.push_back(static_cast<std::uint32_t>(polygon.points.size()));
            appendDeltaEncoded(bucket.polygonDeltas, polygon.points);
            appendShapeProperties(bucket, shape.properties());
            break;
        }
        case Shape::Type::Path: {
            const auto &path = *shape.path();
            LayerBucket &bucket = buckets[path.layerId];
            bucket.layerId = path.layerId;
            bucket.pathWidths.push_back(path.width);
            bucket.pathVertexCounts.push_back(static_cast<std::uint32_t>(path.points.size()));
            appendDeltaEncoded(bucket.pathDeltas, path.points);
            appendShapeProperties(bucket, shape.properties());
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
        }
    }
    return buckets;
}

void writeLayerBucket(schema::CompactLayerShapes::Builder builder, const LayerBucket &bucket)
{
    builder.setLayerId(bucket.layerId);

    auto rects = builder.initRectCoords(bucket.rectCoords.size());
    for (std::size_t i = 0; i < bucket.rectCoords.size(); ++i) {
        rects.set(i, bucket.rectCoords[i]);
    }

    auto polygonCounts = builder.initPolygonVertexCounts(bucket.polygonVertexCounts.size());
    for (std::size_t i = 0; i < bucket.polygonVertexCounts.size(); ++i) {
        polygonCounts.set(i, bucket.polygonVertexCounts[i]);
    }
    auto polygonDeltas = builder.initPolygonDeltas(bucket.polygonDeltas.size());
    for (std::size_t i = 0; i < bucket.polygonDeltas.size(); ++i) {
        polygonDeltas.set(i, bucket.polygonDeltas[i]);
    }

    auto pathWidths = builder.initPathWidths(bucket.pathWidths.size());
    for (std::size_t i = 0; i < bucket.pathWidths.size(); ++i) {
        pathWidths.set(i, bucket.pathWidths[i]);
    }
    auto pathCounts = builder.initPathVertexCounts(bucket.pathVertexCounts.size());
    for (std::size_t i = 0; i < bucket.pathVertexCounts.size(); ++i) {
        pathCounts.set(i, bucket.pathVertexCounts[i]);
    }
    auto pathDeltas = builder.initPathDeltas(bucket.pathDeltas.size());
    for (std::size_t i = 0; i < bucket.pathDeltas.size(); ++i) {
        pathDeltas.set(i, bucket.pathDeltas[i]);
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

    const auto polygonCounts = reader.getPolygonVertexCounts();
    const auto polygonDeltas = reader.getPolygonDeltas();
    std::vector<std::int64_t> polygonStream;
    polygonStream.reserve(polygonDeltas.size());
    for (const auto value : polygonDeltas) {
        polygonStream.push_back(value);
    }
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

    const auto pathWidths = reader.getPathWidths();
    const auto pathCounts = reader.getPathVertexCounts();
    const auto pathDeltas = reader.getPathDeltas();
    std::vector<std::int64_t> pathStream;
    pathStream.reserve(pathDeltas.size());
    for (const auto value : pathDeltas) {
        pathStream.push_back(value);
    }
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

/*!****************************************************************************************
 * \brief Writes in-memory block topology into a CompactBlock builder.
 * \param builder  Destination Cap'n Proto struct.
 * \param block    Source shapes, instances, nets, and bbox.
 *****************************************************************************************/
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

/*!****************************************************************************************
 * \brief Reconstructs a Block from a CompactBlock reader.
 * \param reader   Compact geometry from disk.
 * \return         Block with shapes, instances, nets, and recomputed bbox.
 *****************************************************************************************/
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

/*!****************************************************************************************
 * \brief Tests whether a compact block contains topology worth decoding.
 * \param reader   CompactBlock reader to inspect.
 * \return         True if layer shapes, instances, or nets are non-empty.
 *****************************************************************************************/
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

} // namespace core
