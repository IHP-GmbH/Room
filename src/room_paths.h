#pragma once

#include "enums.h"

#include <optional>
#include <string>

namespace room {

constexpr const char *kRoomFileExtension = ".room";

std::string viewTypeFromString(const std::string &text);
std::optional<ViewType> parseViewTypeName(const std::string &text);
std::string roomFileGlob(ViewType view);
std::string roomFileName(const std::string &cellName, ViewType view);

struct ParsedRoomPath {
    std::string                                         cellName;
    ViewType                                            view = ViewType::Layout;
    bool                                                valid = false;
};

ParsedRoomPath parseRoomFilePath(const std::string &path);
bool isViewRoomFile(const std::string &path, ViewType view);
bool isRoomFilePath(const std::string &path);

} // namespace room
