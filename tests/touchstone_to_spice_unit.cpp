#include "em_model_netlist.h"
#include "touchstone_to_spice.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

int main()
{
    const char *tmpdir = std::getenv("TEMP");
    if (!tmpdir || !*tmpdir) {
        tmpdir = std::getenv("TMP");
    }
    if (!tmpdir || !*tmpdir) {
        tmpdir = "/tmp";
    }
    std::string dir = std::string(tmpdir) + "/room_s2spice_unit";
#ifdef _WIN32
    _mkdir(dir.c_str());
#else
    mkdir(dir.c_str(), 0755);
#endif
    const std::string snp = dir + "/dut.s2p";
    {
        std::ofstream out(snp);
        out << "# HZ S RI R 50\n";
        out << "1e9 0.1 0.0 0.0 0.0 0.0 0.0 0.1 0.0\n";
        out << "2e9 0.1 0.0 0.0 0.0 0.0 0.0 0.1 0.0\n";
    }

    std::ostringstream spice;
    std::string err;
    if (!room::convertTouchstoneToSpice(snp, "dut_cell", 50.0, spice, &err)) {
        std::cerr << "convert failed: " << err << "\n";
        return 1;
    }
    const std::string text = spice.str();
    if (text.find(".SUBCKT dut_cell 1 2 3") == std::string::npos) {
        std::cerr << "missing SUBCKT header with ref node\n" << text << "\n";
        return 2;
    }
    if (text.find(".ENDS") == std::string::npos) {
        std::cerr << "missing .ENDS\n";
        return 3;
    }

    std::vector<room::Property> props = {{"em.lookalike", "1"}, {"type", "subcircuit"}};
    room::applyLookalikeXschemNetlistProps(props);
    bool haveFmt = false;
    for (const auto &p : props) {
        if (p.name == "format" && p.value.find("0 @symname") != std::string::npos) {
            haveFmt = true;
        }
    }
    if (!haveFmt) {
        std::cerr << "lookalike xschem format not applied\n";
        return 4;
    }

    std::cout << "touchstone_to_spice_unit OK\n";
    return 0;
}
