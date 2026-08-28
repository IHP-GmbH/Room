#include "coord_scale.h"

#include <string>

namespace core {

double effectiveDbuPerEditorUnit(const CellContent &content)
{
    if (content.dbuPerEditorUnit() > 0.0) {
        return content.dbuPerEditorUnit();
    }
    if (content.viewType() == ViewType::Layout) {
        return 1.0;
    }
    const std::string &fmt = content.sourceInfo().format();
    if (fmt == "xschem") {
        return kXschemDbuPerEditorUnit;
    }
    return kQucsDbuPerEditorUnit;
}

std::int64_t editorUnitsToDbu(double units, double dbuPerEditorUnit)
{
    if (dbuPerEditorUnit <= 0.0) {
        return static_cast<std::int64_t>(std::llround(units));
    }
    return static_cast<std::int64_t>(std::llround(units * dbuPerEditorUnit));
}

double dbuToEditorUnits(std::int64_t dbu, double dbuPerEditorUnit)
{
    if (dbuPerEditorUnit <= 0.0) {
        return static_cast<double>(dbu);
    }
    return static_cast<double>(dbu) / dbuPerEditorUnit;
}

void orientToQucsPlacement(Orient orient, int &mirror, int &rotate)
{
    mirror = 0;
    rotate = 0;
    switch (orient) {
    case Orient::R90:
        rotate = 1;
        break;
    case Orient::R180:
        rotate = 2;
        break;
    case Orient::R270:
        rotate = 3;
        break;
    case Orient::MX:
        mirror = 1;
        rotate = 2;
        break;
    case Orient::MX90:
        mirror = 1;
        rotate = 3;
        break;
    case Orient::MY:
        mirror = 1;
        rotate = 0;
        break;
    case Orient::MY90:
        mirror = 1;
        rotate = 1;
        break;
    default:
        break;
    }
}

Orient orientFromQucsPlacement(int mirror, int rotate)
{
    if (mirror == 0) {
        switch (rotate & 3) {
        case 1:
            return Orient::R90;
        case 2:
            return Orient::R180;
        case 3:
            return Orient::R270;
        default:
            return Orient::R0;
        }
    }
    switch (rotate & 3) {
    case 0:
        return Orient::MY;
    case 1:
        return Orient::MY90;
    case 2:
        return Orient::MX;
    case 3:
        return Orient::MX90;
    default:
        return Orient::R0;
    }
}

bool hasQucsHistoricalSourceRotate(const std::string &cellOrType)
{
    static const char *kTypes[] = {
        "Vdc",         "Vac",         "Vpulse",         "Vexp",          "Vrect",
        "Vfile",       "Vpwl",        "Varith",         "Idc",           "Iac",
        "Ipulse",      "Iexp",        "Irect",          "Ifile",         "Ipwl",
        "Iarith",      "vsource.sym", "isource.sym",    "vsource_pwl.sym", "isource_pwl.sym",
        "vsource_arith.sym", "isource_arith.sym", "vsource", "isource", "vsource_pwl",
        "isource_pwl", "vsource_arith", "isource_arith",
    };
    for (const char *type : kTypes) {
        if (cellOrType == type) {
            return true;
        }
    }
    return false;
}

Orient orientFromQucsSourcePlacement(int mirror, int rotateField)
{
    // Constructor already applied one rotate(); saved field counts from unrotated geometry.
    const int visualRotate = (rotateField + 3) & 3;
    return orientFromQucsPlacement(mirror, visualRotate);
}

Orient orientFromQucsLibToNativePdk(int mirror, int rotateField)
{
    // Native Xschem/CORE PDK symbols are typically drawn N–S (rot0 vertical), while some Qucs
    // IHP LibComp artwork is E–W (rotate0 horizontal). +1 aligns vertical placements
    // (Qucs rotate=3 → xschem rot 0). Only call when Qucs artwork is E–W and Xschem is N–S.
    const int visualRotate = (rotateField + 1) & 3;
    return orientFromQucsPlacement(mirror, visualRotate);
}

bool qucsLibNeedsEwToNsCompensate(const std::string &model)
{
    // Qucs IHP nonlinear LibComp cells whose default ports are east–west, while the matching
    // Xschem/CORE symbol is north–south. Cells already N–S in Qucs (isolbox, MOS, HBT, …)
    // must NOT get the +1 shift.
    static const char *kExact[] = {
        "dantenna", "dpantenna", "svaricap", "nmoscl_2", "nmoscl_4",
        "diodevdd_2kv", "diodevdd_4kv", "idiodevdd_2kv", "idiodevdd_4kv",
        "diodevss_2kv", "diodevss_4kv", "idiodevss_2kv", "idiodevss_4kv",
    };
    for (const char *name : kExact) {
        if (model == name) {
            return true;
        }
    }
    return false;
}

} // namespace core
