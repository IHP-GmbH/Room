#pragma once

#include "block.h"

#include <compact.capnp.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace room {

/*!****************************************************************************************
 * \brief One regular rectangle grid from a compact layer.
 *
 * columns runs along stepX and rows along stepY, starting at the origin.
 *****************************************************************************************/
struct CompactRectArrayRef {
    std::uint32_t layerId = 0;
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
};

/*!****************************************************************************************
 * \brief Identical rectangles at arbitrary lower-left corners.
 *****************************************************************************************/
struct CompactRectGroupRef {
    std::uint32_t layerId = 0;
    std::int64_t width = 0;
    std::int64_t height = 0;
    std::vector<std::int64_t> llx;
    std::vector<std::int64_t> lly;
};

/*!****************************************************************************************
 * \brief One polygon translated onto each origin. points are absolute at origins[0].
 *****************************************************************************************/
struct CompactPolygonRepeatRef {
    std::uint32_t layerId = 0;
    std::vector<Point> points;
    std::vector<std::int64_t> originX;
    std::vector<std::int64_t> originY;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::int64_t stepX = 0;
    std::int64_t stepY = 0;
    std::int64_t colY = 0;
    std::int64_t rowX = 0;
};

/*!****************************************************************************************
 * \brief One path translated onto each origin. points are absolute at origins[0].
 *****************************************************************************************/
struct CompactPathRepeatRef {
    std::uint32_t layerId = 0;
    std::uint32_t width = 0;
    std::int32_t bgnExt = 0;
    std::int32_t endExt = 0;
    bool round = false;
    std::vector<Point> points;
    std::vector<std::int64_t> originX;
    std::vector<std::int64_t> originY;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::int64_t stepX = 0;
    std::int64_t stepY = 0;
    std::int64_t colY = 0;
    std::int64_t rowX = 0;
};

/*!****************************************************************************************
 * \brief Optional receivers for compact repetitions.
 *
 * A set callback is used only when every member of that repetition has no properties.
 * Otherwise the members are emitted as individual shapes.
 *****************************************************************************************/
struct CompactRepeatHooks {
    std::function<void(const CompactRectArrayRef &)> rectArray;
    std::function<void(const CompactRectGroupRef &)> rectGroup;
    std::function<void(const CompactPolygonRepeatRef &)> polygonRepeat;
    std::function<void(const CompactPathRepeatRef &)> pathRepeat;
};

/*!****************************************************************************************
 * \brief Encodes a Block into compact layer-grouped Cap'n Proto form.
 * \param builder  CompactBlock builder to fill.
 * \param block    Source topology from memory.
 *****************************************************************************************/
void writeCompactBlock(schema::CompactBlock::Builder builder, const Block &block);

/*!****************************************************************************************
 * \brief Decodes a CompactBlock into an in-memory Block.
 * \param reader   CompactBlock reader from disk.
 * \return         Reconstructed block with shapes, instances, and nets.
 *****************************************************************************************/
Block readCompactBlock(schema::CompactBlock::Reader reader);

/*!****************************************************************************************
 * \brief Returns true if the compact block carries geometry or hierarchy.
 * \param reader   CompactBlock reader to inspect.
 * \return         True when layer shapes, instances, or nets are present.
 *****************************************************************************************/
bool compactBlockHasGeometry(schema::CompactBlock::Reader reader);

/*!****************************************************************************************
 * \brief Yields compact shapes one at a time, without storing the block.
 * \param reader  CompactBlock reader from disk.
 * \param emit    Called once per decoded shape. The shape is not retained.
 *****************************************************************************************/
void visitCompactShapes(schema::CompactBlock::Reader reader, const std::function<void(Shape)> &emit,
                        const CompactRepeatHooks *hooks = nullptr);

} // namespace room
