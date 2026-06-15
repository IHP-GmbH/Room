#include "database.h"
#include "gds_exporter.h"
#include "gds_importer.h"
#include "gds_property_codec.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
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
    const std::uint8_t data[16] = {
        0x3e, 0x63, 0x45, 0x78, 0x45, 0x00, 0x00, 0x00,
        0x3e, 0x41, 0x89, 0x37, 0x4b, 0xc6, 0xa7, 0xf0,
    };
    writeRec(f, 0x0305, data, 16);
}

bool writeFixtureGds(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }

    writeInt16(f, 0x0002, 600);
    writeEmptyRec(f, 0x0102);
    writeString(f, 0x0206, "props_test");
    writeUnits(f);

    writeEmptyRec(f, 0x0502);
    writeString(f, 0x0606, "TOP");
    writeInt16(f, 0x2B02, 128);
    writeString(f, 0x2C06, "cell_meta");

    writeEmptyRec(f, 0x0800);
    writeInt16(f, 0x0D02, 1);
    writeInt16(f, 0x0E02, 0);
    writeInt16(f, 0x2B02, 129);
    writeString(f, 0x2C06, "net_A");
    writeInt16(f, 0x2B02, 130);
    writeInt16(f, 0x2C02, 42);

    const std::int32_t xy[10] = {0, 0, 1000, 0, 1000, 1000, 0, 1000, 0, 0};
    std::uint8_t xyBytes[40];
    for (int i = 0; i < 10; ++i) {
        const std::int32_t v = xy[i];
        xyBytes[i * 4 + 0] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
        xyBytes[i * 4 + 1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        xyBytes[i * 4 + 2] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        xyBytes[i * 4 + 3] = static_cast<std::uint8_t>(v & 0xFF);
    }
    writeRec(f, 0x1003, xyBytes, 40);
    writeEmptyRec(f, 0x1100);

    writeEmptyRec(f, 0x0700);
    writeEmptyRec(f, 0x0400);
    std::fclose(f);
    return true;
}

std::vector<core::Property> gdsProperties(const std::vector<core::Property> &props)
{
    std::vector<core::Property> out;
    for (const core::Property &prop : props) {
        if (core::gds_prop::isGdsProperty(prop)) {
            out.push_back(prop);
        }
    }
    return out;
}

bool sameGdsProperties(const std::vector<core::Property> &a, const std::vector<core::Property> &b)
{
    const std::vector<core::Property> ga = gdsProperties(a);
    const std::vector<core::Property> gb = gdsProperties(b);
    if (ga.size() != gb.size()) {
        return false;
    }
    for (std::size_t i = 0; i < ga.size(); ++i) {
        if (ga[i].name != gb[i].name || ga[i].value != gb[i].value) {
            return false;
        }
    }
    return true;
}

const core::Cell *firstCell(const core::Database &db)
{
    if (db.lib().cells().empty()) {
        return nullptr;
    }
    return &db.lib().cells().front();
}

} // namespace

int main(int argc, char *argv[])
{
    const std::string inPath = (argc > 1) ? argv[1] : "build/tests/gds_props_fixture.gds";
    const std::string outPath = (argc > 2) ? argv[2] : "build/tests/gds_props_roundtrip.gds";
    const std::string corePath = (argc > 3) ? argv[3] : "build/tests/gds_props_roundtrip.core";

    if (!writeFixtureGds(inPath)) {
        std::cerr << "error: cannot write fixture GDS\n";
        return 1;
    }

    core::GdsImporter importer;
    core::Database db = importer.importFile(inPath);
    if (!importer.errors().empty()) {
        for (const auto &msg : importer.errors()) {
            std::cerr << "error: " << msg << '\n';
        }
        return 2;
    }

    const core::Cell *cell = firstCell(db);
    if (cell == nullptr) {
        std::cerr << "error: no cells imported\n";
        return 3;
    }

    const std::vector<core::Property> expectedCellProps = {
        core::gds_prop::make(128, "s:cell_meta"),
    };
    if (!sameGdsProperties(cell->properties(), expectedCellProps)) {
        std::cerr << "error: cell GDS properties mismatch after import\n";
        return 4;
    }

    const core::CellContent *layout = cell->findContent(core::ViewType::Layout);
    if (layout == nullptr || layout->block().shapes().empty()) {
        std::cerr << "error: layout shape missing\n";
        return 5;
    }

    const std::vector<core::Property> expectedShapeProps = {
        core::gds_prop::make(129, "s:net_A"),
        core::gds_prop::make(130, "i2:42"),
    };
    if (!sameGdsProperties(layout->block().shapes().front().properties(), expectedShapeProps)) {
        std::cerr << "error: shape GDS properties mismatch after import\n";
        return 6;
    }

    db.saveToFile(corePath);
    const core::Database reloaded = core::Database::loadFromFile(corePath);
    const core::Cell *reloadedCell = firstCell(reloaded);
    const core::CellContent *reloadedLayout = reloadedCell ? reloadedCell->findContent(core::ViewType::Layout) : nullptr;
    if (reloadedCell == nullptr || reloadedLayout == nullptr || reloadedLayout->block().shapes().empty()) {
        std::cerr << "error: reload failed\n";
        return 7;
    }
    if (!sameGdsProperties(reloadedCell->properties(), expectedCellProps) ||
        !sameGdsProperties(reloadedLayout->block().shapes().front().properties(), expectedShapeProps)) {
        std::cerr << "error: GDS properties lost in .core round-trip\n";
        return 8;
    }

    core::GdsExporter exporter;
    exporter.exportFile(reloaded, outPath);
    if (!exporter.errors().empty()) {
        for (const auto &msg : exporter.errors()) {
            std::cerr << "error: " << msg << '\n';
        }
        return 9;
    }

    core::GdsImporter importer2;
    const core::Database roundtrip = importer2.importFile(outPath);
    if (!importer2.errors().empty()) {
        for (const auto &msg : importer2.errors()) {
            std::cerr << "error: " << msg << '\n';
        }
        return 10;
    }

    const core::Cell *rtCell = firstCell(roundtrip);
    const core::CellContent *rtLayout = rtCell ? rtCell->findContent(core::ViewType::Layout) : nullptr;
    if (rtCell == nullptr || rtLayout == nullptr || rtLayout->block().shapes().empty()) {
        std::cerr << "error: GDS export round-trip produced empty layout\n";
        return 11;
    }
    if (!sameGdsProperties(rtCell->properties(), expectedCellProps) ||
        !sameGdsProperties(rtLayout->block().shapes().front().properties(), expectedShapeProps)) {
        std::cerr << "error: GDS properties mismatch after export round-trip\n";
        return 12;
    }

    std::cout << "gds_properties_roundtrip: OK\n";
    return 0;
}
