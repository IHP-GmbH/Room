#include "room_paths.h"

#include <algorithm>
#include <cctype>

namespace room {
namespace {

std::string fileBaseName(const std::string &path)
{
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

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

} // namespace

std::string viewTypeFromString(const std::string &text)
{
    const std::string lower = toLower(text);
    if (lower == "layout") {
        return "layout";
    }
    if (lower == "schematic" || lower == "sch") {
        return "schematic";
    }
    if (lower == "symbol" || lower == "sym") {
        return "symbol";
    }
    if (lower == "abstract" || lower == "abs") {
        return "abstract";
    }
    if (lower == "emmodel" || lower == "em_model") {
        return "emmodel";
    }
    return {};
}

std::optional<ViewType> parseViewTypeName(const std::string &suffix)
{
    const std::string lower = toLower(suffix);
    if (lower == "layout") {
        return ViewType::Layout;
    }
    if (lower == "schematic" || lower == "sch") {
        return ViewType::Schematic;
    }
    if (lower == "symbol" || lower == "sym") {
        return ViewType::Symbol;
    }
    if (lower == "abstract" || lower == "abs") {
        return ViewType::Abstract;
    }
    if (lower == "emmodel" || lower == "em_model") {
        return ViewType::EmModel;
    }
    return std::nullopt;
}

std::string roomFileGlob(ViewType view)
{
    return "*." + viewTypeToString(view) + kRoomFileExtension;
}

std::string roomFileName(const std::string &cellName, ViewType view)
{
    return cellName + "." + viewTypeToString(view) + kRoomFileExtension;
}

ParsedRoomPath parseRoomFilePath(const std::string &path)
{
    ParsedRoomPath parsed;
    const std::string base = fileBaseName(path);
    if (!endsWith(base, kRoomFileExtension)) {
        return parsed;
    }

    const std::string stem = base.substr(0, base.size() - std::string(kRoomFileExtension).size());
    const std::size_t dot = stem.rfind('.');
    if (dot == std::string::npos || dot == 0) {
        return parsed;
    }

    const std::optional<ViewType> view = parseViewTypeName(stem.substr(dot + 1));
    if (!view.has_value()) {
        return parsed;
    }

    parsed.cellName = stem.substr(0, dot);
    parsed.view = *view;
    parsed.valid = true;
    return parsed;
}

bool isViewRoomFile(const std::string &path, ViewType view)
{
    const ParsedRoomPath parsed = parseRoomFilePath(path);
    return parsed.valid && parsed.view == view;
}

bool isRoomFilePath(const std::string &path)
{
    return endsWith(toLower(fileBaseName(path)), kRoomFileExtension);
}

} // namespace room
