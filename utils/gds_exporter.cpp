/*!****************************************************************************************
 * \file gds_exporter.cpp
 * \brief GDSII writer: exports layout views from a ROOM Database.
 *****************************************************************************************/

#include "gds_exporter.h"

#include "cell.h"
#include "gds_property_codec.h"
#include "layer_utils.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <optional>
#include <vector>

namespace room {
namespace {

constexpr std::uint16_t GDS_HEADER   = 0x0002;
constexpr std::uint16_t GDS_BGNLIB   = 0x0102;
constexpr std::uint16_t GDS_LIBNAME  = 0x0206;
constexpr std::uint16_t GDS_UNITS    = 0x0305;
constexpr std::uint16_t GDS_ENDLIB   = 0x0400;
constexpr std::uint16_t GDS_BGNSTR   = 0x0502;
constexpr std::uint16_t GDS_STRNAME  = 0x0606;
constexpr std::uint16_t GDS_ENDSTR   = 0x0700;
constexpr std::uint16_t GDS_BOUNDARY = 0x0800;
constexpr std::uint16_t GDS_PATH     = 0x0900;
constexpr std::uint16_t GDS_SREF     = 0x0A00;
constexpr std::uint16_t GDS_TEXT     = 0x0C00;
constexpr std::uint16_t GDS_TEXTTYPE = 0x1602;
constexpr std::uint16_t GDS_LAYER    = 0x0D02;
constexpr std::uint16_t GDS_DATATYPE = 0x0E02;
constexpr std::uint16_t GDS_WIDTH    = 0x0F03;
constexpr std::uint16_t GDS_XY       = 0x1003;
constexpr std::uint16_t GDS_ENDEL    = 0x1100;
constexpr std::uint16_t GDS_SNAME    = 0x1206;
constexpr std::uint16_t GDS_STRANS   = 0x1A01;
constexpr std::uint16_t GDS_MAG      = 0x1B05;
constexpr std::uint16_t GDS_STRING   = 0x1906;
constexpr std::uint16_t GDS_PROPATTR = 0x2B02;
constexpr std::uint16_t GDS_PROPVALUE_I2 = 0x2C02;
constexpr std::uint16_t GDS_PROPVALUE_I4 = 0x2C03;
constexpr std::uint16_t GDS_PROPVALUE_REAL = 0x2C05;
constexpr std::uint16_t GDS_PROPVALUE_STRING = 0x2C06;

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

void encodeGdsReal(double value, std::uint8_t out[8])
{
    if (value == 0.0) {
        std::memset(out, 0, 8);
        return;
    }

    const bool negative = value < 0.0;
    double mant = negative ? -value : value;
    int exp = 0;
    while (mant >= 1.0 && exp < 63) {
        mant /= 16.0;
        ++exp;
    }
    while (mant > 0.0 && mant < 0.0625 && exp > -64) {
        mant *= 16.0;
        --exp;
    }

    out[0] = static_cast<std::uint8_t>((negative ? 0x80 : 0x00) | (exp + 64));
    for (int i = 1; i < 8; ++i) {
        mant *= 256.0;
        out[i] = static_cast<std::uint8_t>(mant);
        mant -= out[i];
    }
}

void writeGdsReal(FILE *f, std::uint16_t recType, double value)
{
    std::uint8_t data[8];
    encodeGdsReal(value, data);
    writeRec(f, recType, data, 8);
}

void writeString(FILE *f, std::uint16_t recType, const std::string &s)
{
    std::string padded = s;
    if (padded.size() % 2 == 1) {
        padded.push_back('\0');
    }
    writeRec(f, recType, padded.data(), static_cast<int>(padded.size()));
}

std::vector<std::uint8_t> hexDecode(const std::string &hex)
{
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;
    };

    if (hex.size() % 2 != 0) {
        return {};
    }
    std::vector<std::uint8_t> out(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = nibble(hex[i * 2]);
        const int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return {};
        }
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return out;
}

void writeGdsProperty(FILE *f, const Property &prop)
{
    const std::optional<std::int16_t> attr = gds_prop::attrOf(prop);
    if (!attr) {
        return;
    }

    const std::string &value = prop.value;
    if (value.compare(0, 3, "i2:") == 0) {
        writeInt16(f, GDS_PROPATTR, *attr);
        writeInt16(f, GDS_PROPVALUE_I2, static_cast<std::int16_t>(std::stoi(value.substr(3))));
        return;
    }
    if (value.compare(0, 3, "i4:") == 0) {
        writeInt16(f, GDS_PROPATTR, *attr);
        writeInt32(f, GDS_PROPVALUE_I4, std::stoi(value.substr(3)));
        return;
    }
    if (value.compare(0, 5, "rhex:") == 0) {
        const std::vector<std::uint8_t> bytes = hexDecode(value.substr(5));
        if (bytes.size() != 8) {
            return;
        }
        writeInt16(f, GDS_PROPATTR, *attr);
        writeRec(f, GDS_PROPVALUE_REAL, bytes.data(), 8);
        return;
    }
    if (value.compare(0, 2, "s:") == 0) {
        writeInt16(f, GDS_PROPATTR, *attr);
        writeString(f, GDS_PROPVALUE_STRING, value.substr(2));
    }
}

void writeGdsProperties(FILE *f, const std::vector<Property> &properties)
{
    for (const Property &prop : properties) {
        if (gds_prop::isGdsProperty(prop)) {
            writeGdsProperty(f, prop);
        }
    }
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

void writeXY(FILE *f, const std::vector<Point> &pts)
{
    std::vector<std::uint8_t> data(pts.size() * 8);
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const std::int32_t x = static_cast<std::int32_t>(pts[i].x);
        const std::int32_t y = static_cast<std::int32_t>(pts[i].y);
        data[i * 8 + 0] = static_cast<std::uint8_t>((x >> 24) & 0xFF);
        data[i * 8 + 1] = static_cast<std::uint8_t>((x >> 16) & 0xFF);
        data[i * 8 + 2] = static_cast<std::uint8_t>((x >> 8) & 0xFF);
        data[i * 8 + 3] = static_cast<std::uint8_t>(x & 0xFF);
        data[i * 8 + 4] = static_cast<std::uint8_t>((y >> 24) & 0xFF);
        data[i * 8 + 5] = static_cast<std::uint8_t>((y >> 16) & 0xFF);
        data[i * 8 + 6] = static_cast<std::uint8_t>((y >> 8) & 0xFF);
        data[i * 8 + 7] = static_cast<std::uint8_t>(y & 0xFF);
    }
    writeRec(f, GDS_XY, data.data(), static_cast<int>(data.size()));
}

void writeUnits(FILE *f, double dbuPerMicron)
{
    if (dbuPerMicron <= 0.0) {
        dbuPerMicron = 1000.0;
    }
    const double dbuMeters = 1e-6 / dbuPerMicron;
    const double dbuPerUser = 0.001;
    const double userMeters = dbuMeters / dbuPerUser;
    std::uint8_t data[16];
    encodeGdsReal(dbuPerUser, data);
    encodeGdsReal(userMeters, data + 8);
    writeRec(f, GDS_UNITS, data, 16);
}

std::uint16_t orientToGdsStrans(Orient o)
{
    switch (o) {
    case Orient::R0: return 0;
    case Orient::R90: return 1;
    case Orient::R180: return 2;
    case Orient::R270: return 3;
    case Orient::MX: return 0x8000;
    case Orient::MX90: return 0x8001;
    case Orient::MY: return 0x8002;
    case Orient::MY90: return 0x8003;
    }
    return 0;
}

const LayerSpec *layerSpec(const std::vector<LayerSpec> &layers, std::uint32_t layerId)
{
    if (layerId >= layers.size()) {
        return nullptr;
    }
    return &layers[layerId];
}

void writeLayerDatatype(FILE *f, const std::vector<LayerSpec> &layers, std::uint32_t layerId)
{
    const LayerSpec *spec = layerSpec(layers, layerId);
    const std::uint16_t layer = spec ? spec->layerNum : 0;
    const std::uint16_t datatype = spec ? spec->dataType : 0;
    writeInt16(f, GDS_LAYER, static_cast<std::int16_t>(layer));
    writeInt16(f, GDS_DATATYPE, static_cast<std::int16_t>(datatype));
}

std::vector<Point> closedRing(const std::vector<Point> &points)
{
    std::vector<Point> ring = points;
    if (ring.size() >= 3) {
        if (ring.front().x != ring.back().x || ring.front().y != ring.back().y) {
            ring.push_back(ring.front());
        }
    }
    return ring;
}

void writeShape(FILE *f, const std::vector<LayerSpec> &layers, const Shape &shape)
{
    switch (shape.type()) {
    case Shape::Type::Rect: {
        if (const Shape::RectData *rect = shape.rect()) {
            writeEmptyRec(f, GDS_BOUNDARY);
            writeLayerDatatype(f, layers, rect->layerId);
            const Box &b = rect->box;
            writeXY(f, {{b.llx, b.lly}, {b.urx, b.lly}, {b.urx, b.ury}, {b.llx, b.ury}, {b.llx, b.lly}});
            writeGdsProperties(f, shape.properties());
            writeEmptyRec(f, GDS_ENDEL);
        }
        break;
    }
    case Shape::Type::Polygon: {
        if (const Shape::PolygonData *poly = shape.polygon()) {
            writeEmptyRec(f, GDS_BOUNDARY);
            writeLayerDatatype(f, layers, poly->layerId);
            writeXY(f, closedRing(poly->points));
            writeGdsProperties(f, shape.properties());
            writeEmptyRec(f, GDS_ENDEL);
        }
        break;
    }
    case Shape::Type::Path: {
        if (const Shape::PathData *path = shape.path()) {
            if (path->points.empty()) {
                break;
            }
            writeEmptyRec(f, GDS_PATH);
            writeLayerDatatype(f, layers, path->layerId);
            if (path->width > 0) {
                writeInt32(f, GDS_WIDTH, static_cast<std::int32_t>(path->width));
            }
            writeXY(f, path->points);
            writeGdsProperties(f, shape.properties());
            writeEmptyRec(f, GDS_ENDEL);
        }
        break;
    }
    case Shape::Type::Text: {
        if (const Shape::TextData *text = shape.text()) {
            const LayerSpec *spec = layerSpec(layers, text->layerId);
            const std::uint16_t layer = spec ? spec->layerNum : 0;
            const std::uint16_t textType = spec ? spec->dataType : 0;
            writeEmptyRec(f, GDS_TEXT);
            writeInt16(f, GDS_LAYER, static_cast<std::int16_t>(layer));
            writeInt16(f, GDS_TEXTTYPE, static_cast<std::int16_t>(textType));
            if (text->height > 0) {
                writeInt32(f, GDS_WIDTH, static_cast<std::int32_t>(text->height));
            }
            writeXY(f, {text->position});
            writeString(f, GDS_STRING, text->text);
            writeGdsProperties(f, shape.properties());
            writeEmptyRec(f, GDS_ENDEL);
        }
        break;
    }
    }
}

void writeInstance(FILE *f, const Instance &inst)
{
    writeEmptyRec(f, GDS_SREF);
    writeString(f, GDS_SNAME, inst.cellName());
    const Transform &t = inst.transform();
    const std::uint16_t strans = orientToGdsStrans(t.orient);
    if (strans != 0) {
        writeInt16(f, GDS_STRANS, static_cast<std::int16_t>(strans));
    }
    if (t.mag != 1.0) {
        writeGdsReal(f, GDS_MAG, t.mag);
    }
    writeXY(f, {Point{t.x, t.y}});
    writeGdsProperties(f, inst.properties());
    writeEmptyRec(f, GDS_ENDEL);
}

void writeCell(FILE *f, const std::vector<LayerSpec> &layers, const Cell &cell)
{
    const CellContent *content = cell.findContent(ViewType::Layout);
    if (!content) {
        return;
    }

    writeTime(f, GDS_BGNSTR);
    writeString(f, GDS_STRNAME, cell.name());
    writeGdsProperties(f, cell.properties());

    for (const Shape &shape : content->block().shapes()) {
        writeShape(f, layers, shape);
    }
    for (const Instance &inst : content->block().instances()) {
        writeInstance(f, inst);
    }

    writeEmptyRec(f, GDS_ENDSTR);
}

} // namespace

/*!****************************************************************************************
 * \brief Exports all layout cells from a Database to a GDSII file.
 * \param db         Source database.
 * \param gdsPath    Output .gds path.
 *****************************************************************************************/
void GdsExporter::exportFile(const Database &db, const std::string &gdsPath) const
{
    m_warnings.clear();
    m_errors.clear();

    FILE *f = std::fopen(gdsPath.c_str(), "wb");
    if (!f) {
        m_errors.push_back("Cannot open GDS file for writing: " + gdsPath);
        return;
    }

    double dbuPerMicron = 1000.0;
    for (const Cell &cell : db.lib().cells()) {
        if (const CellContent *content = cell.findContent(ViewType::Layout)) {
            if (content->dbuPerMicron() > 0.0) {
                dbuPerMicron = content->dbuPerMicron();
                break;
            }
        }
    }

    writeInt16(f, GDS_HEADER, 600);
    writeTime(f, GDS_BGNLIB);
    writeString(f, GDS_LIBNAME, db.lib().name());
    writeUnits(f, dbuPerMicron);

    for (const Cell &cell : db.lib().cells()) {
        const CellContent *content = cell.findContent(ViewType::Layout);
        if (!content) {
            continue;
        }
        writeCell(f, resolveViewLayers(*content, db.lib()), cell);
    }

    writeEmptyRec(f, GDS_ENDLIB);
    std::fclose(f);
}

} // namespace room
