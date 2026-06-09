#include "database.h"
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
                if (layout.getBlock().getShapes().size() == 0) {
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
    original.saveToFile(corePath);

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

    std::cout << "payload round-trip OK (" << shapeCount(reloaded) << " shapes)\n";
    std::remove(corePath.c_str());
    return 0;
}
