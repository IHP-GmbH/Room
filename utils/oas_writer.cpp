#include "oas_writer.h"

#include "cell.h"
#include "database.h"
#include "layer_utils.h"
#include "oas_strict.h"
#include "property.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <zlib.h>

namespace core {
namespace {

void appendUInt(std::vector<std::uint8_t> &buf, std::uint64_t value)
{
    do {
        std::uint8_t b = static_cast<std::uint8_t>(value & 0x7F);
        value >>= 7;
        if (value != 0) {
            b |= 0x80;
        }
        buf.push_back(b);
    } while (value != 0);
}

void appendOasString(std::vector<std::uint8_t> &buf, const std::string &s)
{
    appendUInt(buf, s.size());
    buf.insert(buf.end(), s.begin(), s.end());
}

void appendSInt(std::vector<std::uint8_t> &buf, std::int64_t value)
{
    std::uint64_t encoded = 0;
    if (value < 0) {
        encoded = (static_cast<std::uint64_t>(-value) << 1) - 1ULL;
    } else {
        encoded = static_cast<std::uint64_t>(value) << 1;
    }
    appendUInt(buf, encoded);
}

void appendNString(std::vector<std::uint8_t> &buf, const std::string &s)
{
    appendUInt(buf, s.size());
    buf.insert(buf.end(), s.begin(), s.end());
}

void appendRecord(std::vector<std::uint8_t> &fileData, const std::vector<std::uint8_t> &recordBody)
{
    fileData.insert(fileData.end(), recordBody.begin(), recordBody.end());
}

bool deflateRawDeflate(const std::uint8_t *src, std::size_t srcLen, std::vector<std::uint8_t> &out)
{
    out.clear();
    if (srcLen == 0) {
        return true;
    }

    const uLong bound = compressBound(static_cast<uLong>(srcLen));
    out.resize(static_cast<std::size_t>(bound));

    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    zs.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(src));
    zs.avail_in = uInt(srcLen);
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = uInt(bound);

    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }

    const int rc = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    if (rc != Z_STREAM_END) {
        return false;
    }

    out.resize(zs.total_out);
    return true;
}

void appendCBlock(std::vector<std::uint8_t> &fileData, const std::vector<std::uint8_t> &payload)
{
    std::vector<std::uint8_t> compressed;
    if (!deflateRawDeflate(payload.data(), payload.size(), compressed)) {
        return;
    }

    std::vector<std::uint8_t> rec;
    appendUInt(rec, 34);
    appendUInt(rec, 0);
    appendUInt(rec, payload.size());
    appendUInt(rec, compressed.size());
    rec.insert(rec.end(), compressed.begin(), compressed.end());
    appendRecord(fileData, rec);
}

std::vector<Point> normalizedPolygonPoints(const std::vector<Point> &points)
{
    if (points.size() < 3) {
        return {};
    }

    std::vector<Point> normalized = points;
    if (normalized.front().x == normalized.back().x && normalized.front().y == normalized.back().y) {
        normalized.pop_back();
    }
    if (normalized.size() < 3) {
        return {};
    }
    return normalized;
}

void appendRectangle(std::vector<std::uint8_t> &buf,
                     std::uint16_t layer,
                     std::uint16_t datatype,
                     std::int64_t x,
                     std::int64_t y,
                     std::uint64_t w,
                     std::uint64_t h);

bool tryAxisAlignedRect(const std::vector<Point> &points,
                        std::int64_t &x,
                        std::int64_t &y,
                        std::uint64_t &w,
                        std::uint64_t &h)
{
    if (points.size() != 4) {
        return false;
    }

    std::int64_t minX = points[0].x;
    std::int64_t maxX = points[0].x;
    std::int64_t minY = points[0].y;
    std::int64_t maxY = points[0].y;
    for (const Point &pt : points) {
        minX = std::min(minX, pt.x);
        maxX = std::max(maxX, pt.x);
        minY = std::min(minY, pt.y);
        maxY = std::max(maxY, pt.y);
    }

    if (minX >= maxX || minY >= maxY) {
        return false;
    }

    int cornerCount = 0;
    for (const Point &pt : points) {
        const bool onCorner = (pt.x == minX || pt.x == maxX) && (pt.y == minY || pt.y == maxY);
        if (!onCorner) {
            return false;
        }
        ++cornerCount;
    }
    if (cornerCount != 4) {
        return false;
    }

    x = minX;
    y = minY;
    w = static_cast<std::uint64_t>(maxX - minX);
    h = static_cast<std::uint64_t>(maxY - minY);
    return w > 0 && h > 0;
}

void appendOrthogonalPolygonAsRects(std::vector<std::uint8_t> &buf,
                                    std::uint16_t layer,
                                    std::uint16_t datatype,
                                    const std::vector<Point> &points)
{
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::uint64_t w = 0;
    std::uint64_t h = 0;
    if (tryAxisAlignedRect(points, x, y, w, h)) {
        appendRectangle(buf, layer, datatype, x, y, w, h);
        return;
    }

    std::vector<std::int64_t> xs;
    std::vector<std::int64_t> ys;
    xs.reserve(points.size());
    ys.reserve(points.size());
    for (const Point &pt : points) {
        xs.push_back(pt.x);
        ys.push_back(pt.y);
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());

    if (xs.size() < 2 || ys.size() < 2) {
        return;
    }

    for (std::size_t yi = 0; yi + 1 < ys.size(); ++yi) {
        const std::int64_t y0 = ys[yi];
        const std::int64_t y1 = ys[yi + 1];
        const std::int64_t sampleY = y0 + (y1 - y0) / 2;
        for (std::size_t xi = 0; xi + 1 < xs.size(); ++xi) {
            const std::int64_t x0 = xs[xi];
            const std::int64_t x1 = xs[xi + 1];
            const std::int64_t sampleX = x0 + (x1 - x0) / 2;

            int inside = 0;
            for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
                const Point &a = points[j];
                const Point &b = points[i];
                if ((a.y > sampleY) != (b.y > sampleY)) {
                    const double xCross =
                        static_cast<double>(b.x - a.x) * (sampleY - a.y) / static_cast<double>(b.y - a.y) + a.x;
                    if (sampleX < static_cast<std::int64_t>(xCross)) {
                        ++inside;
                    }
                }
            }
            if ((inside & 1) == 0) {
                continue;
            }

            const std::uint64_t rw = static_cast<std::uint64_t>(x1 - x0);
            const std::uint64_t rh = static_cast<std::uint64_t>(y1 - y0);
            if (rw > 0 && rh > 0) {
                appendRectangle(buf, layer, datatype, x0, y0, rw, rh);
            }
        }
    }
}

void appendPathAsRects(std::vector<std::uint8_t> &buf,
                       std::uint16_t layer,
                       std::uint16_t datatype,
                       const std::vector<Point> &points,
                       std::uint32_t width)
{
    if (points.size() < 2 || width == 0) {
        return;
    }

    const std::int64_t half = static_cast<std::int64_t>(width) / 2;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Point &p0 = points[i - 1];
        const Point &p1 = points[i];
        if (p0.x == p1.x) {
            const std::int64_t y0 = std::min(p0.y, p1.y);
            const std::int64_t y1 = std::max(p0.y, p1.y);
            const std::uint64_t h = static_cast<std::uint64_t>(y1 - y0);
            if (h > 0) {
                appendRectangle(buf, layer, datatype, p0.x - half, y0, width, h);
            }
        } else if (p0.y == p1.y) {
            const std::int64_t x0 = std::min(p0.x, p1.x);
            const std::int64_t x1 = std::max(p0.x, p1.x);
            const std::uint64_t w = static_cast<std::uint64_t>(x1 - x0);
            if (w > 0) {
                appendRectangle(buf, layer, datatype, x0, p0.y - half, w, width);
            }
        }
    }
}

void appendStartRecord(std::vector<std::uint8_t> &fileData, double dbuPerMicron)
{
    std::vector<std::uint8_t> rec;
    appendUInt(rec, 1);
    appendOasString(rec, "1.0");
    appendUInt(rec, 0);
    appendUInt(rec, static_cast<std::uint64_t>(dbuPerMicron > 0.0 ? dbuPerMicron : 1000.0));
    appendUInt(rec, 0);
    for (int i = 0; i < 12; ++i) {
        appendUInt(rec, 0);
    }
    appendRecord(fileData, rec);
}

void appendEndRecord(std::vector<std::uint8_t> &fileData)
{
    // END: record 2 + padding-string (252 zero bytes) + validation-scheme 0 (256 bytes total).
    std::vector<std::uint8_t> rec;
    appendUInt(rec, 2);
    constexpr std::uint64_t padLen = 252;
    appendUInt(rec, padLen);
    rec.insert(rec.end(), padLen, 0);
    appendUInt(rec, 0);
    appendRecord(fileData, rec);
}

const LayerSpec *viewLayerAt(const CellContent &content, const Lib &lib, std::uint32_t layerId)
{
    const std::vector<LayerSpec> &layers = resolveViewLayers(content, lib);
    if (layerId >= layers.size()) {
        return nullptr;
    }
    return &layers[layerId];
}

void appendPlacement(std::vector<std::uint8_t> &buf,
                     const Instance &inst,
                     const std::unordered_map<std::string, std::uint64_t> &cellRefs)
{
    const auto refIt = cellRefs.find(inst.cellName());
    if (refIt == cellRefs.end()) {
        return;
    }
    std::vector<std::uint8_t> rec;
    appendUInt(rec, 17);
    rec.push_back(0x16); // C X Y explicit placement by reference-number
    appendUInt(rec, refIt->second);
    appendSInt(rec, inst.transform().x);
    appendSInt(rec, inst.transform().y);
    buf.insert(buf.end(), rec.begin(), rec.end());
}

void appendRectangle(std::vector<std::uint8_t> &buf,
                     std::uint16_t layer,
                     std::uint16_t datatype,
                     std::int64_t x,
                     std::int64_t y,
                     std::uint64_t w,
                     std::uint64_t h)
{
    std::vector<std::uint8_t> rec;
    appendUInt(rec, 20);
    rec.push_back(0x7B); // L D Y X H W explicit (no R — KLayout strict rejects explicit rep=0)
    appendUInt(rec, layer);
    appendUInt(rec, datatype);
    appendUInt(rec, w);
    appendUInt(rec, h);
    appendSInt(rec, x);
    appendSInt(rec, y);
    buf.insert(buf.end(), rec.begin(), rec.end());
}

void appendPolygon(std::vector<std::uint8_t> &buf,
                   std::uint16_t layer,
                   std::uint16_t datatype,
                   const std::vector<Point> &points)
{
    if (points.size() < 3) {
        return;
    }
    std::vector<std::uint8_t> rec;
    appendUInt(rec, 21);
    rec.push_back(0x23); // L D P explicit
    appendUInt(rec, layer);
    appendUInt(rec, datatype);
    appendUInt(rec, 3); // absolute (x,y) pairs
    appendUInt(rec, points.size());
    for (const Point &pt : points) {
        appendSInt(rec, pt.x);
        appendSInt(rec, pt.y);
    }
    buf.insert(buf.end(), rec.begin(), rec.end());
}

void appendPath(std::vector<std::uint8_t> &buf,
                std::uint16_t layer,
                std::uint16_t datatype,
                const std::vector<Point> &points,
                std::uint32_t width)
{
    if (points.size() < 2) {
        return;
    }
    std::vector<std::uint8_t> rec;
    appendUInt(rec, 22);
    rec.push_back(0x63); // L D P W explicit
    appendUInt(rec, layer);
    appendUInt(rec, datatype);
    appendUInt(rec, width);
    appendUInt(rec, 3);
    appendUInt(rec, points.size());
    for (const Point &pt : points) {
        appendSInt(rec, pt.x);
        appendSInt(rec, pt.y);
    }
    buf.insert(buf.end(), rec.begin(), rec.end());
}

void appendText(std::vector<std::uint8_t> &buf,
                std::uint16_t layer,
                std::uint16_t datatype,
                const std::string &text,
                std::int64_t x,
                std::int64_t y,
                std::uint32_t height)
{
    std::vector<std::uint8_t> rec;
    appendUInt(rec, 19);
    rec.push_back(0x7E); // C N X Y R T L
    appendOasString(rec, text);
    appendUInt(rec, layer);
    appendUInt(rec, datatype);
    appendSInt(rec, x);
    appendSInt(rec, y);
    appendUInt(rec, 0);
    buf.insert(buf.end(), rec.begin(), rec.end());
    (void)height;
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

void appendCellGeometry(std::vector<std::uint8_t> &buf,
                        const CellContent &content,
                        const Lib &lib,
                        const std::unordered_map<std::string, std::uint64_t> &cellRefs)
{
    for (const Instance &inst : content.block().instances()) {
        appendPlacement(buf, inst, cellRefs);
    }

    for (const Shape &shape : content.block().shapes()) {
        switch (shape.type()) {
        case Shape::Type::Rect: {
            if (const Shape::RectData *rect = shape.rect()) {
                const LayerSpec *spec = viewLayerAt(content, lib, rect->layerId);
                const std::uint16_t layer = spec ? spec->layerNum : 0;
                const std::uint16_t datatype = spec ? spec->dataType : 0;
                const std::uint64_t w = static_cast<std::uint64_t>(rect->box.urx - rect->box.llx);
                const std::uint64_t h = static_cast<std::uint64_t>(rect->box.ury - rect->box.lly);
                appendRectangle(buf, layer, datatype, rect->box.llx, rect->box.lly, w, h);
            }
            break;
        }
        case Shape::Type::Polygon: {
            if (const Shape::PolygonData *poly = shape.polygon()) {
                const LayerSpec *spec = viewLayerAt(content, lib, poly->layerId);
                const std::vector<Point> points = normalizedPolygonPoints(poly->points);
                if (!points.empty()) {
                    appendOrthogonalPolygonAsRects(buf,
                                                   spec ? spec->layerNum : 0,
                                                   spec ? spec->dataType : 0,
                                                   points);
                }
            }
            break;
        }
        case Shape::Type::Path: {
            if (const Shape::PathData *path = shape.path()) {
                const LayerSpec *spec = viewLayerAt(content, lib, path->layerId);
                appendPathAsRects(buf,
                                  spec ? spec->layerNum : 0,
                                  spec ? spec->dataType : 0,
                                  path->points,
                                  path->width);
            }
            break;
        }
        case Shape::Type::Text: {
            if (const Shape::TextData *text = shape.text()) {
                const LayerSpec *spec = viewLayerAt(content, lib, text->layerId);
                appendText(buf,
                           spec ? spec->layerNum : 0,
                           spec ? spec->dataType : 0,
                           text->text,
                           text->position.x,
                           text->position.y,
                           text->height);
            }
            break;
        }
        }
    }
}

bool writeBytes(const std::string &path, const std::vector<std::uint8_t> &data, std::vector<std::string> &errors)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        errors.push_back("Failed to open file '" + path + "'");
        return false;
    }
    if (!data.empty()) {
        out.write(reinterpret_cast<const char *>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
    return static_cast<bool>(out);
}

} // namespace

OasWriter::OasWriter(std::string fileName)
    : m_fileName(std::move(fileName))
{
}

void OasWriter::createMinimalFile(const std::string &cellName)
{
    m_errors.clear();

    std::vector<std::uint8_t> fileData;
    const char magic[] = "%SEMI-OASIS\r\n";
    fileData.insert(fileData.end(), magic, magic + sizeof(magic) - 1);

    std::vector<std::uint8_t> startRec;
    appendUInt(startRec, 1);
    appendOasString(startRec, "1.0");
    appendUInt(startRec, 0);
    appendUInt(startRec, 1000);
    appendUInt(startRec, 0);
    for (int i = 0; i < 12; ++i) {
        appendUInt(startRec, 0);
    }
    fileData.insert(fileData.end(), startRec.begin(), startRec.end());

    std::vector<std::uint8_t> cellNameRec;
    appendUInt(cellNameRec, 3);
    appendNString(cellNameRec, cellName);
    fileData.insert(fileData.end(), cellNameRec.begin(), cellNameRec.end());

    std::vector<std::uint8_t> cellRec;
    appendUInt(cellRec, 13);
    appendUInt(cellRec, 0);
    fileData.insert(fileData.end(), cellRec.begin(), cellRec.end());

    appendEndRecord(fileData);

    writeBytes(m_fileName, fileData, m_errors);
}

const std::string *findProperty(const std::vector<Property> &properties, const std::string &name)
{
    for (const Property &prop : properties) {
        if (prop.name == name) {
            return &prop.value;
        }
    }
    return nullptr;
}

bool exportPreservedStrictOas(const Database &db,
                              const std::string &outPath,
                              std::vector<std::string> &errors)
{
    const std::string *headerB64 = findProperty(db.lib().properties(), kStrictHeaderProp);
    if (headerB64 == nullptr) {
        return false;
    }

    std::vector<std::uint8_t> header;
    if (!base64Decode(*headerB64, header)) {
        errors.push_back("Failed to decode preserved strict OAS header");
        return false;
    }

    std::vector<std::uint8_t> tail;
    if (const std::string *tailB64 = findProperty(db.lib().properties(), kStrictTailProp)) {
        if (!base64Decode(*tailB64, tail)) {
            errors.push_back("Failed to decode preserved strict OAS tail");
            return false;
        }
    }

    std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> geometryByRef;
    for (const Cell &cell : db.lib().cells()) {
        const std::string *refText = findProperty(cell.properties(), kStrictRefProp);
        const std::string *geomB64 = findProperty(cell.properties(), kStrictGeometryProp);
        if (refText == nullptr || geomB64 == nullptr) {
            continue;
        }
        const std::uint64_t ref = static_cast<std::uint64_t>(std::stoull(*refText));
        if (!base64Decode(*geomB64, geometryByRef[ref])) {
            errors.push_back("Failed to decode preserved strict geometry for cell '" + cell.name() + "'");
            return false;
        }
    }

    return assembleStrictFromPreserved(header,
                                       tail,
                                       db.lib().cells().size(),
                                       geometryByRef,
                                       outPath,
                                       errors);
}

void OasWriter::exportDatabase(const Database &db)
{
    m_errors.clear();

    if (exportPreservedStrictOas(db, m_fileName, m_errors)) {
        return;
    }
    if (!m_errors.empty()) {
        return;
    }

    std::vector<std::string> cellNames;
    std::unordered_map<std::string, std::uint64_t> cellRefs;
    std::uint64_t nextRef = 0;
    for (const Cell &cell : db.lib().cells()) {
        cellNames.push_back(cell.name());
        cellRefs[cell.name()] = nextRef++;
    }

    std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> geometryByRef;
    for (const Cell &cell : db.lib().cells()) {
        const auto refIt = cellRefs.find(cell.name());
        if (refIt == cellRefs.end()) {
            continue;
        }
        const CellContent *content = cell.findContent(ViewType::Layout);
        if (content == nullptr) {
            continue;
        }
        appendCellGeometry(geometryByRef[refIt->second], *content, db.lib(), cellRefs);
    }

    const std::string templatePath = siblingPath(m_fileName, ".strict_tpl.oas");
    if (!generateStrictTemplate(cellNames, templatePath, m_errors)) {
        return;
    }

    if (!assembleStrictOas(templatePath, cellNames, geometryByRef, m_fileName, m_errors)) {
        return;
    }

    std::remove(templatePath.c_str());
    std::remove(siblingPath(templatePath, ".cellnames").c_str());
}

} // namespace core
