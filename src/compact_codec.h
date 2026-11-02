#pragma once

#include "block.h"

#include <compact.capnp.h>

#include <functional>

namespace room {

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
void visitCompactShapes(schema::CompactBlock::Reader reader, const std::function<void(Shape)> &emit);

} // namespace room
