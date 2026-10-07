/************************************************************************
 *  CommonDB – ROOM design database
 *
 *  Copyright (C) 2023–2026 IHP Authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 ************************************************************************/

#include "em_lookalike_symbol.h"

#include "coord_scale.h"
#include "database.h"
#include "layer_utils.h"
#include "room_paths.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

namespace room {
namespace {

constexpr double kTargetMaxEditorUnits = 80.0;
constexpr double kPinHalfEditorUnits = 2.0;
constexpr std::size_t kMaxOutlineShapes = 400;

void ensureParentDirectories(const std::string &filePath)
{
    std::string partial;
    for (char ch : filePath) {
        if (ch == '/' || ch == '\\') {
            if (partial.size() > 1) {
#ifdef _WIN32
                _mkdir(partial.c_str());
#else
                mkdir(partial.c_str(), 0755);
#endif
            }
        }
        partial.push_back(ch);
    }
}

void expandBBox(EmLookalikeInput &in, double x, double y)
{
    if (!in.hasBBox) {
        in.bboxLlxUm = in.bboxUrxUm = x;
        in.bboxLlyUm = in.bboxUryUm = y;
        in.hasBBox = true;
        return;
    }
    in.bboxLlxUm = std::min(in.bboxLlxUm, x);
    in.bboxLlyUm = std::min(in.bboxLlyUm, y);
    in.bboxUrxUm = std::max(in.bboxUrxUm, x);
    in.bboxUryUm = std::max(in.bboxUryUm, y);
}

void expandBBoxPoly(EmLookalikeInput &in, const std::vector<std::pair<double, double>> &poly)
{
    for (const auto &pt : poly)
        expandBBox(in, pt.first, pt.second);
}

std::uint32_t ensureNamedLayer(CellContent &content, const std::string &name, LayerPurpose purpose,
                               std::uint16_t layerNum)
{
    return findOrAddViewLayer(content, LayerSpec{layerNum, 0, name, purpose}, {});
}

void appendRectOutline(std::vector<std::vector<std::pair<double, double>>> &polys,
                       double llx, double lly, double urx, double ury)
{
    polys.push_back({{llx, lly}, {urx, lly}, {urx, ury}, {llx, ury}, {llx, lly}});
}

} // namespace

bool fillLookalikeOutlineFromLayoutRoom(const std::string &layoutPath,
                                        const std::string &topCell,
                                        EmLookalikeInput &inout,
                                        std::string *error)
{
    if (error)
        error->clear();
    if (layoutPath.empty() || !isViewRoomFile(layoutPath, ViewType::Layout)) {
        if (error)
            *error = "Not a layout.room file: " + layoutPath;
        return false;
    }

    Database db;
    try {
        db = Database::loadFromFile(layoutPath);
    } catch (const std::exception &ex) {
        if (error)
            *error = ex.what();
        return false;
    }

    const Cell *cell = nullptr;
    if (!topCell.empty())
        cell = db.lib().findCell(topCell);
    if (cell == nullptr && db.lib().cells().size() == 1)
        cell = &db.lib().cells().front();
    if (cell == nullptr && !db.lib().cells().empty())
        cell = &db.lib().cells().front();
    if (cell == nullptr) {
        if (error)
            *error = "No cell in layout.room";
        return false;
    }

    const CellContent *layout = cell->findContent(ViewType::Layout);
    if (layout == nullptr) {
        if (error)
            *error = "No layout view in cell: " + cell->name();
        return false;
    }

    // dbu → µm. Some layout.room files store dbuPerMicron≈1e6 while coordinates are in nm
    // (span ~µm when /1000, but ~nm when /1e6). Sanity-check against the block bbox.
    double dbuPerUm =
        layout->dbuPerMicron() > 0.0 ? layout->dbuPerMicron() : kDefaultLayoutDbuPerMicron;
    {
        const Box &bb = layout->block().bbox();
        const double spanDbu =
            std::max(std::abs(static_cast<double>(bb.urx - bb.llx)),
                     std::abs(static_cast<double>(bb.ury - bb.lly)));
        const double spanUm = spanDbu / dbuPerUm;
        // Typical EM cell: 0.5 µm … few mm. A sub-0.05 µm span with thousands of dbu
        // means dbuPerMicron is ~1000× too large.
        if (spanDbu > 100.0 && spanUm < 0.05)
            dbuPerUm = kDefaultLayoutDbuPerMicron;
    }
    const double toUm = 1.0 / dbuPerUm;

    inout.outlinePolysUm.clear();
    inout.hasBBox = false;
    std::size_t shapeCount = 0;

    auto addPoly = [&](std::vector<std::pair<double, double>> poly) {
        if (poly.size() < 2)
            return;
        expandBBoxPoly(inout, poly);
        if (shapeCount < kMaxOutlineShapes) {
            inout.outlinePolysUm.push_back(std::move(poly));
            ++shapeCount;
        }
    };

    for (const Shape &shape : layout->block().shapes()) {
        switch (shape.type()) {
        case Shape::Type::Rect: {
            const Shape::RectData *r = shape.rect();
            if (!r)
                break;
            addPoly({{r->box.llx * toUm, r->box.lly * toUm},
                     {r->box.urx * toUm, r->box.lly * toUm},
                     {r->box.urx * toUm, r->box.ury * toUm},
                     {r->box.llx * toUm, r->box.ury * toUm},
                     {r->box.llx * toUm, r->box.lly * toUm}});
            break;
        }
        case Shape::Type::Polygon: {
            const Shape::PolygonData *p = shape.polygon();
            if (!p || p->points.size() < 2)
                break;
            std::vector<std::pair<double, double>> poly;
            poly.reserve(p->points.size());
            for (const Point &pt : p->points)
                poly.emplace_back(pt.x * toUm, pt.y * toUm);
            addPoly(std::move(poly));
            break;
        }
        case Shape::Type::Path: {
            const Shape::PathData *p = shape.path();
            if (!p || p->points.size() < 2)
                break;
            std::vector<std::pair<double, double>> poly;
            poly.reserve(p->points.size());
            for (const Point &pt : p->points)
                poly.emplace_back(pt.x * toUm, pt.y * toUm);
            addPoly(std::move(poly));
            break;
        }
        default:
            break;
        }
    }

    if (!inout.hasBBox) {
        const Box &bb = layout->block().bbox();
        if (bb.urx > bb.llx && bb.ury > bb.lly) {
            inout.bboxLlxUm = bb.llx * toUm;
            inout.bboxLlyUm = bb.lly * toUm;
            inout.bboxUrxUm = bb.urx * toUm;
            inout.bboxUryUm = bb.ury * toUm;
            inout.hasBBox = true;
        }
    }

    if (!inout.hasBBox) {
        if (error)
            *error = "Layout has no geometry for lookalike outline";
        return false;
    }

    // Dense layouts: keep bbox body only (pins still use real port XY).
    if (shapeCount >= kMaxOutlineShapes)
        inout.outlinePolysUm.clear();

    return true;
}

std::string writeLookalikeSymbolRoom(const EmLookalikeInput &in)
{
    if (in.outputPath.empty())
        return "Symbol output path is empty.";
    if (in.cellName.empty())
        return "Cell name is empty.";

    EmLookalikeInput local = in;
    if (!local.hasBBox) {
        for (const auto &poly : local.outlinePolysUm)
            expandBBoxPoly(local, poly);
    }
    // Always fold port XY into the scale bbox so pins and outline share one scale
    // (GDS ports in µm must not sit 1000× outside a mis-scaled layout.room outline).
    for (const EmLookalikePort &p : local.ports) {
        if (p.hasPosition)
            expandBBox(local, p.xUm, p.yUm);
    }
    if (!local.hasBBox) {
        // Degenerate fallback: unit square so pins can still be placed.
        local.bboxLlxUm = -0.5;
        local.bboxLlyUm = -0.5;
        local.bboxUrxUm = 0.5;
        local.bboxUryUm = 0.5;
        local.hasBBox = true;
    }

    if (local.outlinePolysUm.empty()) {
        appendRectOutline(local.outlinePolysUm, local.bboxLlxUm, local.bboxLlyUm, local.bboxUrxUm,
                          local.bboxUryUm);
    }

    const double width = std::max(1e-9, local.bboxUrxUm - local.bboxLlxUm);
    const double height = std::max(1e-9, local.bboxUryUm - local.bboxLlyUm);
    const double cx = 0.5 * (local.bboxLlxUm + local.bboxUrxUm);
    const double cy = 0.5 * (local.bboxLlyUm + local.bboxUryUm);
    const double scale = kTargetMaxEditorUnits / std::max(width, height);
    const double dbu = kXschemDbuPerEditorUnit;

    auto toSym = [&](double xUm, double yUm) -> Point {
        const double ex = (xUm - cx) * scale;
        const double ey = (yUm - cy) * scale;
        return Point{editorUnitsToDbu(ex, dbu), editorUnitsToDbu(ey, dbu)};
    };

    try {
        ensureParentDirectories(local.outputPath);

        Database db;
        db.setGenerator("EMStudio/lookalike-symbol");
        Cell &cell = db.lib().getOrCreateCell(local.cellName);
        CellContent &content = cell.getOrCreateContent(ViewType::Symbol, kDefaultLayoutDbuPerMicron);
        content.setDbuPerEditorUnit(kXschemDbuPerEditorUnit);
        content.block().shapes().clear();
        content.block().nets().clear();
        content.layers().clear();
        content.properties().clear();
        content.properties().push_back({"em.lookalike", "1"});
        content.properties().push_back({"type", "subcircuit"});

        const std::uint32_t drawingLayer =
            ensureNamedLayer(content, "drawing", LayerPurpose::Drawing, 1);
        const std::uint32_t pinLayer = ensureNamedLayer(content, "pin", LayerPurpose::Pin, 2);

        for (const auto &polyUm : local.outlinePolysUm) {
            if (polyUm.size() < 2)
                continue;
            Shape::PolygonData poly;
            poly.layerId = drawingLayer;
            poly.points.reserve(polyUm.size());
            for (const auto &pt : polyUm)
                poly.points.push_back(toSym(pt.first, pt.second));
            content.block().shapes().push_back(Shape(std::move(poly)));
        }

        // Stable pin order by Touchstone index.
        std::vector<EmLookalikePort> ports = local.ports;
        std::sort(ports.begin(), ports.end(),
                  [](const EmLookalikePort &a, const EmLookalikePort &b) { return a.index < b.index; });

        const std::int64_t pinHalf = editorUnitsToDbu(kPinHalfEditorUnits, dbu);
        int fallbackSlot = 0;
        const int nPorts = static_cast<int>(ports.size());

        for (EmLookalikePort &port : ports) {
            if (port.name.empty())
                port.name = "P" + std::to_string(port.index > 0 ? port.index : (fallbackSlot + 1));
            if (port.index == 0)
                port.index = static_cast<std::uint16_t>(fallbackSlot + 1);

            Point pinPt;
            if (port.hasPosition) {
                pinPt = toSym(port.xUm, port.yUm);
            } else {
                // Alternate left / right of the scaled bbox.
                const double side = (fallbackSlot % 2 == 0) ? local.bboxLlxUm : local.bboxUrxUm;
                const double t =
                    nPorts <= 1 ? 0.5
                                : (0.15 + 0.7 * static_cast<double>(fallbackSlot) / (nPorts - 1));
                const double y = local.bboxLlyUm + t * (local.bboxUryUm - local.bboxLlyUm);
                pinPt = toSym(side, y);
            }
            ++fallbackSlot;

            Shape::RectData rect;
            rect.layerId = pinLayer;
            rect.box = Box(pinPt.x - pinHalf, pinPt.y - pinHalf, pinPt.x + pinHalf, pinPt.y + pinHalf);
            Shape shape(rect);
            shape.properties().push_back({"lab", port.name});
            shape.properties().push_back({"name", port.name});
            shape.properties().push_back({"pinnumber", std::to_string(port.index)});
            shape.properties().push_back({"direction", "inout"});
            shape.properties().push_back({"pinWidth", "5"});
            content.block().shapes().push_back(std::move(shape));

            Net net(port.name);
            net.terms().emplace_back(port.name, pinLayer, pinPt);
            content.block().nets().push_back(std::move(net));
        }

        content.block().recomputeBBox();
        db.lib().recomputeAllBBoxes(ViewType::Symbol);
        db.saveToFile(local.outputPath, ViewType::Symbol);
    } catch (const std::exception &ex) {
        return std::string("Failed to write lookalike symbol: ") + ex.what();
    } catch (...) {
        return "Failed to write lookalike symbol (unknown error).";
    }
    return {};
}

} // namespace room
