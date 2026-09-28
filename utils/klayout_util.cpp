#include "klayout_util.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace room {
namespace {

std::string quoteArg(const std::string &value)
{
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '"') {
            out += "\\\"";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('"');
    return out;
}

bool fileExists(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    return input.good();
}

std::string normalizePath(std::string path)
{
    for (char &ch : path) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    return path;
}

std::string scriptPath(const std::string &name)
{
    if (const char *root = std::getenv("CORE_SOURCE_DIR")) {
        return normalizePath(std::string(root) + "/scripts/" + name);
    }
    return "scripts/" + name;
}

} // namespace

std::string findKLayoutExecutable()
{
    if (const char *fromEnv = std::getenv("KLAYOUT_EXE")) {
        if (fileExists(fromEnv)) {
            return fromEnv;
        }
    }

#ifdef _WIN32
    if (const char *roaming = std::getenv("APPDATA")) {
        const std::string app = std::string(roaming) + "\\KLayout\\klayout_app.exe";
        if (fileExists(app)) {
            return app;
        }
    }
    if (const char *local = std::getenv("LOCALAPPDATA")) {
        const std::string app = std::string(local) + "\\KLayout\\klayout_app.exe";
        if (fileExists(app)) {
            return app;
        }
    }
    const char *winPaths[] = {
        "C:\\Program Files\\KLayout\\klayout_app.exe",
        "C:\\Program Files (x86)\\KLayout\\klayout_app.exe",
    };
    for (const char *path : winPaths) {
        if (fileExists(path)) {
            return path;
        }
    }
#endif

    const char *pathCandidates[] = {
        "klayout_app",
        "klayout_app.exe",
        "klayout",
        "klayout.exe",
    };
    for (const char *name : pathCandidates) {
        std::ostringstream probe;
#ifdef _WIN32
        probe << quoteArg(name) << " -b -v >nul 2>&1";
#else
        probe << quoteArg(name) << " -b -v >/dev/null 2>&1";
#endif
        if (std::system(probe.str().c_str()) == 0) {
            return name;
        }
    }

    return {};
}

bool runKLayoutBatch(const std::string &scriptName,
                     const std::unordered_map<std::string, std::string> &variables,
                     std::vector<std::string> &errors)
{
    const std::string klayout = findKLayoutExecutable();
    if (klayout.empty()) {
        errors.push_back("KLayout not found (set KLAYOUT_EXE or add klayout to PATH)");
        return false;
    }

    const std::string resolvedScript = scriptPath(scriptName);
    if (!fileExists(resolvedScript)) {
        errors.push_back("KLayout script not found: " + resolvedScript);
        return false;
    }

    std::ostringstream cmd;
#ifdef _WIN32
    cmd << quoteArg(klayout) << " -b";
    for (const auto &entry : variables) {
        cmd << " -rd " << entry.first << '=' << normalizePath(entry.second);
    }
    cmd << " -r " << normalizePath(resolvedScript);
#else
    cmd << quoteArg(klayout) << " -b";
    for (const auto &entry : variables) {
        cmd << " -rd " << entry.first << '=' << quoteArg(normalizePath(entry.second));
    }
    cmd << " -r " << quoteArg(resolvedScript);
#endif

    const int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        errors.push_back("KLayout batch failed (exit " + std::to_string(rc) + "): " + cmd.str());
        return false;
    }
    return true;
}

} // namespace room
