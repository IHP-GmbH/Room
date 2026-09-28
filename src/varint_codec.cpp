#include "varint_codec.h"

#include <cstdint>

namespace room {
namespace {

std::uint64_t zigzagEncode(std::int64_t value)
{
    return static_cast<std::uint64_t>((value << 1) ^ (value >> 63));
}

std::int64_t zigzagDecode(std::uint64_t value)
{
    return static_cast<std::int64_t>((value >> 1) ^ (~(value & 1) + 1));
}

void appendVarint(std::vector<std::uint8_t> &out, std::uint64_t value)
{
    while (value >= 0x80) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}

bool readVarint(const std::uint8_t *data, std::size_t size, std::size_t &cursor, std::uint64_t &value)
{
    value = 0;
    unsigned shift = 0;
    while (cursor < size) {
        const std::uint8_t byte = data[cursor++];
        value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return true;
        }
        shift += 7;
        if (shift > 63) {
            return false;
        }
    }
    return false;
}

} // namespace

std::vector<std::uint8_t> encodeVarint64(const std::vector<std::int64_t> &values)
{
    std::vector<std::uint8_t> out;
    out.reserve(values.size() * 2);
    for (const std::int64_t value : values) {
        appendVarint(out, zigzagEncode(value));
    }
    return out;
}

std::vector<std::int64_t> decodeVarint64(const std::uint8_t *data, std::size_t size)
{
    std::vector<std::int64_t> out;
    std::size_t cursor = 0;
    while (cursor < size) {
        std::uint64_t encoded = 0;
        if (!readVarint(data, size, cursor, encoded)) {
            break;
        }
        out.push_back(zigzagDecode(encoded));
    }
    return out;
}

} // namespace room
