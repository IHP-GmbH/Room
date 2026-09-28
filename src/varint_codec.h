#pragma once

#include <cstdint>
#include <vector>

namespace room {

std::vector<std::uint8_t> encodeVarint64(const std::vector<std::int64_t> &values);
std::vector<std::int64_t> decodeVarint64(const std::uint8_t *data, std::size_t size);

} // namespace room
