#include "oas_reader.h"
#include "oas_writer.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace {

void appendUInt(std::vector<std::uint8_t> &out, std::uint64_t value)
{
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7fu);
        value >>= 7;
        if (value != 0) {
            byte = static_cast<std::uint8_t>(byte | 0x80u);
        }
        out.push_back(byte);
    } while (value != 0);
}

void appendOasString(std::vector<std::uint8_t> &out, const std::string &s)
{
    appendUInt(out, static_cast<std::uint64_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

bool writeBytes(const std::string &path, const std::vector<std::uint8_t> &bytes)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

/*! Build OAS where CELL(ref) appears before CELLNAME so the reader emits a
 *  placeholder then a rename to the real name. Tiny fixture — never wafer. */
bool writeRefBeforeNameOas(const std::string &path, const std::string &cellName)
{
    std::vector<std::uint8_t> fileData;
    const char magic[] = "%SEMI-OASIS\r\n";
    fileData.insert(fileData.end(), magic, magic + sizeof(magic) - 1);

    std::vector<std::uint8_t> startRec;
    appendUInt(startRec, 1);
    appendOasString(startRec, "1.0");
    appendUInt(startRec, 0);
    appendUInt(startRec, 1000);
    appendUInt(startRec, 0);
    for (int i = 0; i < 12; ++i) {
        appendUInt(startRec, 0);
    }
    fileData.insert(fileData.end(), startRec.begin(), startRec.end());

    // CELL by reference 0 before any CELLNAME → placeholder #oasref:0
    std::vector<std::uint8_t> cellRec;
    appendUInt(cellRec, 13);
    appendUInt(cellRec, 0);
    fileData.insert(fileData.end(), cellRec.begin(), cellRec.end());

    // Implicit CELLNAME binds ref 0 to the real name.
    std::vector<std::uint8_t> cellNameRec;
    appendUInt(cellNameRec, 3);
    appendOasString(cellNameRec, cellName);
    fileData.insert(fileData.end(), cellNameRec.begin(), cellNameRec.end());

    std::vector<std::uint8_t> endRec;
    appendUInt(endRec, 2);
    fileData.insert(fileData.end(), endRec.begin(), endRec.end());

    return writeBytes(path, fileData);
}

} // namespace

int main(int argc, char *argv[])
{
    const std::string outDir = (argc > 1) ? argv[1] : ".";
#ifdef _WIN32
    _mkdir(outDir.c_str());
#else
    mkdir(outDir.c_str(), 0755);
#endif
    const std::string minimalPath = outDir + "/oas_progress_minimal.oas";
    const std::string renamePath = outDir + "/oas_progress_rename.oas";

    {
        room::OasWriter writer(minimalPath);
        writer.createMinimalFile("TOP_CELL");
        if (!writer.errors().empty()) {
            for (const std::string &err : writer.errors()) {
                std::cerr << "error: " << err << '\n';
            }
            return 2;
        }

        room::OasReader reader(minimalPath);
        room::OasHierarchy hierarchy;
        int progressCount = 0;
        std::string lastProgress;
        const bool ok = reader.readHierarchy(
            hierarchy,
            [&](const std::string &name) {
                ++progressCount;
                lastProgress = name;
            });
        if (!ok) {
            for (const std::string &err : reader.errors()) {
                std::cerr << "error: " << err << '\n';
            }
            return 3;
        }
        if (progressCount < 1 || lastProgress != "TOP_CELL") {
            std::cerr << "error: expected progress for TOP_CELL, got count="
                      << progressCount << " last='" << lastProgress << "'\n";
            return 4;
        }
        if (hierarchy.allCells.count("TOP_CELL") == 0) {
            std::cerr << "error: TOP_CELL missing from hierarchy\n";
            return 5;
        }
        for (const std::string &cell : hierarchy.allCells) {
            if (cell.rfind("#oasref:", 0) == 0) {
                std::cerr << "error: leftover placeholder " << cell << '\n';
                return 6;
            }
        }
    }

    {
        if (!writeRefBeforeNameOas(renamePath, "REAL_CHILD")) {
            std::cerr << "error: failed to write rename fixture\n";
            return 7;
        }

        room::OasReader reader(renamePath);
        room::OasHierarchy hierarchy;
        int progressPlaceholders = 0;
        int renameCount = 0;
        std::string renamedTo;
        const bool ok = reader.readHierarchy(
            hierarchy,
            [&](const std::string &name) {
                if (name.rfind("#oasref:", 0) == 0) {
                    ++progressPlaceholders;
                }
            },
            [&](const std::string &from, const std::string &to) {
                if (from.rfind("#oasref:", 0) == 0) {
                    ++renameCount;
                    renamedTo = to;
                }
            });
        if (!ok) {
            for (const std::string &err : reader.errors()) {
                std::cerr << "error: " << err << '\n';
            }
            return 8;
        }
        if (progressPlaceholders < 1) {
            std::cerr << "error: expected placeholder progress event\n";
            return 9;
        }
        if (renameCount < 1 || renamedTo != "REAL_CHILD") {
            std::cerr << "error: expected rename to REAL_CHILD, renames="
                      << renameCount << " to='" << renamedTo << "'\n";
            return 10;
        }
        if (hierarchy.allCells.count("REAL_CHILD") == 0) {
            std::cerr << "error: REAL_CHILD missing after rename\n";
            return 11;
        }
        for (const std::string &cell : hierarchy.allCells) {
            if (cell.rfind("#oasref:", 0) == 0) {
                std::cerr << "error: leftover placeholder after rename " << cell << '\n';
                return 12;
            }
        }
    }

    std::cout << "oas_progress_callbacks OK\n";
    return 0;
}
