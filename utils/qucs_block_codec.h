#pragma once

#include "block.h"
#include "instance.h"
#include "layer_spec.h"
#include "primitive_resolver.h"
#include "property.h"

#include <cstdint>
#include <string>
#include <vector>

namespace core::qucs_codec {

struct WireRecord {
    std::int64_t x1 = 0;
    std::int64_t y1 = 0;
    std::int64_t x2 = 0;
    std::int64_t y2 = 0;
    std::string label;
    std::int64_t labelX = 0;
    std::int64_t labelY = 0;
    std::int64_t dist = 0;
    std::string nodeSet;
};

struct CodecOptions {
    std::string qucsVersion = "0.0.19";
    std::vector<std::string> primitiveCorePaths;
    std::string techLibrary;
    std::string qucsPrimitiveLib;
};

std::vector<std::string> collectProperties(const std::vector<Property> &props, const std::string &prefix);

std::string formatComponentLine(const Instance &inst, double dbuPerEditorUnit, const PrimitiveResolver *resolver,
                                const std::string &qucsLibrary);

std::vector<std::string> formatWireLines(const Block &block, const std::vector<LayerSpec> &layers,
                                         double dbuPerEditorUnit, const std::string &sourceFormat);

Instance parseComponentLine(const std::string &line, std::vector<std::string> &warnings);

WireRecord parseWireLine(const std::string &line, std::vector<std::string> &warnings);

void importWireLinesToBlock(const std::vector<std::string> &lines, Block &block, double dbuPerEditorUnit,
                            std::vector<std::string> &warnings);

std::int64_t computeDisplayDivisor(const Block &block, const std::vector<LayerSpec> &layers, double dbuPerEditorUnit);

std::string scaleComponentLineCoordinates(const std::string &line, std::int64_t divisor, bool multiply);

std::string scaleWireLineCoordinates(const std::string &line, std::int64_t divisor, bool multiply);

} // namespace core::qucs_codec
