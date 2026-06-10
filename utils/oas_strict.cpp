#include "oas_strict.h"

#include "klayout_util.h"

#include <cstring>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {
namespace {

constexpr char kMagic[] = "%SEMI-OASIS\r\n";

bool readFileBytes(const std::string &path, std::vector<std::uint8_t> &out, std::vector<std::string> &errors)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        errors.push_back("Failed to read file '" + path + "'");
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char *>(out.data()), size);
    }
    return static_cast<bool>(in);
}

bool writeCellNameList(const std::string &path,
                       const std::vector<std::string> &names,
                       std::vector<std::string> &errors)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        errors.push_back("Failed to write cell name list '" + path + "'");
        return false;
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            out.put('\n');
        }
        out.write(names[i].data(), static_cast<std::streamsize>(names[i].size()));
    }
    return static_cast<bool>(out);
}

std::size_t readUIntAt(const std::vector<std::uint8_t> &data, std::size_t pos, std::uint64_t &value)
{
    value = 0;
    std::size_t shift = 0;
    while (pos < data.size()) {
        const std::uint8_t b = data[pos++];
        value |= static_cast<std::uint64_t>(b & 0x7F) << shift;
        shift += 7;
        if ((b & 0x80) == 0) {
            return pos;
        }
    }
    return std::string::npos;
}

std::size_t findEndRecordOffset(const std::vector<std::uint8_t> &data)
{
    if (data.size() < 16) {
        return data.size();
    }

    for (std::size_t i = data.size(); i-- > 13;) {
        if (data[i] != 0x02) {
            continue;
        }
        std::uint64_t padLen = 0;
        const std::size_t afterType = readUIntAt(data, i + 1, padLen);
        if (afterType == std::string::npos) {
            continue;
        }
        if (afterType + padLen + 1 > data.size()) {
            continue;
        }
        bool padded = padLen > 0;
        for (std::uint64_t k = 0; k < padLen && padded; ++k) {
            if (data[afterType + k] != 0x80) {
                padded = false;
            }
        }
        if (padded) {
            return i;
        }
    }

    return data.size();
}

std::size_t findCellRefBlockStart(const std::vector<std::uint8_t> &data,
                                  std::size_t endPos,
                                  std::size_t cellCount)
{
    if (cellCount == 0 || endPos < 13) {
        return std::string::npos;
    }

    const std::size_t blockLen = cellCount * 2;
    const std::size_t searchStart = 13;
    const std::size_t searchEnd = endPos > blockLen ? endPos - blockLen : 13;

    for (std::size_t pos = searchStart; pos <= searchEnd; ++pos) {
        bool ok = true;
        for (std::size_t k = 0; k < cellCount; ++k) {
            if (data[pos + 2 * k] != 0x0D) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            continue;
        }

        bool refsMatch = true;
        for (std::size_t k = 0; k < cellCount; ++k) {
            const std::uint8_t ref = data[pos + 2 * k + 1];
            if (ref != cellCount - 1 - k) {
                refsMatch = false;
                break;
            }
        }
        if (refsMatch) {
            return pos;
        }
    }

    return std::string::npos;
}

std::string siblingPath(const std::string &basePath, const char *suffix)
{
    const std::size_t slash = basePath.find_last_of("/\\");
    const std::size_t dot = basePath.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return basePath.substr(0, dot) + suffix;
    }
    return basePath + suffix;
}

} // namespace

bool generateStrictTemplate(const std::vector<std::string> &cellNames,
                            const std::string &templatePath,
                            std::vector<std::string> &errors)
{
    if (cellNames.empty()) {
        errors.push_back("Cannot generate strict OAS template without cells");
        return false;
    }

    const std::string nameListPath = siblingPath(templatePath, ".cellnames");
    if (!writeCellNameList(nameListPath, cellNames, errors)) {
        return false;
    }

    const std::unordered_map<std::string, std::string> vars = {
        {"out", templatePath},
        {"list", nameListPath},
    };

    if (!runKLayoutBatch("klayout_write_cells_from_file.drc", vars, errors)) {
        return false;
    }

    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(templatePath, bytes, errors)) {
        return false;
    }
    if (bytes.size() < sizeof(kMagic) - 1
        || std::memcmp(bytes.data(), kMagic, sizeof(kMagic) - 1) != 0) {
        errors.push_back("KLayout strict template is missing OASIS magic");
        return false;
    }

    const std::size_t endPos = findEndRecordOffset(bytes);
    const std::size_t blockStart = findCellRefBlockStart(bytes, endPos, cellNames.size());
    if (blockStart == std::string::npos) {
        errors.push_back("Failed to locate CELL reference block in strict template");
        return false;
    }

    return true;
}

bool assembleStrictOas(const std::string &templatePath,
                       const std::vector<std::string> &cellNames,
                       const std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> &geometryByRef,
                       const std::string &outPath,
                       std::vector<std::string> &errors)
{
    std::vector<std::uint8_t> tmpl;
    if (!readFileBytes(templatePath, tmpl, errors)) {
        return false;
    }

    const std::size_t endPos = findEndRecordOffset(tmpl);
    const std::size_t blockStart = findCellRefBlockStart(tmpl, endPos, cellNames.size());
    if (blockStart == std::string::npos) {
        errors.push_back("Failed to locate CELL reference block in strict template");
        return false;
    }

    const std::size_t blockEnd = blockStart + cellNames.size() * 2;
    if (blockEnd > endPos) {
        errors.push_back("Strict template CELL block overlaps END record");
        return false;
    }

    std::vector<std::uint8_t> out;
    out.reserve(tmpl.size() + 1024);

    out.insert(out.end(), tmpl.begin(), tmpl.begin() + static_cast<std::ptrdiff_t>(blockStart));

    for (std::size_t k = 0; k < cellNames.size(); ++k) {
        out.push_back(0x0D);
        const std::uint8_t ref = tmpl[blockStart + 2 * k + 1];
        out.push_back(ref);

        const auto geomIt = geometryByRef.find(ref);
        if (geomIt != geometryByRef.end()) {
            out.insert(out.end(), geomIt->second.begin(), geomIt->second.end());
        }
    }

    out.insert(out.end(), tmpl.begin() + static_cast<std::ptrdiff_t>(blockEnd), tmpl.end());

    std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        errors.push_back("Failed to open file '" + outPath + "'");
        return false;
    }
    if (!out.empty()) {
        file.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    return static_cast<bool>(file);
}

std::size_t cellRecordEnd(const std::vector<std::uint8_t> &data, std::size_t pos)
{
    if (pos >= data.size() || data[pos] != 0x0D) {
        return std::string::npos;
    }
    std::uint64_t ref = 0;
    const std::size_t next = readUIntAt(data, pos + 1, ref);
    return next == std::string::npos ? std::string::npos : next;
}

std::size_t findGeometryStart(const std::vector<std::uint8_t> &data, std::uint64_t topRef)
{
    for (std::size_t i = 13; i + 1 < data.size(); ++i) {
        if (data[i] != 0x0D) {
            continue;
        }
        std::uint64_t ref = 0;
        const std::size_t next = readUIntAt(data, i + 1, ref);
        if (next != std::string::npos && ref == topRef) {
            return i;
        }
    }
    return std::string::npos;
}

std::size_t findNextCellPos(const std::vector<std::uint8_t> &data,
                            std::size_t from,
                            std::uint64_t ref)
{
    for (std::size_t j = from; j + 1 < data.size(); ++j) {
        if (data[j] != 0x0D) {
            continue;
        }
        std::uint64_t nextRef = 0;
        const std::size_t next = readUIntAt(data, j + 1, nextRef);
        if (next != std::string::npos && nextRef == ref) {
            return j;
        }
    }
    return std::string::npos;
}

std::string base64Encode(const std::vector<std::uint8_t> &data)
{
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 2 < data.size()) {
        const unsigned v = (static_cast<unsigned>(data[i]) << 16)
                           | (static_cast<unsigned>(data[i + 1]) << 8)
                           | static_cast<unsigned>(data[i + 2]);
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
        out.push_back(kAlphabet[v & 0x3F]);
        i += 3;
    }

    if (i < data.size()) {
        unsigned v = static_cast<unsigned>(data[i]) << 16;
        if (i + 1 < data.size()) {
            v |= static_cast<unsigned>(data[i + 1]) << 8;
        }
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        if (i + 1 < data.size()) {
            out.push_back(kAlphabet[(v >> 6) & 0x3F]);
            out.push_back('=');
        } else {
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

bool base64Decode(const std::string &encoded, std::vector<std::uint8_t> &out)
{
    auto decodeChar = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9') {
            return c - '0' + 52;
        }
        if (c == '+') {
            return 62;
        }
        if (c == '/') {
            return 63;
        }
        return -1;
    };

    out.clear();
    int val = 0;
    int valb = -8;
    for (char c : encoded) {
        if (c == '=') {
            break;
        }
        const int d = decodeChar(c);
        if (d < 0) {
            continue;
        }
        val = (val << 6) | d;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<std::uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return true;
}

bool extractStrictGeometryFromFile(const std::string &path,
                                   std::size_t cellCount,
                                   std::vector<std::uint8_t> &header,
                                   std::vector<std::uint8_t> &tail,
                                   std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> &geometryByRef,
                                   std::vector<std::string> &errors)
{
    std::vector<std::uint8_t> data;
    if (!readFileBytes(path, data, errors)) {
        return false;
    }
    if (cellCount == 0) {
        errors.push_back("Cannot extract strict geometry from file without cells");
        return false;
    }

    const std::uint64_t topRef = static_cast<std::uint64_t>(cellCount - 1);
    const std::size_t geomStart = findGeometryStart(data, topRef);
    if (geomStart == std::string::npos) {
        errors.push_back("Failed to locate strict geometry section in OAS file");
        return false;
    }

    header.assign(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(geomStart));
    tail.clear();

    geometryByRef.clear();
    std::size_t pos = geomStart;
    for (std::int64_t ref = static_cast<std::int64_t>(topRef); ref >= 0; --ref) {
        const std::size_t cellEnd = cellRecordEnd(data, pos);
        if (cellEnd == std::string::npos) {
            errors.push_back("Malformed CELL record while extracting strict geometry");
            return false;
        }

        std::uint64_t readRef = 0;
        readUIntAt(data, pos + 1, readRef);
        if (readRef != static_cast<std::uint64_t>(ref)) {
            errors.push_back("Unexpected CELL reference while extracting strict geometry");
            return false;
        }

        std::size_t geomEnd = data.size();
        if (ref > 0) {
            const std::size_t nextCell = findNextCellPos(data, cellEnd, static_cast<std::uint64_t>(ref - 1));
            if (nextCell == std::string::npos) {
                errors.push_back("Failed to locate next CELL while extracting strict geometry");
                return false;
            }
            geomEnd = nextCell;
        }

        geometryByRef[static_cast<std::uint64_t>(ref)].assign(data.begin() + static_cast<std::ptrdiff_t>(cellEnd),
                                                                 data.begin() + static_cast<std::ptrdiff_t>(geomEnd));
        pos = geomEnd;
    }

    if (pos < data.size()) {
        tail.assign(data.begin() + static_cast<std::ptrdiff_t>(pos), data.end());
    }

    return true;
}

bool assembleStrictFromPreserved(const std::vector<std::uint8_t> &header,
                                 const std::vector<std::uint8_t> &tail,
                                 std::size_t cellCount,
                                 const std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> &geometryByRef,
                                 const std::string &outPath,
                                 std::vector<std::string> &errors)
{
    if (cellCount == 0) {
        errors.push_back("Cannot assemble strict OAS without cells");
        return false;
    }

    std::vector<std::uint8_t> out;
    out.reserve(header.size() + tail.size() + geometryByRef.size() * 64);
    out.insert(out.end(), header.begin(), header.end());

    for (std::int64_t ref = static_cast<std::int64_t>(cellCount - 1); ref >= 0; --ref) {
        out.push_back(0x0D);
        out.push_back(static_cast<std::uint8_t>(ref));

        const auto geomIt = geometryByRef.find(static_cast<std::uint64_t>(ref));
        if (geomIt != geometryByRef.end()) {
            out.insert(out.end(), geomIt->second.begin(), geomIt->second.end());
        }
    }

    out.insert(out.end(), tail.begin(), tail.end());

    std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        errors.push_back("Failed to open file '" + outPath + "'");
        return false;
    }
    if (!out.empty()) {
        file.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    return static_cast<bool>(file);
}

} // namespace core
