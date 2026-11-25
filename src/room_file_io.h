#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace capnp {
class MessageBuilder;
}

namespace room {

/*! 8-byte magic: "ROOMZ\\0\\x01\\0" — zlib-wrapped Cap'n Proto payload follows. */
inline constexpr char kRoomZMagic[8] = {'R', 'O', 'O', 'M', 'Z', '\0', 1, '\0'};

bool isRoomZPayload(const void *data, std::size_t size);

/*! Compress Cap'n Proto bytes to ROOMZ frame (magic + uint64 LE uncompressed size + zlib).
 *  \param level  zlib 0..9 (clamped). */
std::vector<std::uint8_t> compressRoomPayload(const void *data, std::size_t size, int level = 1);

/*! Deflate a Cap'n Proto message straight into a ROOMZ frame.
 *  Holds the compressed bytes, not a second full uncompressed copy. */
std::vector<std::uint8_t> compressRoomMessage(capnp::MessageBuilder &message, int level = 1);

/*! If ROOMZ, inflate to Cap'n Proto bytes; otherwise copy raw bytes. */
std::vector<std::uint8_t> decodeRoomFileBytes(const void *data, std::size_t size);

struct RoomFileReadTiming {
    double disk_s = 0;
    double inflate_s = 0;
};

/*! Read a .room file from disk into Cap'n Proto message bytes (handles ROOMZ). */
std::vector<std::uint8_t> readRoomFileBytes(const std::string &path);

/*! Timings from the most recent readRoomFileBytes call on this thread. */
RoomFileReadTiming lastRoomFileReadTiming();

} // namespace room
