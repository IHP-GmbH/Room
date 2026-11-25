#include "room_file_io.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/io.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
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

constexpr std::size_t kDeflateChunk = 1u << 20;

int clampLevel(int level)
{
    if (level < 0) {
        return 0;
    }
    if (level > 9) {
        return 9;
    }
    return level;
}

class DeflateOutputStream : public kj::OutputStream {
public:
    explicit DeflateOutputStream(int level)
    {
        std::memset(&m_stream, 0, sizeof(m_stream));
        if (deflateInit(&m_stream, clampLevel(level)) != Z_OK) {
            throw std::runtime_error("deflateInit failed for ROOM payload");
        }
        m_out.resize(kRoomZHeaderSize);
        m_used = kRoomZHeaderSize;
    }

    ~DeflateOutputStream() override
    {
        if (!m_finished) {
            deflateEnd(&m_stream);
        }
    }

    void write(const void *buffer, size_t size) override
    {
        const auto *bytes = static_cast<const Bytef *>(buffer);
        while (size > 0) {
            const uInt chunk = static_cast<uInt>(std::min(size, kDeflateChunk));
            m_stream.next_in = const_cast<Bytef *>(bytes);
            m_stream.avail_in = chunk;
            m_uncompressed += chunk;
            while (m_stream.avail_in > 0) {
                pump(Z_NO_FLUSH);
            }
            bytes += chunk;
            size -= chunk;
        }
    }

    void finish()
    {
        if (m_finished) {
            return;
        }
        m_stream.next_in = nullptr;
        m_stream.avail_in = 0;
        int rc = Z_OK;
        while (rc != Z_STREAM_END) {
            rc = pump(Z_FINISH);
        }
        deflateEnd(&m_stream);
        m_finished = true;
        m_out.resize(m_used);
        std::memcpy(m_out.data(), kRoomZMagic, sizeof(kRoomZMagic));
        writeLe64(m_out.data() + sizeof(kRoomZMagic), m_uncompressed);
    }

    std::vector<std::uint8_t> take()
    {
        return std::move(m_out);
    }

private:
    int pump(int flush)
    {
        if (m_out.size() - m_used < kDeflateChunk) {
            m_out.resize(m_used + kDeflateChunk);
        }
        m_stream.next_out = m_out.data() + m_used;
        m_stream.avail_out = static_cast<uInt>(m_out.size() - m_used);
        const int rc = deflate(&m_stream, flush);
        if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
            throw std::runtime_error("deflate failed for ROOM payload");
        }
        m_used = m_out.size() - m_stream.avail_out;
        return rc;
    }

    z_stream m_stream{};
    std::vector<std::uint8_t> m_out;
    std::size_t m_used = 0;
    std::uint64_t m_uncompressed = 0;
    bool m_finished = false;
};

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
    DeflateOutputStream stream(level);
    stream.write(data, size);
    stream.finish();
    return stream.take();
}

std::vector<std::uint8_t> compressRoomMessage(capnp::MessageBuilder &message, int level)
{
    DeflateOutputStream stream(level);
    capnp::writeMessage(stream, message);
    stream.finish();
    return stream.take();
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

namespace {
thread_local RoomFileReadTiming g_lastReadTiming;
}

RoomFileReadTiming lastRoomFileReadTiming()
{
    return g_lastReadTiming;
}

std::vector<std::uint8_t> readRoomFileBytes(const std::string &path)
{
    const auto diskStarted = std::chrono::steady_clock::now();
    std::vector<std::uint8_t> raw = readFileBytes(path);
    const double disk_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - diskStarted).count();
    if (!isRoomZPayload(raw.data(), raw.size())) {
        g_lastReadTiming.disk_s = disk_s;
        g_lastReadTiming.inflate_s = 0;
        // Uncompressed Cap'n Proto: keep the single buffer (no second copy).
        return raw;
    }
    const auto inflateStarted = std::chrono::steady_clock::now();
    std::vector<std::uint8_t> inflated = inflateRoomZ(raw.data(), raw.size());
    const double inflate_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - inflateStarted).count();
    raw.clear();
    raw.shrink_to_fit();
    g_lastReadTiming.disk_s = disk_s;
    g_lastReadTiming.inflate_s = inflate_s;
    return inflated;
}

} // namespace room
