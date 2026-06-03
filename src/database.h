#pragma once

#include "lib.h"

#include <string>

namespace cdb {

class Database {
public:
    Database();

    const std::string &version() const { return version_; }
    const std::string &generator() const { return generator_; }
    const std::string &technology() const { return technology_; }

    void setGenerator(std::string value) { generator_ = std::move(value); }
    void setTechnology(std::string value) { technology_ = std::move(value); }
    void setVersion(std::string value) { version_ = std::move(value); }

    Lib &lib() { return lib_; }
    const Lib &lib() const { return lib_; }

    void saveToFile(const std::string &path) const;
    static Database loadFromFile(const std::string &path);

private:
    std::string version_ = "0.1";
    std::string generator_;
    std::string technology_;
    Lib lib_{"default"};
};

} // namespace cdb
