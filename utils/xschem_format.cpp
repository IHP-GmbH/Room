#include "xschem_format.h"

#include <cctype>

namespace room::xschem {

ViewType viewTypeForExtension(const std::string &extension)
{
    if (extension == ".sym") {
        return ViewType::Symbol;
    }
    return ViewType::Schematic;
}

std::string extensionForViewType(ViewType viewType)
{
    if (viewType == ViewType::Symbol) {
        return ".sym";
    }
    return ".sch";
}

Orient orientFromXschem(int rotate, int mirror)
{
    Orient base = Orient::R0;
    switch (rotate & 3) {
    case 1: base = Orient::R90; break;
    case 2: base = Orient::R180; break;
    case 3: base = Orient::R270; break;
    default: base = Orient::R0; break;
    }
    if (mirror != 0) {
        switch (base) {
        case Orient::R0: return Orient::MX;
        case Orient::R90: return Orient::MX90;
        case Orient::R180: return Orient::MY;
        case Orient::R270: return Orient::MY90;
        default: return base;
        }
    }
    return base;
}

void xschemFromOrient(Orient orient, int &rotate, int &mirror)
{
    mirror = 0;
    switch (orient) {
    case Orient::R90:
    case Orient::MX90:
        rotate = 1;
        break;
    case Orient::R180:
    case Orient::MY:
        rotate = 2;
        break;
    case Orient::R270:
    case Orient::MY90:
        rotate = 3;
        break;
    default:
        rotate = 0;
        break;
    }
    switch (orient) {
    case Orient::MX:
    case Orient::MX90:
    case Orient::MY:
    case Orient::MY90:
        mirror = 1;
        break;
    default:
        mirror = 0;
        break;
    }
}

} // namespace room::xschem
