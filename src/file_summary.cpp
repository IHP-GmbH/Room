#include "file_summary.h"

#include "cell.h"
#include "lib.h"

#include <database.capnp.h>

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/io.h>
#include <kj/std/iostream.h>

#include <fstream>
#include <set>
#include <stdexcept>

namespace room {
namespace {

schema::ViewType toSchemaViewType(ViewType v)
{
    switch (v) {
    case ViewType::Layout: return schema::ViewType::LAYOUT;
    case ViewType::Schematic: return schema::ViewType::SCHEMATIC;
    case ViewType::Symbol: return schema::ViewType::SYMBOL;
    case ViewType::Abstract: return schema::ViewType::ABSTRACT;
    case ViewType::EmModel: return schema::ViewType::EM_MODEL;
    }
    return schema::ViewType::LAYOUT;
}

ViewType fromSchemaViewType(schema::ViewType v)
{
    switch (v) {
    case schema::ViewType::LAYOUT: return ViewType::Layout;
    case schema::ViewType::SCHEMATIC: return ViewType::Schematic;
    case schema::ViewType::SYMBOL: return ViewType::Symbol;
    case schema::ViewType::ABSTRACT: return ViewType::Abstract;
    case schema::ViewType::EM_MODEL: return ViewType::EmModel;
    }
    return ViewType::Layout;
}

FileSummary scanViewTypes(schema::Lib::Reader libReader)
{
    FileSummary summary;
    std::set<ViewType> seen;
    const auto cells = libReader.getCells();
    summary.cellCount = static_cast<std::uint32_t>(cells.size());

    for (const auto cellReader : cells) {
        if (summary.primaryCell.empty()) {
            summary.primaryCell = cellReader.getName().cStr();
        }
        for (const auto contentReader : cellReader.getContents()) {
            seen.insert(fromSchemaViewType(contentReader.getViewType()));
        }
    }

    if (seen.size() == 1) {
        summary.view = *seen.begin();
    } else if (!seen.empty()) {
        summary.view = *seen.begin();
    }
    return summary;
}

} // namespace

bool FileSummary::empty() const
{
    return cellCount == 0 && primaryCell.empty();
}

FileSummary FileSummary::fromLib(const Lib &lib, ViewType fileView)
{
    FileSummary summary;
    summary.view = fileView;
    summary.cellCount = static_cast<std::uint32_t>(lib.cells().size());
    if (!lib.cells().empty()) {
        summary.primaryCell = lib.cells().front().name();
    }
    return summary;
}

FileSummary readFileSummary(schema::FileSummary::Reader reader)
{
    FileSummary summary;
    summary.view = fromSchemaViewType(reader.getView());
    summary.cellCount = reader.getCellCount();
    summary.primaryCell = reader.getPrimaryCell().cStr();
    return summary;
}

void writeFileSummary(schema::FileSummary::Builder builder, const FileSummary &summary)
{
    builder.setView(toSchemaViewType(summary.view));
    builder.setCellCount(summary.cellCount);
    builder.setPrimaryCell(summary.primaryCell);
}

RoomFileInfo sniffRoomFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open file for reading: " + path);
    }

    kj::std::StdInputStream kjIn(in);
    capnp::InputStreamMessageReader reader(kjIn, {64 * 1024 * 1024});
    const auto root = reader.getRoot<schema::Database>();

    RoomFileInfo info;
    info.version = root.getVersion().cStr();
    info.generator = root.getGenerator().cStr();
    info.technology = root.getTechnology().cStr();
    info.libName = root.getLib().getName().cStr();

    if (root.hasSummary()) {
        info.summary = readFileSummary(root.getSummary());
    } else {
        info.summary = scanViewTypes(root.getLib());
    }
    return info;
}

} // namespace room
