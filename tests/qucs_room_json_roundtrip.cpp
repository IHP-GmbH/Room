#include "database.h"
#include "qucs_exporter.h"
#include "qucs_importer.h"
#include "room_json_io.h"
#include "room_paths.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace {

void ensureDirectory(const std::string &dirPath)
{
#ifdef _WIN32
    _mkdir(dirPath.c_str());
#else
    mkdir(dirPath.c_str(), 0755);
#endif
}

std::string readFile(const std::string &path)
{
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot read " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool containsLinePrefix(const std::string &text, const std::string &needle)
{
    return text.find(needle) != std::string::npos;
}

void require(bool cond, const char *msg)
{
    if (!cond) {
        throw std::runtime_error(msg);
    }
}

} // namespace

/*!
 * Qucs → ROOM → JSON → ROOM → Qucs round-trip using rc_lowpass.sch
 * (not EMTest testbench).
 */
int main(int argc, char *argv[])
{
    const std::string schIn = argc > 1 ? argv[1]
                                       : std::string("examples/qucs_to_room/data/rc_lowpass.sch");
    const std::string outDir = argc > 2 ? argv[2] : std::string("build/tests");
    ensureDirectory(outDir);

    const std::string room1 = outDir + "/rc_lowpass.schematic.room";
    const std::string json1 = outDir + "/rc_lowpass.schematic.json";
    const std::string room2 = outDir + "/rc_lowpass_from_json.schematic.room";
    const std::string schOut = outDir + "/rc_lowpass_roundtrip.sch";

    try {
        room::QucsImporter::Options opts;
        opts.libName = "qucs_json_rt";
        room::QucsImporter importer(opts);
        room::Database db = importer.importFile(schIn);
        if (!importer.errors().empty()) {
            for (const auto &e : importer.errors()) {
                std::cerr << "import error: " << e << '\n';
            }
            return 1;
        }
        require(!db.lib().cells().empty(), "no cells after Qucs import");
        const std::string cellName = db.lib().cells().front().name();
        const room::CellContent *sch0 = db.lib().cells().front().findContent(room::ViewType::Schematic);
        require(sch0 != nullptr, "no schematic content after Qucs import");
        const std::size_t inst0 = sch0->block().instances().size();
        const std::size_t shape0 = sch0->block().shapes().size();
        require(inst0 >= 2, "expected R/C(/GND) instances");
        require(shape0 >= 1, "expected wire shapes");

        db.setGenerator("qucs_room_json_roundtrip");
        db.setTechnology("qucs");
        db.saveToFile(room1, room::ViewType::Schematic);

        room::RoomJsonIo::Result je = room::RoomJsonIo::roomFileToJson(room1, json1, cellName);
        require(je.ok, je.message.c_str());

        room::RoomJsonIo::Result ji = room::RoomJsonIo::jsonFileToRoom(json1, room2);
        require(ji.ok, ji.message.c_str());

        room::Database db2 = room::Database::loadFromFile(room2);
        const room::Cell *cell2 = db2.lib().findCell(cellName);
        require(cell2 != nullptr, "cell missing after JSON import");
        const room::CellContent *sch2 = cell2->findContent(room::ViewType::Schematic);
        require(sch2 != nullptr, "schematic missing after JSON import");
        require(sch2->block().instances().size() == inst0, "instance count changed through JSON");
        require(sch2->block().shapes().size() == shape0, "shape count changed through JSON");

        room::QucsExporter exporter;
        exporter.exportCell(db2, cellName, schOut);
        if (!exporter.errors().empty()) {
            for (const auto &e : exporter.errors()) {
                std::cerr << "export error: " << e << '\n';
            }
            return 2;
        }

        const std::string outText = readFile(schOut);
        require(containsLinePrefix(outText, "<Qucs Schematic"), "missing Qucs header");
        require(containsLinePrefix(outText, "<Components>"), "missing Components");
        require(containsLinePrefix(outText, "<Wires>"), "missing Wires");
        // rc_lowpass has R1 50 Ohm and C1 1 pF
        require(containsLinePrefix(outText, "<R R1"), "missing R1 after round-trip");
        require(containsLinePrefix(outText, "<C C1"), "missing C1 after round-trip");
        require(containsLinePrefix(outText, "50 Ohm") || containsLinePrefix(outText, "50Ohm"),
                "missing R1 value");
        require(containsLinePrefix(outText, "1 pF") || containsLinePrefix(outText, "1pF"),
                "missing C1 value");

        std::cout << "qucs_room_json_roundtrip OK (" << cellName << ")\n"
                  << "  instances=" << inst0 << " shapes=" << shape0 << '\n'
                  << "  json=" << json1 << '\n'
                  << "  sch=" << schOut << '\n';
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << "qucs_room_json_roundtrip failed: " << ex.what() << '\n';
        return 3;
    }
}
