// Round-trips each Xschem schematic and symbol in a directory through ROOM (import,
// save, load, export) and compares the exported records with the original file.
// Differences listed in <cases-dir>/known_differences.txt are expected; any other
// difference, and any listed one that no longer occurs, fails the test.
// Usage: xschem_roundtrip_cases <cases-dir> <output-dir>

#include "database.h"
#include "xschem_exporter.h"
#include "xschem_importer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Carriage returns are dropped so CRLF checkouts compare like LF ones.
std::string readText(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::ostringstream stream;
    stream << in.rdbuf();
    std::string text = stream.str();
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

// Splits Xschem text into bare words and brace groups (braces stripped, escapes kept).
std::vector<std::string> tokens(const std::string &text, std::vector<bool> &braced)
{
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        std::string token;
        if (text[i] == '{') {
            int depth = 1;
            for (++i; i < text.size() && depth > 0; ++i) {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    token += text[i];
                    token += text[++i];
                    continue;
                }
                depth += text[i] == '{' ? 1 : text[i] == '}' ? -1 : 0;
                if (depth > 0) {
                    token += text[i];
                }
            }
            braced.push_back(true);
        } else {
            while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
                token += text[i++];
            }
            braced.push_back(false);
        }
        out.push_back(token);
    }
    return out;
}

// Attribute order and quoting style are not semantic; key=value pairs are.
std::string attributes(const std::string &text)
{
    std::vector<std::string> pairs;
    std::size_t i = 0;
    while (i < text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        std::string key;
        while (i < text.size() && text[i] != '=' && !std::isspace(static_cast<unsigned char>(text[i]))) {
            key += text[i++];
        }
        std::string value;
        if (i < text.size() && text[i] == '=') {
            ++i;
            if (i < text.size() && text[i] == '"') {
                for (++i; i < text.size() && text[i] != '"'; ++i) {
                    if (text[i] == '\\' && i + 1 < text.size()) {
                        value += text[i++];
                    }
                    value += text[i];
                }
                ++i;
            } else {
                while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
                    value += text[i++];
                }
            }
        }
        pairs.push_back(key + "=" + value);
    }
    std::sort(pairs.begin(), pairs.end());
    std::string out;
    for (const std::string &pair : pairs) {
        out += pair + ";";
    }
    return out;
}

std::string number(const std::string &word)
{
    char *end = nullptr;
    const double value = std::strtod(word.c_str(), &end);
    if (end == word.c_str() || *end != '\0') {
        return word;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.12g", value == 0.0 ? 0.0 : value);
    return buffer;
}

// A record starts with an unbraced one-letter record kind.
bool startsRecord(const std::string &word, bool braced)
{
    return !braced && word.size() == 1 && std::string("vGKVSEFCNTBLPA").find(word) != std::string::npos;
}

// One normalized line per record; header records without content and the version line are skipped.
std::vector<std::string> records(const std::string &text)
{
    std::vector<bool> braced;
    const std::vector<std::string> words = tokens(text, braced);
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < words.size()) {
        const std::string kind = words[i];
        std::size_t end = i + 1;
        while (end < words.size() && !startsRecord(words[end], braced[end])) {
            ++end;
        }
        std::string line = kind;
        if (std::string("GKVSEF").find(kind) != std::string::npos) {
            if (end == i + 2 && !words[i + 1].empty()) {
                out.push_back(kind + " {" + (kind == "K" ? attributes(words[i + 1]) : words[i + 1]) + "}");
            }
        } else if (kind != "v") {
            for (std::size_t k = i + 1; k < end; ++k) {
                const bool last = k + 1 == end;
                line += " " + (braced[k] ? "{" + (last ? attributes(words[k]) : words[k]) + "}" : number(words[k]));
            }
            out.push_back(line);
        }
        i = end;
    }
    std::sort(out.begin(), out.end());
    return out;
}

// "<case> lost: <record>" or "<case> added: <record>", sorted.
std::vector<std::string> differences(const std::string &name, const std::vector<std::string> &before,
                                     const std::vector<std::string> &after)
{
    std::vector<std::string> lost, added, out;
    std::set_difference(before.begin(), before.end(), after.begin(), after.end(), std::back_inserter(lost));
    std::set_difference(after.begin(), after.end(), before.begin(), before.end(), std::back_inserter(added));
    for (const std::string &line : lost) {
        out.push_back(name + " lost: " + line);
    }
    for (const std::string &line : added) {
        out.push_back(name + " added: " + line);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool roundTrip(const fs::path &source, const fs::path &outputDir, std::vector<std::string> &found, std::string &error)
{
    const std::string cell = source.stem().string();
    room::XschemImporter importer;
    room::Database db = importer.importFile(source.string());
    if (!importer.errors().empty()) {
        error = "import: " + importer.errors().front();
        return false;
    }
    const bool symbol = source.extension() == ".sym";
    const fs::path roomPath = outputDir / (cell + (symbol ? ".symbol.room" : ".schematic.room"));
    db.saveToFile(roomPath.string(), symbol ? room::ViewType::Symbol : room::ViewType::Schematic);
    const room::Database reloaded = room::Database::loadFromFile(roomPath.string());

    const fs::path exported = outputDir / source.filename();
    room::XschemExporter exporter;
    exporter.exportCell(reloaded, cell, exported.string());
    if (!exporter.errors().empty()) {
        error = "export: " + exporter.errors().front();
        return false;
    }
    found = differences(source.filename().string(), records(readText(source)), records(readText(exported)));
    return true;
}

std::vector<std::string> knownDifferences(const fs::path &file)
{
    std::vector<std::string> out;
    std::istringstream in(readText(file));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line[0] != '#') {
            out.push_back(line);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cerr << "usage: xschem_roundtrip_cases <cases-dir> <output-dir>\n";
        return 2;
    }
    const fs::path outputDir = fs::path(argv[2]) / "xschem_roundtrip_cases";
    fs::create_directories(outputDir);
    std::vector<fs::path> cases;
    for (const auto &entry : fs::directory_iterator(argv[1])) {
        if (entry.path().extension() == ".sch" || entry.path().extension() == ".sym") {
            cases.push_back(entry.path());
        }
    }
    std::sort(cases.begin(), cases.end());
    if (cases.empty()) {
        std::cerr << "no .sch or .sym cases in " << argv[1] << '\n';
        return 2;
    }
    const std::vector<std::string> known = knownDifferences(fs::path(argv[1]) / "known_differences.txt");
    int failures = 0;
    for (const fs::path &source : cases) {
        std::vector<std::string> found;
        std::string error;
        bool ok = false;
        try {
            ok = roundTrip(source, outputDir, found, error);
        } catch (const std::exception &e) {
            error = e.what();
        }
        if (!ok) {
            std::cout << "FAIL " << source.filename().string() << ": " << error << '\n';
            ++failures;
            continue;
        }
        const std::string prefix = source.filename().string() + " ";
        std::vector<std::string> expected;
        std::copy_if(known.begin(), known.end(), std::back_inserter(expected),
                     [&prefix](const std::string &line) { return line.rfind(prefix, 0) == 0; });
        if (found == expected) {
            std::cout << "PASS " << source.filename().string()
                      << (expected.empty() ? "" : " (" + std::to_string(expected.size()) + " known differences)") << '\n';
            continue;
        }
        std::cout << "FAIL " << source.filename().string() << '\n';
        std::vector<std::string> unexpected, fixed;
        std::set_difference(found.begin(), found.end(), expected.begin(), expected.end(), std::back_inserter(unexpected));
        std::set_difference(expected.begin(), expected.end(), found.begin(), found.end(), std::back_inserter(fixed));
        for (const std::string &line : unexpected) {
            std::cout << "  new difference: " << line << '\n';
        }
        for (const std::string &line : fixed) {
            std::cout << "  no longer occurs, update known_differences.txt: " << line << '\n';
        }
        ++failures;
    }
    for (const std::string &line : known) {
        const std::string name = line.substr(0, line.find(' '));
        if (std::none_of(cases.begin(), cases.end(), [&name](const fs::path &c) { return c.filename().string() == name; })) {
            std::cout << "FAIL no case file for known difference: " << line << '\n';
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
