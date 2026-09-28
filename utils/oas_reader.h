#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace room {

class Database;

struct OasHierarchy {
    std::vector<std::string> topCells;
    std::unordered_map<std::string, std::vector<std::string>> children;
    std::unordered_set<std::string> allCells;
};

class OasReader {
public:
    explicit OasReader(std::string fileName);

    bool                                                readHierarchy(OasHierarchy &out);

    bool                                                importDatabase(Database &db,
                                                                  const std::string &libName = "oas_import",
                                                                  double defaultDbuPerMicron = 1000.0);

    const std::vector<std::string> &                    errors() const { return m_errors; }
    const std::vector<std::string> &                    warnings() const { return m_warnings; }
    const std::unordered_map<std::uint64_t, std::string> &cellNamesByRef() const { return m_cellNamesByRef; }

private:
    std::string                                         m_fileName;
    std::vector<std::string>                            m_errors;
    std::vector<std::string>                            m_warnings;
    std::unordered_map<std::uint64_t, std::string>      m_cellNamesByRef;
};

} // namespace room
