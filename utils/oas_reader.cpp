#include "oas_reader.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <zlib.h>

namespace core {
namespace {

#ifndef Q_UNUSED
#define Q_UNUSED(x) (void)(x)
#endif

using StringList = std::vector<std::string>;
using ByteBuffer = std::vector<std::uint8_t>;
using CellNameTable = std::unordered_map<std::uint64_t, std::string>;
using CellNameSet = std::unordered_set<std::string>;

constexpr char kUtf8Replacement[] = "\xEF\xBF\xBD";

static std::string bytesToLatin1(const ByteBuffer &b)
{
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

static std::string bytesToUtf8(const ByteBuffer &b)
{
    return bytesToLatin1(b);
}

static std::string decodeOasName(const ByteBuffer &raw)
{
    const std::string utf8 = bytesToUtf8(raw);
    if (utf8.find(kUtf8Replacement) != std::string::npos) {
        return bytesToLatin1(raw);
    }
    return utf8;
}

static const std::string &mapValue(const CellNameTable &table, std::uint64_t key)
{
    static const std::string kEmpty;
    const auto it = table.find(key);
    return it == table.end() ? kEmpty : it->second;
}

// oasReader.cpp
//#include <cstdint>
#include <cstring>
//#include <limits>
#include <cstdlib>

#include <zlib.h>

#ifndef OAS_TRACE
#define OAS_TRACE 0
#endif

#ifndef OAS_GUARD
#define OAS_GUARD 1
#endif

#ifndef OAS_GUARD_MAX_RECORDS
#define OAS_GUARD_MAX_RECORDS 50000000ULL
#endif

#ifndef OAS_GUARD_STALL_LIMIT
#define OAS_GUARD_STALL_LIMIT 200000ULL
#endif

#ifndef OAS_GUARD_TINY_PROGRESS_BYTES
#define OAS_GUARD_TINY_PROGRESS_BYTES 1
#endif

#ifndef OAS_GUARD_LOG_EVERY_N_RECORDS
#define OAS_GUARD_LOG_EVERY_N_RECORDS 200000ULL
#endif

#ifndef OAS_GUARD_HEX_AROUND
#define OAS_GUARD_HEX_AROUND 64
#endif

/*!********************************************************************************************************************
 * \brief Lightweight cursor over a memory-mapped OASIS byte buffer.
 *
 * Holds the current pointer \c p and the end pointer \c end and provides a bounds check helper.
 *********************************************************************************************************************/
struct OasCursor
{
    const std::uint8_t *p   = nullptr;
    const std::uint8_t *end = nullptr;

    /*!****************************************************************************************************************
     * \brief Checks whether at least \p n bytes are available from the current cursor position.
     * \param n Number of bytes required.
     * \return True if \p n bytes can be read safely, false otherwise.
     *****************************************************************************************************************/
    bool has(int n) const { return p && (p + n <= end); }
};

#ifdef OAS_DEBUG
/*!********************************************************************************************************************
 * \brief Produces a hex dump of up to \p maxBytes from a buffer range.
 *
 * The function converts the first \p maxBytes bytes starting at \p p into a space-separated hex string.
 *
 * \param p Pointer to the start of the buffer range.
 * \param end Pointer one past the end of the buffer range.
 * \param maxBytes Maximum number of bytes to dump.
 * \return Hex-encoded string with spaces between bytes.
 *********************************************************************************************************************/
/*!********************************************************************************************************************
 * \brief Produces a hex dump around a pointer (bytes before/after).
 *
 * Useful to verify offsets when string decoding looks wrong.
 *
 * \param base File base pointer.
 * \param end File end pointer.
 * \param p Current pointer.
 * \param radius Bytes before/after.
 * \return Hex-encoded string with marker position info.
 *********************************************************************************************************************/
#endif

/*!********************************************************************************************************************
 * \brief Reads a single byte and advances the cursor.
 *
 * \param c Cursor to read from.
 * \param out Output byte.
 * \return True on success, false if out of bounds.
 *********************************************************************************************************************/
static inline bool readByte(OasCursor &c, std::uint8_t &out)
{
    if (!c.has(1)) return false;
    out = *c.p++;
    return true;
}

/*!********************************************************************************************************************
 * \brief Reads an OASIS unsigned-integer (varint) and advances the cursor.
 *
 * OASIS uses a 7-bit payload per byte with continuation bit 0x80.
 *
 * \param c Cursor to read from.
 * \param out Decoded value.
 * \return True on success, false on out-of-bounds or overflow.
 *********************************************************************************************************************/
static inline bool readUInt(OasCursor &c, std::uint64_t &out)
{
    out = 0;
    int shift = 0;

    for (;;) {
        if (!c.has(1)) return false;
        std::uint8_t b = *c.p++;
        out |= (std::uint64_t(b & 0x7F) << shift);
        if ((b & 0x80) == 0) break;
        shift += 7;
        if (shift > 63) return false;
    }
    return true;
}

/*!********************************************************************************************************************
 * \brief Heuristic validation for a decoded cell name.
 *
 * Rejects empty strings, excessively large strings, control characters, and U+FFFD replacement characters.
 *
 * \param s Candidate cell name.
 * \return True if the string looks reasonable, false otherwise.
 *********************************************************************************************************************/
static bool isLikelyValidCellName(const std::string& s)
{
    if (s.empty()) return false;
    if (s.size() > 4096) return false;

    for (unsigned char ch : s) {
        unsigned char u = ch;

        if (u < 0x20 || u == 0x7F) return false;
        if (u == 0xFFFD) return false;
    }

    return true;
}

/*!********************************************************************************************************************
 * \brief Reads an OASIS signed-integer and advances the cursor.
 *
 * OASIS signed-integer is encoded via an unsigned varint with zigzag-like mapping:
 *   out = (u & 1) ? -((u + 1) >> 1) : (u >> 1)
 *
 * \param c Cursor to read from.
 * \param out Decoded signed value.
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static inline bool readSInt(OasCursor &c, std::int64_t &out)
{
    std::uint64_t u = 0;
    if (!readUInt(c, u)) return false;
    out = (u & 1ULL) ? -std::int64_t((u + 1ULL) >> 1) : std::int64_t(u >> 1);
    return true;
}

/*!********************************************************************************************************************
 * \brief Reads a length-prefixed byte string and advances the cursor.
 *
 * \param c Cursor to read from.
 * \param out Output byte array.
 * \return True on success, false if out-of-bounds.
 *********************************************************************************************************************/
static inline bool readString(OasCursor &c, ByteBuffer &out)
{
    std::uint64_t n = 0;
    if (!readUInt(c, n)) return false;
    if (n > std::uint64_t(c.end - c.p)) return false;
    out.assign(c.p, c.p + static_cast<std::size_t>(n));
    c.p += static_cast<std::ptrdiff_t>(n);
    return true;
}

/*!********************************************************************************************************************
 * \brief Reads an A-string (Latin-1) and advances the cursor.
 *
 * \param c Cursor to read from.
 * \param out Output string.
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static inline bool readAString(OasCursor &c, std::string &out)
{
    ByteBuffer b;
    if (!readString(c, b)) return false;
    out = bytesToLatin1(b);
    return true;
}

/*!********************************************************************************************************************
 * \brief Reads an N-string (name string) and advances the cursor.
 *
 * Primary decoding is UTF-8; if replacement characters are present, a Latin-1 fallback is used.
 *
 * \param c Cursor to read from.
 * \param out Output string.
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static inline bool readNString(OasCursor &c, std::string &out)
{
    ByteBuffer b;
    if (!readString(c, b)) return false;

    out = decodeOasName(b);
    return true;
}

/*!********************************************************************************************************************
 * \brief Skips an OASIS real value and advances the cursor.
 *
 * The encoding starts with a type code (0..7) followed by payload according to the OASIS spec.
 *
 * \param c Cursor to advance.
 * \return True on success, false on parse failure or out-of-bounds.
 *********************************************************************************************************************/
static inline bool skipReal(OasCursor &c)
{
    std::uint64_t t = 0;
    if (!readUInt(c, t)) return false;
    switch (t) {
    case 0: { std::uint64_t x; return readUInt(c, x); }
    case 1: { std::uint64_t x; return readUInt(c, x); }
    case 2: { std::uint64_t d; return readUInt(c, d); }
    case 3: { std::uint64_t d; return readUInt(c, d); }
    case 4: { std::uint64_t n,d; return readUInt(c,n) && readUInt(c,d); }
    case 5: { std::uint64_t n,d; return readUInt(c,n) && readUInt(c,d); }
    case 6: { if (!c.has(4)) return false; c.p += 4; return true; }
    case 7: { if (!c.has(8)) return false; c.p += 8; return true; }
    default:
        return false;
    }
}

/*!********************************************************************************************************************
 * \brief Skips a 1-delta encoded value and advances the cursor.
 * \param c Cursor to advance.
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static inline bool skip1Delta(OasCursor &c)
{
    std::int64_t v;
    return readSInt(c, v);
}

/*!********************************************************************************************************************
 * \brief Skips a 2-delta encoded value and advances the cursor.
 * \param c Cursor to advance.
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static inline bool skip2Delta(OasCursor &c)
{
    std::uint64_t v;
    return readUInt(c, v);
}

/*!********************************************************************************************************************
 * \brief Skips a 3-delta encoded value and advances the cursor.
 * \param c Cursor to advance.
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static inline bool skip3Delta(OasCursor &c)
{
    std::int64_t v;
    return readSInt(c, v);
}

/*!********************************************************************************************************************
 * \brief Skips a g-delta encoded value and advances the cursor.
 *
 * g-delta is stored as an unsigned-integer. If the LSB is 1, a second unsigned-integer follows.
 *
 * \param c Cursor to advance.
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static inline bool skipGDelta(OasCursor &c)
{
    std::uint64_t a = 0;
    if (!readUInt(c, a)) return false;
    if (a & 1ULL) {
        std::uint64_t b = 0;
        return readUInt(c, b);
    }
    return true;
}

/*!********************************************************************************************************************
 * \brief Skips an OASIS point-list and advances the cursor.
 *
 * \param c Cursor to advance.
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static inline bool skipPointList(OasCursor &c)
{
    std::uint64_t ptType = 0;
    if (!readUInt(c, ptType)) return false;

    std::uint64_t count = 0;
    if (!readUInt(c, count)) return false;

    switch (ptType) {
    case 0:
    case 1:
        for (std::uint64_t i = 0; i < count; i++) if (!skip1Delta(c)) return false;
        return true;

    case 2:
        for (std::uint64_t i = 0; i < count; i++) if (!skip2Delta(c)) return false;
        return true;

    case 3:
        for (std::uint64_t i = 0; i < count; i++) {
            if (!skip3Delta(c)) return false;
            if (!skip3Delta(c)) return false;
        }
        return true;

    case 4:
        for (std::uint64_t i = 0; i < count; i++) if (!skipGDelta(c)) return false;
        return true;

    default:
        return false;
    }
}

/*!********************************************************************************************************************
 * \brief Skips an OASIS repetition structure and advances the cursor.
 *
 * \param c Cursor to advance.
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static inline bool skipRepetition(OasCursor &c)
{
    std::uint64_t repType = 0;
    if (!readUInt(c, repType)) return false;

    switch (repType) {
    case 0:
        return true;

    case 1: { // x-dimension y-dimension x-space y-space
        std::uint64_t nx = 0, ny = 0, xs = 0, ys = 0;
        return readUInt(c, nx) && readUInt(c, ny) && readUInt(c, xs) && readUInt(c, ys);
    }

    case 2: { // x-dimension x-space
        std::uint64_t nx = 0, xs = 0;
        return readUInt(c, nx) && readUInt(c, xs);
    }

    case 3: { // y-dimension y-space
        std::uint64_t ny = 0, ys = 0;
        return readUInt(c, ny) && readUInt(c, ys);
    }

    case 4: { // x-dimension x-space1 ... x-space(N-1), where N = xdim + 2 => count = xdim + 1
        std::uint64_t xdim = 0;
        if (!readUInt(c, xdim)) return false;
        for (std::uint64_t i = 0; i < xdim + 1; ++i) {
            std::uint64_t xs = 0;
            if (!readUInt(c, xs)) return false;
        }
        return true;
    }

    case 5: { // x-dimension grid x-space1 ... x-space(N-1)
        std::uint64_t xdim = 0, grid = 0;
        if (!readUInt(c, xdim)) return false;
        if (!readUInt(c, grid)) return false;
        for (std::uint64_t i = 0; i < xdim + 1; ++i) {
            std::uint64_t xs = 0;
            if (!readUInt(c, xs)) return false;
        }
        return true;
    }

    case 6: { // y-dimension y-space1 ... y-space(M-1), count = ydim + 1
        std::uint64_t ydim = 0;
        if (!readUInt(c, ydim)) return false;
        for (std::uint64_t i = 0; i < ydim + 1; ++i) {
            std::uint64_t ys = 0;
            if (!readUInt(c, ys)) return false;
        }
        return true;
    }

    case 7: { // y-dimension grid y-space1 ... y-space(M-1)
        std::uint64_t ydim = 0, grid = 0;
        if (!readUInt(c, ydim)) return false;
        if (!readUInt(c, grid)) return false;
        for (std::uint64_t i = 0; i < ydim + 1; ++i) {
            std::uint64_t ys = 0;
            if (!readUInt(c, ys)) return false;
        }
        return true;
    }

    case 8: { // n-dimension m-dimension n-displacement m-displacement (g-delta, g-delta)
        std::uint64_t nd = 0, md = 0;
        return readUInt(c, nd) && readUInt(c, md) && skipGDelta(c) && skipGDelta(c);
    }

    case 9: { // dimension displacement (g-delta)
        std::uint64_t dim = 0;
        return readUInt(c, dim) && skipGDelta(c);
    }

    case 10: { // dimension displacement1 ... displacement(P-1), count = dim + 1
        std::uint64_t dim = 0;
        if (!readUInt(c, dim)) return false;
        for (std::uint64_t i = 0; i < dim + 1; ++i) {
            if (!skipGDelta(c)) return false;
        }
        return true;
    }

    case 11: { // dimension grid displacement1 ... displacement(P-1), count = dim + 1
        std::uint64_t dim = 0, grid = 0;
        if (!readUInt(c, dim)) return false;
        if (!readUInt(c, grid)) return false;
        for (std::uint64_t i = 0; i < dim + 1; ++i) {
            if (!skipGDelta(c)) return false;
        }
        return true;
    }

    default:
        return false;
    }
}

/*!********************************************************************************************************************
 * \brief Skips an OASIS interval and advances the cursor.
 *
 * \param c Cursor to advance.
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static inline bool skipInterval(OasCursor &c)
{
    std::uint64_t type = 0;
    if (!readUInt(c, type)) return false;
    std::uint64_t a = 0, b = 0;

    switch (type) {
    case 0: return true;
    case 1: return readUInt(c, a);
    case 2: return readUInt(c, a);
    case 3: return readUInt(c, a);
    case 4: return readUInt(c, a) && readUInt(c, b);
    default:
        return false;
    }
}

/*!********************************************************************************************************************
 * \brief Heuristically skips an XGEOMETRY-like vendor record payload.
 *
 * Attempts to parse a common pattern and advances the cursor only if the record looks plausible.
 *
 * \param c Cursor to attempt advancing.
 * \return True if the cursor was advanced, false otherwise.
 *********************************************************************************************************************/
static inline bool trySkipXGeometryLike(OasCursor &c)
{
    OasCursor t = c;

    std::uint8_t info = 0;
    if (!readByte(t, info)) return false;

    std::uint64_t a = 0, layer = 0, dtype = 0;
    if (!readUInt(t, a))     return false;
    if (!readUInt(t, layer)) return false;
    if (!readUInt(t, dtype)) return false;

    if (layer > 1000000ULL || dtype > 1000000ULL) return false;

    ByteBuffer blob;
    if (!readString(t, blob)) return false;

    if (blob.size() > 10 * 1024 * 1024) return false;

    std::int64_t x = 0, y = 0;
    if (!readSInt(t, x)) return false;
    if (!readSInt(t, y)) return false;

    if (std::llabs(x) > (1LL << 50) || std::llabs(y) > (1LL << 50)) return false;

    c = t;
    return true;
}

/*!********************************************************************************************************************
 * \brief Heuristically skips an XNAME/XELEMENT-like vendor record payload.
 *
 * Tries to parse a pattern: unsigned-int followed by a length-prefixed byte string.
 *
 * \param c Cursor to attempt advancing.
 * \return True if the cursor was advanced, false otherwise.
 *********************************************************************************************************************/
static inline bool trySkipXNameLike(OasCursor &c)
{
    OasCursor t = c;

    std::uint64_t n = 0;
    if (!readUInt(t, n)) return false;

    ByteBuffer s;
    if (!readString(t, s)) return false;

    if (n > (1ULL << 40)) return false;
    if (s.size() > 32 * 1024 * 1024) return false;

    c = t;
    return true;
}

/*!********************************************************************************************************************
 * \brief Skips a property value and advances the cursor.
 *
 * Handles both real types (0..7) and additional property encodings (8..15 subset).
 *
 * \param c Cursor to advance.
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static inline bool skipPropertyValue(OasCursor &c)
{
    std::uint64_t t = 0;
    if (!readUInt(c, t)) return false;

    if (t <= 7) {
        return skipReal(c);
    }

    switch (t) {
    case 8:  { std::uint64_t u; return readUInt(c, u); }
    case 9:  { std::int64_t s;  return readSInt(c, s); }
    case 10: { std::string x; return readAString(c, x); }
    case 11: { ByteBuffer b; return readString(c, b); }
    case 12: { std::string n; return readNString(c, n); }
    case 13: { std::uint64_t rn; return readUInt(c, rn); }
    case 14: { std::uint64_t rn; return readUInt(c, rn); }
    case 15: { std::uint64_t rn; return readUInt(c, rn); }
    default:
        return false;
    }
}

/*!********************************************************************************************************************
 * \brief Constructs an OASIS reader for a file.
 * \param fileName Path to the OASIS file.
 *********************************************************************************************************************/
/*!********************************************************************************************************************
 * \brief Returns a copy of the collected error messages.
 * \return Error list.
 *********************************************************************************************************************/
/*!********************************************************************************************************************
 * \brief Inflates raw DEFLATE data into a pre-sized output buffer.
 *
 * Uses zlib raw mode (windowBits = -MAX_WBITS) and validates that the produced output size matches \p expectedOutLen.
 *
 * \param src Pointer to compressed data.
 * \param srcLen Size of compressed data in bytes.
 * \param expectedOutLen Expected uncompressed size in bytes.
 * \param out Output buffer (resized to \p expectedOutLen).
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static bool inflateRawDeflate(const std::uint8_t *src, int srcLen, int expectedOutLen, ByteBuffer &out)
{
    out.clear();
    out.resize(static_cast<std::size_t>(expectedOutLen));

    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    zs.next_in   = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
    zs.avail_in  = uInt(srcLen);
    zs.next_out  = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = uInt(expectedOutLen);

    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        return false;
    }

    const int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);

    if (rc != Z_STREAM_END) {
        return false;
    }

    if (int(zs.total_out) != expectedOutLen) {
        return false;
    }

    return true;
}

/*!********************************************************************************************************************
 * \brief Parser state carried across record parsing.
 *********************************************************************************************************************/
struct OasParseState
{
    CellNameTable cellNameByRef;

    const std::uint8_t *fileBase = nullptr;
    const std::uint8_t *fileEnd  = nullptr;

    std::uint64_t nextCellNameRef = 0;
    bool    explicitCellNameRefsSeen = false;
    bool    implicitCellNameRefsSeen = false;

    std::string currentCell;
    std::string modalPlacementCell;

    bool    seenEnd = false;

#if OAS_TRACE
    int     traceLimit = 2000;
    int     traceCount = 0;
#endif

#if OAS_GUARD
    std::uint64_t recordCount = 0;
    std::uint64_t stallCount  = 0;
    std::uint64_t lastOff     = 0;
    std::uint64_t lastGoodOff = 0;
    std::chrono::steady_clock::time_point guardStart{};
#endif
};


/*!********************************************************************************************************************
 * \brief Guarded debug log helper (throttled).
 *
 * Adds a line to errors and optionally qDebug().
 *
 * \param errors Error list.
 * \param st Parser state.
 * \param s Message.
 *********************************************************************************************************************/
/*static inline void dbgLog(std::vector<std::string> &errors, OasParseState &st, const std::string &s)
{
    errors.push_back( s;
#if OAS_TRACE
    Q_UNUSED(st);
#else
    Q_UNUSED(st);
#endif
}*/

/*!********************************************************************************************************************
 * \brief Parses a buffer of OASIS records into a hierarchy model.
 * \param c Cursor over the buffer.
 * \param out Output hierarchy.
 * \param errors Error list (append-only).
 * \param st Parser state.
 * \return True on success, false on parse failure.
 *********************************************************************************************************************/
static bool parseBuffer(OasCursor &c,
                        OasHierarchy &out,
                        std::vector<std::string> &errors,
                        OasParseState &st,
                        bool stopOnEnd = true);

static inline bool peekUInt(const OasCursor &c, std::uint64_t &out)
{
    OasCursor t = c;
    return readUInt(t, out);
}

static inline bool skipRepetitionEx(OasCursor &c)
{
    std::uint64_t repType = 0;
    if (!readUInt(c, repType)) return false;

    if (repType > 64) return false;

    switch (repType) {
    case 0:
        return true;

    case 1: {
        std::uint64_t nx = 0, ny = 0;
        std::int64_t dx = 0, dy = 0;
        return readUInt(c, nx) && readUInt(c, ny) && readSInt(c, dx) && readSInt(c, dy);
    }
    case 2: {
        std::uint64_t n = 0;
        std::int64_t dx = 0, dy = 0;
        return readUInt(c, n) && readSInt(c, dx) && readSInt(c, dy);
    }
    case 3: {
        std::uint64_t n = 0;
        return readUInt(c, n) && skipPointList(c);
    }

    case 10: {
        std::uint64_t dim = 0;
        if (!readUInt(c, dim)) return false;
        if (dim > 1000000ULL) return false;

        for (std::uint64_t i = 0; i < dim; ++i) {
            if (!skipGDelta(c)) return false; // x
            if (!skipGDelta(c)) return false; // y
        }
        return true;
    }
    case 11: {
        std::uint64_t dim = 0;
        std::uint64_t grid = 0;
        if (!readUInt(c, dim)) return false;
        if (!readUInt(c, grid)) return false;
        if (dim > 1000000ULL || grid > 100000000ULL) return false;

        // Often dim-1 displacement pairs
        const std::uint64_t k = (dim > 0) ? (dim - 1) : 0;
        for (std::uint64_t i = 0; i < k; ++i) {
            if (!skipGDelta(c)) return false; // x
            if (!skipGDelta(c)) return false; // y
        }
        return true;
    }

    default:
        return false;
    }
}

/*!********************************************************************************************************************
 * \brief Parses a single OASIS record and advances the cursor.
 *
 * \param c Cursor positioned at the record start; advanced past the record on success.
 * \param out Output hierarchy.
 * \param errors Error list (append-only).
 * \param st Parser state.
 * \return True on successful parsing/skipping, false on failure.
 *********************************************************************************************************************/
static bool parseOneRecord(OasCursor &c,
                           OasHierarchy &out,
                           std::vector<std::string> &errors,
                           OasParseState &st)
{
#ifdef OAS_DEBUG
    const std::uint8_t *recStart = c.p;
#endif

    std::uint64_t recId = 0;
    if (!readUInt(c, recId)) return false;

#if OAS_TRACE
    if (st.traceCount < st.traceLimit) {
#ifdef OAS_DEBUG
#endif
    }
#endif

    switch (recId) {
    case 0:
        goto done_ok;

    case 1: {
        std::string version;
        if (!readAString(c, version)) goto done_fail;

#ifdef OAS_DEBUG
#endif

        std::uint64_t realType = 0;
        if (!readUInt(c, realType)) goto done_fail;

#ifdef OAS_DEBUG
#endif

        switch(realType) {
        case 0: {
            std::uint64_t x = 0;
            if (!readUInt(c, x)) goto done_fail;
#ifdef OAS_DEBUG
#endif
            break;
        }
        case 1: {
            std::uint64_t x = 0;
            if (!readUInt(c, x)) goto done_fail;
#ifdef OAS_DEBUG
#endif
            break;
        }
        case 2: {
            std::uint64_t d = 0;
            if (!readUInt(c, d)) goto done_fail;
#ifdef OAS_DEBUG
#endif
            break;
        }
        case 3: {
            std::uint64_t d = 0;
            if (!readUInt(c, d)) goto done_fail;
#ifdef OAS_DEBUG
#endif
            break;
        }
        case 4:
        case 5: {
            std::uint64_t n = 0;
            std::uint64_t d = 0;
            if (!readUInt(c, n)) goto done_fail;
            if (!readUInt(c, d)) goto done_fail;
#ifdef OAS_DEBUG
#endif
            break;
        }
        case 6: {
            if (!c.has(4)) goto done_fail;
#ifdef OAS_DEBUG
            ByteBuffer raw(reinterpret_cast<const char*>(c.p), 4);
#endif
            c.p += 4;
            break;
        }
        case 7: {
            if (!c.has(8)) goto done_fail;
#ifdef OAS_DEBUG
            ByteBuffer raw(reinterpret_cast<const char*>(c.p), 8);
#endif
            c.p += 8;
            break;
        }
        default:
            goto done_fail;
        }

        std::uint64_t offsetFlag = 0;
        if (!readUInt(c, offsetFlag)) goto done_fail;

#ifdef OAS_DEBUG
#endif

        if (offsetFlag == 0) {
            for (int i = 0; i < 12; ++i) {
                std::uint64_t tmp = 0;
                if (!readUInt(c, tmp)) goto done_fail;
#ifdef OAS_DEBUG
#endif
            }
        } else {
            for (int i = 0; i < 12; ++i) {
                std::int64_t tmp = 0;
                if (!readSInt(c, tmp)) goto done_fail;
#ifdef OAS_DEBUG
#endif
            }
        }

        goto done_ok;
    }

    case 2: {
#ifdef OAS_DEBUG
        ByteBuffer tail(reinterpret_cast<const char*>(c.p), int(c.end - c.p));
#endif

        st.seenEnd = true;
        goto done_ok;
    }

    case 3: {
        // implicit CELLNAME
        st.implicitCellNameRefsSeen = true;

        if (st.explicitCellNameRefsSeen) {
            static bool warned = false;
            if (!warned) {
                warned = true;
            }
        }

        ByteBuffer raw;
        if (!readString(c, raw)) goto done_fail;

        const std::string name = decodeOasName(raw);

        if (!isLikelyValidCellName(name)) goto done_ok;

        const std::uint64_t rn = st.nextCellNameRef++;

        st.cellNameByRef.emplace(rn, name);
        out.allCells.insert(name);
        out.children[name];

#ifdef OAS_DEBUG
#endif
        goto done_ok;
    }

    case 4: {
        // explicit CELLNAME
        st.explicitCellNameRefsSeen = true;

        if (st.implicitCellNameRefsSeen) {
            static bool warned = false;
            if (!warned) {
                warned = true;
#ifdef OAS_DEBUG
#endif
            }
        }

        ByteBuffer raw;
        if (!readString(c, raw)) goto done_fail;

        std::uint64_t rn = 0;
        if (!readUInt(c, rn)) goto done_fail;

        const std::string name = decodeOasName(raw);

#ifdef OAS_DEBUG
#endif

        if (st.nextCellNameRef <= rn) st.nextCellNameRef = rn + 1;

        if (!isLikelyValidCellName(name)) {
            st.cellNameByRef.emplace(rn, name);
            goto done_ok;
        }

        st.cellNameByRef.emplace(rn, name);
        out.allCells.insert(name);
        out.children[name];
        goto done_ok;
    }

    case 5:  { std::string s; if (!readAString(c, s)) goto done_fail; goto done_ok; }
    case 6:  { std::string s; std::uint64_t rn = 0; if (!readAString(c, s)) goto done_fail; if (!readUInt(c, rn)) goto done_fail; goto done_ok; }
    case 7:  { std::string s; if (!readNString(c, s)) goto done_fail; goto done_ok; }
    case 8:  { std::string s; std::uint64_t rn = 0; if (!readNString(c, s)) goto done_fail; if (!readUInt(c, rn)) goto done_fail; goto done_ok; }
    case 9: {
        std::string s;
        if (!readAString(c, s)) goto done_fail;

        const std::uint64_t rn = st.nextCellNameRef++;
        st.cellNameByRef.emplace(rn, s);

        if (isLikelyValidCellName(s)) {
            out.allCells.insert(s);
            out.children[s];
        }

#ifdef OAS_DEBUG
#endif
        goto done_ok;
    }

    case 10: {
        std::string s;
        std::uint64_t rn = 0;
        if (!readAString(c, s)) goto done_fail;
        if (!readUInt(c, rn)) goto done_fail;

        st.cellNameByRef.emplace(rn, s);
        if (st.nextCellNameRef <= rn) {
            st.nextCellNameRef = rn + 1;
        }

        if (isLikelyValidCellName(s)) {
            out.allCells.insert(s);
            out.children[s];
        }

#ifdef OAS_DEBUG
#endif
        goto done_ok;
    }

    case 11: {
        std::string s;
        if (!readNString(c, s)) goto done_fail;
        if (!skipInterval(c)) goto done_fail;
        if (!skipInterval(c)) goto done_fail;
        goto done_ok;
    }

    case 12: {
        std::string s;
        if (!readNString(c, s)) goto done_fail;
        if (!skipInterval(c)) goto done_fail;
        if (!skipInterval(c)) goto done_fail;
        if (!skipInterval(c)) goto done_fail;
        if (!skipInterval(c)) goto done_fail;
        goto done_ok;
    }

    case 13: {
        std::uint64_t rn = 0;
        if (!readUInt(c, rn)) goto done_fail;
#ifdef OAS_DEBUG
#endif
        const std::string name = mapValue(st.cellNameByRef, rn);
        if (name.empty()) {
#ifdef OAS_DEBUG
#endif
            goto done_fail;
        }

        st.currentCell = name;
        out.allCells.insert(name);
        out.children[name];
        st.modalPlacementCell.clear();
        goto done_ok;
    }

    case 14: {
        ByteBuffer raw;
#ifdef OAS_DEBUG
        const std::uint8_t *strAt = c.p;
#endif
        if (!readString(c, raw)) goto done_fail;

        const std::string name = decodeOasName(raw);

#ifdef OAS_DEBUG
#endif

        st.currentCell = name;
        out.allCells.insert(name);
        out.children[name];
        st.modalPlacementCell.clear();
        goto done_ok;
    }

    case 15:
    case 16:
        goto done_ok;

    case 17: {
        if (st.currentCell.empty()) {
#ifdef OAS_DEBUG
#endif
            goto done_fail;
        }

        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        const bool R    = (info & 0x01) != 0;
        const bool Y    = (info & 0x02) != 0;
        const bool X    = (info & 0x04) != 0;
        const bool N    = (info & 0x08) != 0;
        const bool Cbit = (info & 0x10) != 0;

        std::string placedCell;

        if (Cbit) {
            if (N) {
#ifdef OAS_DEBUG
                const std::uint8_t *nmAt = c.p;
#endif
                if (!readNString(c, placedCell)) goto done_fail;

                if (!isLikelyValidCellName(placedCell)) {
#ifdef OAS_DEBUG
#endif
                }
            } else {
                // N=0 -> reference-number
                std::uint64_t rn = 0;
                if (!readUInt(c, rn)) goto done_fail;

                placedCell = mapValue(st.cellNameByRef, rn);
                if (placedCell.empty()) {
#ifdef OAS_DEBUG
#endif
                    goto done_fail;
                }
            }

            st.modalPlacementCell = placedCell;
        } else {
            placedCell = st.modalPlacementCell;
        }

        if (X) { std::int64_t x; if (!readSInt(c, x)) goto done_fail; }
        if (Y) { std::int64_t y; if (!readSInt(c, y)) goto done_fail; }

        if (R) {
            if (!skipRepetition(c)) goto done_fail;
        }

        if (!placedCell.empty()) {
            out.children[st.currentCell].push_back(placedCell);
            out.allCells.insert(placedCell);
        }

        goto done_ok;
    }

    case 18: {
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        const bool R    = (info & 0x01) != 0;
        const bool T    = (info & 0x02) != 0;
        const bool Y    = (info & 0x04) != 0;
        const bool X    = (info & 0x08) != 0;
        const bool N    = (info & 0x10) != 0;
        const bool Cbit = (info & 0x20) != 0;

        if (Cbit) {
            if (N) { std::uint64_t rn = 0; if (!readUInt(c, rn)) goto done_fail; }
            else   { std::string s; if (!readAString(c, s)) goto done_fail; }
        }

        if (X) { std::int64_t x; if (!readSInt(c, x)) goto done_fail; }
        if (Y) { std::int64_t y; if (!readSInt(c, y)) goto done_fail; }

        if (T) { std::uint64_t tt; if (!readUInt(c, tt)) goto done_fail; }
        if (R) { if (!skipRepetition(c)) goto done_fail; }

        goto done_ok;
    }

    case 19: { // TEXT
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        // bit pattern: 0 C N X Y R T L  (from spec)
        const bool L    = (info & 0x01) != 0;
        const bool T    = (info & 0x02) != 0;
        const bool R    = (info & 0x04) != 0;
        const bool Y    = (info & 0x08) != 0;
        const bool X    = (info & 0x10) != 0;
        const bool N    = (info & 0x20) != 0;
        const bool Cbit = (info & 0x40) != 0;

        // text-string / reference-number / modal textstring
        if (Cbit) {
            if (N) {
                std::uint64_t rn = 0;
                if (!readUInt(c, rn)) goto done_fail;
            } else {
                std::string s;
                if (!readAString(c, s)) goto done_fail;
            }
        } else {
            // C=0 => no modal textstring, bytes in record
        }

        if (L) { std::uint64_t tlayer = 0; if (!readUInt(c, tlayer)) goto done_fail; }
        if (T) { std::uint64_t ttype  = 0; if (!readUInt(c, ttype))  goto done_fail; }

        if (X) { std::int64_t vx = 0; if (!readSInt(c, vx)) goto done_fail; }
        if (Y) { std::int64_t vy = 0; if (!readSInt(c, vy)) goto done_fail; }

        if (R) { if (!skipRepetition(c)) goto done_fail; }

        goto done_ok;
    }

    case 20: {
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        const bool L = (info & 0x01) != 0;
        const bool D = (info & 0x02) != 0;
        const bool R = (info & 0x04) != 0;
        const bool Y = (info & 0x08) != 0;
        const bool X = (info & 0x10) != 0;
        const bool H = (info & 0x20) != 0;
        const bool W = (info & 0x40) != 0;

        if (L) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (D) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (W) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (H) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (X) { std::int64_t v;  if (!readSInt(c, v)) goto done_fail; }
        if (Y) { std::int64_t v;  if (!readSInt(c, v)) goto done_fail; }
        if (R) { if (!skipRepetition(c)) goto done_fail; }

        goto done_ok;
    }

    case 21: {
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        const bool L = (info & 0x01) != 0;
        const bool D = (info & 0x02) != 0;
        const bool R = (info & 0x04) != 0;
        const bool Y = (info & 0x08) != 0;
        const bool X = (info & 0x10) != 0;
        const bool P = (info & 0x20) != 0;

        if (L) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (D) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (P) { if (!skipPointList(c)) goto done_fail; }
        if (X) { std::int64_t v;  if (!readSInt(c, v)) goto done_fail; }
        if (Y) { std::int64_t v;  if (!readSInt(c, v)) goto done_fail; }
        if (R) { if (!skipRepetition(c)) goto done_fail; }

        goto done_ok;
    }

    case 22: {
#ifdef OAS_DEBUG
        const std::uint64_t recOff = std::uint64_t(recStart - st.fileBase);
#endif

        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        const bool L = (info & 0x01) != 0;
        const bool D = (info & 0x02) != 0;
        const bool R = (info & 0x04) != 0;
        const bool Y = (info & 0x08) != 0;
        const bool X = (info & 0x10) != 0;
        const bool P = (info & 0x20) != 0;
        const bool W = (info & 0x40) != 0;
        const bool E = (info & 0x80) != 0;

#ifdef OAS_DEBUG
#endif

        if (L) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (D) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (W) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }

        auto parse_PXY = [&](OasCursor &cc, bool orderPXY) -> bool {
            if (orderPXY) {
                if (P) {
                    OasCursor t = cc;
                    std::uint64_t ptType = 0, count = 0;
                    if (readUInt(t, ptType) && readUInt(t, count)) {
#ifdef OAS_DEBUG
#endif
                    }
                    if (!skipPointList(cc)) return false;
                }
                if (X) { std::int64_t v; if (!readSInt(cc, v)) return false; }
                if (Y) { std::int64_t v; if (!readSInt(cc, v)) return false; }
            } else {
                if (X) { std::int64_t v; if (!readSInt(cc, v)) return false; }
                if (Y) { std::int64_t v; if (!readSInt(cc, v)) return false; }
                if (P) {
                    OasCursor t = cc;
                    std::uint64_t ptType = 0, count = 0;
                    if (readUInt(t, ptType) && readUInt(t, count)) {
#ifdef OAS_DEBUG
#endif
                    }
                    if (!skipPointList(cc)) return false;
                }
            }
            return true;
        };

        auto parse_E = [&](OasCursor &cc) -> bool {
            if (!E) return true;
            std::uint64_t a = 0, b = 0;
            return readUInt(cc, a) && readUInt(cc, b);
        };

        auto parse_R = [&](OasCursor &cc) -> bool {
            if (!R) return true;

            std::uint64_t repPeek = 0;
            if (peekUInt(cc, repPeek)) {
#ifdef OAS_DEBUG
#endif
            } else {
#ifdef OAS_DEBUG
#endif
            }

#ifdef OAS_DEBUG
            const std::uint64_t repOff = std::uint64_t(cc.p - st.fileBase);
#endif

            if (!skipRepetitionEx(cc)) {
#ifdef OAS_DEBUG
#endif
                return false;
            }
            return true;
        };

        {
            OasCursor t = c;
            bool ok = true;
            ok = ok && parse_PXY(t, /*orderPXY=*/true);
            ok = ok && parse_E(t);
            ok = ok && parse_R(t);

            if (ok) {
                c = t;
                goto done_ok;
            }
        }

        {
            OasCursor t = c;
            bool ok = true;
            ok = ok && parse_PXY(t, /*orderPXY=*/false);
            ok = ok && parse_E(t);
            ok = ok && parse_R(t);

            if (ok) {
                c = t;
                goto done_ok;
            }
        }

#ifdef OAS_DEBUG
#endif
        goto done_fail;
    }

    case 23: {
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        const bool L   = (info & 0x01) != 0;
        const bool D   = (info & 0x02) != 0;
        const bool R   = (info & 0x04) != 0;
        const bool Y   = (info & 0x08) != 0;
        const bool X   = (info & 0x10) != 0;
        const bool H   = (info & 0x20) != 0;
        const bool W   = (info & 0x40) != 0;
        const bool Dlt = (info & 0x80) != 0;

        if (L) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (D) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (W) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (H) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (X) { std::int64_t v;  if (!readSInt(c, v)) goto done_fail; }
        if (Y) { std::int64_t v;  if (!readSInt(c, v)) goto done_fail; }

        if (Dlt) {
            if (!skip1Delta(c)) goto done_fail;
            if (!skip1Delta(c)) goto done_fail;
        }

        if (R) { if (!skipRepetition(c)) goto done_fail; }

        goto done_ok;
    }

    case 24: {
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        const bool L = (info & 0x01) != 0;
        const bool D = (info & 0x02) != 0;
        const bool R = (info & 0x04) != 0;
        const bool Y = (info & 0x08) != 0;
        const bool X = (info & 0x10) != 0;
        const bool H = (info & 0x20) != 0;
        const bool W = (info & 0x40) != 0;
        const bool T = (info & 0x80) != 0;

        if (L) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (D) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (W) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (H) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (X) { std::int64_t v;  if (!readSInt(c, v)) goto done_fail; }
        if (Y) { std::int64_t v;  if (!readSInt(c, v)) goto done_fail; }
        if (T) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (R) { if (!skipRepetition(c)) goto done_fail; }

        goto done_ok;
    }

    case 25: {
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        const bool L = (info & 0x01) != 0;
        const bool D = (info & 0x02) != 0;
        const bool R = (info & 0x04) != 0;
        const bool Y = (info & 0x08) != 0;
        const bool X = (info & 0x10) != 0;
        const bool W = (info & 0x20) != 0;

        if (L) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (D) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (W) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (X) { std::int64_t v;  if (!readSInt(c, v)) goto done_fail; }
        if (Y) { std::int64_t v;  if (!readSInt(c, v)) goto done_fail; }
        if (R) { if (!skipRepetition(c)) goto done_fail; }

        goto done_ok;
    }

    case 28: {
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        const bool S    = (info & 0x01) != 0;
        const bool N    = (info & 0x02) != 0;
        const bool Cbit = (info & 0x04) != 0;
        const bool V    = (info & 0x08) != 0;

        const std::uint64_t UUUU = (std::uint64_t(info) >> 4) & 0x0F;

        if (Cbit) {
            if (N) { std::uint64_t rn = 0; if (!readUInt(c, rn)) goto done_fail; }
            else   { std::string s; if (!readNString(c, s)) goto done_fail; }
        }

        if (V) {
            Q_UNUSED(S);
            goto done_ok;
        }

        std::uint64_t cnt = 0;
        if (UUUU < 15) cnt = UUUU;
        else { if (!readUInt(c, cnt)) goto done_fail; }

        for (std::uint64_t i = 0; i < cnt; ++i) {
            if (!skipPropertyValue(c)) goto done_fail;
        }

        Q_UNUSED(S);
        goto done_ok;
    }

    case 29:
        goto done_ok;

    case 30: {
        std::uint64_t attr = 0;
        if (!readUInt(c, attr)) goto done_fail;
        ByteBuffer b;
        if (!readString(c, b)) goto done_fail;
        Q_UNUSED(attr);
        goto done_ok;
    }

    case 31: {
        std::uint64_t attr = 0, rn = 0;
        if (!readUInt(c, attr)) goto done_fail;
        ByteBuffer b;
        if (!readString(c, b)) goto done_fail;
        if (!readUInt(c, rn)) goto done_fail;
        Q_UNUSED(attr);
        goto done_ok;
    }

    case 32: {
        std::uint64_t attr = 0;
        if (!readUInt(c, attr)) goto done_fail;
        ByteBuffer b;
        if (!readString(c, b)) goto done_fail;
        Q_UNUSED(attr);
        goto done_ok;
    }

    case 33: {
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        std::uint64_t attr = 0;
        if (!readUInt(c, attr)) goto done_fail;

        const bool L = (info & 0x01) != 0;
        const bool D = (info & 0x02) != 0;
        const bool R = (info & 0x04) != 0;
        const bool Y = (info & 0x08) != 0;
        const bool X = (info & 0x10) != 0;

        if (L) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (D) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; }
        if (X) { std::int64_t v;  if (!readSInt(c, v)) goto done_fail; }
        if (Y) { std::int64_t v;  if (!readSInt(c, v)) goto done_fail; }
        if (R) { if (!skipRepetition(c)) goto done_fail; }

        ByteBuffer geom;
        if (!readString(c, geom)) goto done_fail;

        Q_UNUSED(attr);
        goto done_ok;
    }

    case 34: {
        std::uint64_t compType = 0, uncomp = 0, comp = 0;
        if (!readUInt(c, compType)) goto done_fail;
        if (!readUInt(c, uncomp)) goto done_fail;
        if (!readUInt(c, comp)) goto done_fail;

        if (comp > std::uint64_t(c.end - c.p)) goto done_fail;

        const std::uint8_t *compData = c.p;
        c.p += int(comp);

        if (compType != 0) {
#ifdef OAS_DEBUG
#endif
            goto done_fail;
        }

        ByteBuffer dec;
        if (!inflateRawDeflate(compData, int(comp), int(uncomp), dec)) {
#ifdef OAS_DEBUG
#endif
            goto done_fail;
        }

#ifdef OAS_DEBUG
#endif

        OasCursor sub;
        sub.p   = reinterpret_cast<const std::uint8_t*>(dec.data());
        sub.end = sub.p + dec.size();

        OasParseState subSt = st;
        subSt.fileBase = sub.p;
        subSt.fileEnd  = sub.end;
        subSt.seenEnd  = false;

#if OAS_TRACE
        subSt.traceCount = 0;
#endif

#if OAS_GUARD
        subSt.recordCount = 0;
        subSt.stallCount  = 0;
        subSt.lastOff     = 0;
        subSt.lastGoodOff = 0;
        subSt.guardStart = {};
#endif

        if (!parseBuffer(sub, out, errors, subSt, false)) goto done_fail;

        st.cellNameByRef            = subSt.cellNameByRef;
        st.nextCellNameRef          = subSt.nextCellNameRef;
        st.explicitCellNameRefsSeen = subSt.explicitCellNameRefsSeen;
        st.implicitCellNameRefsSeen = subSt.implicitCellNameRefsSeen;

        goto done_ok;
    }

    default:
        if (trySkipXGeometryLike(c) || trySkipXNameLike(c)) {
            goto done_ok;
        }

#ifdef OAS_DEBUG
#endif
        goto done_fail;
    }

done_ok:
#if OAS_DEBUG
    if (st.traceCount < st.traceLimit) {
    }
    st.traceCount++;
#endif
    return true;

done_fail:
#ifdef OAS_DEBUG
#endif
    return false;
}

/*!********************************************************************************************************************
 * \brief Checks whether the remaining bytes are only padding.
 *
 * Treats NUL, space, tab, LF and CR as padding.
 *
 * \param p Pointer to start of remaining bytes.
 * \param end Pointer one past the end.
 * \return True if the remaining region is padding-only, false otherwise.
 *********************************************************************************************************************/
static inline bool isPaddingTail(const std::uint8_t *p, const std::uint8_t *end)
{
    while (p < end) {
        const std::uint8_t b = *p++;
        if (b == 0x00) continue;
        if (b == 0x20) continue;
        if (b == 0x09) continue;
        if (b == 0x0A) continue;
        if (b == 0x0D) continue;
        return false;
    }
    return true;
}

/*!********************************************************************************************************************
 * \brief Parses records from the cursor until END or padding tail.
 *
 * \param c Cursor over the buffer.
 * \param out Output hierarchy.
 * \param errors Error list (append-only).
 * \param st Parser state.
 * \return True on success, false on parse failure.
 *********************************************************************************************************************/
static bool parseBuffer(OasCursor &c,
                        OasHierarchy &out,
                        std::vector<std::string> &errors,
                        OasParseState &st,
                        bool stopOnEnd /* = true */)
{
#if OAS_DEBUG
    if (!false) {
        (void)st.guardStart;
    }
#endif

    while (c.p < c.end) {

        if (isPaddingTail(c.p, c.end)) {
#if OAS_DEBUG
#endif
            break;
        }

        const std::uint8_t *before = c.p;

#if OAS_DEBUG
        st.recordCount++;
        const std::uint64_t offBefore = std::uint64_t(before - st.fileBase);

        if ((st.recordCount % OAS_GUARD_LOG_EVERY_N_RECORDS) == 0) {
        }

        if (st.recordCount > OAS_GUARD_MAX_RECORDS) {
            return false;
        }
#endif

        if (!parseOneRecord(c, out, errors, st)) {

            if (isPaddingTail(before, c.end)) {
#if OAS_DEBUG
#endif
                break;
            }

#if OAS_DEBUG
#endif
            return false;
        }

#if OAS_DEBUG
        const std::uint64_t offAfter = std::uint64_t(c.p - st.fileBase);
        const std::uint64_t delta = (offAfter >= offBefore) ? (offAfter - offBefore) : 0;

        if (delta <= OAS_GUARD_TINY_PROGRESS_BYTES) {
            st.stallCount++;
        } else {
            st.stallCount = 0;
            st.lastGoodOff = offAfter;
        }

        if (offAfter == offBefore) {
            return false;
        }

        if (st.stallCount > OAS_GUARD_STALL_LIMIT) {
            return false;
        }

        st.lastOff = offAfter;
#endif

        if (stopOnEnd && st.seenEnd) {
#if OAS_DEBUG
#endif
            break;
        }
    }

    return true;
}

} // namespace

OasReader::OasReader(std::string fileName)
    : fileName_(std::move(fileName))
{
}

bool OasReader::readHierarchy(OasHierarchy &out)
{
    out.topCells.clear();
    out.children.clear();
    out.allCells.clear();
    errors_.clear();

    if (fileName_.empty()) {
        errors_.push_back("Empty OASIS filename.");
        return false;
    }

    std::ifstream input(fileName_, std::ios::binary);
    if (!input) {
        errors_.push_back("Failed to open OASIS for read: '" + fileName_ + "'");
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff szOff = input.tellg();
    if (szOff < 16) {
        errors_.push_back("OASIS file too small: '" + fileName_ + "'");
        return false;
    }
    const auto sz = static_cast<std::size_t>(szOff);
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> fileBytes(sz);
    input.read(reinterpret_cast<char *>(fileBytes.data()), static_cast<std::streamsize>(sz));
    if (!input) {
        errors_.push_back("Failed to read OASIS: '" + fileName_ + "'");
        return false;
    }

    const std::uint8_t *base = fileBytes.data();
    const std::uint8_t *const fileEnd = base + sz;

    static const char magic[] = "%SEMI-OASIS";
    if (std::memcmp(base, magic, sizeof(magic) - 1) != 0) {
        errors_.push_back("Not an OASIS file (missing %SEMI-OASIS magic).");
        return false;
    }

    const std::uint8_t *p = base + (sizeof(magic) - 1);
    while (p < fileEnd && (*p == '\r' || *p == '\n')) {
        ++p;
    }

    OasCursor c;
    c.p = p;
    c.end = fileEnd;

    OasParseState st;
    st.fileBase = base;
    st.fileEnd = fileEnd;

#if OAS_GUARD
    st.recordCount = 0;
    st.stallCount = 0;
    st.lastOff = static_cast<std::uint64_t>(c.p - st.fileBase);
    st.lastGoodOff = st.lastOff;
    st.guardStart = {};
#endif

    if (!parseBuffer(c, out, errors_, st)) {
        return false;
    }

    CellNameSet referenced;
    for (const auto &entry : out.children) {
        for (const std::string &child : entry.second) {
            referenced.insert(child);
        }
    }

    out.topCells.clear();
    out.topCells.reserve(out.allCells.size());
    for (const std::string &cellName : out.allCells) {
        if (referenced.find(cellName) == referenced.end()) {
            out.topCells.push_back(cellName);
        }
    }
    std::sort(out.topCells.begin(), out.topCells.end());
    return true;
}

} // namespace core
