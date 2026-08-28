#include "qucs_block_codec.h"

#include "coord_scale.h"
#include "qucs_exporter.h"
#include "qucs_importer.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace core::qucs_codec {
namespace {

std::int64_t absCoord(std::int64_t value)
{
    return value < 0 ? -value : value;
}

void scaleFields(std::vector<std::string> &tokens, const std::vector<std::size_t> &indices, std::int64_t divisor, bool multiply)
{
    if (divisor <= 1) {
        return;
    }
    for (const std::size_t index : indices) {
        if (index >= tokens.size()) {
            continue;
        }
        const std::int64_t value = std::stoll(tokens[index]);
        tokens[index] = std::to_string(multiply ? value * divisor : value / divisor);
    }
}

std::string rebuildLine(const std::vector<std::string> &tokens)
{
    std::string line = "<";
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            line.push_back(' ');
        }
        line += tokens[i];
    }
    line.push_back('>');
    return line;
}

std::vector<std::string> splitQucsTokens(const std::string &line)
{
    std::vector<std::string> tokens;
    std::string current;
    bool inQuote = false;

    for (char ch : line) {
        if (ch == '"') {
            inQuote = !inQuote;
            current.push_back(ch);
            if (!inQuote) {
                tokens.push_back(current);
                current.clear();
            }
        } else if (!inQuote && std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

} // namespace

std::vector<std::string> collectProperties(const std::vector<Property> &props, const std::string &prefix)
{
    std::vector<std::string> lines;
    for (const Property &prop : props) {
        if (prop.name == prefix) {
            lines.push_back(prop.value);
        }
    }
    return lines;
}

std::string formatComponentLine(const Instance &inst, double dbuPerEditorUnit, const PrimitiveResolver *resolver,
                                const std::string &qucsLibrary)
{
    QucsExporter::Options options;
    if (resolver != nullptr) {
        (void)resolver;
    }
    (void)qucsLibrary;
    QucsExporter exporter(options);
    return exporter.exportInstanceAsLine(inst, dbuPerEditorUnit, "");
}

std::vector<std::string> formatWireLines(const Block &block, const std::vector<LayerSpec> &layers,
                                         double dbuPerEditorUnit, const std::string &sourceFormat)
{
    QucsExporter exporter;
    return exporter.exportBlockWiresAsLines(block, layers, dbuPerEditorUnit, sourceFormat);
}

Instance parseComponentLine(const std::string &line, std::vector<std::string> &warnings)
{
    QucsImporter importer;
    Instance inst = importer.parseComponentLinePublic(line);
    warnings.insert(warnings.end(), importer.warnings().begin(), importer.warnings().end());
    return inst;
}

WireRecord parseWireLine(const std::string &line, std::vector<std::string> &warnings)
{
    QucsImporter importer;
    const QucsImporter::WireRecord wire = importer.parseWireLinePublic(line);
    warnings.insert(warnings.end(), importer.warnings().begin(), importer.warnings().end());
    WireRecord record;
    record.x1 = wire.x1;
    record.y1 = wire.y1;
    record.x2 = wire.x2;
    record.y2 = wire.y2;
    record.label = wire.label;
    record.labelX = wire.labelX;
    record.labelY = wire.labelY;
    record.dist = wire.dist;
    record.nodeSet = wire.nodeSet;
    return record;
}

void importWireLinesToBlock(const std::vector<std::string> &lines, Block &block, double dbuPerEditorUnit,
                            std::vector<std::string> &warnings)
{
    QucsImporter importer;
    importer.importWireLines(block, lines, dbuPerEditorUnit);
    warnings.insert(warnings.end(), importer.warnings().begin(), importer.warnings().end());
}

std::int64_t computeDisplayDivisor(const Block &block, const std::vector<LayerSpec> &layers, double dbuPerEditorUnit)
{
    std::int64_t maxAbs = 0;
    for (const Instance &inst : block.instances()) {
        const std::int64_t x = static_cast<std::int64_t>(std::llround(dbuToEditorUnits(inst.transform().x, dbuPerEditorUnit)));
        const std::int64_t y = static_cast<std::int64_t>(std::llround(dbuToEditorUnits(inst.transform().y, dbuPerEditorUnit)));
        maxAbs = std::max(maxAbs, absCoord(x));
        maxAbs = std::max(maxAbs, absCoord(y));
    }

    QucsExporter exporter;
    std::vector<std::string> dummyWarnings;
    for (const std::string &line : exporter.exportBlockWiresAsLines(block, layers, dbuPerEditorUnit, "")) {
        std::string inner = line;
        if (inner.size() >= 2 && inner.front() == '<' && inner.back() == '>') {
            inner = inner.substr(1, inner.size() - 2);
        }
        const auto tokens = splitQucsTokens(inner);
        if (tokens.size() >= 4) {
            for (std::size_t i = 0; i < 4; ++i) {
                maxAbs = std::max(maxAbs, absCoord(std::stoll(tokens[i])));
            }
        }
        (void)dummyWarnings;
    }

    if (maxAbs <= 5000) {
        return 1;
    }
    return maxAbs > 50000 ? 1000 : 10;
}

std::string scaleComponentLineCoordinates(const std::string &line, std::int64_t divisor, bool multiply)
{
    if (divisor <= 1) {
        return line;
    }
    std::string inner = line;
    if (inner.size() >= 2 && inner.front() == '<' && inner.back() == '>') {
        inner = inner.substr(1, inner.size() - 2);
    }
    auto tokens = splitQucsTokens(inner);
    if (tokens.size() < 7) {
        return line;
    }
    scaleFields(tokens, {3, 4, 5, 6}, divisor, multiply);
    return rebuildLine(tokens);
}

std::string scaleWireLineCoordinates(const std::string &line, std::int64_t divisor, bool multiply)
{
    if (divisor <= 1) {
        return line;
    }
    std::string inner = line;
    if (inner.size() >= 2 && inner.front() == '<' && inner.back() == '>') {
        inner = inner.substr(1, inner.size() - 2);
    }
    auto tokens = splitQucsTokens(inner);
    if (tokens.size() < 4) {
        return line;
    }
    scaleFields(tokens, {0, 1, 2, 3}, divisor, multiply);
    return rebuildLine(tokens);
}

} // namespace core::qucs_codec
