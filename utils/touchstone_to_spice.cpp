/************************************************************************
 *  CommonDB – ROOM design database
 *
 *  Copyright (C) 2023–2026 IHP Authors
 *
 *  Based on S2Spice (Dan Dickey / Jim Mittel) via Qucs-S S2Spice by Vadim Kuznetsov.
 ************************************************************************/

#include "touchstone_to_spice.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

namespace room {
namespace {

constexpr int kMaxFreqs = 16001;
constexpr int kMaxPorts = 8;

std::string toUpper(std::string s)
{
    for (char &c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

std::vector<std::string> splitWs(const std::string &line)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : line) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

int portsFromSuffix(const std::string &path)
{
    const auto slash = path.find_last_of("/\\");
    const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
    const auto dot = file.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= file.size()) {
        return 0;
    }
    std::string ext = toUpper(file.substr(dot + 1));
    // s2p → 2, S1P → 1
    if (ext.size() >= 3 && ext.front() == 'S' && ext.back() == 'P') {
        try {
            return std::stoi(ext.substr(1, ext.size() - 2));
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

void setError(std::string *errorOut, const std::string &msg)
{
    if (errorOut) {
        *errorOut = msg;
    }
}

} // namespace

bool convertTouchstoneToSpice(const std::string &snpPath, const std::string &deviceName, double z0,
                              std::ostream &out, std::string *errorOut)
{
    if (snpPath.empty() || deviceName.empty()) {
        setError(errorOut, "Touchstone path or device name is empty.");
        return false;
    }

    const int ports = portsFromSuffix(snpPath);
    if (ports < 1 || ports > kMaxPorts) {
        setError(errorOut, "Invalid port number in file: " + snpPath);
        return false;
    }

    std::ifstream in(snpPath);
    if (!in) {
        setError(errorOut, "Failed to open file: " + snpPath);
        return false;
    }

    // .SUBCKT name 1..N N+1(ref)
    out << ".SUBCKT " << deviceName;
    for (int i = 0; i <= ports; ++i) {
        out << ' ' << (i + 1);
    }
    out << '\n';

    std::string next_line;
    while (std::getline(in, next_line)) {
        if (next_line.empty()) {
            continue;
        }
        if (next_line[0] == '#') {
            break;
        }
    }
    if (next_line.empty() || next_line[0] != '#') {
        setError(errorOut, "Missing Touchstone format line in: " + snpPath);
        return false;
    }

    next_line = toUpper(next_line);
    int funits = 1000000000; // GHz default
    if (next_line.find(" HZ ") != std::string::npos) {
        funits = 1;
    } else if (next_line.find(" KHZ ") != std::string::npos) {
        funits = 1000;
    } else if (next_line.find(" MHZ ") != std::string::npos) {
        funits = 1000000;
    } else if (next_line.find(" GHZ ") != std::string::npos) {
        funits = 1000000000;
    }

    const bool dB = next_line.find(" DB ") != std::string::npos;
    const bool RI = next_line.find(" RI ") != std::string::npos;
    if (next_line.find(" S ") == std::string::npos) {
        setError(errorOut, "Wrong data in file: " + snpPath);
        return false;
    }

    const auto headerToks = splitWs(next_line);
    double zFile = 50.0;
    if (!headerToks.empty()) {
        try {
            zFile = std::stod(headerToks.back());
        } catch (...) {
            zFile = 50.0;
        }
    }
    double z[kMaxPorts];
    for (int i = 0; i < ports; ++i) {
        z[i] = (z0 < 0.0) ? zFile : z0;
    }

    for (int i = 0; i < ports; ++i) {
        out << "R" << (i + 1) << "N " << (i + 1) << ' ' << (10 * (i + 1)) << ' ' << -z[i] << '\n';
        out << "R" << (i + 1) << "P " << (10 * (i + 1)) << ' ' << (10 * (i + 1) + 1) << ' '
            << (2 * z[i]) << '\n';
    }
    out << '\n';

    // Allocate S matrix: [freq][i][j][re/im]
    auto s = std::vector<double>(static_cast<std::size_t>(kMaxFreqs) * ports * ports * 2, 0.0);
    auto at = [&](int f, int i, int j, int comp) -> double & {
        return s[((((static_cast<std::size_t>(f) * ports) + i) * ports) + j) * 2
                 + static_cast<std::size_t>(comp)];
    };
    std::vector<double> freqs(static_cast<std::size_t>(kMaxFreqs), 0.0);

    int f = 0;
    while (std::getline(in, next_line)) {
        if (next_line.empty()) {
            continue;
        }
        if (next_line[0] == '#') {
            continue;
        }
        if (next_line.rfind("!noise parameters", 0) == 0
            || toUpper(next_line).rfind("!NOISE PARAMETERS", 0) == 0) {
            setError(errorOut, "Noise simulation in S2P files is not supported; noise data ignored.");
            break;
        }
        if (next_line[0] == '!') {
            continue;
        }

        auto tmp = splitWs(next_line);
        const int need = 2 * (ports * ports) + 1;
        while (static_cast<int>(tmp.size()) < need && std::getline(in, next_line)) {
            auto more = splitWs(next_line);
            tmp.insert(tmp.end(), more.begin(), more.end());
        }
        if (static_cast<int>(tmp.size()) < need) {
            setError(errorOut, "Touchstone file parse error!");
            return false;
        }

        try {
            freqs[static_cast<std::size_t>(f)] = std::stod(tmp[0]);
        } catch (...) {
            setError(errorOut, "Touchstone file parse error!");
            return false;
        }
        int idx = 1;
        if (ports != 2) {
            for (int i = 0; i < ports; ++i) {
                for (int j = 0; j < ports; ++j) {
                    at(f, i, j, 0) = std::stod(tmp[static_cast<std::size_t>(idx++)]);
                    at(f, i, j, 1) = std::stod(tmp[static_cast<std::size_t>(idx++)]);
                }
            }
        } else {
            // Touchstone 2-port column order: S11 S21 S12 S22
            at(f, 0, 0, 0) = std::stod(tmp[static_cast<std::size_t>(idx++)]);
            at(f, 0, 0, 1) = std::stod(tmp[static_cast<std::size_t>(idx++)]);
            at(f, 1, 0, 0) = std::stod(tmp[static_cast<std::size_t>(idx++)]);
            at(f, 1, 0, 1) = std::stod(tmp[static_cast<std::size_t>(idx++)]);
            at(f, 0, 1, 0) = std::stod(tmp[static_cast<std::size_t>(idx++)]);
            at(f, 0, 1, 1) = std::stod(tmp[static_cast<std::size_t>(idx++)]);
            at(f, 1, 1, 0) = std::stod(tmp[static_cast<std::size_t>(idx++)]);
            at(f, 1, 1, 1) = std::stod(tmp[static_cast<std::size_t>(idx++)]);
        }
        ++f;
        if (f >= kMaxFreqs) {
            break;
        }
    }
    const int numf = f;
    if (numf <= 0) {
        setError(errorOut, "No S-parameter frequency points in: " + snpPath);
        return false;
    }

    constexpr double pi = 3.14159265358979323846;
    int model_cnt = 0;
    for (int i = 0; i < ports; ++i) {
        for (int j = 0; j < ports; ++j) {
            ++model_cnt;
            if (j + 1 == ports) {
                out << 'A' << (i + 1) << (j + 1) << " %vd(" << (10 * (j + 1)) << ' ' << (ports + 1)
                    << ") %vd(" << (i + 1) << (j + 1) << ", " << (ports + 1) << ") xfer" << model_cnt
                    << '\n';
            } else {
                out << 'A' << (i + 1) << (j + 1) << " %vd(" << (10 * (j + 1)) << ' ' << (ports + 1)
                    << ") %vd(" << (i + 1) << (j + 1) << ", " << (i + 1) << (j + 2) << ") xfer"
                    << model_cnt << '\n';
            }
            out << ".model xfer" << model_cnt << " xfer R_I=true table=[\n";

            double offset = 0;
            for (int fi = 0; fi < numf; ++fi) {
                double a = at(fi, i, j, 0);
                double b = at(fi, i, j, 1);
                double mag = 0;
                double ph = 0;
                if (RI) {
                    mag = a;
                    ph = b;
                } else {
                    if (dB) {
                        a = std::pow(10.0, a / 20.0);
                    }
                    ph = a * std::sin(b * pi / 180.0);
                    mag = a * std::cos(b * pi / 180.0);
                }
                out << "+ " << (freqs[static_cast<std::size_t>(fi)] * funits) << "Hz " << mag << ' '
                    << (ph + offset) << '\n';
            }
            out << "+ ]\n\n";
        }
    }

    out << ".ENDS\n";
    return true;
}

bool writeTouchstoneSpiceFile(const std::string &snpPath, const std::string &deviceName, double z0,
                              const std::string &spicePath, std::string *errorOut)
{
    std::ofstream out(spicePath);
    if (!out) {
        setError(errorOut, "Cannot open spice output: " + spicePath);
        return false;
    }
    return convertTouchstoneToSpice(snpPath, deviceName, z0, out, errorOut);
}

} // namespace room
