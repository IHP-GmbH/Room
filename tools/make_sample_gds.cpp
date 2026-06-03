#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

void writeRec(FILE *f, std::uint16_t recType, const void *data, int dataLen)
{
    const std::uint16_t len = static_cast<std::uint16_t>(4 + dataLen);
    const std::uint8_t hdr[4] = {
        static_cast<std::uint8_t>(len >> 8),
        static_cast<std::uint8_t>(len & 0xFF),
        static_cast<std::uint8_t>(recType >> 8),
        static_cast<std::uint8_t>(recType & 0xFF),
    };
    std::fwrite(hdr, 1, 4, f);
    if (dataLen > 0) {
        std::fwrite(data, 1, static_cast<std::size_t>(dataLen), f);
    }
}

void writeEmptyRec(FILE *f, std::uint16_t recType)
{
    writeRec(f, recType, nullptr, 0);
}

void writeInt16(FILE *f, std::uint16_t recType, std::int16_t value)
{
    const std::uint8_t data[2] = {
        static_cast<std::uint8_t>((value >> 8) & 0xFF),
        static_cast<std::uint8_t>(value & 0xFF),
    };
    writeRec(f, recType, data, 2);
}

void writeInt32(FILE *f, std::uint16_t recType, std::int32_t value)
{
    const std::uint8_t data[4] = {
        static_cast<std::uint8_t>((value >> 24) & 0xFF),
        static_cast<std::uint8_t>((value >> 16) & 0xFF),
        static_cast<std::uint8_t>((value >> 8) & 0xFF),
        static_cast<std::uint8_t>(value & 0xFF),
    };
    writeRec(f, recType, data, 4);
}

void writeString(FILE *f, std::uint16_t recType, const std::string &s)
{
    std::string padded = s;
    if (padded.size() % 2 == 1) {
        padded.push_back('\0');
    }
    writeRec(f, recType, padded.data(), static_cast<int>(padded.size()));
}

void writeUnits(FILE *f)
{
    std::uint8_t data[16] = {
        0x3e, 0x63, 0x45, 0x78, 0x45, 0x00, 0x00, 0x00, // 0.001 user units / dbu
        0x3e, 0x41, 0x89, 0x37, 0x4b, 0xc6, 0xa7, 0xf0, // 1e-9 meters
    };
    writeRec(f, 0x0305, data, 16);
}

void writeTime(FILE *f, std::uint16_t recType)
{
    std::time_t now = std::time(nullptr);
    std::tm *t = std::localtime(&now);
    std::int16_t parts[12] = {
        static_cast<std::int16_t>(t->tm_year + 1900),
        static_cast<std::int16_t>(t->tm_mon + 1),
        static_cast<std::int16_t>(t->tm_mday),
        static_cast<std::int16_t>(t->tm_hour),
        static_cast<std::int16_t>(t->tm_min),
        static_cast<std::int16_t>(t->tm_sec),
        static_cast<std::int16_t>(t->tm_year + 1900),
        static_cast<std::int16_t>(t->tm_mon + 1),
        static_cast<std::int16_t>(t->tm_mday),
        static_cast<std::int16_t>(t->tm_hour),
        static_cast<std::int16_t>(t->tm_min),
        static_cast<std::int16_t>(t->tm_sec),
    };
    std::vector<std::uint8_t> data(24);
    for (int i = 0; i < 12; ++i) {
        data[i * 2] = static_cast<std::uint8_t>((parts[i] >> 8) & 0xFF);
        data[i * 2 + 1] = static_cast<std::uint8_t>(parts[i] & 0xFF);
    }
    writeRec(f, recType, data.data(), 24);
}

void writeXY(FILE *f, const std::vector<std::pair<std::int32_t, std::int32_t>> &pts)
{
    std::vector<std::uint8_t> data(pts.size() * 8);
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const std::int32_t x = pts[i].first;
        const std::int32_t y = pts[i].second;
        data[i * 8 + 0] = static_cast<std::uint8_t>((x >> 24) & 0xFF);
        data[i * 8 + 1] = static_cast<std::uint8_t>((x >> 16) & 0xFF);
        data[i * 8 + 2] = static_cast<std::uint8_t>((x >> 8) & 0xFF);
        data[i * 8 + 3] = static_cast<std::uint8_t>(x & 0xFF);
        data[i * 8 + 4] = static_cast<std::uint8_t>((y >> 24) & 0xFF);
        data[i * 8 + 5] = static_cast<std::uint8_t>((y >> 16) & 0xFF);
        data[i * 8 + 6] = static_cast<std::uint8_t>((y >> 8) & 0xFF);
        data[i * 8 + 7] = static_cast<std::uint8_t>(y & 0xFF);
    }
    writeRec(f, 0x1003, data.data(), static_cast<int>(data.size()));
}

} // namespace

int main(int argc, char *argv[])
{
    const char *outPath = (argc >= 2) ? argv[1] : "testdata/sample.gds";
    FILE *f = std::fopen(outPath, "wb");
    if (!f) {
        std::fprintf(stderr, "Cannot write %s\n", outPath);
        return 1;
    }

    writeInt16(f, 0x0002, 600);
    writeTime(f, 0x0102);
    writeString(f, 0x0206, "demo_lib");
    writeUnits(f);

    // INV cell
    writeTime(f, 0x0502);
    writeString(f, 0x0606, "INV");
    writeInt16(f, 0x0D02, 1);
    writeInt16(f, 0x0E02, 0);
    writeRec(f, 0x0800, nullptr, 0);
    writeXY(f, {{0, 0}, {1000, 0}, {1000, 500}, {0, 500}, {0, 0}});
    writeEmptyRec(f, 0x1100);
    writeEmptyRec(f, 0x0700);

    // TOP cell
    writeTime(f, 0x0502);
    writeString(f, 0x0606, "TOP");
    writeInt16(f, 0x0D02, 2);
    writeInt16(f, 0x0E02, 0);
    writeRec(f, 0x0900, nullptr, 0);
    writeInt32(f, 0x0F03, 200);
    writeXY(f, {{500, 500}, {5000, 5000}});
    writeEmptyRec(f, 0x1100);

    writeInt16(f, 0x0D02, 3);
    writeRec(f, 0x0C00, nullptr, 0);
    writeString(f, 0x1906, "TOPCELL");
    writeXY(f, {{100, 100}});
    writeEmptyRec(f, 0x1100);

    writeRec(f, 0x0A00, nullptr, 0);
    writeString(f, 0x1206, "INV");
    writeXY(f, {{2000, 2000}});
    writeEmptyRec(f, 0x1100);

    writeEmptyRec(f, 0x0700);
    writeEmptyRec(f, 0x0400);

    std::fclose(f);
    std::printf("Created %s\n", outPath);
    return 0;
}
