#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace core {

struct OasHierarchy {
    std::vector<std::string> topCells;
    std::unordered_map<std::string, std::vector<std::string>> children;
    std::unordered_set<std::string> allCells;
};

class OasReader {
public:
    explicit OasReader(std::string fileName);

    bool                                                readHierarchy(OasHierarchy &out);

    const std::vector<std::string> &                    errors() const { return m_errors; }
    const std::vector<std::string> &                    warnings() const { return m_warnings; }

private:
    std::string                                         m_fileName;
    std::vector<std::string>                            m_errors;
    std::vector<std::string>                            m_warnings;
};

} // namespace core
