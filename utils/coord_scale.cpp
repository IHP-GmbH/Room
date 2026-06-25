#include "coord_scale.h"

#include "enums.h"

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

} // namespace core
