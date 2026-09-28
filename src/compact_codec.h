#pragma once

#include "block.h"

#include <compact.capnp.h>

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

} // namespace room
