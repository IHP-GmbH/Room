#pragma once

#include <string>
#include <vector>

namespace room {

class Database;

class OasWriter {
public:
    explicit OasWriter(std::string fileName);

    void                                                createMinimalFile(const std::string &cellName);
    void                                                exportDatabase(const Database &db);

    const std::vector<std::string> &                    errors() const { return m_errors; }

private:
    std::string                                         m_fileName;
    std::vector<std::string>                            m_errors;
};

} // namespace room
