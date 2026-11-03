#include "oas_reader.h"

#include "database.h"
#include "oas_geometry.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <zlib.h>

namespace room {
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
 * OASIS signed-integer: the unsigned value's low bit is the sign.
 * A set bit means negative, and the magnitude is the remaining bits.
 *   out = (u & 1) ? -(u >> 1) : (u >> 1)
 *
 * \param c Cursor to read from.
 * \param out Decoded signed value.
 * \return True on success, false otherwise.
 *********************************************************************************************************************/
static inline bool readSInt(OasCursor &c, std::int64_t &out)
{
    std::uint64_t u = 0;
    if (!readUInt(c, u)) return false;
    out = (u & 1ULL) ? -std::int64_t(u >> 1) : std::int64_t(u >> 1);
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
static inline bool readGDeltaPair(OasCursor &c, std::int64_t &dx, std::int64_t &dy)
{
    std::uint64_t word = 0;
    if (!readUInt(c, word)) {
        return false;
    }
    if ((word & 1ULL) != 0) {
        dx = ((word & 2ULL) == 0) ? static_cast<std::int64_t>(word >> 2)
                                  : -static_cast<std::int64_t>(word >> 2);
        return readSInt(c, dy);
    }
    const std::int64_t magnitude = static_cast<std::int64_t>(word >> 4);
    switch ((word >> 1) & 7ULL) {
    case 0: dx = magnitude; dy = 0; break;
    case 1: dx = 0; dy = magnitude; break;
    case 2: dx = -magnitude; dy = 0; break;
    case 3: dx = 0; dy = -magnitude; break;
    case 4: dx = magnitude; dy = magnitude; break;
    case 5: dx = -magnitude; dy = magnitude; break;
    case 6: dx = -magnitude; dy = -magnitude; break;
    default: dx = magnitude; dy = -magnitude; break;
    }
    return true;
}

static bool read2Delta(OasCursor &c, std::int64_t &dx, std::int64_t &dy)
{
    std::uint64_t word = 0;
    if (!readUInt(c, word)) {
        return false;
    }
    const std::int64_t magnitude = static_cast<std::int64_t>(word >> 2);
    switch (word & 3ULL) {
    case 0: dx = magnitude; dy = 0; break;
    case 1: dx = 0; dy = magnitude; break;
    case 2: dx = -magnitude; dy = 0; break;
    default: dx = 0; dy = -magnitude; break;
    }
    return true;
}

static bool read3Delta(OasCursor &c, std::int64_t &dx, std::int64_t &dy)
{
    std::uint64_t word = 0;
    if (!readUInt(c, word)) {
        return false;
    }
    const std::int64_t magnitude = static_cast<std::int64_t>(word >> 3);
    switch (word & 7ULL) {
    case 0: dx = magnitude; dy = 0; break;
    case 1: dx = 0; dy = magnitude; break;
    case 2: dx = -magnitude; dy = 0; break;
    case 3: dx = 0; dy = -magnitude; break;
    case 4: dx = magnitude; dy = magnitude; break;
    case 5: dx = -magnitude; dy = magnitude; break;
    case 6: dx = -magnitude; dy = -magnitude; break;
    default: dx = magnitude; dy = -magnitude; break;
    }
    return true;
}

static bool readPointList(OasCursor &c, bool forPolygon, std::vector<Point> &points)
{
    std::uint64_t ptType = 0;
    if (!readUInt(c, ptType)) {
        return false;
    }
    std::uint64_t count = 0;
    if (!readUInt(c, count)) {
        return false;
    }
    if (count == 0) {
        return false;
    }

    points.clear();
    points.push_back(Point{0, 0});
    std::int64_t x = 0;
    std::int64_t y = 0;

    if (ptType == 0 || ptType == 1) {
        bool horizontal = ptType == 0;
        for (std::uint64_t i = 0; i < count; ++i) {
            std::int64_t delta = 0;
            if (!readSInt(c, delta)) {
                return false;
            }
            if (horizontal) {
                x += delta;
            } else {
                y += delta;
            }
            horizontal = !horizontal;
            points.push_back(Point{x, y});
        }
        if (forPolygon) {
            points.push_back(horizontal ? Point{0, y} : Point{x, 0});
        }
        return true;
    }

    if (ptType == 2 || ptType == 3 || ptType == 4) {
        for (std::uint64_t i = 0; i < count; ++i) {
            std::int64_t dx = 0;
            std::int64_t dy = 0;
            const bool ok = ptType == 2 ? read2Delta(c, dx, dy)
                            : ptType == 3 ? read3Delta(c, dx, dy)
                                          : readGDeltaPair(c, dx, dy);
            if (!ok) {
                return false;
            }
            x += dx;
            y += dy;
            points.push_back(Point{x, y});
        }
        return true;
    }

    if (ptType == 5) {
        std::int64_t vx = 0;
        std::int64_t vy = 0;
        for (std::uint64_t i = 0; i < count; ++i) {
            std::int64_t dx = 0;
            std::int64_t dy = 0;
            if (!readGDeltaPair(c, dx, dy)) {
                return false;
            }
            vx += dx;
            vy += dy;
            x += vx;
            y += vy;
            points.push_back(Point{x, y});
        }
        return true;
    }

    return false;
}

static inline bool skipPointList(OasCursor &c)
{
    std::vector<Point> points;
    return readPointList(c, false, points);
}

struct RepOffset {
    std::int64_t dx = 0;
    std::int64_t dy = 0;
};

struct OasArray {
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::int64_t stepX = 0;
    std::int64_t stepY = 0;
    std::int64_t colY = 0;
    std::int64_t rowX = 0;
};

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

    default:
        return false;
    }
}

static std::uint32_t repetitionCount(std::uint64_t dimension, bool zeroStep)
{
    if (zeroStep) {
        return 1;
    }
    const std::uint64_t count = dimension + 2;
    if (count > 0xffffffffULL) {
        return 0;
    }
    return static_cast<std::uint32_t>(count);
}

static bool assignLattice(std::int64_t ax, std::int64_t ay, std::uint32_t alongA,
                          std::int64_t bx, std::int64_t by, std::uint32_t alongB,
                          OasArray &out)
{
    out = {};
    if (alongA == 0 || alongB == 0) {
        return false;
    }
    if (alongA <= 1 && alongB <= 1) {
        return true;
    }
    out.columns = alongA;
    out.rows = alongB;
    out.stepX = ax;
    out.colY = ay;
    out.rowX = bx;
    out.stepY = by;
    return true;
}

struct RepStat {
    std::uint64_t calls = 0;
    std::uint64_t members = 0;
};

static RepStat g_repStat[12];

static bool repStatsOnly()
{
    static const bool on = std::getenv("OAS_REP_STATS") != nullptr;
    return on;
}

static void noteRep(std::uint64_t type, std::uint64_t members)
{
    if (type < 12) {
        g_repStat[type].calls += 1;
        g_repStat[type].members += members;
    }
}

static void dumpRepStats()
{
    std::fprintf(stderr, "rep-stats\n");
    for (int type = 1; type <= 11; ++type) {
        if (g_repStat[type].calls == 0) {
            continue;
        }
        std::fprintf(stderr, "type %d calls %llu members %llu\n", type,
                     static_cast<unsigned long long>(g_repStat[type].calls),
                     static_cast<unsigned long long>(g_repStat[type].members));
    }
}

static bool readArrayRep(OasCursor &c, OasArray &modal, std::shared_ptr<const std::vector<Point>> &modalOffsets)
{
    std::uint64_t repType = 0;
    if (!readUInt(c, repType)) {
        return false;
    }
    if (repType == 0) {
        return true;
    }

    OasArray next;
    bool regular = false;
    std::shared_ptr<std::vector<Point>> built;
    std::uint64_t listed = 0;
    const bool tally = repStatsOnly();
    switch (repType) {
    case 1: {
        std::uint64_t nx = 0, ny = 0, xs = 0, ys = 0;
        if (!readUInt(c, nx) || !readUInt(c, ny) || !readUInt(c, xs) || !readUInt(c, ys)) {
            return false;
        }
        const std::uint32_t columns = repetitionCount(nx, xs == 0);
        const std::uint32_t rows = repetitionCount(ny, ys == 0);
        regular = assignLattice(static_cast<std::int64_t>(xs), 0, columns,
                                   0, static_cast<std::int64_t>(ys), rows, next);
        break;
    }
    case 2: {
        std::uint64_t nx = 0, xs = 0;
        if (!readUInt(c, nx) || !readUInt(c, xs)) {
            return false;
        }
        const std::uint32_t columns = repetitionCount(nx, xs == 0);
        regular = assignLattice(static_cast<std::int64_t>(xs), 0, columns, 0, 0, 1, next);
        break;
    }
    case 3: {
        std::uint64_t ny = 0, ys = 0;
        if (!readUInt(c, ny) || !readUInt(c, ys)) {
            return false;
        }
        const std::uint32_t rows = repetitionCount(ny, ys == 0);
        regular = assignLattice(0, 0, 1, 0, static_cast<std::int64_t>(ys), rows, next);
        break;
    }
    case 4:
    case 5:
    case 6:
    case 7: {
        std::uint64_t dim = 0;
        std::uint64_t grid = 1;
        if (!readUInt(c, dim)) {
            return false;
        }
        if ((repType == 5 || repType == 7) && !readUInt(c, grid)) {
            return false;
        }
        listed = 1;
        if (!tally) {
            built = std::make_shared<std::vector<Point>>();
            built->push_back(Point{0, 0});
        }
        std::int64_t cursorPos = 0;
        const bool vertical = repType == 6 || repType == 7;
        for (std::uint64_t i = 0; i <= dim; ++i) {
            std::uint64_t space = 0;
            if (!readUInt(c, space)) {
                return false;
            }
            const std::int64_t delta = static_cast<std::int64_t>(space) * static_cast<std::int64_t>(grid);
            if (delta == 0) {
                continue;
            }
            cursorPos += delta;
            ++listed;
            if (!tally) {
                built->push_back(vertical ? Point{0, cursorPos} : Point{cursorPos, 0});
            }
        }
        if (!tally && built && built->size() <= 1) {
            built.reset();
        }
        break;
    }
    case 8: {
        std::uint64_t n = 0, m = 0;
        std::int64_t ax = 0, ay = 0, bx = 0, by = 0;
        if (!readUInt(c, n) || !readUInt(c, m) || !readGDeltaPair(c, ax, ay) || !readGDeltaPair(c, bx, by)) {
            return false;
        }
        const std::uint32_t alongA = (ax == 0 && ay == 0) ? 1 : repetitionCount(n, false);
        const std::uint32_t alongB = (bx == 0 && by == 0) ? 1 : repetitionCount(m, false);
        regular = assignLattice(ax, ay, alongA, bx, by, alongB, next);
        break;
    }
    case 9: {
        std::uint64_t n = 0;
        std::int64_t ax = 0, ay = 0;
        if (!readUInt(c, n) || !readGDeltaPair(c, ax, ay)) {
            return false;
        }
        const std::uint32_t alongA = (ax == 0 && ay == 0) ? 1 : repetitionCount(n, false);
        regular = assignLattice(ax, ay, alongA, 0, 0, 1, next);
        break;
    }
    case 10:
    case 11: {
        std::uint64_t dim = 0;
        std::uint64_t grid = 1;
        if (!readUInt(c, dim)) {
            return false;
        }
        if (repType == 11 && !readUInt(c, grid)) {
            return false;
        }
        listed = 1;
        if (!tally) {
            built = std::make_shared<std::vector<Point>>();
            built->push_back(Point{0, 0});
        }
        std::int64_t x = 0;
        std::int64_t y = 0;
        for (std::uint64_t i = 0; i <= dim; ++i) {
            std::int64_t dx = 0;
            std::int64_t dy = 0;
            if (!readGDeltaPair(c, dx, dy)) {
                return false;
            }
            dx *= static_cast<std::int64_t>(grid);
            dy *= static_cast<std::int64_t>(grid);
            if (dx == 0 && dy == 0) {
                continue;
            }
            x += dx;
            y += dy;
            ++listed;
            if (!tally) {
                built->push_back(Point{x, y});
            }
        }
        if (!tally && built && built->size() <= 1) {
            built.reset();
        }
        break;
    }
    default:
        return false;
    }

    if (regular) {
        const std::uint64_t members =
            (next.columns > 1 || next.rows > 1)
                ? static_cast<std::uint64_t>(next.columns) * static_cast<std::uint64_t>(next.rows)
                : 1;
        noteRep(repType, members);
        modal = next;
        modalOffsets.reset();
    } else if (built && built->size() > 1) {
        noteRep(repType, built->size());
        modal = {};
        modalOffsets = built;
    } else {
        if (listed > 1) {
            noteRep(repType, listed);
        }
        modal = {};
        modalOffsets.reset();
    }
    return true;
}

static bool polygonIsBox(const std::vector<Point> &points, std::int64_t &llx, std::int64_t &lly,
                         std::uint64_t &width, std::uint64_t &height)
{
    if (points.size() < 4) {
        return false;
    }
    llx = points[0].x;
    lly = points[0].y;
    std::int64_t urx = points[0].x;
    std::int64_t ury = points[0].y;
    for (const Point &pt : points) {
        llx = std::min(llx, pt.x);
        lly = std::min(lly, pt.y);
        urx = std::max(urx, pt.x);
        ury = std::max(ury, pt.y);
    }
    if (urx <= llx || ury <= lly) {
        return false;
    }
    for (const Point &pt : points) {
        if ((pt.x != llx && pt.x != urx) || (pt.y != lly && pt.y != ury)) {
            return false;
        }
    }
    width = static_cast<std::uint64_t>(urx - llx);
    height = static_cast<std::uint64_t>(ury - lly);
    return true;
}

static bool readRepetitionOffsets(OasCursor &c, std::vector<RepOffset> &offsets)
{
    offsets.clear();
    offsets.push_back({0, 0});

    std::uint64_t repType = 0;
    if (!readUInt(c, repType)) {
        return false;
    }

    switch (repType) {
    case 0:
        return true;
    case 1: {
        std::uint64_t nx = 0;
        std::uint64_t ny = 0;
        std::uint64_t xs = 0;
        std::uint64_t ys = 0;
        if (!readUInt(c, nx) || !readUInt(c, ny) || !readUInt(c, xs) || !readUInt(c, ys)) {
            return false;
        }
        offsets.clear();
        for (std::uint64_t iy = 0; iy < ny; ++iy) {
            for (std::uint64_t ix = 0; ix < nx; ++ix) {
                offsets.push_back({static_cast<std::int64_t>(ix * xs), static_cast<std::int64_t>(iy * ys)});
            }
        }
        return true;
    }
    case 2: {
        std::uint64_t nx = 0;
        std::uint64_t xs = 0;
        if (!readUInt(c, nx) || !readUInt(c, xs)) {
            return false;
        }
        offsets.clear();
        for (std::uint64_t ix = 0; ix < nx; ++ix) {
            offsets.push_back({static_cast<std::int64_t>(ix * xs), 0});
        }
        return true;
    }
    case 3: {
        std::uint64_t ny = 0;
        std::uint64_t ys = 0;
        if (!readUInt(c, ny) || !readUInt(c, ys)) {
            return false;
        }
        offsets.clear();
        for (std::uint64_t iy = 0; iy < ny; ++iy) {
            offsets.push_back({0, static_cast<std::int64_t>(iy * ys)});
        }
        return true;
    }
    default: {
        OasCursor saved = c;
        if (!skipRepetition(c)) {
            c = saved;
            return false;
        }
        offsets.clear();
        offsets.push_back({0, 0});
        return true;
    }
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
        switch (t) {
        case 0: { std::uint64_t x; return readUInt(c, x); }
        case 1: { std::uint64_t x; return readUInt(c, x); }
        case 2: { std::uint64_t d; return readUInt(c, d); }
        case 3: { std::uint64_t d; return readUInt(c, d); }
        case 4: { std::uint64_t n, d; return readUInt(c, n) && readUInt(c, d); }
        case 5: { std::uint64_t n, d; return readUInt(c, n) && readUInt(c, d); }
        case 6: { if (!c.has(4)) return false; c.p += 4; return true; }
        case 7: { if (!c.has(8)) return false; c.p += 8; return true; }
        default: return false;
        }
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

    OasImportContext *                                  geom = nullptr;
    std::uint64_t                                       modalLayer = 0;
    std::uint64_t                                       modalDatatype = 0;
    std::uint64_t                                       modalWidth = 0;
    std::uint64_t                                       modalHeight = 0;
    std::uint64_t                                       modalPathHalfwidth = 0;
    std::int64_t                                        modalGeomX = 0;
    std::int64_t                                        modalGeomY = 0;
    std::int64_t                                        modalPlacementX = 0;
    std::int64_t                                        modalPlacementY = 0;
    bool                                                xyAbsolute = true;
    bool                                                tablesAtEnd = false;
    OasArray                                            modalArray;
    std::shared_ptr<const std::vector<Point>>          modalOffsets;
    std::int32_t                                        modalPathBgn = 0;
    std::int32_t                                        modalPathEnd = 0;
    std::uint64_t                                       modalCtrap = 0;
    std::uint64_t                                       modalRadius = 0;
    std::shared_ptr<std::vector<std::string>>           textByRef = std::make_shared<std::vector<std::string>>();
    std::uint64_t                                       nextTextStringRef = 0;
    std::string                                         modalText;
    std::uint64_t                                       modalTextHeight = 0;
    std::uint64_t                                       modalTextLayer = 0;
    std::uint64_t                                       modalTextType = 0;
    std::int64_t                                        modalTextX = 0;
    std::int64_t                                        modalTextY = 0;
    std::vector<Point>                                  modalPolygonPoints;
    std::vector<Point>                                  modalPathPoints;

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

static std::string cellRefPlaceholder(std::uint64_t rn)
{
    return std::string("#oasref:") + std::to_string(rn);
}

static void retargetHierarchyName(OasHierarchy &out, const std::string &from, const std::string &to)
{
    if (from == to) {
        return;
    }
    if (out.allCells.erase(from) > 0) {
        out.allCells.insert(to);
    }
    auto found = out.children.find(from);
    if (found != out.children.end()) {
        auto existing = out.children.find(to);
        if (existing == out.children.end()) {
            out.children.emplace(to, std::move(found->second));
        } else {
            existing->second.insert(existing->second.end(), found->second.begin(), found->second.end());
        }
        out.children.erase(found);
    }
    for (auto &entry : out.children) {
        for (std::string &child : entry.second) {
            if (child == from) {
                child = to;
            }
        }
    }
}

static std::string cellNameForRef(OasParseState &st, std::uint64_t rn)
{
    const std::string &known = mapValue(st.cellNameByRef, rn);
    if (!known.empty()) {
        return known;
    }
    const std::string placeholder = cellRefPlaceholder(rn);
    st.cellNameByRef.emplace(rn, placeholder);
    return placeholder;
}

static void bindCellRefName(OasParseState &st, OasHierarchy &out, std::uint64_t rn, const std::string &name)
{
    if (name.empty()) {
        return;
    }
    const auto it = st.cellNameByRef.find(rn);
    const std::string previous = it == st.cellNameByRef.end() ? std::string() : it->second;
    st.cellNameByRef[rn] = name;
    if (previous.empty() || previous == name) {
        out.allCells.insert(name);
        out.children[name];
        return;
    }
    retargetHierarchyName(out, previous, name);
    out.allCells.insert(name);
    out.children[name];
    if (st.currentCell == previous) {
        st.currentCell = name;
    }
    if (st.modalPlacementCell == previous) {
        st.modalPlacementCell = name;
    }
    if (st.geom != nullptr) {
        st.geom->renameCell(previous, name);
    }
}

static std::string textRefPlaceholder(std::uint64_t rn)
{
    return std::string("\x01oastext:") + std::to_string(rn);
}

static void bindTextRef(OasParseState &st, std::uint64_t rn, std::string name)
{
    if (name.empty() || st.textByRef == nullptr) {
        return;
    }
    if (st.textByRef->size() <= rn) {
        st.textByRef->resize(static_cast<std::size_t>(rn) + 1);
    }
    const std::string previous = (*st.textByRef)[static_cast<std::size_t>(rn)];
    (*st.textByRef)[static_cast<std::size_t>(rn)] = std::move(name);
    if (st.modalText == previous && !previous.empty()) {
        st.modalText = (*st.textByRef)[static_cast<std::size_t>(rn)];
    }
}

static std::string textForRef(OasParseState &st, std::uint64_t rn)
{
    if (st.textByRef != nullptr && rn < st.textByRef->size()) {
        const std::string &known = (*st.textByRef)[static_cast<std::size_t>(rn)];
        if (!known.empty()) {
            return known;
        }
    }
    return textRefPlaceholder(rn);
}

static bool readModalCoord(OasCursor &c, bool absolute, std::int64_t &modal)
{
    std::int64_t value = 0;
    if (!readSInt(c, value)) {
        return false;
    }
    modal = absolute ? value : modal + value;
    return true;
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

static Orient orientFromOasis(int angle, bool mirror)
{
    angle &= 3;
    if (!mirror) {
        switch (angle) {
        case 1: return Orient::R90;
        case 2: return Orient::R180;
        case 3: return Orient::R270;
        default: return Orient::R0;
        }
    }
    switch (angle) {
    case 1: return Orient::MY90;
    case 2: return Orient::MX;
    case 3: return Orient::MX90;
    default: return Orient::MY;
    }
}

static void placeOutline(OasParseState &st, std::vector<Point> points, const OasArray &rep, bool repeated)
{
    if (st.geom == nullptr || points.size() < 3) {
        return;
    }
    for (Point &pt : points) {
        pt.x += st.modalGeomX;
        pt.y += st.modalGeomY;
    }
    st.geom->setModalLayer(st.modalLayer, st.modalDatatype);
    const std::vector<Point> *copies = nullptr;
    if (repeated && st.modalOffsets && st.modalOffsets->size() > 1 && rep.columns <= 1 && rep.rows <= 1) {
        copies = st.modalOffsets.get();
    }
    std::int64_t boxX = 0;
    std::int64_t boxY = 0;
    std::uint64_t boxW = 0;
    std::uint64_t boxH = 0;
    if (polygonIsBox(points, boxX, boxY, boxW, boxH) &&
        (rep.columns > 1 || rep.rows > 1 || copies != nullptr)) {
        st.geom->addRectangle(boxX, boxY, boxW, boxH, rep.columns, rep.rows, rep.stepX, rep.stepY, copies,
                              rep.colY, rep.rowX);
        return;
    }
    st.geom->addPolygon(points, rep.columns, rep.rows, rep.stepX, rep.stepY, copies, rep.colY, rep.rowX);
}

static void resetCellModals(OasParseState &st)
{
    st.xyAbsolute = true;
    st.modalLayer = 0;
    st.modalDatatype = 0;
    st.modalWidth = 0;
    st.modalHeight = 0;
    st.modalPathHalfwidth = 0;
    st.modalGeomX = 0;
    st.modalGeomY = 0;
    st.modalPolygonPoints.clear();
    st.modalPathPoints.clear();
    st.modalPlacementCell.clear();
    st.modalPlacementX = 0;
    st.modalPlacementY = 0;
    st.modalText.clear();
    st.modalTextHeight = 0;
    st.modalTextLayer = 0;
    st.modalTextType = 0;
    st.modalTextX = 0;
    st.modalTextY = 0;
    st.modalArray = {};
    st.modalOffsets.reset();
    st.modalPathBgn = 0;
    st.modalPathEnd = 0;
    st.modalCtrap = 0;
    st.modalRadius = 0;
}

static void placeCell(OasParseState &st, const std::string &name, std::int64_t x, std::int64_t y,
                      const OasArray &rep, bool repeated, Orient orient)
{
    if (st.geom == nullptr || name.empty()) {
        return;
    }
    if (repeated && st.modalOffsets && st.modalOffsets->size() > 1 && rep.columns <= 1 && rep.rows <= 1) {
        st.geom->addPlacement(name, x, y, 0, 0, 0, 0, orient, 0, 0, st.modalOffsets.get());
        return;
    }
    st.geom->addPlacement(name, x, y, rep.columns, rep.rows, rep.stepX, rep.stepY, orient, rep.colY, rep.rowX);
}

static bool readTrapezoidRecord(OasCursor &c, OasParseState &st, int recordId)
{
    std::uint8_t info = 0;
    if (!readByte(c, info)) {
        return false;
    }
    if ((info & 0x01) != 0) {
        std::uint64_t v = 0;
        if (!readUInt(c, v)) return false;
        st.modalLayer = v;
    }
    if ((info & 0x02) != 0) {
        std::uint64_t v = 0;
        if (!readUInt(c, v)) return false;
        st.modalDatatype = v;
    }
    if ((info & 0x40) != 0 && !readUInt(c, st.modalWidth)) return false;
    if ((info & 0x20) != 0 && !readUInt(c, st.modalHeight)) return false;

    std::int64_t deltaA = 0;
    std::int64_t deltaB = 0;
    if ((recordId == 23 || recordId == 24) && !readSInt(c, deltaA)) return false;
    if ((recordId == 23 || recordId == 25) && !readSInt(c, deltaB)) return false;
    if ((info & 0x10) != 0 && !readModalCoord(c, st.xyAbsolute, st.modalGeomX)) return false;
    if ((info & 0x08) != 0 && !readModalCoord(c, st.xyAbsolute, st.modalGeomY)) return false;
    if ((info & 0x04) != 0 && !readArrayRep(c, st.modalArray, st.modalOffsets)) return false;

    const std::int64_t w = static_cast<std::int64_t>(st.modalWidth);
    const std::int64_t h = static_cast<std::int64_t>(st.modalHeight);
    std::vector<Point> points(4);
    if ((info & 0x80) != 0) {
        points[0] = Point{0, std::max<std::int64_t>(deltaA, 0)};
        points[1] = Point{0, h + std::min<std::int64_t>(deltaB, 0)};
        points[2] = Point{w, h - std::max<std::int64_t>(deltaB, 0)};
        points[3] = Point{w, -std::min<std::int64_t>(deltaA, 0)};
    } else {
        points[0] = Point{std::max<std::int64_t>(deltaA, 0), h};
        points[1] = Point{w + std::min<std::int64_t>(deltaB, 0), h};
        points[2] = Point{w - std::max<std::int64_t>(deltaB, 0), 0};
        points[3] = Point{-std::min<std::int64_t>(deltaA, 0), 0};
    }
    placeOutline(st, points, (info & 0x04) != 0 ? st.modalArray : OasArray{}, (info & 0x04) != 0);
    return true;
}

static bool ctrapPoints(std::uint64_t type, std::int64_t w, std::int64_t h, std::vector<Point> &points,
                        std::uint64_t &bboxW, std::uint64_t &bboxH)
{
    static const int kTable[26][4][4] = {
        {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, -1, 0, 1}, {1, 0, 0, 0}},
        {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 0, 0, 1}, {1, -1, 0, 0}},
        {{0, 0, 0, 0}, {0, 1, 0, 1}, {1, 0, 0, 1}, {1, 0, 0, 0}},
        {{0, 1, 0, 0}, {0, 0, 0, 1}, {1, 0, 0, 1}, {1, 0, 0, 0}},
        {{0, 0, 0, 0}, {0, 1, 0, 1}, {1, -1, 0, 1}, {1, 0, 0, 0}},
        {{0, 1, 0, 0}, {0, 0, 0, 1}, {1, 0, 0, 1}, {1, -1, 0, 0}},
        {{0, 0, 0, 0}, {0, 1, 0, 1}, {1, 0, 0, 1}, {1, -1, 0, 0}},
        {{0, 1, 0, 0}, {0, 0, 0, 1}, {1, -1, 0, 1}, {1, 0, 0, 0}},
        {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 0, -1, 1}, {1, 0, 0, 0}},
        {{0, 0, 0, 0}, {0, 0, -1, 1}, {1, 0, 0, 1}, {1, 0, 0, 0}},
        {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 0, 0, 1}, {1, 0, 1, 0}},
        {{0, 0, 1, 0}, {0, 0, 0, 1}, {1, 0, 0, 1}, {1, 0, 0, 0}},
        {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 0, -1, 1}, {1, 0, 1, 0}},
        {{0, 0, 1, 0}, {0, 0, -1, 1}, {1, 0, 0, 1}, {1, 0, 0, 0}},
        {{0, 0, 0, 0}, {0, 0, -1, 1}, {1, 0, 0, 1}, {1, 0, 1, 0}},
        {{0, 0, 1, 0}, {0, 0, 0, 1}, {1, 0, -1, 1}, {1, 0, 0, 0}},
        {{0, 0, 0, 0}, {0, 0, 1, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}},
        {{0, 0, 0, 0}, {0, 0, 1, 0}, {1, 0, 1, 0}, {0, 0, 0, 0}},
        {{0, 0, 0, 0}, {1, 0, 1, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}},
        {{0, 0, 1, 0}, {1, 0, 1, 0}, {1, 0, 0, 0}, {0, 0, 1, 0}},
        {{0, 0, 0, 0}, {0, 1, 0, 1}, {0, 2, 0, 0}, {0, 0, 0, 0}},
        {{0, 0, 0, 1}, {0, 2, 0, 1}, {0, 1, 0, 0}, {0, 0, 0, 1}},
        {{0, 0, 0, 0}, {0, 0, 2, 0}, {1, 0, 1, 0}, {0, 0, 0, 0}},
        {{1, 0, 0, 0}, {0, 0, 1, 0}, {1, 0, 2, 0}, {1, 0, 0, 0}},
        {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 0, 0, 1}, {1, 0, 0, 0}},
        {{0, 0, 0, 0}, {0, 0, 1, 0}, {1, 0, 1, 0}, {1, 0, 0, 0}},
    };
    if (type >= 26) {
        return false;
    }
    points.clear();
    std::int64_t maxX = 0;
    std::int64_t maxY = 0;
    for (int i = 0; i < 4; ++i) {
        const int *m = kTable[type][i];
        const std::int64_t x = m[0] * w + m[1] * h;
        const std::int64_t y = m[2] * w + m[3] * h;
        points.push_back(Point{x, y});
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
    }
    if (points.back().x == points.front().x && points.back().y == points.front().y) {
        points.pop_back();
    }
    bboxW = static_cast<std::uint64_t>(std::max<std::int64_t>(maxX, 0));
    bboxH = static_cast<std::uint64_t>(std::max<std::int64_t>(maxY, 0));
    return points.size() >= 3;
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
    const std::uint8_t *recStart = c.p;

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
            if (st.geom != nullptr && x > 0) {
                st.geom->setDbuPerMicron(static_cast<double>(x));
            }
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
        st.tablesAtEnd = offsetFlag != 0;

        //  Flag 0: the six table-offset pairs are here. Flag 1: they follow END.
        if (!st.tablesAtEnd) {
            for (int i = 0; i < 12; ++i) {
                std::uint64_t tmp = 0;
                if (!readUInt(c, tmp)) goto done_fail;
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
        bindCellRefName(st, out, rn, name);
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
        bindCellRefName(st, out, rn, name);
        goto done_ok;
    }

    case 5: {
        std::string s;
        if (!readAString(c, s)) goto done_fail;
        bindTextRef(st, st.nextTextStringRef++, s);
        goto done_ok;
    }
    case 6: {
        std::string s;
        std::uint64_t rn = 0;
        if (!readAString(c, s)) goto done_fail;
        if (!readUInt(c, rn)) goto done_fail;
        if (st.nextTextStringRef <= rn) {
            st.nextTextStringRef = rn + 1;
        }
        bindTextRef(st, rn, s);
        goto done_ok;
    }
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
        const std::string name = cellNameForRef(st, rn);
        st.currentCell = name;
        out.allCells.insert(name);
        out.children[name];
        resetCellModals(st);
        if (st.geom != nullptr) {
            st.geom->beginCell(name);
        }
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
        resetCellModals(st);
        if (st.geom != nullptr) {
            st.geom->beginCell(name);
        }
        goto done_ok;
    }

    case 15:
        st.xyAbsolute = true;
        goto done_ok;

    case 16:
        st.xyAbsolute = false;
        goto done_ok;

    case 17: {
        if (st.currentCell.empty()) {
#ifdef OAS_DEBUG
#endif
            goto done_fail;
        }

        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        const bool R    = (info & 0x08) != 0;
        const bool Y    = (info & 0x10) != 0;
        const bool X    = (info & 0x20) != 0;
        const bool N    = (info & 0x40) != 0;
        const bool Cbit = (info & 0x80) != 0;

        std::string placedCell;

        if (Cbit) {
            if (N) {
                std::uint64_t rn = 0;
                if (!readUInt(c, rn)) goto done_fail;
                placedCell = cellNameForRef(st, rn);
            } else {
                if (!readNString(c, placedCell)) goto done_fail;
            }

            st.modalPlacementCell = placedCell;
        } else {
            placedCell = st.modalPlacementCell;
        }

        if (X) {
            if (!readModalCoord(c, st.xyAbsolute, st.modalPlacementX)) goto done_fail;
        }
        if (Y) {
            if (!readModalCoord(c, st.xyAbsolute, st.modalPlacementY)) goto done_fail;
        }
        const std::int64_t px = st.modalPlacementX;
        const std::int64_t py = st.modalPlacementY;
        if (R && !readArrayRep(c, st.modalArray, st.modalOffsets)) goto done_fail;
        const OasArray rep = R ? st.modalArray : OasArray{};

        if (!placedCell.empty()) {
            out.children[st.currentCell].push_back(placedCell);
            out.allCells.insert(placedCell);
            if (st.geom != nullptr) {
                placeCell(st, placedCell, px, py, rep, R,
                          orientFromOasis((info & 0x06) >> 1, (info & 0x01) != 0));
            }
        }

        goto done_ok;
    }

    case 18: {
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        const bool Flip = (info & 0x01) != 0;
        const bool Angle = (info & 0x02) != 0;
        const bool Mag = (info & 0x04) != 0;
        const bool R = (info & 0x08) != 0;
        const bool Y = (info & 0x10) != 0;
        const bool X = (info & 0x20) != 0;
        const bool N = (info & 0x40) != 0;
        const bool Cbit = (info & 0x80) != 0;
        (void)Flip;

        std::string placedCell;
        if (Cbit) {
            if (N) {
                std::uint64_t rn = 0;
                if (!readUInt(c, rn)) goto done_fail;
                placedCell = cellNameForRef(st, rn);
            } else if (!readNString(c, placedCell)) {
                goto done_fail;
            }
            st.modalPlacementCell = placedCell;
        } else {
            placedCell = st.modalPlacementCell;
        }
        if (Mag && !skipReal(c)) goto done_fail;
        if (Angle && !skipReal(c)) goto done_fail;
        if (X && !readModalCoord(c, st.xyAbsolute, st.modalPlacementX)) goto done_fail;
        if (Y && !readModalCoord(c, st.xyAbsolute, st.modalPlacementY)) goto done_fail;
        if (R && !readArrayRep(c, st.modalArray, st.modalOffsets)) goto done_fail;
        const OasArray rep = R ? st.modalArray : OasArray{};

        if (!placedCell.empty()) {
            out.children[st.currentCell].push_back(placedCell);
            out.allCells.insert(placedCell);
            if (st.geom != nullptr) {
                placeCell(st, placedCell, st.modalPlacementX, st.modalPlacementY, rep, R, Orient::R0);
            }
        }
        goto done_ok;
    }

    case 19: { // TEXT
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;

        const bool L    = (info & 0x01) != 0;
        const bool T    = (info & 0x02) != 0;
        const bool R    = (info & 0x04) != 0;
        const bool Y    = (info & 0x08) != 0;
        const bool X    = (info & 0x10) != 0;
        const bool N    = (info & 0x20) != 0;
        const bool Cbit = (info & 0x40) != 0;

        if (Cbit) {
            if (N) {
                std::uint64_t rn = 0;
                if (!readUInt(c, rn)) goto done_fail;
                st.modalText = textForRef(st, rn);
            } else {
                std::string s;
                if (!readAString(c, s)) goto done_fail;
                st.modalText = s;
            }
        }

        if (L) {
            std::uint64_t tlayer = 0;
            if (!readUInt(c, tlayer)) goto done_fail;
            st.modalTextLayer = tlayer;
        }
        if (T) {
            std::uint64_t ttype = 0;
            if (!readUInt(c, ttype)) goto done_fail;
            st.modalTextType = ttype;
            st.modalTextHeight = static_cast<std::uint32_t>(ttype);
        }

        if (X && !readModalCoord(c, st.xyAbsolute, st.modalTextX)) goto done_fail;
        if (Y && !readModalCoord(c, st.xyAbsolute, st.modalTextY)) goto done_fail;

        if (st.geom != nullptr) {
            st.geom->setModalLayer(st.modalTextLayer, st.modalTextType);
            if (R && !readArrayRep(c, st.modalArray, st.modalOffsets)) goto done_fail;
            st.geom->addText(st.modalText, st.modalTextX, st.modalTextY, st.modalTextHeight);
        } else if (R && !readArrayRep(c, st.modalArray, st.modalOffsets)) {
            goto done_fail;
        }

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
        const bool Square = (info & 0x80) != 0;

        if (L) {
            std::uint64_t v = 0;
            if (!readUInt(c, v)) goto done_fail;
            st.modalLayer = v;
        }
        if (D) {
            std::uint64_t v = 0;
            if (!readUInt(c, v)) goto done_fail;
            st.modalDatatype = v;
        }
        if (W) {
            if (!readUInt(c, st.modalWidth)) goto done_fail;
        }
        if (Square) {
            st.modalHeight = st.modalWidth;
        } else if (H) {
            if (!readUInt(c, st.modalHeight)) goto done_fail;
        }
        if (X) {
            if (!readModalCoord(c, st.xyAbsolute, st.modalGeomX)) goto done_fail;
        }
        if (Y) {
            if (!readModalCoord(c, st.xyAbsolute, st.modalGeomY)) goto done_fail;
        }
        const std::int64_t gx = st.modalGeomX;
        const std::int64_t gy = st.modalGeomY;

        if (st.geom != nullptr) {
            st.geom->setModalLayer(st.modalLayer, st.modalDatatype);
            if (R && !readArrayRep(c, st.modalArray, st.modalOffsets)) goto done_fail;
            const OasArray rep = R ? st.modalArray : OasArray{};
            const std::vector<Point> *copies = nullptr;
            if (R && st.modalOffsets && st.modalOffsets->size() > 1 && rep.columns <= 1 && rep.rows <= 1) {
                copies = st.modalOffsets.get();
            }
            st.geom->addRectangle(gx, gy, st.modalWidth, st.modalHeight, rep.columns, rep.rows, rep.stepX, rep.stepY,
                                  copies, rep.colY, rep.rowX);
        } else if (R && !readArrayRep(c, st.modalArray, st.modalOffsets)) {
            goto done_fail;
        }

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

        if (L) {
            std::uint64_t v = 0;
            if (!readUInt(c, v)) goto done_fail;
            st.modalLayer = v;
        }
        if (D) {
            std::uint64_t v = 0;
            if (!readUInt(c, v)) goto done_fail;
            st.modalDatatype = v;
        }

        std::vector<Point> points;
        if (P) {
            if (!readPointList(c, true, points)) goto done_fail;
            st.modalPolygonPoints = points;
        } else {
            points = st.modalPolygonPoints;
        }
        if (X && !readModalCoord(c, st.xyAbsolute, st.modalGeomX)) goto done_fail;
        if (Y && !readModalCoord(c, st.xyAbsolute, st.modalGeomY)) goto done_fail;
        if (R && !readArrayRep(c, st.modalArray, st.modalOffsets)) goto done_fail;
        const OasArray rep = R ? st.modalArray : OasArray{};
        placeOutline(st, points, rep, R);
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

        if (L) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; st.modalLayer = v; }
        if (D) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; st.modalDatatype = v; }
        if (W) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; st.modalPathHalfwidth = v; }
        if (E) {
            std::uint64_t scheme = 0;
            if (!readUInt(c, scheme)) goto done_fail;
            if ((scheme & 0x0c) == 0x0c) {
                std::int64_t ext = 0;
                if (!readSInt(c, ext)) goto done_fail;
                st.modalPathBgn = static_cast<std::int32_t>(ext);
            } else if ((scheme & 0x0c) == 0x04) {
                st.modalPathBgn = 0;
            } else if ((scheme & 0x0c) == 0x08) {
                st.modalPathBgn = static_cast<std::int32_t>(st.modalPathHalfwidth);
            }
            if ((scheme & 0x03) == 0x03) {
                std::int64_t ext = 0;
                if (!readSInt(c, ext)) goto done_fail;
                st.modalPathEnd = static_cast<std::int32_t>(ext);
            } else if ((scheme & 0x03) == 0x01) {
                st.modalPathEnd = 0;
            } else if ((scheme & 0x03) == 0x02) {
                st.modalPathEnd = static_cast<std::int32_t>(st.modalPathHalfwidth);
            }
        }
        std::vector<Point> pathPoints;
        if (P) {
            if (!readPointList(c, false, pathPoints)) goto done_fail;
            st.modalPathPoints = pathPoints;
        } else {
            pathPoints = st.modalPathPoints;
        }
        if (X) {
            if (!readModalCoord(c, st.xyAbsolute, st.modalGeomX)) goto done_fail;
        }
        if (Y) {
            if (!readModalCoord(c, st.xyAbsolute, st.modalGeomY)) goto done_fail;
        }
        if (R && !readArrayRep(c, st.modalArray, st.modalOffsets)) goto done_fail;
        const OasArray pathRep = R ? st.modalArray : OasArray{};
        if (st.geom != nullptr && pathPoints.size() >= 2) {
            for (Point &pt : pathPoints) {
                pt.x += st.modalGeomX;
                pt.y += st.modalGeomY;
            }
            st.geom->setModalLayer(st.modalLayer, st.modalDatatype);
            const std::uint64_t fullWidth = st.modalPathHalfwidth > (0xffffffffULL / 2) ? 0xffffffffULL : st.modalPathHalfwidth * 2;
            const std::vector<Point> *copies = nullptr;
            if (R && st.modalOffsets && st.modalOffsets->size() > 1 && pathRep.columns <= 1 && pathRep.rows <= 1) {
                copies = st.modalOffsets.get();
            }
            st.geom->addPath(pathPoints, static_cast<std::uint32_t>(fullWidth),
                             pathRep.columns, pathRep.rows, pathRep.stepX, pathRep.stepY,
                             st.modalPathBgn, st.modalPathEnd, false, copies, pathRep.colY, pathRep.rowX);
        }
        goto done_ok;
    }

    case 23:
    case 24:
    case 25:
        if (!readTrapezoidRecord(c, st, static_cast<int>(recId))) goto done_fail;
        goto done_ok;

    case 26: {
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;
        if ((info & 0x01) != 0) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; st.modalLayer = v; }
        if ((info & 0x02) != 0) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; st.modalDatatype = v; }
        if ((info & 0x80) != 0) { if (!readUInt(c, st.modalCtrap)) goto done_fail; }
        if ((info & 0x40) != 0) { if (!readUInt(c, st.modalWidth)) goto done_fail; }
        if ((info & 0x20) != 0) { if (!readUInt(c, st.modalHeight)) goto done_fail; }
        if ((info & 0x10) != 0 && !readModalCoord(c, st.xyAbsolute, st.modalGeomX)) goto done_fail;
        if ((info & 0x08) != 0 && !readModalCoord(c, st.xyAbsolute, st.modalGeomY)) goto done_fail;
        if ((info & 0x04) != 0 && !readArrayRep(c, st.modalArray, st.modalOffsets)) goto done_fail;
        std::vector<Point> ctrap;
        std::uint64_t bboxW = 0;
        std::uint64_t bboxH = 0;
        if (!ctrapPoints(st.modalCtrap, static_cast<std::int64_t>(st.modalWidth),
                         static_cast<std::int64_t>(st.modalHeight), ctrap, bboxW, bboxH)) {
            goto done_fail;
        }
        st.modalWidth = bboxW;
        st.modalHeight = bboxH;
        placeOutline(st, ctrap, (info & 0x04) != 0 ? st.modalArray : OasArray{}, (info & 0x04) != 0);
        goto done_ok;
    }

    case 27: {
        std::uint8_t info = 0;
        if (!readByte(c, info)) goto done_fail;
        if ((info & 0x01) != 0) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; st.modalLayer = v; }
        if ((info & 0x02) != 0) { std::uint64_t v; if (!readUInt(c, v)) goto done_fail; st.modalDatatype = v; }
        if ((info & 0x20) != 0) { if (!readUInt(c, st.modalRadius)) goto done_fail; }
        if ((info & 0x10) != 0 && !readModalCoord(c, st.xyAbsolute, st.modalGeomX)) goto done_fail;
        if ((info & 0x08) != 0 && !readModalCoord(c, st.xyAbsolute, st.modalGeomY)) goto done_fail;
        if ((info & 0x04) != 0 && !readArrayRep(c, st.modalArray, st.modalOffsets)) goto done_fail;
        if (st.geom != nullptr && st.modalRadius > 0) {
            const OasArray rep = (info & 0x04) != 0 ? st.modalArray : OasArray{};
            const std::vector<Point> *copies = nullptr;
            if ((info & 0x04) != 0 && st.modalOffsets && st.modalOffsets->size() > 1 &&
                rep.columns <= 1 && rep.rows <= 1) {
                copies = st.modalOffsets.get();
            }
            st.geom->setModalLayer(st.modalLayer, st.modalDatatype);
            const std::uint64_t diameter = st.modalRadius > (0xffffffffULL / 2) ? 0xffffffffULL : st.modalRadius * 2;
            st.geom->addPath(std::vector<Point>{Point{st.modalGeomX, st.modalGeomY}},
                             static_cast<std::uint32_t>(diameter),
                             rep.columns, rep.rows, rep.stepX, rep.stepY,
                             static_cast<std::int32_t>(st.modalRadius),
                             static_cast<std::int32_t>(st.modalRadius),
                             true, copies, rep.colY, rep.rowX);
        }
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
        if (R) { if (!readArrayRep(c, st.modalArray, st.modalOffsets)) goto done_fail; }

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
        st.currentCell              = subSt.currentCell;
        st.modalLayer               = subSt.modalLayer;
        st.modalDatatype            = subSt.modalDatatype;
        st.modalWidth               = subSt.modalWidth;
        st.modalHeight              = subSt.modalHeight;
        st.modalPathHalfwidth       = subSt.modalPathHalfwidth;
        st.modalGeomX               = subSt.modalGeomX;
        st.modalGeomY               = subSt.modalGeomY;
        st.modalText                = subSt.modalText;
        st.modalTextLayer           = subSt.modalTextLayer;
        st.modalTextType            = subSt.modalTextType;
        st.modalTextX               = subSt.modalTextX;
        st.modalTextY               = subSt.modalTextY;
        st.modalTextHeight          = subSt.modalTextHeight;
        st.modalPolygonPoints       = subSt.modalPolygonPoints;
        st.modalPathPoints          = subSt.modalPathPoints;
        st.modalPlacementCell       = subSt.modalPlacementCell;
        st.modalPlacementX          = subSt.modalPlacementX;
        st.modalPlacementY          = subSt.modalPlacementY;
        st.xyAbsolute               = subSt.xyAbsolute;
        st.modalArray               = subSt.modalArray;
        st.modalOffsets             = subSt.modalOffsets;
        st.modalPathBgn             = subSt.modalPathBgn;
        st.modalPathEnd             = subSt.modalPathEnd;
        st.modalCtrap               = subSt.modalCtrap;
        st.modalRadius              = subSt.modalRadius;
        st.nextTextStringRef        = subSt.nextTextStringRef;
        st.nextCellNameRef          = subSt.nextCellNameRef;

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
    if (errors.empty()) {
        const std::uint64_t off = (recStart >= st.fileBase && recStart < st.fileEnd)
            ? static_cast<std::uint64_t>(recStart - st.fileBase)
            : 0;
        errors.push_back("OAS parse failed at record " + std::to_string(recId) + " offset " + std::to_string(off));
    }
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

        if (c.p == before) {
            errors.push_back("OAS record made no progress");
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
    : m_fileName(std::move(fileName))
{
}

bool OasReader::readHierarchy(OasHierarchy &out)
{
    out.topCells.clear();
    out.children.clear();
    out.allCells.clear();
    m_errors.clear();

    if (m_fileName.empty()) {
        m_errors.push_back("Empty OASIS filename.");
        return false;
    }

    std::ifstream input(m_fileName, std::ios::binary);
    if (!input) {
        m_errors.push_back("Failed to open OASIS for read: '" + m_fileName + "'");
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff szOff = input.tellg();
    if (szOff < 16) {
        m_errors.push_back("OASIS file too small: '" + m_fileName + "'");
        return false;
    }
    const auto sz = static_cast<std::size_t>(szOff);
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> fileBytes(sz);
    input.read(reinterpret_cast<char *>(fileBytes.data()), static_cast<std::streamsize>(sz));
    if (!input) {
        m_errors.push_back("Failed to read OASIS: '" + m_fileName + "'");
        return false;
    }

    const std::uint8_t *base = fileBytes.data();
    const std::uint8_t *const fileEnd = base + sz;

    static const char magic[] = "%SEMI-OASIS";
    if (std::memcmp(base, magic, sizeof(magic) - 1) != 0) {
        m_errors.push_back("Not an OASIS file (missing %SEMI-OASIS magic).");
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

    if (!parseBuffer(c, out, m_errors, st)) {
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

bool OasReader::importDatabase(Database &db, const std::string &libName, double defaultDbuPerMicron)
{
    m_errors.clear();
    m_warnings.clear();

    OasHierarchy hierarchy;
    OasImportContext geomCtx(db, libName, defaultDbuPerMicron);
    if (m_fileName.empty()) {
        m_errors.push_back("Empty OASIS filename.");
        return false;
    }

    std::ifstream input(m_fileName, std::ios::binary);
    if (!input) {
        m_errors.push_back("Failed to open OASIS for read: '" + m_fileName + "'");
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff szOff = input.tellg();
    if (szOff < 16) {
        m_errors.push_back("OASIS file too small: '" + m_fileName + "'");
        return false;
    }
    const auto sz = static_cast<std::size_t>(szOff);
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> fileBytes(sz);
    input.read(reinterpret_cast<char *>(fileBytes.data()), static_cast<std::streamsize>(sz));
    if (!input) {
        m_errors.push_back("Failed to read OASIS: '" + m_fileName + "'");
        return false;
    }

    const std::uint8_t *base = fileBytes.data();
    const std::uint8_t *const fileEnd = base + sz;

    static const char magic[] = "%SEMI-OASIS";
    if (std::memcmp(base, magic, sizeof(magic) - 1) != 0) {
        m_errors.push_back("Not an OASIS file (missing %SEMI-OASIS magic).");
        return false;
    }

    const std::uint8_t *p = base + (sizeof(magic) - 1);
    while (p < fileEnd && (*p == '\r' || *p == '\n')) {
        ++p;
    }

    OasCursor cursor;
    cursor.p = p;
    cursor.end = fileEnd;

    OasParseState state;
    state.fileBase = base;
    state.fileEnd = fileEnd;
    state.geom = &geomCtx;

    if (!parseBuffer(cursor, hierarchy, m_errors, state)) {
        return false;
    }

    dumpRepStats();
    if (repStatsOnly()) {
        return true;
    }
    geomCtx.resolveTextPlaceholders(state.textByRef ? *state.textByRef : std::vector<std::string>{});

    m_cellNamesByRef = state.cellNameByRef;
    m_warnings.insert(m_warnings.end(), geomCtx.warnings().begin(), geomCtx.warnings().end());
    geomCtx.finalize();
    return true;
}

} // namespace room
