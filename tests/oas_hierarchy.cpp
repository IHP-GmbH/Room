#include "oas_reader.h"
#include "oas_writer.h"

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;

void printMs(const char *label, Clock::duration duration)
{
    const double ms = std::chrono::duration<double, std::milli>(duration).count();
    std::cout << label << ": " << std::fixed << std::setprecision(3) << ms << " ms\n";
}

std::size_t placementCount(const core::OasHierarchy &hierarchy)
{
    std::size_t count = 0;
    for (const auto &entry : hierarchy.children) {
        count += entry.second.size();
    }
    return count;
}

bool readHierarchyFile(const std::string &path, core::OasHierarchy &hierarchy, Clock::duration &elapsed)
{
    const Clock::time_point t0 = Clock::now();
    core::OasReader reader(path);
    const bool ok = reader.readHierarchy(hierarchy);
    elapsed = Clock::now() - t0;

    if (!ok) {
        for (const std::string &msg : reader.errors()) {
            std::cerr << "error: " << msg << '\n';
        }
    }
    return ok;
}

void printHierarchyStats(const core::OasHierarchy &hierarchy)
{
    std::cout << "cells: " << hierarchy.allCells.size() << '\n';
    std::cout << "top cells: " << hierarchy.topCells.size() << '\n';
    std::cout << "placements: " << placementCount(hierarchy) << '\n';
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " smoke <output.oas> <cellName>\n"
                  << "  " << argv[0] << " read <input.oas>\n";
        return 1;
    }

    const std::string mode = argv[1];
    const std::string path = argv[2];

    if (mode == "smoke") {
        if (argc != 4) {
            std::cerr << "Usage: " << argv[0] << " smoke <output.oas> <cellName>\n";
            return 1;
        }

        const std::string cellName = argv[3];
        std::cout << "=== OAS smoke: create + read ===\n";
        std::cout << "output: " << path << '\n';
        std::cout << "cell:   " << cellName << "\n\n";

        const Clock::time_point t0 = Clock::now();
        core::OasWriter writer(path);
        writer.createMinimalFile(cellName);
        const Clock::time_point t1 = Clock::now();

        if (!writer.errors().empty()) {
            for (const std::string &msg : writer.errors()) {
                std::cerr << "error: " << msg << '\n';
            }
            return 2;
        }
        printMs("  OAS create", t1 - t0);

        core::OasHierarchy hierarchy;
        Clock::duration readElapsed{};
        if (!readHierarchyFile(path, hierarchy, readElapsed)) {
            return 3;
        }
        printMs("  OAS read", readElapsed);
        printHierarchyStats(hierarchy);

        if (hierarchy.allCells.find(cellName) == hierarchy.allCells.end()) {
            std::cerr << "error: created cell not found in hierarchy\n";
            return 4;
        }

        std::cout << "\n=== timing summary ===\n";
        printMs("OAS create", t1 - t0);
        printMs("OAS read", readElapsed);
        printMs("Total", t1 - t0 + readElapsed);
        printHierarchyStats(hierarchy);
        return 0;
    }

    if (mode == "read") {
        std::cout << "=== OAS hierarchy read ===\n";
        std::cout << "input: " << path << "\n\n";

        core::OasHierarchy hierarchy;
        Clock::duration readElapsed{};
        if (!readHierarchyFile(path, hierarchy, readElapsed)) {
            return 2;
        }

        printMs("  OAS read", readElapsed);
        printHierarchyStats(hierarchy);

        std::cout << "\n=== timing summary ===\n";
        printMs("OAS read", readElapsed);
        printHierarchyStats(hierarchy);
        return 0;
    }

    std::cerr << "Unknown mode: " << mode << " (expected smoke or read)\n";
    return 1;
}
