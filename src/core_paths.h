#pragma once

#include "enums.h"

#include <optional>
#include <string>

namespace core {

constexpr const char *kCoreFileExtension = ".core";

std::string viewTypeFromString(const std::string &text);
std::optional<ViewType> parseViewTypeName(const std::string &text);
std::string coreFileGlob(ViewType view);
std::string coreFileName(const std::string &cellName, ViewType view);

struct ParsedCorePath {
    std::string                                         cellName;
    ViewType                                            view = ViewType::Layout;
    bool                                                valid = false;
};

ParsedCorePath parseCoreFilePath(const std::string &path);
bool isViewCoreFile(const std::string &path, ViewType view);
bool isCoreFilePath(const std::string &path);

} // namespace core
