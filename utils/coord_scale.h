#pragma once

#include "cell_content.h"

#include <cmath>
#include <cstdint>

namespace core {

constexpr double kDefaultLayoutDbuPerMicron = 1000.0;
constexpr double kQucsDbuPerEditorUnit = 1.0;
constexpr double kXschemDbuPerEditorUnit = 1000.0;

/*! Returns DBU-per-native-editor-unit for schematic/symbol views (1 for Qucs, 1000 for Xschem sub-grid). */
double effectiveDbuPerEditorUnit(const CellContent &content);

std::int64_t editorUnitsToDbu(double units, double dbuPerEditorUnit);
double dbuToEditorUnits(std::int64_t dbu, double dbuPerEditorUnit);

} // namespace core
