// Per-cell geometry check of a GDSII file against a ROOM file.
// Boundaries and paths are fingerprinted the same way the streaming importer stores them.
// Texts and placements are not part of the check. One cell is compared at a time.

#include "serialization.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct Fp {
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    bool operator<(const Fp &other) const { return a < other.a || (a == other.a && b < other.b); }
    bool operator==(const Fp &other) const { return a == other.a && b == other.b; }
};

struct Hasher {
    std::uint64_t a = 0x243f6a8885a308d3ull;
    std::uint64_t b = 0x13198a2e03707344ull;
    void add(std::uint64_t value)
    {
        a = (a ^ value) * 0xbf58476d1ce4e5b9ull;
        b = (b ^ (value + 0x94d049bb133111ebull)) * 0x94d049bb133111ebull;
    }
    Fp finish() const { return Fp{a, b}; }
};

std::uint16_t be16(const std::uint8_t *p)
{
    return (static_cast<std::uint16_t>(p[0]) << 8) | p[1];
}

std::int32_t be32(const std::uint8_t *p)
{
    return (static_cast<std::int32_t>(p[0]) << 24) | (static_cast<std::int32_t>(p[1]) << 16) |
           (static_cast<std::int32_t>(p[2]) << 8) | static_cast<std::int32_t>(p[3]);
}

std::string decodeGdsString(const std::uint8_t *payload, int len)
{
    while (len > 0 && payload[len - 1] == 0) {
        --len;
    }
    return std::string(reinterpret_cast<const char *>(payload), static_cast<std::size_t>(len));
}

struct GdsSpan {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
};

struct Point32 {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

bool asRect(const std::vector<Point32> &points, std::int64_t &llx, std::int64_t &lly, std::int64_t &urx, std::int64_t &ury)
{
    std::size_t n = points.size();
    if (n == 5 && points.front().x == points.back().x && points.front().y == points.back().y) {
        n = 4;
    }
    if (n != 4) {
        return false;
    }
    llx = urx = points[0].x;
    lly = ury = points[0].y;
    for (std::size_t i = 1; i < n; ++i) {
        llx = std::min<std::int64_t>(llx, points[i].x);
        lly = std::min<std::int64_t>(lly, points[i].y);
        urx = std::max<std::int64_t>(urx, points[i].x);
        ury = std::max<std::int64_t>(ury, points[i].y);
    }
    if (llx == urx || lly == ury) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const bool onX = points[i].x == llx || points[i].x == urx;
        const bool onY = points[i].y == lly || points[i].y == ury;
        if (!onX || !onY) {
            return false;
        }
    }
    return true;
}

Fp hashRect(std::uint16_t layer, std::uint16_t dataType, std::int64_t llx, std::int64_t lly, std::int64_t urx, std::int64_t ury)
{
    Hasher hasher;
    hasher.add(1);
    hasher.add(layer);
    hasher.add(dataType);
    hasher.add(static_cast<std::uint64_t>(llx));
    hasher.add(static_cast<std::uint64_t>(lly));
    hasher.add(static_cast<std::uint64_t>(urx));
    hasher.add(static_cast<std::uint64_t>(ury));
    return hasher.finish();
}

Fp hashPoints(std::uint64_t kind, std::uint16_t layer, std::uint16_t dataType, std::uint32_t width,
              const std::vector<Point32> &points, std::size_t n)
{
    Hasher hasher;
    hasher.add(kind);
    hasher.add(layer);
    hasher.add(dataType);
    hasher.add(width);
    hasher.add(n);
    for (std::size_t i = 0; i < n; ++i) {
        hasher.add(static_cast<std::uint64_t>(points[i].x));
        hasher.add(static_cast<std::uint64_t>(points[i].y));
    }
    return hasher.finish();
}

std::unordered_map<std::string, GdsSpan> indexGds(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open GDS: " + path);
    }
    std::unordered_map<std::string, GdsSpan> cells;
    std::vector<char> chunk(8 * 1024 * 1024);
    std::vector<std::uint8_t> hold;
    std::uint64_t base = 0;
    std::string name;
    std::uint64_t begin = 0;
    bool inCell = false;

    auto handle = [&](std::uint16_t recType, const std::uint8_t *payload, int payloadLen, std::uint64_t recPos, unsigned recLen) {
        constexpr std::uint16_t GDS_BGNSTR = 0x0502;
        constexpr std::uint16_t GDS_STRNAME = 0x0606;
        constexpr std::uint16_t GDS_ENDSTR = 0x0700;
        if (recType == GDS_BGNSTR) {
            inCell = true;
            begin = recPos;
            name.clear();
        } else if (recType == GDS_STRNAME && inCell) {
            name = decodeGdsString(payload, payloadLen);
        } else if (recType == GDS_ENDSTR && inCell) {
            if (name.empty()) {
                name = "cell_" + std::to_string(cells.size());
            }
            cells[name] = GdsSpan{begin, recPos + recLen};
            inCell = false;
        }
    };

    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const std::size_t nread = static_cast<std::size_t>(in.gcount());
        if (nread == 0) {
            break;
        }
        hold.insert(hold.end(), reinterpret_cast<std::uint8_t *>(chunk.data()),
                    reinterpret_cast<std::uint8_t *>(chunk.data()) + nread);
        std::size_t i = 0;
        while (i + 4 <= hold.size()) {
            const unsigned len = (unsigned(hold[i]) << 8) | hold[i + 1];
            const unsigned recType = (unsigned(hold[i + 2]) << 8) | hold[i + 3];
            if (len < 4 || (len & 1) || i + len > hold.size()) {
                break;
            }
            handle(static_cast<std::uint16_t>(recType), hold.data() + i + 4, static_cast<int>(len - 4), base + i, len);
            i += len;
        }
        if (i == 0 && hold.size() > 1024 * 1024) {
            throw std::runtime_error("Malformed GDS record");
        }
        base += i;
        hold.erase(hold.begin(), hold.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return cells;
}

std::vector<Fp> fingerprintsFromGds(const std::string &path, const GdsSpan &span)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot reopen GDS");
    }
    in.seekg(static_cast<std::streamoff>(span.begin));
    const std::uint64_t nbytes = span.end - span.begin;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(nbytes));
    if (!in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Failed to read GDS cell");
    }

    enum class Kind { None, Boundary, Path };
    Kind kind = Kind::None;
    std::uint16_t layer = 0;
    std::uint16_t dataType = 0;
    std::uint32_t width = 0;
    std::vector<Point32> points;
    std::vector<Fp> out;

    auto commit = [&]() {
        if (kind == Kind::Boundary) {
            std::size_t n = points.size();
            if (n >= 2 && points.front().x == points.back().x && points.front().y == points.back().y) {
                --n;
            }
            if (n >= 3) {
                std::int64_t llx = 0, lly = 0, urx = 0, ury = 0;
                if (asRect(points, llx, lly, urx, ury)) {
                    out.push_back(hashRect(layer, dataType, llx, lly, urx, ury));
                } else {
                    out.push_back(hashPoints(2, layer, dataType, 0, points, n));
                }
            }
        } else if (kind == Kind::Path && !points.empty()) {
            out.push_back(hashPoints(3, layer, dataType, width, points, points.size()));
        }
        kind = Kind::None;
        layer = 0;
        dataType = 0;
        width = 0;
        points.clear();
    };

    std::size_t i = 0;
    while (i + 4 <= bytes.size()) {
        const unsigned len = (unsigned(bytes[i]) << 8) | bytes[i + 1];
        const unsigned recType = (unsigned(bytes[i + 2]) << 8) | bytes[i + 3];
        if (len < 4 || (len & 1) || i + len > bytes.size()) {
            break;
        }
        const std::uint8_t *payload = bytes.data() + i + 4;
        const int payloadLen = static_cast<int>(len - 4);
        switch (recType) {
        case 0x0800:
        case 0x2D00:
            commit();
            kind = Kind::Boundary;
            break;
        case 0x0900:
            commit();
            kind = Kind::Path;
            break;
        case 0x0C00:
        case 0x0A00:
        case 0x0B00:
            commit();
            break;
        case 0x0D02:
        case 0x1602:
        case 0x2E02:
            if (payloadLen >= 2) {
                layer = be16(payload);
            }
            break;
        case 0x0E02:
            if (payloadLen >= 2) {
                dataType = be16(payload);
            }
            break;
        case 0x0F03:
            if (payloadLen >= 4) {
                width = static_cast<std::uint32_t>(be32(payload));
            } else if (payloadLen >= 2) {
                width = be16(payload);
            }
            break;
        case 0x1003:
            points.clear();
            for (int p = 0; p + 8 <= payloadLen; p += 8) {
                points.push_back(Point32{be32(payload + p), be32(payload + p + 4)});
            }
            break;
        case 0x1100:
            commit();
            break;
        default:
            break;
        }
        i += len;
    }
    commit();
    std::sort(out.begin(), out.end());
    return out;
}

std::pair<std::uint16_t, std::uint16_t> layerOf(const std::vector<room::LayerSpec> &layers, std::uint32_t layerId)
{
    if (layerId < layers.size()) {
        return {layers[layerId].layerNum, layers[layerId].dataType};
    }
    return {0, 0};
}

Fp hashRoomShape(const room::Shape &shape, const std::vector<room::LayerSpec> &layers)
{
    if (const room::Shape::RectData *rect = shape.rect()) {
        const auto layer = layerOf(layers, rect->layerId);
        return hashRect(layer.first, layer.second, rect->box.llx, rect->box.lly, rect->box.urx, rect->box.ury);
    }
    if (const room::Shape::PolygonData *polygon = shape.polygon()) {
        const auto layer = layerOf(layers, polygon->layerId);
        std::vector<Point32> points;
        points.reserve(polygon->points.size());
        for (const room::Point &point : polygon->points) {
            points.push_back(Point32{static_cast<std::int32_t>(point.x), static_cast<std::int32_t>(point.y)});
        }
        std::size_t n = points.size();
        if (n >= 2 && points.front().x == points.back().x && points.front().y == points.back().y) {
            --n;
        }
        return hashPoints(2, layer.first, layer.second, 0, points, n);
    }
    if (const room::Shape::PathData *path = shape.path()) {
        const auto layer = layerOf(layers, path->layerId);
        std::vector<Point32> points;
        points.reserve(path->points.size());
        for (const room::Point &point : path->points) {
            points.push_back(Point32{static_cast<std::int32_t>(point.x), static_cast<std::int32_t>(point.y)});
        }
        return hashPoints(3, layer.first, layer.second, path->width, points, points.size());
    }
    Hasher hasher;
    hasher.add(0);
    return hasher.finish();
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: wafer_geom_xor gds room\n";
        return 2;
    }
    const std::string gdsPath = argv[1];
    const std::string roomPath = argv[2];

    std::cout << "indexing gds" << std::endl;
    const auto gdsCells = indexGds(gdsPath);
    std::cout << "gds cells " << gdsCells.size() << std::endl;

    std::unordered_map<std::string, std::vector<room::LayerSpec>> layersByCell;
    std::vector<std::string> roomCells;
    std::unordered_map<std::string, bool> roomSeen;
    std::size_t mismatches = 0;
    std::size_t compared = 0;
    std::size_t missingGds = 0;

    auto compareCell = [&](const std::string &name, std::vector<Fp> roomFp) {
        std::sort(roomFp.begin(), roomFp.end());
        const auto it = gdsCells.find(name);
        if (it == gdsCells.end()) {
            ++missingGds;
            if (missingGds <= 5) {
                std::cout << "MISSING in gds: " << name << std::endl;
            }
            return;
        }
        const std::vector<Fp> gdsFp = fingerprintsFromGds(gdsPath, it->second);
        ++compared;
        if (roomFp != gdsFp) {
            ++mismatches;
            if (mismatches <= 10) {
                std::cout << "DIFF " << name << " room " << roomFp.size() << " gds " << gdsFp.size() << std::endl;
            }
        }
        if (compared % 500 == 0) {
            std::cout << "compared " << compared << std::endl;
        }
    };

    std::string current;
    std::vector<Fp> currentFp;
    room::LayoutVisitHandlers handlers;
    handlers.onCell = [&](const room::VisitedCell &cell) {
        layersByCell[cell.name] = cell.layers;
        roomCells.push_back(cell.name);
    };
    handlers.onShape = [&](const std::string &cellName, room::Shape shape) {
        if (shape.type() == room::Shape::Type::Text || shape.type() == room::Shape::Type::Arc) {
            return;
        }
        if (cellName != current) {
            if (!current.empty()) {
                roomSeen[current] = true;
                compareCell(current, std::move(currentFp));
                currentFp.clear();
            }
            current = cellName;
        }
        const auto it = layersByCell.find(cellName);
        const std::vector<room::LayerSpec> empty;
        currentFp.push_back(hashRoomShape(shape, it == layersByCell.end() ? empty : it->second));
    };

    std::cout << "reading room" << std::endl;
    room::visitLayoutFile(roomPath, handlers);
    if (!current.empty()) {
        roomSeen[current] = true;
        compareCell(current, std::move(currentFp));
    }
    for (const std::string &name : roomCells) {
        if (!roomSeen[name]) {
            compareCell(name, {});
            roomSeen[name] = true;
        }
    }

    std::size_t missingRoom = 0;
    for (const auto &entry : gdsCells) {
        if (!roomSeen[entry.first]) {
            ++missingRoom;
            if (missingRoom <= 5) {
                std::cout << "MISSING in room: " << entry.first << std::endl;
            }
        }
    }

    std::cout << "compared " << compared << " mismatches " << mismatches
              << " missing_gds " << missingGds << " missing_room " << missingRoom << std::endl;
    if (mismatches == 0 && missingGds == 0 && missingRoom == 0) {
        std::cout << "PASSED" << std::endl;
        return 0;
    }
    std::cout << "FAILED" << std::endl;
    return 1;
}
