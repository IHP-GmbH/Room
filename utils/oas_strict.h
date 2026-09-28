#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace room {

constexpr const char kStrictHeaderProp[] = "core.oas.strictHeader";
constexpr const char kStrictTailProp[] = "core.oas.strictTail";
constexpr const char kStrictGeometryProp[] = "core.oas.strictGeometry";
constexpr const char kStrictRefProp[] = "core.oas.strictRef";

bool generateStrictTemplate(const std::vector<std::string> &cellNames,
                            const std::string &templatePath,
                            std::vector<std::string> &errors);

bool assembleStrictOas(const std::string &templatePath,
                       const std::vector<std::string> &cellNames,
                       const std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> &geometryByRef,
                       const std::string &outPath,
                       std::vector<std::string> &errors);

bool extractStrictGeometryFromFile(const std::string &path,
                                   std::size_t cellCount,
                                   std::vector<std::uint8_t> &header,
                                   std::vector<std::uint8_t> &tail,
                                   std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> &geometryByRef,
                                   std::vector<std::string> &errors);

bool assembleStrictFromPreserved(const std::vector<std::uint8_t> &header,
                                 const std::vector<std::uint8_t> &tail,
                                 std::size_t cellCount,
                                 const std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> &geometryByRef,
                                 const std::string &outPath,
                                 std::vector<std::string> &errors);

std::string base64Encode(const std::vector<std::uint8_t> &data);
bool base64Decode(const std::string &encoded, std::vector<std::uint8_t> &out);

} // namespace room
