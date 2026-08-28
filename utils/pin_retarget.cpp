#include "pin_retarget.h"

#include "property.h"

#include <algorithm>
#include <cmath>

namespace core {
namespace {

const std::string *findProp(const std::vector<Property> &props, const std::string &name)
{
    for (const Property &prop : props) {
        if (prop.name == name) {
            return &prop.value;
        }
    }
    return nullptr;
}

Point applyOrient(Point p, Orient orient)
{
    switch (orient) {
    case Orient::R90:
        return Point{-p.y, p.x};
    case Orient::R180:
        return Point{-p.x, -p.y};
    case Orient::R270:
        return Point{p.y, -p.x};
    case Orient::MX:
        return Point{p.x, -p.y};
    case Orient::MX90:
        return Point{p.y, p.x};
    case Orient::MY:
        return Point{-p.x, p.y};
    case Orient::MY90:
        return Point{-p.y, -p.x};
    case Orient::R0:
    default:
        return p;
    }
}

Point transformLocal(Point local, const Transform &xf)
{
    const Point r = applyOrient(local, xf.orient);
    return Point{xf.x + r.x, xf.y + r.y};
}

Point scaleEditorPin(Point editorUnits, double dbuPerEditorUnit)
{
    if (dbuPerEditorUnit <= 0.0 || dbuPerEditorUnit == 1.0) {
        return editorUnits;
    }
    return Point{static_cast<std::int64_t>(std::llround(editorUnits.x * dbuPerEditorUnit)),
                 static_cast<std::int64_t>(std::llround(editorUnits.y * dbuPerEditorUnit))};
}

bool isIhpFetModel(const std::string &model)
{
    return model == "sg13_lv_nmos" || model == "sg13_hv_nmos" || model == "sg13_lv_pmos"
        || model == "sg13_hv_pmos" || model == "sg13_lv_rf_nmos" || model == "sg13_hv_rf_nmos"
        || model == "sg13_lv_rf_pmos" || model == "sg13_hv_rf_pmos";
}

bool isIsolboxModel(const std::string &model)
{
    return model == "isolbox";
}

void appendMappedPins(const Transform &xfFrom, const Transform &xfTo, double dbuPerEditorUnit,
                      const Point *from, const Point *to, std::size_t n,
                      std::vector<std::pair<Point, Point>> &out)
{
    // Some Xschem-origin CORE files store editor-unit coordinates while advertising dbu=1000.
    // If the instance origin looks like editor space, keep pin tables unscaled.
    double scale = (dbuPerEditorUnit > 0.0) ? dbuPerEditorUnit : 1.0;
    if (scale > 1.0 && std::llabs(xfFrom.x) < 100000 && std::llabs(xfFrom.y) < 100000) {
        scale = 1.0;
    }
    for (std::size_t i = 0; i < n; ++i) {
        out.emplace_back(transformLocal(scaleEditorPin(from[i], scale), xfFrom),
                         transformLocal(scaleEditorPin(to[i], scale), xfTo));
    }
}

} // namespace

std::string pinRetargetBaseName(const std::string &cellOrRef)
{
    std::string type = cellOrRef;
    const auto slash = type.find_last_of("/\\");
    if (slash != std::string::npos) {
        type = type.substr(slash + 1);
    }
    if (type.size() > 4 && type.compare(type.size() - 4, 4, ".sym") == 0) {
        type.resize(type.size() - 4);
    }
    return type;
}

std::string pinRetargetModelName(const Instance &inst)
{
    const std::string base = pinRetargetBaseName(inst.cellName());
    if (base == "Lib" || base == "SpiceLib") {
        if (const std::string *model = findProp(inst.properties(), "param.1"); model && !model->empty()) {
            return pinRetargetBaseName(*model);
        }
    }
    if (const std::string *model = findProp(inst.properties(), "qucs.model"); model && !model->empty()) {
        return pinRetargetBaseName(*model);
    }
    if (const std::string *model = findProp(inst.properties(), "model"); model && !model->empty()) {
        return pinRetargetBaseName(*model);
    }
    return base;
}

void appendQucsToXschemPinRetargets(const Instance &inst, double dbuPerEditorUnit,
                                    std::vector<std::pair<Point, Point>> &out)
{
    const std::string model = pinRetargetModelName(inst);
    // Qucs-origin: wires sit on LibComp pins; CORE transform matches Qucs placement.
    const Transform &xf = inst.transform();

    if (isIhpFetModel(model)) {
        static const Point kQucs[] = {{0, -30}, {0, 30}, {20, 0}, {-30, 0}};
        static const Point kXschem[] = {{20, -30}, {20, 30}, {20, 0}, {-20, 0}};
        appendMappedPins(xf, xf, dbuPerEditorUnit, kQucs, kXschem, 4, out);
        return;
    }

    if (isIsolboxModel(model)) {
        static const Point kQucs[] = {{0, -90}, {0, -30}, {0, 30}};
        static const Point kXschem[] = {{0, -60}, {0, 0}, {0, 60}};
        appendMappedPins(xf, xf, dbuPerEditorUnit, kQucs, kXschem, 3, out);
    }
}

void appendXschemToQucsPinRetargets(const Instance &inst, double dbuPerEditorUnit,
                                    std::vector<std::pair<Point, Point>> &out)
{
    const std::string model = pinRetargetModelName(inst);
    // Keep from/to in the CORE transform frame (where Xschem wires attach). Stale Qucs
    // mirror/rotate props must not skew the destination — export derives placement from orient.
    const Transform &xf = inst.transform();

    if (isIhpFetModel(model)) {
        static const Point kQucs[] = {{0, -30}, {0, 30}, {20, 0}, {-30, 0}};
        static const Point kXschem[] = {{20, -30}, {20, 30}, {20, 0}, {-20, 0}};
        appendMappedPins(xf, xf, dbuPerEditorUnit, kXschem, kQucs, 4, out);
        return;
    }

    if (isIsolboxModel(model)) {
        static const Point kQucs[] = {{0, -90}, {0, -30}, {0, 30}};
        static const Point kXschem[] = {{0, -60}, {0, 0}, {0, 60}};
        appendMappedPins(xf, xf, dbuPerEditorUnit, kXschem, kQucs, 3, out);
    }
}

void retargetPoint(Point &pt, const std::vector<std::pair<Point, Point>> &retargets, double dbuPerEditorUnit)
{
    double scale = (dbuPerEditorUnit > 0.0) ? dbuPerEditorUnit : 1.0;
    // Match the unscaled-editor heuristic used when building retarget pairs.
    if (scale > 1.0 && !retargets.empty()) {
        const std::int64_t mag = std::llabs(retargets.front().first.x) + std::llabs(retargets.front().first.y);
        if (mag < 200000) {
            scale = 1.0;
        }
    }
    const std::int64_t tol = std::max<std::int64_t>(2, static_cast<std::int64_t>(std::llround(2.0 * scale)));
    const std::int64_t tol2 = tol * tol;
    for (const auto &pair : retargets) {
        const std::int64_t dx = pt.x - pair.first.x;
        const std::int64_t dy = pt.y - pair.first.y;
        if (dx * dx + dy * dy <= tol2) {
            pt = pair.second;
            return;
        }
    }
}

namespace {

std::int64_t matchTol(double dbuPerEditorUnit, const std::vector<std::pair<Point, Point>> &pinRetargets)
{
    double scale = (dbuPerEditorUnit > 0.0) ? dbuPerEditorUnit : 1.0;
    if (scale > 1.0 && !pinRetargets.empty()) {
        const std::int64_t mag =
            std::llabs(pinRetargets.front().first.x) + std::llabs(pinRetargets.front().first.y);
        if (mag < 200000) {
            scale = 1.0;
        }
    }
    return std::max<std::int64_t>(2, static_cast<std::int64_t>(std::llround(2.0 * scale)));
}

bool nearPts(const Point &a, const Point &b, std::int64_t tol)
{
    const std::int64_t dx = a.x - b.x;
    const std::int64_t dy = a.y - b.y;
    return dx * dx + dy * dy <= tol * tol;
}

bool sameCoord(std::int64_t a, std::int64_t b, std::int64_t tol) { return std::llabs(a - b) <= tol; }

void liftHorizontalRunToY(std::vector<std::vector<Point>> &polylines, Point seed, std::int64_t newY,
                          std::int64_t tol)
{
    // BFS over horizontal connectivity using coordinate equality (points are copied per segment).
    std::vector<Point> frontier{seed};
    std::vector<Point> reached;
    while (!frontier.empty()) {
        const Point cur = frontier.back();
        frontier.pop_back();
        bool already = false;
        for (const Point &r : reached) {
            if (nearPts(r, cur, tol)) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        reached.push_back(cur);
        for (const auto &poly : polylines) {
            if (poly.size() < 2) {
                continue;
            }
            for (std::size_t i = 1; i < poly.size(); ++i) {
                const Point &a = poly[i - 1];
                const Point &b = poly[i];
                if (!sameCoord(a.y, b.y, tol)) {
                    continue;
                }
                if (nearPts(a, cur, tol)) {
                    frontier.push_back(b);
                } else if (nearPts(b, cur, tol)) {
                    frontier.push_back(a);
                }
            }
        }
    }
    for (auto &poly : polylines) {
        for (Point &pt : poly) {
            for (const Point &r : reached) {
                if (nearPts(pt, r, tol)) {
                    pt.y = newY;
                    break;
                }
            }
        }
    }
}

void liftVerticalRunToX(std::vector<std::vector<Point>> &polylines, Point seed, std::int64_t newX,
                        std::int64_t tol)
{
    std::vector<Point> frontier{seed};
    std::vector<Point> reached;
    while (!frontier.empty()) {
        const Point cur = frontier.back();
        frontier.pop_back();
        bool already = false;
        for (const Point &r : reached) {
            if (nearPts(r, cur, tol)) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        reached.push_back(cur);
        for (const auto &poly : polylines) {
            if (poly.size() < 2) {
                continue;
            }
            for (std::size_t i = 1; i < poly.size(); ++i) {
                const Point &a = poly[i - 1];
                const Point &b = poly[i];
                if (!sameCoord(a.x, b.x, tol)) {
                    continue;
                }
                if (nearPts(a, cur, tol)) {
                    frontier.push_back(b);
                } else if (nearPts(b, cur, tol)) {
                    frontier.push_back(a);
                }
            }
        }
    }
    for (auto &poly : polylines) {
        for (Point &pt : poly) {
            for (const Point &r : reached) {
                if (nearPts(pt, r, tol)) {
                    pt.x = newX;
                    break;
                }
            }
        }
    }
}

} // namespace

void retargetWirePolylinesQucsToCore(std::vector<std::vector<Point>> &polylines,
                                     const std::vector<std::pair<Point, Point>> &pinRetargets,
                                     double dbuPerEditorUnit)
{
    if (pinRetargets.empty() || polylines.empty()) {
        return;
    }
    const std::int64_t tol = matchTol(dbuPerEditorUnit, pinRetargets);

    // Lift T-junction buses onto the CORE pin axis before snapping endpoints.
    for (const auto &pair : pinRetargets) {
        const Point &oldPt = pair.first;
        const Point &newPt = pair.second;
        if (sameCoord(oldPt.x, newPt.x, tol) && !sameCoord(oldPt.y, newPt.y, tol)) {
            for (auto &poly : polylines) {
                if (poly.size() < 2) {
                    continue;
                }
                for (std::size_t i = 1; i < poly.size(); ++i) {
                    Point &a = poly[i - 1];
                    Point &b = poly[i];
                    if (!sameCoord(a.x, b.x, tol)) {
                        continue; // need vertical stub to pin
                    }
                    Point *junc = nullptr;
                    if (nearPts(a, oldPt, tol)) {
                        junc = &b;
                    } else if (nearPts(b, oldPt, tol)) {
                        junc = &a;
                    }
                    if (junc == nullptr || nearPts(*junc, oldPt, tol)) {
                        continue;
                    }
                    // Only lift if junction feeds a horizontal bus (T), not a lone vertical to gnd.
                    bool hasHorizontal = false;
                    for (auto &poly2 : polylines) {
                        if (poly2.size() < 2) {
                            continue;
                        }
                        for (std::size_t j = 1; j < poly2.size(); ++j) {
                            Point &u = poly2[j - 1];
                            Point &v = poly2[j];
                            if (!sameCoord(u.y, v.y, tol)) {
                                continue;
                            }
                            if (nearPts(u, *junc, tol) || nearPts(v, *junc, tol)) {
                                hasHorizontal = true;
                                break;
                            }
                        }
                        if (hasHorizontal) {
                            break;
                        }
                    }
                    if (hasHorizontal) {
                        liftHorizontalRunToY(polylines, *junc, newPt.y, tol);
                    }
                }
            }
            // Pin sits on a horizontal: raise that whole run to CORE pin Y.
            for (auto &poly : polylines) {
                if (poly.size() < 2) {
                    continue;
                }
                for (std::size_t i = 1; i < poly.size(); ++i) {
                    Point &a = poly[i - 1];
                    Point &b = poly[i];
                    if (!sameCoord(a.y, b.y, tol)) {
                        continue;
                    }
                    if (nearPts(a, oldPt, tol) || nearPts(b, oldPt, tol)) {
                        liftHorizontalRunToY(polylines, oldPt, newPt.y, tol);
                    }
                }
            }
        } else if (sameCoord(oldPt.y, newPt.y, tol) && !sameCoord(oldPt.x, newPt.x, tol)) {
            for (auto &poly : polylines) {
                if (poly.size() < 2) {
                    continue;
                }
                for (std::size_t i = 1; i < poly.size(); ++i) {
                    Point &a = poly[i - 1];
                    Point &b = poly[i];
                    if (!sameCoord(a.y, b.y, tol)) {
                        continue;
                    }
                    Point *junc = nullptr;
                    if (nearPts(a, oldPt, tol)) {
                        junc = &b;
                    } else if (nearPts(b, oldPt, tol)) {
                        junc = &a;
                    }
                    if (junc == nullptr) {
                        continue;
                    }
                    bool hasVertical = false;
                    for (auto &poly2 : polylines) {
                        if (poly2.size() < 2) {
                            continue;
                        }
                        for (std::size_t j = 1; j < poly2.size(); ++j) {
                            Point &u = poly2[j - 1];
                            Point &v = poly2[j];
                            if (!sameCoord(u.x, v.x, tol)) {
                                continue;
                            }
                            if (nearPts(u, *junc, tol) || nearPts(v, *junc, tol)) {
                                hasVertical = true;
                                break;
                            }
                        }
                        if (hasVertical) {
                            break;
                        }
                    }
                    if (hasVertical) {
                        liftVerticalRunToX(polylines, *junc, newPt.x, tol);
                    }
                }
            }
            for (auto &poly : polylines) {
                if (poly.size() < 2) {
                    continue;
                }
                for (std::size_t i = 1; i < poly.size(); ++i) {
                    Point &a = poly[i - 1];
                    Point &b = poly[i];
                    if (!sameCoord(a.x, b.x, tol)) {
                        continue;
                    }
                    if (nearPts(a, oldPt, tol) || nearPts(b, oldPt, tol)) {
                        liftVerticalRunToX(polylines, oldPt, newPt.x, tol);
                    }
                }
            }
        }
    }

    for (auto &poly : polylines) {
        for (Point &pt : poly) {
            retargetPoint(pt, pinRetargets, dbuPerEditorUnit);
        }
    }
}

} // namespace core
