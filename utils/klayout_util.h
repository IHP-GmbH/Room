#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace room {

std::string findKLayoutExecutable();

bool runKLayoutBatch(const std::string &scriptPath,
                     const std::unordered_map<std::string, std::string> &variables,
                     std::vector<std::string> &errors);

} // namespace room
