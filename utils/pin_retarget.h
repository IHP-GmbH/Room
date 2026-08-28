#pragma once

#include "instance.h"
#include "types.h"

#include <string>
#include <utility>
#include <vector>

namespace core {

/*! Strip path and optional .sym suffix → bare cell/model name. */
std::string pinRetargetBaseName(const std::string &cellOrRef);

/*! Resolve LibComp model (param.1) or bare cell name for pin tables. */
std::string pinRetargetModelName(const Instance &inst);

/*! Qucs LibComp pin centers → native Xschem/CORE pin centers (same units as instance/wires).
 *  \param dbuPerEditorUnit  Scale for local pin offsets (1 for Qucs-origin, 1000 for Xschem-origin).
 */
void appendQucsToXschemPinRetargets(const Instance &inst, double dbuPerEditorUnit,
                                    std::vector<std::pair<Point, Point>> &out);

/*! Native Xschem/CORE pin centers → Qucs LibComp pin centers. */
void appendXschemToQucsPinRetargets(const Instance &inst, double dbuPerEditorUnit,
                                    std::vector<std::pair<Point, Point>> &out);

/*! Move pt from pair.first → pair.second when within tolerance (same units as points).
 *  \param dbuPerEditorUnit  Used to scale the match tolerance (2 editor units).
 */
void retargetPoint(Point &pt, const std::vector<std::pair<Point, Point>> &retargets,
                   double dbuPerEditorUnit = 1.0);

/*! Retarget Qucs-.lib wire polylines onto CORE/Xschem pin centers and lift T-junction
 *  buses so both tools draw the same Manhattan topology against CORE symbols.
 */
void retargetWirePolylinesQucsToCore(std::vector<std::vector<Point>> &polylines,
                                     const std::vector<std::pair<Point, Point>> &pinRetargets,
                                     double dbuPerEditorUnit);

} // namespace core
