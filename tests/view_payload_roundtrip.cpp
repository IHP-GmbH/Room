#include "database.h"
#include "compact_codec.h"
#include "gds_importer.h"

#include <database.capnp.h>
#include <views.capnp.h>

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/io.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace {

std::size_t shapeCount(const core::Database &db)
{
    std::size_t total = 0;
    for (const auto &cell : db.lib().cells()) {
        const core::CellContent *content = cell.findContent(core::ViewType::Layout);
        if (content != nullptr) {
            total += content->block().shapes().size();
        }
    }
    return total;
}

void ensureDirectory(const std::string &dirPath)
{
    std::string partial;
    partial.reserve(dirPath.size());
    for (char ch : dirPath) {
        partial.push_back(ch);
        if (ch != '/' && ch != '\\') {
            continue;
        }
        if (partial.size() <= 1) {
            continue;
        }
#ifdef _WIN32
        _mkdir(partial.c_str());
#else
        mkdir(partial.c_str(), 0755);
#endif
    }
#ifdef _WIN32
    _mkdir(partial.c_str());
#else
    mkdir(partial.c_str(), 0755);
#endif
}

bool payloadMatchesViewType(core::schema::ViewPayload::Reader payload, core::schema::ViewType viewType)
{
    switch (viewType) {
    case core::schema::ViewType::LAYOUT:
        return payload.which() == core::schema::ViewPayload::LAYOUT;
    case core::schema::ViewType::SCHEMATIC:
        return payload.which() == core::schema::ViewPayload::SCHEMATIC;
    case core::schema::ViewType::SYMBOL:
        return payload.which() == core::schema::ViewPayload::SYMBOL;
    case core::schema::ViewType::ABSTRACT:
        return payload.which() == core::schema::ViewPayload::ABSTRACT;
    }
    return false;
}

void verifyPayloadInFile(const std::string &corePath)
{
    std::ifstream in(corePath, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("Cannot open core file: " + corePath);
    }

    const std::streamsize fileSize = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<char> buffer(static_cast<std::size_t>(fileSize));
    if (!in.read(buffer.data(), fileSize)) {
        throw std::runtime_error("Cannot read core file: " + corePath);
    }

    kj::ArrayInputStream inputStream(
        kj::arrayPtr(reinterpret_cast<const kj::byte *>(buffer.data()), buffer.size()));
    capnp::InputStreamMessageReader reader(inputStream);
    const auto root = reader.getRoot<core::schema::Database>();

    if (root.getVersion().cStr() != std::string("1.0")) {
        throw std::runtime_error("Expected format version 1.0");
    }

    bool foundPayload = false;
    for (const auto cell : root.getLib().getCells()) {
        for (const auto content : cell.getContents()) {
            const auto payload = content.getPayload();
            if (!payloadMatchesViewType(payload, content.getViewType())) {
                continue;
            }
            foundPayload = true;
            if (content.getViewType() == core::schema::ViewType::LAYOUT) {
                const auto layout = payload.getLayout();
                if (layout.getLayers().size() == 0) {
                    throw std::runtime_error("Layout payload missing layers");
                }
                const bool hasBlockShapes = layout.getBlock().getShapes().size() > 0;
                const bool hasCompactShapes = core::compactBlockHasGeometry(layout.getCompact());
                if (!hasBlockShapes && !hasCompactShapes) {
                    throw std::runtime_error("Layout payload missing shapes");
                }
            }
        }
    }

    if (!foundPayload) {
        throw std::runtime_error("No per-view payload found in .core file");
    }
}

} // namespace

int main(int argc, char *argv[])
{
    const std::string gdsPath = (argc > 1) ? argv[1] : "testdata/sample.gds";
    const std::string corePath = (argc > 2) ? argv[2] : "build/tests/view_payload_roundtrip.core";

    core::GdsImporter importer;
    core::Database original = importer.importFile(gdsPath);
    if (!importer.errors().empty()) {
        for (const auto &msg : importer.errors()) {
            std::cerr << "error: " << msg << '\n';
        }
        return 1;
    }

    original.setVersion("1.0");
    const std::size_t slash = corePath.find_last_of("/\\");
    if (slash != std::string::npos) {
        ensureDirectory(corePath.substr(0, slash));
    }
    original.saveToFile(corePath, core::ViewType::Layout);

    try {
        verifyPayloadInFile(corePath);
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 2;
    }

    const core::Database reloaded = core::Database::loadFromFile(corePath);
    if (shapeCount(reloaded) != shapeCount(original)) {
        std::cerr << "error: shape count mismatch after payload round-trip\n";
        return 3;
    }

    for (const auto &cell : original.lib().cells()) {
        const core::CellContent *layout = cell.findContent(core::ViewType::Layout);
        if (layout == nullptr) {
            continue;
        }
        const core::Cell *reloadedCell = reloaded.lib().findCell(cell.name());
        const core::CellContent *reloadedLayout =
            reloadedCell != nullptr ? reloadedCell->findContent(core::ViewType::Layout) : nullptr;
        if (reloadedLayout == nullptr || reloadedLayout->layers().size() != layout->layers().size()) {
            std::cerr << "error: per-view layer count mismatch after payload round-trip\n";
            return 4;
        }
    }

    std::cout << "payload round-trip OK (" << shapeCount(reloaded) << " shapes)\n";
    std::remove(corePath.c_str());
    return 0;
}
