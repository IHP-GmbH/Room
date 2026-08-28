#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace core {

struct ResolvedPrimitive {
    bool        found = false;
    std::string corePath;
    std::string cellName;
    std::string techLibrary;
    std::string logicalRef;
};

class PrimitiveResolver {
public:
    void setTechLibrary(const std::string &techLibrary);
    void setQucsLibrary(const std::string &qucsLibrary);
    void addCorePath(const std::string &corePath);
    void loadFromEnvironment();

    ResolvedPrimitive resolveReference(const std::string &ref) const;

    /*! Reads CORE_PRIMITIVE_LIBS_FILE (newline-separated) or CORE_PRIMITIVE_LIBS env. */
    static std::vector<std::string> primitiveCorePathsFromEnvironment();

private:
    struct Entry {
        std::string corePath;
        std::string cellName;
        std::string techLibrary;
        std::string logicalRef;
    };

    void registerEntry(const Entry &entry);
    static std::vector<std::string> splitListEnv(const char *value);

    std::unordered_map<std::string, Entry> index_;
    std::vector<std::string>               corePaths_;
    std::string                            techLibrary_;
    std::string                            qucsLibrary_;
};

} // namespace core
