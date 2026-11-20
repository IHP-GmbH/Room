#include "room_file_io.h"

#include <zlib.h>

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace room {
namespace {

std::uint64_t readLe64(const std::uint8_t *p)
{
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    }
    return v;
}

void writeLe64(std::uint8_t *p, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>(v & 0xffu);
        v >>= 8;
    }
}

constexpr std::size_t kRoomZHeaderSize = sizeof(kRoomZMagic) + 8;

std::vector<std::uint8_t> readFileBytes(const std::string &path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("Cannot open file for reading: " + path);
    }
    const std::streamsize fileSize = in.tellg();
    if (fileSize <= 0) {
        throw std::runtime_error("Cannot read file size: " + path);
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(fileSize));
    if (!in.read(reinterpret_cast<char *>(raw.data()), fileSize)) {
        throw std::runtime_error("Failed to read file: " + path);
    }
    return raw;
}

std::vector<std::uint8_t> inflateRoomZ(const std::uint8_t *bytes, std::size_t size)
{
    const std::uint64_t uncompressed = readLe64(bytes + sizeof(kRoomZMagic));
    if (uncompressed == 0 || uncompressed > (16ull << 30)) {
        throw std::runtime_error("Invalid ROOMZ uncompressed size");
    }
    const std::uint8_t *payload = bytes + kRoomZHeaderSize;
    const std::size_t payloadSize = size - kRoomZHeaderSize;

    std::vector<std::uint8_t> out(static_cast<std::size_t>(uncompressed));
    uLongf outLen = static_cast<uLongf>(out.size());
    const int rc = uncompress(out.data(), &outLen, payload, static_cast<uLong>(payloadSize));
    if (rc != Z_OK || outLen != uncompressed) {
        throw std::runtime_error("zlib uncompress failed for ROOMZ payload");
    }
    return out;
}

} // namespace

bool isRoomZPayload(const void *data, std::size_t size)
{
    if (data == nullptr || size < kRoomZHeaderSize) {
        return false;
    }
    return std::memcmp(data, kRoomZMagic, sizeof(kRoomZMagic)) == 0;
}

std::vector<std::uint8_t> compressRoomPayload(const void *data, std::size_t size, int level)
{
    if (data == nullptr || size == 0) {
        throw std::runtime_error("Cannot compress empty ROOM payload");
    }
    if (level < 0) {
        level = 0;
    } else if (level > 9) {
        level = 9;
    }

    // Single output buffer: ROOMZ header + zlib bytes (no intermediate vector + memcpy).
    const uLong bound = compressBound(static_cast<uLong>(size));
    std::vector<std::uint8_t> out(kRoomZHeaderSize + static_cast<std::size_t>(bound));
    uLongf outLen = bound;
    const int rc = compress2(out.data() + kRoomZHeaderSize, &outLen,
                             static_cast<const Bytef *>(data), static_cast<uLong>(size), level);
    if (rc != Z_OK) {
        throw std::runtime_error("zlib compress2 failed for ROOM payload");
    }

    std::memcpy(out.data(), kRoomZMagic, sizeof(kRoomZMagic));
    writeLe64(out.data() + sizeof(kRoomZMagic), static_cast<std::uint64_t>(size));
    out.resize(kRoomZHeaderSize + static_cast<std::size_t>(outLen));
    return out;
}

std::vector<std::uint8_t> decodeRoomFileBytes(const void *data, std::size_t size)
{
    if (data == nullptr || size == 0) {
        throw std::runtime_error("Empty ROOM file");
    }
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    if (!isRoomZPayload(bytes, size)) {
        return std::vector<std::uint8_t>(bytes, bytes + size);
    }
    return inflateRoomZ(bytes, size);
}

std::vector<std::uint8_t> readRoomFileBytes(const std::string &path)
{
    std::vector<std::uint8_t> raw = readFileBytes(path);
    if (!isRoomZPayload(raw.data(), raw.size())) {
        // Uncompressed Cap'n Proto: keep the single buffer (no second copy).
        return raw;
    }
    std::vector<std::uint8_t> inflated = inflateRoomZ(raw.data(), raw.size());
    raw.clear();
    raw.shrink_to_fit();
    return inflated;
}

} // namespace room
