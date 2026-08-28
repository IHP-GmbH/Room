#include "primitive_resolver.h"

#include "core_paths.h"

#include <cstdlib>
#include <fstream>

namespace core {
namespace {

std::string toLower(std::string value)
{
    for (char &ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool endsWith(const std::string &text, const std::string &suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string fileNameOnly(const std::string &path)
{
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string dirnameOf(const std::string &path)
{
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return {};
    }
    return path.substr(0, slash);
}

std::string joinPath(const std::string &left, const std::string &right)
{
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    if (left.back() == '/' || left.back() == '\\') {
        return left + right;
    }
    return left + "/" + right;
}

std::string logicalPrimitiveRef(const std::string &techLibrary, const std::string &cellName)
{
    if (techLibrary.empty()) {
        return coreFileName(cellName, ViewType::Symbol);
    }
    return techLibrary + "/" + coreFileName(cellName, ViewType::Symbol);
}

void splitTechAndStem(const std::string &ref, std::string &techLibrary, std::string &stem)
{
    techLibrary.clear();
    stem = ref;
    const std::size_t slash = ref.find_last_of("/\\");
    if (slash != std::string::npos) {
        techLibrary = ref.substr(0, slash);
        stem = ref.substr(slash + 1);
    }
}

std::string stemWithoutExtension(const std::string &stem)
{
    const std::size_t dot = stem.rfind('.');
    if (dot == std::string::npos) {
        return stem;
    }
    return stem.substr(0, dot);
}

} // namespace

void PrimitiveResolver::setTechLibrary(const std::string &techLibrary) { techLibrary_ = techLibrary; }

void PrimitiveResolver::setQucsLibrary(const std::string &qucsLibrary) { qucsLibrary_ = qucsLibrary; }

std::vector<std::string> PrimitiveResolver::splitListEnv(const char *value)
{
    std::vector<std::string> items;
    if (value == nullptr || *value == '\0') {
        return items;
    }
    std::string raw(value);
    const char split = raw.find(';') != std::string::npos ? ';' : ':';
    std::size_t start = 0;
    while (start < raw.size()) {
        std::size_t end = raw.find(split, start);
        if (end == std::string::npos) {
            end = raw.size();
        }
        std::string item = raw.substr(start, end - start);
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) {
            item.erase(item.begin());
        }
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) {
            item.pop_back();
        }
        if (!item.empty()) {
            items.push_back(item);
        }
        start = end + 1;
    }
    return items;
}

void PrimitiveResolver::loadFromEnvironment()
{
    if (const char *tech = std::getenv("LIBMAN_TECH_LIBRARY")) {
        setTechLibrary(tech);
    }
    if (const char *lib = std::getenv("QUCS_PRIMITIVE_LIB")) {
        setQucsLibrary(lib);
    } else if (!techLibrary_.empty()) {
        setQucsLibrary(techLibrary_);
    } else {
        setQucsLibrary("IHP_PDK_nonlinear_components");
    }

    for (const std::string &path : primitiveCorePathsFromEnvironment()) {
        addCorePath(path);
    }
}

std::vector<std::string> PrimitiveResolver::primitiveCorePathsFromEnvironment()
{
    std::vector<std::string> paths;
    if (const char *listFile = std::getenv("CORE_PRIMITIVE_LIBS_FILE")) {
        if (*listFile != '\0') {
            std::ifstream in(listFile);
            std::string line;
            while (std::getline(in, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                    line.pop_back();
                }
                std::size_t start = 0;
                while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
                    ++start;
                }
                line = line.substr(start);
                if (!line.empty()) {
                    paths.push_back(line);
                }
            }
        }
    }
    if (!paths.empty()) {
        return paths;
    }
    if (const char *libs = std::getenv("CORE_PRIMITIVE_LIBS")) {
        return splitListEnv(libs);
    }
    if (const char *single = std::getenv("CORE_PRIMITIVE_LIB")) {
        paths.push_back(single);
    }
    return paths;
}

void PrimitiveResolver::addCorePath(const std::string &corePath)
{
    if (corePath.empty()) {
        return;
    }
    corePaths_.push_back(corePath);

    const ParsedCorePath parsed = parseCoreFilePath(corePath);
    if (!parsed.valid || parsed.view != ViewType::Symbol) {
        return;
    }

    // Prefer library name from path: .../<techLib>/<cell>/<cell>.symbol.core
    // Do not use LIBMAN_TECH_LIBRARY when it is a multi-attach list ("a;b") — that used to
    // register keys like "sg13g2_pr;analogLib/cell.sym" which never match "sg13g2_pr/cell.sym".
    std::string techLibrary;
    const std::string cellDir = dirnameOf(corePath);
    const std::string libDir = fileNameOnly(dirnameOf(cellDir));
    if (!libDir.empty() && libDir != "." && libDir != "..") {
        techLibrary = libDir;
    }
    if (techLibrary.empty() && !techLibrary_.empty() && techLibrary_.find(';') == std::string::npos
        && techLibrary_.find(':') == std::string::npos) {
        techLibrary = techLibrary_;
    } else if (techLibrary.empty()) {
        const std::string parentDir = fileNameOnly(cellDir);
        if (!parentDir.empty() && parentDir != "." && parentDir != "..") {
            techLibrary = parentDir;
        }
    }

    Entry entry;
    entry.corePath = corePath;
    entry.cellName = parsed.cellName;
    entry.techLibrary = techLibrary;
    entry.logicalRef = logicalPrimitiveRef(techLibrary, parsed.cellName);
    registerEntry(entry);
}

void PrimitiveResolver::registerEntry(const Entry &entry)
{
    const std::string symName = entry.cellName + ".sym";
    index_[symName] = entry;
    index_[entry.cellName] = entry;
    index_[entry.logicalRef] = entry;
    index_[fileNameOnly(entry.corePath)] = entry;
    if (!entry.techLibrary.empty()) {
        index_[entry.techLibrary + "/" + symName] = entry;
        index_[entry.techLibrary + "/" + entry.cellName] = entry;
    }
}

ResolvedPrimitive PrimitiveResolver::resolveReference(const std::string &ref) const
{
    ResolvedPrimitive resolved;
    if (ref.empty()) {
        return resolved;
    }

    const auto it = index_.find(ref);
    if (it != index_.end()) {
        resolved.found = true;
        resolved.corePath = it->second.corePath;
        resolved.cellName = it->second.cellName;
        resolved.techLibrary = it->second.techLibrary;
        resolved.logicalRef = it->second.logicalRef;
        return resolved;
    }

    std::string techLibrary;
    std::string stem;
    splitTechAndStem(ref, techLibrary, stem);
    const std::string lowerStem = toLower(stem);

    if (endsWith(lowerStem, ".sym")) {
        const std::string cellName = stemWithoutExtension(stem);
        const std::string key = techLibrary.empty() ? cellName + ".sym" : techLibrary + "/" + cellName + ".sym";
        if (const auto hit = index_.find(key); hit != index_.end()) {
            resolved.found = true;
            resolved.corePath = hit->second.corePath;
            resolved.cellName = hit->second.cellName;
            resolved.techLibrary = hit->second.techLibrary;
            resolved.logicalRef = hit->second.logicalRef;
            return resolved;
        }

        // Multi-attach / Xschem refs: "sg13g2_pr/sg13_lv_nmos.sym" must still resolve via bare cell.
        if (!techLibrary.empty()) {
            if (const auto hit = index_.find(cellName + ".sym"); hit != index_.end()) {
                resolved.found = true;
                resolved.corePath = hit->second.corePath;
                resolved.cellName = hit->second.cellName;
                resolved.techLibrary = hit->second.techLibrary.empty() ? techLibrary : hit->second.techLibrary;
                resolved.logicalRef = hit->second.logicalRef;
                return resolved;
            }
            if (const auto hit = index_.find(cellName); hit != index_.end()) {
                resolved.found = true;
                resolved.corePath = hit->second.corePath;
                resolved.cellName = hit->second.cellName;
                resolved.techLibrary = hit->second.techLibrary.empty() ? techLibrary : hit->second.techLibrary;
                resolved.logicalRef = hit->second.logicalRef;
                return resolved;
            }
        }

        const std::string singleTech =
            (techLibrary_.find(';') == std::string::npos && techLibrary_.find(':') == std::string::npos)
                ? techLibrary_
                : std::string{};
        const std::string expectedLogical =
            logicalPrimitiveRef(techLibrary.empty() ? singleTech : techLibrary, cellName);
        if (const auto hit = index_.find(expectedLogical); hit != index_.end()) {
            resolved.found = true;
            resolved.corePath = hit->second.corePath;
            resolved.cellName = hit->second.cellName;
            resolved.techLibrary = hit->second.techLibrary;
            resolved.logicalRef = hit->second.logicalRef;
            return resolved;
        }

        for (const std::string &corePath : corePaths_) {
            const ParsedCorePath parsed = parseCoreFilePath(corePath);
            if (!parsed.valid || parsed.view != ViewType::Symbol || parsed.cellName != cellName) {
                continue;
            }
            if (!techLibrary.empty()) {
                const std::string parent = fileNameOnly(dirnameOf(corePath));
                const std::string libDir = fileNameOnly(dirnameOf(dirnameOf(corePath)));
                if (parent != techLibrary && libDir != techLibrary) {
                    continue;
                }
            }
            resolved.found = true;
            resolved.corePath = corePath;
            resolved.cellName = parsed.cellName;
            resolved.techLibrary = techLibrary.empty() ? singleTech : techLibrary;
            if (resolved.techLibrary.empty()) {
                resolved.techLibrary = fileNameOnly(dirnameOf(dirnameOf(corePath)));
            }
            resolved.logicalRef = logicalPrimitiveRef(resolved.techLibrary, parsed.cellName);
            return resolved;
        }
    }

    if (endsWith(lowerStem, ".core")) {
        const ParsedCorePath parsed = parseCoreFilePath(stem);
        if (parsed.valid && parsed.view == ViewType::Symbol) {
            for (const std::string &corePath : corePaths_) {
                if (fileNameOnly(corePath) == stem || corePath == ref) {
                    resolved.found = true;
                    resolved.corePath = corePath;
                    resolved.cellName = parsed.cellName;
                    resolved.techLibrary = techLibrary.empty() ? techLibrary_ : techLibrary;
                    resolved.logicalRef = logicalPrimitiveRef(resolved.techLibrary, parsed.cellName);
                    return resolved;
                }
            }
        }
    }

    return resolved;
}

} // namespace core
