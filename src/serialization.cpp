#include "serialization.h"

#include <database.capnp.h>
#include <design.capnp.h>
#include <dm.capnp.h>
#include <common.capnp.h>

namespace cdb {
namespace {

schema::Orient toSchemaOrient(Orient o)
{
    switch (o) {
    case Orient::R0: return schema::Orient::R0;
    case Orient::R90: return schema::Orient::R90;
    case Orient::R180: return schema::Orient::R180;
    case Orient::R270: return schema::Orient::R270;
    case Orient::MY: return schema::Orient::MY;
    case Orient::MX: return schema::Orient::MX;
    case Orient::MX90: return schema::Orient::MX90;
    case Orient::MY90: return schema::Orient::MY90;
    }
    return schema::Orient::R0;
}

Orient fromSchemaOrient(schema::Orient o)
{
    switch (o) {
    case schema::Orient::R0: return Orient::R0;
    case schema::Orient::R90: return Orient::R90;
    case schema::Orient::R180: return Orient::R180;
    case schema::Orient::R270: return Orient::R270;
    case schema::Orient::MY: return Orient::MY;
    case schema::Orient::MX: return Orient::MX;
    case schema::Orient::MX90: return Orient::MX90;
    case schema::Orient::MY90: return Orient::MY90;
    }
    return Orient::R0;
}

schema::ViewType toSchemaViewType(ViewType v)
{
    switch (v) {
    case ViewType::Layout: return schema::ViewType::LAYOUT;
    case ViewType::Schematic: return schema::ViewType::SCHEMATIC;
    case ViewType::Symbol: return schema::ViewType::SYMBOL;
    case ViewType::Abstract: return schema::ViewType::ABSTRACT;
    }
    return schema::ViewType::LAYOUT;
}

ViewType fromSchemaViewType(schema::ViewType v)
{
    switch (v) {
    case schema::ViewType::LAYOUT: return ViewType::Layout;
    case schema::ViewType::SCHEMATIC: return ViewType::Schematic;
    case schema::ViewType::SYMBOL: return ViewType::Symbol;
    case schema::ViewType::ABSTRACT: return ViewType::Abstract;
    }
    return ViewType::Layout;
}

schema::LayerPurpose toSchemaPurpose(LayerPurpose p)
{
    switch (p) {
    case LayerPurpose::Drawing: return schema::LayerPurpose::DRAWING;
    case LayerPurpose::Pin: return schema::LayerPurpose::PIN;
    case LayerPurpose::Label: return schema::LayerPurpose::LABEL;
    case LayerPurpose::Boundary: return schema::LayerPurpose::BOUNDARY;
    case LayerPurpose::Blockage: return schema::LayerPurpose::BLOCKAGE;
    case LayerPurpose::Wire: return schema::LayerPurpose::WIRE;
    case LayerPurpose::Fill: return schema::LayerPurpose::FILL;
    case LayerPurpose::Other: return schema::LayerPurpose::OTHER;
    }
    return schema::LayerPurpose::DRAWING;
}

LayerPurpose fromSchemaPurpose(schema::LayerPurpose p)
{
    switch (p) {
    case schema::LayerPurpose::DRAWING: return LayerPurpose::Drawing;
    case schema::LayerPurpose::PIN: return LayerPurpose::Pin;
    case schema::LayerPurpose::LABEL: return LayerPurpose::Label;
    case schema::LayerPurpose::BOUNDARY: return LayerPurpose::Boundary;
    case schema::LayerPurpose::BLOCKAGE: return LayerPurpose::Blockage;
    case schema::LayerPurpose::WIRE: return LayerPurpose::Wire;
    case schema::LayerPurpose::FILL: return LayerPurpose::Fill;
    case schema::LayerPurpose::OTHER: return LayerPurpose::Other;
    }
    return LayerPurpose::Drawing;
}

schema::Net::SigType toSchemaSigType(SigType s)
{
    switch (s) {
    case SigType::Signal: return schema::Net::SigType::SIGNAL;
    case SigType::Power: return schema::Net::SigType::POWER;
    case SigType::Ground: return schema::Net::SigType::GROUND;
    case SigType::Clock: return schema::Net::SigType::CLOCK;
    }
    return schema::Net::SigType::SIGNAL;
}

SigType fromSchemaSigType(schema::Net::SigType s)
{
    switch (s) {
    case schema::Net::SigType::SIGNAL: return SigType::Signal;
    case schema::Net::SigType::POWER: return SigType::Power;
    case schema::Net::SigType::GROUND: return SigType::Ground;
    case schema::Net::SigType::CLOCK: return SigType::Clock;
    }
    return SigType::Signal;
}

void writePoint(schema::Point::Builder b, const Point &p)
{
    b.setX(p.x);
    b.setY(p.y);
}

Point readPoint(schema::Point::Reader r)
{
    return Point{r.getX(), r.getY()};
}

void writeBox(schema::Box::Builder b, const Box &box)
{
    b.setLlx(box.llx);
    b.setLly(box.lly);
    b.setUrx(box.urx);
    b.setUry(box.ury);
}

Box readBox(schema::Box::Reader r)
{
    return Box{r.getLlx(), r.getLly(), r.getUrx(), r.getUry()};
}

void writeTransform(schema::Transform::Builder b, const Transform &t)
{
    b.setX(t.x);
    b.setY(t.y);
    b.setOrient(toSchemaOrient(t.orient));
    b.setMag(t.mag);
}

Transform readTransform(schema::Transform::Reader r)
{
    Transform t;
    t.x = r.getX();
    t.y = r.getY();
    t.orient = fromSchemaOrient(r.getOrient());
    t.mag = r.getMag();
    return t;
}

void writeProperties(capnp::List<schema::Property>::Builder listBuilder,
                     const std::vector<Property> &props)
{
    for (std::size_t i = 0; i < props.size(); ++i) {
        listBuilder[i].setName(props[i].name);
        listBuilder[i].setValue(props[i].value);
    }
}

std::vector<Property> readProperties(capnp::List<schema::Property>::Reader listReader)
{
    std::vector<Property> out;
    out.reserve(listReader.size());
    for (const auto item : listReader) {
        out.push_back({item.getName().cStr(), item.getValue().cStr()});
    }
    return out;
}

void writeLayerSpec(schema::LayerSpec::Builder b, const LayerSpec &layer)
{
    b.setLayerNum(layer.layerNum);
    b.setDataType(layer.dataType);
    b.setName(layer.name);
    b.setPurpose(toSchemaPurpose(layer.purpose));
}

LayerSpec readLayerSpec(schema::LayerSpec::Reader r)
{
    LayerSpec layer;
    layer.layerNum = r.getLayerNum();
    layer.dataType = r.getDataType();
    layer.name = r.getName().cStr();
    layer.purpose = fromSchemaPurpose(r.getPurpose());
    return layer;
}

void writeShape(schema::Shape::Builder b, const Shape &shape)
{
    switch (shape.type()) {
    case Shape::Type::Rect: {
        const auto &r = *shape.rect();
        auto rb = b.initRect();
        writeBox(rb.initBox(), r.box);
        rb.setLayerId(r.layerId);
        break;
    }
    case Shape::Type::Polygon: {
        const auto &p = *shape.polygon();
        auto pb = b.initPolygon();
        auto pts = pb.initPoints(p.points.size());
        for (std::size_t i = 0; i < p.points.size(); ++i) {
            writePoint(pts[i], p.points[i]);
        }
        pb.setLayerId(p.layerId);
        break;
    }
    case Shape::Type::Path: {
        const auto &p = *shape.path();
        auto pb = b.initPath();
        auto pts = pb.initPoints(p.points.size());
        for (std::size_t i = 0; i < p.points.size(); ++i) {
            writePoint(pts[i], p.points[i]);
        }
        pb.setWidth(p.width);
        pb.setLayerId(p.layerId);
        break;
    }
    case Shape::Type::Text: {
        const auto &t = *shape.text();
        auto tb = b.initText();
        writePoint(tb.initPosition(), t.position);
        tb.setText(t.text);
        tb.setLayerId(t.layerId);
        tb.setHeight(t.height);
        break;
    }
    }
    writeProperties(b.initProperties(shape.properties().size()), shape.properties());
}

Shape readShape(schema::Shape::Reader r)
{
    Shape shape(Shape::RectData{});
    switch (r.which()) {
    case schema::Shape::RECT: {
        const auto rr = r.getRect();
        Shape::RectData data;
        data.box = readBox(rr.getBox());
        data.layerId = rr.getLayerId();
        shape = Shape(data);
        break;
    }
    case schema::Shape::POLYGON: {
        const auto pr = r.getPolygon();
        Shape::PolygonData data;
        for (const auto pt : pr.getPoints()) {
            data.points.push_back(readPoint(pt));
        }
        data.layerId = pr.getLayerId();
        shape = Shape(data);
        break;
    }
    case schema::Shape::PATH: {
        const auto pr = r.getPath();
        Shape::PathData data;
        for (const auto pt : pr.getPoints()) {
            data.points.push_back(readPoint(pt));
        }
        data.width = pr.getWidth();
        data.layerId = pr.getLayerId();
        shape = Shape(data);
        break;
    }
    case schema::Shape::TEXT: {
        const auto tr = r.getText();
        Shape::TextData data;
        data.position = readPoint(tr.getPosition());
        data.text = tr.getText().cStr();
        data.layerId = tr.getLayerId();
        data.height = tr.getHeight();
        shape = Shape(data);
        break;
    }
    default:
        break;
    }
    shape.properties() = readProperties(r.getProperties());
    return shape;
}

void writeBlock(schema::Block::Builder b, const Block &block)
{
    auto shapes = b.initShapes(block.shapes().size());
    for (std::size_t i = 0; i < block.shapes().size(); ++i) {
        writeShape(shapes[i], block.shapes()[i]);
    }
    auto insts = b.initInstances(block.instances().size());
    for (std::size_t i = 0; i < block.instances().size(); ++i) {
        const auto &inst = block.instances()[i];
        auto ib = insts[i];
        ib.setCellName(inst.cellName());
        writeTransform(ib.initTransform(), inst.transform());
        writeProperties(ib.initProperties(inst.properties().size()), inst.properties());
    }
    auto nets = b.initNets(block.nets().size());
    for (std::size_t i = 0; i < block.nets().size(); ++i) {
        const auto &net = block.nets()[i];
        auto nb = nets[i];
        nb.setName(net.name());
        nb.setSigType(toSchemaSigType(net.sigType()));
        auto terms = nb.initTerms(net.terms().size());
        for (std::size_t j = 0; j < net.terms().size(); ++j) {
            terms[j].setName(net.terms()[j].name());
            terms[j].setLayerId(net.terms()[j].layerId());
            writePoint(terms[j].initPosition(), net.terms()[j].position());
        }
    }
    writeBox(b.initBbox(), block.bbox());
}

Block readBlock(schema::Block::Reader r)
{
    Block block;
    for (const auto shape : r.getShapes()) {
        block.shapes().push_back(readShape(shape));
    }
    for (const auto inst : r.getInstances()) {
        Instance instance(inst.getCellName().cStr(), readTransform(inst.getTransform()));
        instance.properties() = readProperties(inst.getProperties());
        block.instances().push_back(std::move(instance));
    }
    for (const auto net : r.getNets()) {
        Net n(net.getName().cStr(), fromSchemaSigType(net.getSigType()));
        for (const auto term : net.getTerms()) {
            n.terms().emplace_back(term.getName().cStr(), term.getLayerId(), readPoint(term.getPosition()));
        }
        block.nets().push_back(std::move(n));
    }
    return block;
}

void writeCellContent(schema::CellContent::Builder b, const CellContent &content)
{
    b.setViewType(toSchemaViewType(content.viewType()));
    b.setDbuPerMicron(content.dbuPerMicron());
    auto layers = b.initLayers(content.layers().size());
    for (std::size_t i = 0; i < content.layers().size(); ++i) {
        writeLayerSpec(layers[i], content.layers()[i]);
    }
    writeProperties(b.initProperties(content.properties().size()), content.properties());
    writeBlock(b.initBlock(), content.block());
}

CellContent readCellContent(schema::CellContent::Reader r)
{
    CellContent content(fromSchemaViewType(r.getViewType()), r.getDbuPerMicron());
    for (const auto layer : r.getLayers()) {
        content.layers().push_back(readLayerSpec(layer));
    }
    content.properties() = readProperties(r.getProperties());
    content.block() = readBlock(r.getBlock());
    return content;
}

} // namespace

void writeDatabase(schema::Database::Builder root, const Database &db)
{
    root.setVersion(db.version());
    root.setGenerator(db.generator());
    root.setTechnology(db.technology());

    auto libBuilder = root.initLib();
    libBuilder.setName(db.lib().name());
    writeProperties(libBuilder.initProperties(db.lib().properties().size()), db.lib().properties());

    auto cells = libBuilder.initCells(db.lib().cells().size());
    for (std::size_t ci = 0; ci < db.lib().cells().size(); ++ci) {
        const auto &cell = db.lib().cells()[ci];
        auto cellBuilder = cells[ci];
        cellBuilder.setName(cell.name());
        writeProperties(cellBuilder.initProperties(cell.properties().size()), cell.properties());

        auto contents = cellBuilder.initContents(cell.contents().size());
        for (std::size_t i = 0; i < cell.contents().size(); ++i) {
            writeCellContent(contents[i], cell.contents()[i]);
        }
    }
}

Database readDatabase(schema::Database::Reader root)
{
    Database db;
    db.setVersion(root.getVersion().cStr());
    db.setGenerator(root.getGenerator().cStr());
    db.setTechnology(root.getTechnology().cStr());

    const auto libReader = root.getLib();
    db.lib() = Lib(libReader.getName().cStr());
    db.lib().properties() = readProperties(libReader.getProperties());

    for (const auto cellReader : libReader.getCells()) {
        Cell cell(cellReader.getName().cStr());
        cell.properties() = readProperties(cellReader.getProperties());
        for (const auto contentReader : cellReader.getContents()) {
            cell.contents().push_back(readCellContent(contentReader));
        }
        db.lib().cells().push_back(std::move(cell));
    }
    return db;
}

} // namespace cdb
