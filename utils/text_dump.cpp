#include "text_dump.h"

#include "cell_content.h"
#include "enums.h"

#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace core {
namespace {

void indent(std::ostream &out, int level)
{
    for (int i = 0; i < level; ++i) {
        out << "  ";
    }
}

void dumpBox(std::ostream &out, const Box &box)
{
    out << "(" << box.llx << "," << box.lly << ")-(" << box.urx << "," << box.ury << ")";
}

void dumpShape(std::ostream &out, const Shape &shape, int level, const std::vector<LayerSpec> &layers)
{
    indent(out, level);
    out << "shape ";
    switch (shape.type()) {
    case Shape::Type::Rect:
        out << "rect layer=";
        if (const auto *r = shape.rect()) {
            if (r->layerId < layers.size()) {
                out << layers[r->layerId].name;
            } else {
                out << r->layerId;
            }
            out << " box=";
            dumpBox(out, r->box);
        }
        break;
    case Shape::Type::Polygon:
        out << "polygon layer=";
        if (const auto *p = shape.polygon()) {
            if (p->layerId < layers.size()) {
                out << layers[p->layerId].name;
            } else {
                out << p->layerId;
            }
            out << " points=" << p->points.size();
        }
        break;
    case Shape::Type::Path:
        out << "path layer=";
        if (const auto *p = shape.path()) {
            if (p->layerId < layers.size()) {
                out << layers[p->layerId].name;
            } else {
                out << p->layerId;
            }
            out << " width=" << p->width << " points=" << p->points.size();
        }
        break;
    case Shape::Type::Text:
        out << "text layer=";
        if (const auto *t = shape.text()) {
            if (t->layerId < layers.size()) {
                out << layers[t->layerId].name;
            } else {
                out << t->layerId;
            }
            out << " \"" << t->text << "\" at (" << t->position.x << "," << t->position.y << ")";
        }
        break;
    }
    out << "\n";
}

} // namespace

void TextDumper::dump(const Database &db, std::ostream &out) const
{
    out << "=== CORE text dump ===\n";
    out << "version: " << db.version() << "\n";
    out << "generator: " << db.generator() << "\n";
    out << "technology: " << db.technology() << "\n";
    out << "lib: " << db.lib().name() << "\n";
    out << "layers: " << db.lib().layers().size() << "\n";
    out << "cells: " << db.lib().cells().size() << "\n\n";

    const auto &layers = db.lib().layers();
    for (const auto &cell : db.lib().cells()) {
        out << "cell \"" << cell.name() << "\"\n";
        for (const auto &content : cell.contents()) {
            indent(out, 1);
            out << "type " << viewTypeToString(content.viewType())
                << " dbuPerMicron=" << std::fixed << std::setprecision(3) << content.dbuPerMicron()
                << "\n";

            const auto &block = content.block();
            indent(out, 1);
            out << "bbox=";
            dumpBox(out, block.bbox());
            out << "\n";

            indent(out, 1);
            out << "shapes=" << block.shapes().size()
                << " instances=" << block.instances().size()
                << " nets=" << block.nets().size() << "\n";

            for (const auto &shape : block.shapes()) {
                dumpShape(out, shape, 2, layers);
            }
            for (const auto &inst : block.instances()) {
                indent(out, 2);
                out << "instance -> \"" << inst.cellName() << "\" at ("
                    << inst.transform().x << "," << inst.transform().y << ")\n";
            }
            for (const auto &net : block.nets()) {
                indent(out, 2);
                out << "net \"" << net.name() << "\" sig=" << sigTypeToString(net.sigType())
                    << " terms=" << net.terms().size() << "\n";
            }
        }
        out << "\n";
    }
}

void TextDumper::dumpToFile(const Database &db, const std::string &path) const
{
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Cannot open dump file: " + path);
    }
    dump(db, out);
}

} // namespace core
