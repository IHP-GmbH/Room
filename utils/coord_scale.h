#pragma once

#include "cell_content.h"
#include "enums.h"

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

/*! Map abstract orientation to Qucs LibComp mirror/rotate fields (MultiViewComponent::recreate semantics). */
void orientToQucsPlacement(Orient orient, int &mirror, int &rotate);

/*! Inverse of orientFromQucsSourcePlacement for Xschem → Qucs export of Volt_/Ampere_ sources. */
void orientToQucsSourcePlacement(Orient orient, int &mirror, int &rotate);

/*! Inverse of orientToQucsPlacement for Qucs .sch import. */
Orient orientFromQucsPlacement(int mirror, int rotate);

/*! True for Qucs Volt_/Ampere_ models that call rotate() once in the constructor. */
bool hasQucsHistoricalSourceRotate(const std::string &cellOrType);

/*! Map a Qucs schematic rotate field to CORE/Xschem visual orientation for historical sources.
 *  Default on-screen sources save rotate=1; that corresponds to Orient::R0 once geometry is upright.
 */
Orient orientFromQucsSourcePlacement(int mirror, int rotateField);

/*! Map Qucs LibComp rotate to Xschem when remapping onto native PDK symbols (E–W Qucs → N–S Xschem). */
Orient orientFromQucsLibToNativePdk(int mirror, int rotateField);

/*! True when Qucs IHP LibComp default ports are E–W but native Xschem/CORE symbol is N–S. */
bool qucsLibNeedsEwToNsCompensate(const std::string &model);

} // namespace core
