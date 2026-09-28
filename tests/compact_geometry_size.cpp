#include "database.h"
#include "gds_importer.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace {

std::size_t shapeCount(const room::Database &db)
{
    std::size_t count = 0;
    for (const room::Cell &cell : db.lib().cells()) {
        for (const room::CellContent &content : cell.contents()) {
            count += content.block().shapes().size();
        }
    }
    return count;
}

std::size_t instanceCount(const room::Database &db)
{
    std::size_t count = 0;
    for (const room::Cell &cell : db.lib().cells()) {
        for (const room::CellContent &content : cell.contents()) {
            count += content.block().instances().size();
        }
    }
    return count;
}

std::size_t shapePropertyCount(const room::Database &db)
{
    std::size_t count = 0;
    for (const room::Cell &cell : db.lib().cells()) {
        for (const room::CellContent &content : cell.contents()) {
            for (const room::Shape &shape : content.block().shapes()) {
                count += shape.properties().size();
            }
        }
    }
    return count;
}

void tagFirstShapes(room::Database &db, std::size_t maxShapes)
{
    std::size_t tagged = 0;
    for (room::Cell &cell : db.lib().cells()) {
        for (room::CellContent &content : cell.contents()) {
            for (room::Shape &shape : content.block().shapes()) {
                shape.properties().push_back({"test.tag", std::to_string(tagged)});
                shape.properties().push_back({"test.layer", "metal1"});
                if (++tagged >= maxShapes) {
                    return;
                }
            }
        }
    }
}

using Clock = std::chrono::steady_clock;

void printMs(const char *label, Clock::duration duration)
{
    const double ms = std::chrono::duration<double, std::milli>(duration).count();
    std::cout << label << ": " << std::fixed << std::setprecision(3) << ms << " ms\n";
}

std::uintmax_t fileSizeBytes(const std::string &path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("Cannot read file size: " + path);
    }
    return static_cast<std::uintmax_t>(in.tellg());
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

} // namespace

int main(int argc, char *argv[])
{
    const std::string gdsPath = (argc > 1) ? argv[1] : "testdata/sample.gds";
    const std::string verbosePath = (argc > 2) ? argv[2] : "build/tests/compact_compare_verbose.room";
    const std::string compactPath = (argc > 3) ? argv[3] : "build/tests/compact_compare_compact.room";

    const std::size_t slash = verbosePath.find_last_of("/\\");
    if (slash != std::string::npos) {
        ensureDirectory(verbosePath.substr(0, slash));
    }

    room::GdsImporter importer;
    room::Database db = importer.importFile(gdsPath);
    if (!importer.errors().empty()) {
        for (const auto &msg : importer.errors()) {
            std::cerr << "error: " << msg << '\n';
        }
        return 1;
    }

    tagFirstShapes(db, 8);

    const std::size_t shapesBefore = shapeCount(db);
    const std::size_t instancesBefore = instanceCount(db);
    const std::size_t propertiesBefore = shapePropertyCount(db);

    room::SaveOptions verbose;
    verbose.compactGeometry = false;
    const Clock::time_point tSaveVerbose0 = Clock::now();
    db.saveToFile(verbosePath, room::ViewType::Layout, verbose);
    const Clock::time_point tSaveVerbose1 = Clock::now();

    const Clock::time_point tSaveCompact0 = Clock::now();
    db.saveToFile(compactPath, room::ViewType::Layout);
    const Clock::time_point tSaveCompact1 = Clock::now();

    const Clock::time_point tLoadVerbose0 = Clock::now();
    const room::Database verboseReloaded = room::Database::loadFromFile(verbosePath);
    const Clock::time_point tLoadVerbose1 = Clock::now();

    const Clock::time_point tLoadCompact0 = Clock::now();
    const room::Database reloaded = room::Database::loadFromFile(compactPath);
    const Clock::time_point tLoadCompact1 = Clock::now();

    const std::uintmax_t gdsSize = fileSizeBytes(gdsPath);
    const std::uintmax_t verboseSize = fileSizeBytes(verbosePath);
    const std::uintmax_t compactSize = fileSizeBytes(compactPath);
    const double ratio = verboseSize == 0
                             ? 0.0
                             : 100.0 * static_cast<double>(compactSize) / static_cast<double>(verboseSize);

    std::cout << "gds:     " << gdsSize << " bytes (" << gdsPath << ")\n";
    std::cout << "verbose: " << verboseSize << " bytes (" << verbosePath << ")\n";
    std::cout << "compact: " << compactSize << " bytes (" << compactPath << ")\n";
    std::cout << "ratio:   " << ratio << "% of verbose\n";
    std::cout << "\n=== encoding timing ===\n";
    printMs("verbose save", tSaveVerbose1 - tSaveVerbose0);
    printMs("compact save", tSaveCompact1 - tSaveCompact0);
    printMs("verbose load", tLoadVerbose1 - tLoadVerbose0);
    printMs("compact load", tLoadCompact1 - tLoadCompact0);

    if (shapeCount(reloaded) != shapesBefore) {
        std::cerr << "error: shape count mismatch after compact reload\n";
        return 2;
    }
    if (instanceCount(reloaded) != instancesBefore) {
        std::cerr << "error: instance count mismatch after compact reload\n";
        return 3;
    }
    if (shapePropertyCount(reloaded) != propertiesBefore) {
        std::cerr << "error: shape property count mismatch after compact reload\n";
        return 5;
    }

    if (shapePropertyCount(verboseReloaded) != propertiesBefore) {
        std::cerr << "error: shape property count mismatch after verbose reload\n";
        return 6;
    }

    if (shapesBefore > 0 && compactSize >= verboseSize) {
        std::cerr << "error: expected compact .room to be smaller than verbose encoding\n";
        return 4;
    }

    std::cout << "compact geometry size OK\n";
    return 0;
}
