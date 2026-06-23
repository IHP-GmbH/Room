#pragma once

#include "enums.h"

#include <database.capnp.h>

#include <cstdint>
#include <string>

namespace core {

class Lib;

/*! \brief Denormalized file header: mandatory view + cell inventory (no geometry). */
struct FileSummary {
    ViewType                                            view = ViewType::Layout;
    std::uint32_t                                       cellCount = 0;
    std::string                                         primaryCell;

    static FileSummary                                  fromLib(const Lib &lib, ViewType fileView);
    bool                                                empty() const;
};

void writeFileSummary(schema::FileSummary::Builder builder, const FileSummary &summary);
FileSummary readFileSummary(schema::FileSummary::Reader reader);

/*! \brief Metadata read from a .core file without decoding geometry payloads. */
struct CoreFileInfo {
    std::string                                         version;
    std::string                                         generator;
    std::string                                         technology;
    std::string                                         libName;
    FileSummary                                         summary;
};

CoreFileInfo sniffCoreFile(const std::string &path);

} // namespace core
