#include "gds_importer.h"

#include "cell.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace core {
namespace {

constexpr std::uint16_t GDS_UNITS    = 0x0305;
constexpr std::uint16_t GDS_ENDLIB   = 0x0400;
constexpr std::uint16_t GDS_STRNAME  = 0x0606;
constexpr std::uint16_t GDS_ENDSTR   = 0x0700;
constexpr std::uint16_t GDS_BOUNDARY = 0x0800;
constexpr std::uint16_t GDS_PATH     = 0x0900;
constexpr std::uint16_t GDS_SREF     = 0x0A00;
constexpr std::uint16_t GDS_AREF     = 0x0B00;
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
constexpr std::uint16_t GDS_BOX      = 0x2D00;
constexpr std::uint16_t GDS_BOXTYPE  = 0x2E02;

std::uint16_t be16(const std::uint8_t *p)
{
    return (static_cast<std::uint16_t>(p[0]) << 8) | p[1];
}

std::int32_t be32(const std::uint8_t *p)
{
    return (static_cast<std::int32_t>(p[0]) << 24) |
           (static_cast<std::int32_t>(p[1]) << 16) |
           (static_cast<std::int32_t>(p[2]) << 8) |
           static_cast<std::int32_t>(p[3]);
}

double readGdsReal(const std::uint8_t *p)
{
    const std::uint8_t sign = (p[0] & 0x80) ? 1 : 0;
    const int exp = static_cast<int>(p[0] & 0x7F) - 64;
    double mant = 0.0;
    for (int i = 1; i < 8; ++i) {
        mant += static_cast<double>(p[i]) * std::pow(256.0, -(i));
    }
    double value = mant * std::pow(16.0, exp);
    return sign ? -value : value;
}

std::string decodeGdsString(const std::uint8_t *payload, int len)
{
    while (len > 0 && payload[len - 1] == 0) {
        --len;
    }
    return std::string(reinterpret_cast<const char *>(payload), static_cast<std::size_t>(len));
}

Orient gdsStransToOrient(std::uint16_t strans)
{
    const bool reflect = (strans & 0x8000) != 0;
    const int angle = strans & 0x0003;
    if (reflect) {
        switch (angle) {
        case 0: return Orient::MX;
        case 1: return Orient::MX90;
        case 2: return Orient::MY;
        case 3: return Orient::MY90;
        }
    }
    switch (angle) {
    case 0: return Orient::R0;
    case 1: return Orient::R90;
    case 2: return Orient::R180;
    case 3: return Orient::R270;
    }
    return Orient::R0;
}

struct LayerKey {
    std::uint16_t layer = 0;
    std::uint16_t dataType = 0;

    bool operator==(const LayerKey &other) const
    {
        return layer == other.layer && dataType == other.dataType;
    }
};

struct LayerKeyHash {
    std::size_t operator()(const LayerKey &k) const
    {
        return (static_cast<std::size_t>(k.layer) << 16) | k.dataType;
    }
};

struct ElementDraft {
    enum class Type { None, Boundary, Path, Text, Box, Sref, Aref };

    Type type = Type::None;
    std::uint16_t layer = 0;
    std::uint16_t dataType = 0;
    std::uint32_t width = 0;
    std::vector<Point> points;
    std::string text;
    std::string sname;
    Transform transform{};
    std::uint16_t strans = 0;
    double mag = 1.0;
};

class GdsParser {
public:
    GdsParser(const std::string &path, GdsImporter::Options options,
                std::vector<std::string> &warnings, std::vector<std::string> &errors)
        : path_(path), options_(std::move(options)), warnings_(warnings), errors_(errors) {}

    Database parse()
    {
        std::ifstream in(path_, std::ios::binary);
        if (!in) {
            errors_.push_back("Cannot open GDS file: " + path_);
            return Database{};
        }

        in.seekg(0, std::ios::end);
        const auto fileSize = in.tellg();
        in.seekg(0, std::ios::beg);
        if (fileSize < 4) {
            errors_.push_back("GDS file too small: " + path_);
            return Database{};
        }

        std::vector<std::uint8_t> data(static_cast<std::size_t>(fileSize));
        in.read(reinterpret_cast<char *>(data.data()), fileSize);
        if (!in) {
            errors_.push_back("Failed to read GDS file: " + path_);
            return Database{};
        }

        Database db;
        db.setGenerator("CORE GdsImporter");
        db.lib() = Lib(options_.libName);

        bool sawEndLib = false;
        std::size_t offset = 0;
        Cell *currentCell = nullptr;
        ElementDraft draft;
        double dbuPerMicron = options_.defaultDbuPerMicron;

        auto layerIndex = [&](std::uint16_t layer, std::uint16_t dataType) -> std::uint32_t {
            LayerKey key{layer, dataType};
            auto it = layerMap_.find(key);
            if (it != layerMap_.end()) {
                return it->second;
            }
            const std::uint32_t id = static_cast<std::uint32_t>(globalLayers_.size());
            LayerSpec spec;
            spec.layerNum = layer;
            spec.dataType = dataType;
            std::ostringstream oss;
            oss << "L" << layer << "/D" << dataType;
            spec.name = oss.str();
            globalLayers_.push_back(spec);
            layerMap_[key] = id;
            return id;
        };

        auto flushDraft = [&]() {
            if (!currentCell || draft.type == ElementDraft::Type::None) {
                draft = ElementDraft{};
                return;
            }
            CellContent &content = currentCell->getOrCreateContent(ViewType::Layout, dbuPerMicron);
            Block &block = content.block();

            switch (draft.type) {
            case ElementDraft::Type::Boundary:
            case ElementDraft::Type::Box: {
                if (draft.points.size() >= 3) {
                    Shape::PolygonData poly;
                    poly.layerId = layerIndex(draft.layer, draft.dataType);
                    poly.points = draft.points;
                    if (!poly.points.empty() && poly.points.front().x == poly.points.back().x &&
                        poly.points.front().y == poly.points.back().y) {
                        poly.points.pop_back();
                    }
                    block.shapes().emplace_back(poly);
                }
                break;
            }
            case ElementDraft::Type::Path: {
                if (!draft.points.empty()) {
                    Shape::PathData path;
                    path.layerId = layerIndex(draft.layer, draft.dataType);
                    path.points = draft.points;
                    path.width = draft.width;
                    block.shapes().emplace_back(path);
                }
                break;
            }
            case ElementDraft::Type::Text: {
                Shape::TextData text;
                text.layerId = layerIndex(draft.layer, draft.dataType);
                text.text = draft.text;
                text.height = draft.width;
                if (!draft.points.empty()) {
                    text.position = draft.points.front();
                }
                block.shapes().emplace_back(text);
                break;
            }
            case ElementDraft::Type::Sref:
            case ElementDraft::Type::Aref: {
                draft.transform.orient = gdsStransToOrient(draft.strans);
                draft.transform.mag = draft.mag;
                block.instances().emplace_back(draft.sname, draft.transform);
                break;
            }
            default:
                break;
            }
            draft = ElementDraft{};
        };

        while (offset + 4 <= data.size()) {
            const std::uint16_t recLen = be16(data.data() + offset);
            const std::uint16_t recType = be16(data.data() + offset + 2);
            if (recLen < 4 || offset + recLen > data.size()) {
                if (sawEndLib) {
                    break;
                }
                errors_.push_back("Malformed GDS record at offset " + std::to_string(offset));
                break;
            }
            const std::uint8_t *payload = data.data() + offset + 4;
            const int payloadLen = static_cast<int>(recLen) - 4;

            switch (recType) {
            case GDS_UNITS:
                if (payloadLen >= 16) {
                    const double userPerDbu = readGdsReal(payload);
                    const double metersPerUser = readGdsReal(payload + 8);
                    const double metersPerDbu = userPerDbu * metersPerUser;
                    if (metersPerDbu > 0.0) {
                        dbuPerMicron = 1e-6 / metersPerDbu;
                    }
                }
                break;
            case GDS_STRNAME: {
                flushDraft();
                const std::string name = decodeGdsString(payload, payloadLen);
                Cell &cell = db.lib().getOrCreateCell(name);
                currentCell = &cell;
                break;
            }
            case GDS_ENDSTR:
                flushDraft();
                currentCell = nullptr;
                break;
            case GDS_BOUNDARY:
                flushDraft();
                draft.type = ElementDraft::Type::Boundary;
                break;
            case GDS_PATH:
                flushDraft();
                draft.type = ElementDraft::Type::Path;
                break;
            case GDS_TEXT:
                flushDraft();
                draft.type = ElementDraft::Type::Text;
                break;
            case GDS_BOX:
                flushDraft();
                draft.type = ElementDraft::Type::Box;
                break;
            case GDS_SREF:
                flushDraft();
                draft.type = ElementDraft::Type::Sref;
                break;
            case GDS_AREF:
                flushDraft();
                draft.type = ElementDraft::Type::Aref;
                warnings_.push_back("AREF geometry expansion is not implemented; storing as instance reference");
                break;
            case GDS_LAYER:
                if (payloadLen >= 2) {
                    draft.layer = be16(payload);
                }
                break;
            case GDS_DATATYPE:
            case GDS_TEXTTYPE:
            case GDS_BOXTYPE:
                if (payloadLen >= 2) {
                    draft.dataType = be16(payload);
                }
                break;
            case GDS_WIDTH:
                if (payloadLen >= 4) {
                    draft.width = static_cast<std::uint32_t>(be32(payload));
                } else if (payloadLen >= 2) {
                    draft.width = be16(payload);
                }
                break;
            case GDS_SNAME:
                draft.sname = decodeGdsString(payload, payloadLen);
                break;
            case GDS_STRING:
                draft.text = decodeGdsString(payload, payloadLen);
                break;
            case GDS_STRANS:
                if (payloadLen >= 2) {
                    draft.strans = be16(payload);
                }
                break;
            case GDS_MAG:
                if (payloadLen >= 8) {
                    draft.mag = readGdsReal(payload);
                }
                break;
            case GDS_XY:
                draft.points.clear();
                for (int i = 0; i + 8 <= payloadLen; i += 8) {
                    draft.points.push_back({be32(payload + i), be32(payload + i + 4)});
                }
                if ((draft.type == ElementDraft::Type::Sref || draft.type == ElementDraft::Type::Aref) &&
                    !draft.points.empty()) {
                    draft.transform.x = draft.points.front().x;
                    draft.transform.y = draft.points.front().y;
                }
                break;
            case GDS_ENDEL:
                flushDraft();
                break;
            case GDS_ENDLIB:
                sawEndLib = true;
                break;
            default:
                break;
            }

            offset += recLen;
        }

        if (!sawEndLib) {
            warnings_.push_back("ENDLIB not found; file may be truncated");
        }

        for (auto &cell : db.lib().cells()) {
            if (CellContent *content = cell.findContent(ViewType::Layout)) {
                content->setDbuPerMicron(dbuPerMicron);
                content->block().recomputeBBox();
            }
        }

        db.lib().layers() = globalLayers_;

        return db;
    }

private:
    std::string path_;
    GdsImporter::Options options_;
    std::vector<std::string> &warnings_;
    std::vector<std::string> &errors_;
    std::vector<LayerSpec> globalLayers_;
    std::unordered_map<LayerKey, std::uint32_t, LayerKeyHash> layerMap_;
};

} // namespace

GdsImporter::GdsImporter() : options_{} {}
GdsImporter::GdsImporter(const Options &options) : options_(options) {}

Database GdsImporter::importFile(const std::string &gdsPath) const
{
    warnings_.clear();
    errors_.clear();
    GdsParser parser(gdsPath, options_, warnings_, errors_);
    Database db = parser.parse();
    if (!errors_.empty()) {
        return Database{};
    }
    return db;
}

} // namespace core
