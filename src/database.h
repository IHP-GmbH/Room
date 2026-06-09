#pragma once

#include "lib.h"

#include <string>

namespace core {

class Database {
public:
    Database();

    const std::string &                                 version() const { return m_version; }
    const std::string &                                 generator() const { return m_generator; }
    const std::string &                                 technology() const { return m_technology; }

    void                                                setGenerator(std::string value) { m_generator = std::move(value); }
    void                                                setTechnology(std::string value) { m_technology = std::move(value); }
    void                                                setVersion(std::string value) { m_version = std::move(value); }

    Lib &                                               lib() { return m_lib; }
    const Lib &                                         lib() const { return m_lib; }

    void                                                saveToFile(const std::string &path);
    static Database                                     loadFromFile(const std::string &path);

private:
    std::string                                         m_version = "1.0";
    std::string                                         m_generator;
    std::string                                         m_technology;
    Lib                                                 m_lib{"default"};
};

} // namespace core
