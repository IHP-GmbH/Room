#include "gds_stream_import.h"
#include "oas_exporter.h"
#include "oas_importer.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct Counts {
    std::size_t cells = 0;
    std::size_t layers = 0;
    std::size_t shapes = 0;
    std::size_t instances = 0;
};

void printMs(const char *label, Clock::duration duration)
{
    const double sec = std::chrono::duration<double>(duration).count();
    std::cout << label << ": " << std::fixed << std::setprecision(3) << sec << " s" << std::endl;
}

std::uintmax_t fileSizeBytes(const std::string &path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return 0;
    }
    return static_cast<std::uintmax_t>(in.tellg());
}

void printSize(const char *label, std::uintmax_t bytes, const std::string &path)
{
    const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::cout << label << ": " << bytes << " bytes (" << std::fixed << std::setprecision(1) << mib
              << " MiB) " << path << std::endl;
}

void printRatio(const char *label, std::uintmax_t part, std::uintmax_t whole)
{
    if (whole == 0) {
        std::cout << label << ": n/a" << std::endl;
        return;
    }
    const double pct = 100.0 * static_cast<double>(part) / static_cast<double>(whole);
    std::cout << label << ": " << std::fixed << std::setprecision(1) << pct << "%" << std::endl;
}

Counts countLayout(const room::Database &db)
{
    Counts counts;
    counts.cells = db.lib().cells().size();
    counts.layers = db.lib().layers().size();
    for (const room::Cell &cell : db.lib().cells()) {
        for (const room::CellContent &content : cell.contents()) {
            counts.shapes += content.block().shapes().size();
            counts.instances += content.block().instances().size();
        }
    }
    return counts;
}

void printCounts(const char *label, const Counts &counts)
{
    std::cout << label << ": cells=" << counts.cells << " layers=" << counts.layers
              << " shapes=" << counts.shapes << " instances=" << counts.instances << std::endl;
}

bool sameCounts(const Counts &a, const Counts &b)
{
    return a.cells == b.cells && a.layers == b.layers && a.shapes == b.shapes && a.instances == b.instances;
}

void printMessages(const char *kind, const std::vector<std::string> &messages)
{
    for (const std::string &message : messages) {
        std::cerr << kind << ": " << message << std::endl;
    }
}

int failIfErrors(const std::vector<std::string> &errors, int code)
{
    if (errors.empty()) {
        return 0;
    }
    return code;
}

void printPeakRss()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        const double mib = static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
        std::cout << "peak rss: " << counters.PeakWorkingSetSize << " bytes (" << std::fixed
                  << std::setprecision(1) << mib << " MiB)" << std::endl;
    }
#endif
}

std::string joinPath(const std::string &dir, const std::string &name)
{
    if (dir.empty()) {
        return name;
    }
    const char last = dir.back();
    if (last == '/' || last == '\\') {
        return dir + name;
    }
    return dir + "\\" + name;
}

int runGds(const std::string &gdsPath, const std::string &workDir, std::uintmax_t &compactBytes)
{
    const std::string compactPath = joinPath(workDir, "compact.room");
    std::cout << "=== GDS stream -> compact ROOM ===" << std::endl;
    std::cout << "input: " << gdsPath << std::endl;

    const Clock::time_point t0 = Clock::now();
    const room::GdsStreamImportResult imported = room::importGdsStreamToRoom(gdsPath, compactPath);
    const Clock::time_point t1 = Clock::now();
    printMessages("error", imported.errors);
    if (!imported.errors.empty()) {
        return 2;
    }
    printMs("stream import+save", t1 - t0);
    std::cout << "cells=" << imported.stats.cells
              << " rects=" << imported.stats.rects
              << " polygons=" << imported.stats.polygons
              << " paths=" << imported.stats.paths
              << " texts=" << imported.stats.texts
              << " srefs=" << imported.stats.srefs
              << " arefs=" << imported.stats.arefs << std::endl;

    const Clock::time_point t2 = Clock::now();
    const room::GdsStreamStats stats = room::readCompactRoomStats(compactPath);
    const Clock::time_point t3 = Clock::now();
    printMs("compact stat read", t3 - t2);
    std::cout << "reread cells=" << stats.cells
              << " rects=" << stats.rects
              << " polygons=" << stats.polygons
              << " paths=" << stats.paths
              << " texts=" << stats.texts
              << " srefs=" << stats.srefs
              << " arefs=" << stats.arefs << std::endl;
    const std::uint64_t inputPlaces = imported.stats.srefs + imported.stats.arefMembers;
    const std::uint64_t filePlaces = stats.srefs + stats.arefMembers;
    std::cout << "placements input=" << inputPlaces << " expanded=" << filePlaces
              << " file-srefs=" << stats.srefs << " file-arefs=" << stats.arefs << std::endl;
    if (stats.cells != imported.stats.cells || stats.rects != imported.stats.rects ||
        stats.polygons != imported.stats.polygons || stats.paths != imported.stats.paths ||
        stats.texts != imported.stats.texts || inputPlaces != filePlaces) {
        std::cerr << "error: compact reread counts differ" << std::endl;
        return 3;
    }

    const std::uintmax_t gdsBytes = fileSizeBytes(gdsPath);
    compactBytes = fileSizeBytes(compactPath);
    printSize("gds", gdsBytes, gdsPath);
    printSize("compact", compactBytes, compactPath);
    printRatio("compact/gds", compactBytes, gdsBytes);
    std::cout << "compact counts match" << std::endl;
    return 0;
}

int runOas(const std::string &oasPath, const std::string &workDir, std::uintmax_t gdsCompactBytes)
{
    const std::string compactPath = joinPath(workDir, "from_oas.room");
    const std::string roundPath = joinPath(workDir, "roundtrip.oas");

    std::cout << "=== OAS -> ROOM -> OAS ===" << std::endl;
    std::cout << "input: " << oasPath << std::endl;

    const Clock::time_point t0 = Clock::now();
    room::OasImporter importer;
    room::Database db = importer.importFile(oasPath);
    const Clock::time_point t1 = Clock::now();
    printMessages("warning", importer.warnings());
    printMessages("error", importer.errors());
    if (const int code = failIfErrors(importer.errors(), 7)) {
        return code;
    }

    const Counts importedCounts = countLayout(db);
    printCounts("imported", importedCounts);
    printMs("OAS import", t1 - t0);

    const Clock::time_point tSave0 = Clock::now();
    db.saveToFile(compactPath, room::ViewType::Layout);
    const Clock::time_point tSave1 = Clock::now();
    printMs("compact save", tSave1 - tSave0);

    const Clock::time_point tLoad0 = Clock::now();
    room::Database reloaded = room::Database::loadFromFile(compactPath);
    const Clock::time_point tLoad1 = Clock::now();
    printMs("compact load", tLoad1 - tLoad0);
    const Counts reloadedCounts = countLayout(reloaded);
    printCounts("compact reload", reloadedCounts);
    if (!sameCounts(importedCounts, reloadedCounts)) {
        std::cerr << "error: OAS compact reload counts differ" << std::endl;
        return 8;
    }

    room::OasExporter exporter;
    const Clock::time_point tExport0 = Clock::now();
    exporter.exportFile(reloaded, roundPath);
    const Clock::time_point tExport1 = Clock::now();
    printMessages("warning", exporter.warnings());
    printMessages("error", exporter.errors());
    printMs("OAS export", tExport1 - tExport0);
    if (const int code = failIfErrors(exporter.errors(), 9)) {
        return code;
    }

    const std::uintmax_t oasBytes = fileSizeBytes(oasPath);
    const std::uintmax_t compactBytes = fileSizeBytes(compactPath);
    const std::uintmax_t roundBytes = fileSizeBytes(roundPath);
    printSize("oas", oasBytes, oasPath);
    printSize("compact", compactBytes, compactPath);
    printSize("roundtrip oas", roundBytes, roundPath);
    printRatio("compact/oas", compactBytes, oasBytes);
    if (gdsCompactBytes != 0) {
        printRatio("oas-compact/gds-compact", compactBytes, gdsCompactBytes);
    }

    const Clock::time_point tRe0 = Clock::now();
    room::OasImporter again;
    room::Database roundDb = again.importFile(roundPath);
    const Clock::time_point tRe1 = Clock::now();
    printMessages("warning", again.warnings());
    printMessages("error", again.errors());
    printMs("OAS re-import", tRe1 - tRe0);
    if (const int code = failIfErrors(again.errors(), 10)) {
        return code;
    }
    const Counts roundCounts = countLayout(roundDb);
    printCounts("re-imported", roundCounts);
    if (!sameCounts(importedCounts, roundCounts)) {
        std::cerr << "error: OAS round-trip counts differ" << std::endl;
        return 11;
    }
    std::cout << "OAS round-trip counts match" << std::endl;
    return 0;
}

void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " <work-dir> [--gds file.gds] [--oas file.oas]\n"
              << "  Writes verbose.room, compact.room, roundtrip.gds and, when given,\n"
              << "  from_oas.room and roundtrip.oas. Prints seconds and byte sizes.\n";
}

} // namespace

int main(int argc, char *argv[])
{
    std::string workDir;
    std::string gdsPath;
    std::string oasPath;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--gds" && i + 1 < argc) {
            gdsPath = argv[++i];
        } else if (arg == "--oas" && i + 1 < argc) {
            oasPath = argv[++i];
        } else if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (workDir.empty()) {
            workDir = arg;
        } else {
            printUsage(argv[0]);
            return 1;
        }
    }
    if (workDir.empty() || (gdsPath.empty() && oasPath.empty())) {
        printUsage(argv[0]);
        return 1;
    }

    std::uintmax_t gdsCompactBytes = 0;
    if (!gdsPath.empty()) {
        if (const int code = runGds(gdsPath, workDir, gdsCompactBytes)) {
            printPeakRss();
            return code;
        }
    }
    if (!oasPath.empty()) {
        if (const int code = runOas(oasPath, workDir, gdsCompactBytes)) {
            printPeakRss();
            return code;
        }
    }
    printPeakRss();
    return 0;
}
