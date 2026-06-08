#pragma once

#include <string>
#include <vector>

namespace core {

class OasWriter {
public:
    explicit OasWriter(std::string fileName);

    void createMinimalFile(const std::string &cellName);

    const std::vector<std::string> &errors() const { return errors_; }

private:
    std::string fileName_;
    std::vector<std::string> errors_;
};

} // namespace core
