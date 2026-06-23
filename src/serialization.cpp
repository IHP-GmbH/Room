/*!****************************************************************************************
 * \file serialization.cpp
 * \brief Cap'n Proto serialization between Database and schema::Database messages.
 *
 * Writes and reads cells, view payloads (compact or verbose), layers, properties, and
 * LibIndex. View topology decoding auto-selects compact vs block encoding on load.
 *****************************************************************************************/

#include "serialization.h"

#include "source_info.h"
#include "file_summary.h"

#include "compact_codec.h"
#include "layer_utils.h"
#include "lib_index.h"

#include <cmath>

#include <database.capnp.h>
#include <design.capnp.h>
#include <dm.capnp.h>
#include <common.capnp.h>
#include <views.capnp.h>
#include <index.capnp.h>

namespace core {
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
    if (box.empty()) {
        b.setLlx(1);
        b.setLly(1);
        b.setUrx(0);
        b.setUry(0);
        return;
    }
    b.setLlx(box.llx);
    b.setLly(box.lly);
    b.setUrx(box.urx);
    b.setUry(box.ury);
}

Box readBox(schema::Box::Reader r)
{
    const std::int64_t llx = r.getLlx();
    const std::int64_t lly = r.getLly();
    const std::int64_t urx = r.getUrx();
    const std::int64_t ury = r.getUry();
    if (llx > urx || lly > ury) {
        return Box{};
    }
    return Box{llx, lly, urx, ury};
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
    case Shape::Type::Arc: {
        const auto &a = *shape.arc();
        auto ab = b.initArc();
        ab.setCenterX(static_cast<double>(a.center.x) / 1000.0);
        ab.setCenterY(static_cast<double>(a.center.y) / 1000.0);
        ab.setRadius(a.radius);
        ab.setStartAngle(a.startAngle);
        ab.setEndAngle(a.endAngle);
        ab.setWidth(a.width);
        ab.setLayerId(a.layerId);
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
        const auto points = pr.getPoints();
        data.points.reserve(points.size());
        for (const auto pt : points) {
            data.points.push_back(readPoint(pt));
        }
        data.layerId = pr.getLayerId();
        shape = Shape(data);
        break;
    }
    case schema::Shape::PATH: {
        const auto pr = r.getPath();
        Shape::PathData data;
        const auto points = pr.getPoints();
        data.points.reserve(points.size());
        for (const auto pt : points) {
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
    case schema::Shape::ARC: {
        const auto ar = r.getArc();
        Shape::ArcData data;
        data.center = Point{static_cast<std::int64_t>(std::llround(ar.getCenterX() * 1000.0)),
                            static_cast<std::int64_t>(std::llround(ar.getCenterY() * 1000.0))};
        data.radius = ar.getRadius();
        data.startAngle = ar.getStartAngle();
        data.endAngle = ar.getEndAngle();
        data.width = ar.getWidth();
        data.layerId = ar.getLayerId();
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
    const auto shapes = r.getShapes();
    const auto instances = r.getInstances();
    const auto nets = r.getNets();
    block.shapes().reserve(shapes.size());
    block.instances().reserve(instances.size());
    block.nets().reserve(nets.size());

    for (const auto shape : shapes) {
        block.shapes().push_back(readShape(shape));
    }
    for (const auto inst : instances) {
        Instance instance(inst.getCellName().cStr(), readTransform(inst.getTransform()));
        instance.properties() = readProperties(inst.getProperties());
        block.instances().push_back(std::move(instance));
    }
    for (const auto net : nets) {
        Net n(net.getName().cStr(), fromSchemaSigType(net.getSigType()));
        const auto terms = net.getTerms();
        n.terms().reserve(terms.size());
        for (const auto term : terms) {
            n.terms().emplace_back(term.getName().cStr(), term.getLayerId(), readPoint(term.getPosition()));
        }
        block.nets().push_back(std::move(n));
    }
    return block;
}

template <typename ViewBuilder>
void writeViewLayers(ViewBuilder viewBuilder, const std::vector<LayerSpec> &layers)
{
    auto layerList = viewBuilder.initLayers(layers.size());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        writeLayerSpec(layerList[i], layers[i]);
    }
}

template <typename ViewReader>
std::vector<LayerSpec> readViewLayers(ViewReader viewReader)
{
    const auto layerReader = viewReader.getLayers();
    std::vector<LayerSpec> layers;
    layers.reserve(layerReader.size());
    for (const auto layer : layerReader) {
        layers.push_back(readLayerSpec(layer));
    }
    return layers;
}

void writePCellInfo(schema::PCellInfo::Builder builder, const PCellInfo &pCell)
{
    builder.setMasterName(pCell.masterName());
    writeProperties(builder.initParameters(pCell.parameters().size()), pCell.parameters());
}

PCellInfo readPCellInfo(schema::PCellInfo::Reader reader)
{
    PCellInfo pCell;
    pCell.setMasterName(reader.getMasterName().cStr());
    pCell.parameters() = readProperties(reader.getParameters());
    return pCell;
}

void writeBlockShell(schema::Block::Builder b, const Block &block)
{
    b.initShapes(0);
    b.initInstances(0);
    b.initNets(0);
    writeBox(b.initBbox(), block.bbox());
}

template <typename ViewBuilder>
void writeViewTopology(ViewBuilder viewBuilder,
                       const std::vector<LayerSpec> &layers,
                       const Block &block,
                       bool compactGeometry)
{
    writeViewLayers(viewBuilder, layers);
    if (compactGeometry) {
        writeBlockShell(viewBuilder.initBlock(), block);
        writeCompactBlock(viewBuilder.initCompact(), block);
    } else {
        writeBlock(viewBuilder.initBlock(), block);
    }
}

bool blockHasTopology(schema::Block::Reader block)
{
    return block.getShapes().size() > 0 || block.getInstances().size() > 0 || block.getNets().size() > 0;
}

template <typename ViewReader>
Block readViewTopology(ViewReader viewReader)
{
    const auto blockReader = viewReader.getBlock();
    const auto compactReader = viewReader.getCompact();
    if (blockHasTopology(blockReader)) {
        return readBlock(blockReader);
    }
    if (compactBlockHasGeometry(compactReader)) {
        return readCompactBlock(compactReader);
    }
    return readBlock(blockReader);
}

void writeViewPayload(schema::ViewPayload::Builder payload,
                      ViewType viewType,
                      const std::vector<LayerSpec> &layers,
                      const Block &block,
                      bool compactGeometry)
{
    switch (viewType) {
    case ViewType::Layout: {
        auto layout = payload.initLayout();
        writeViewTopology(layout, layers, block, compactGeometry);
        break;
    }
    case ViewType::Schematic: {
        auto schematic = payload.initSchematic();
        writeViewTopology(schematic, layers, block, compactGeometry);
        break;
    }
    case ViewType::Symbol: {
        auto symbol = payload.initSymbol();
        writeViewTopology(symbol, layers, block, compactGeometry);
        break;
    }
    case ViewType::Abstract: {
        auto abstract = payload.initAbstract();
        writeViewTopology(abstract, layers, block, compactGeometry);
        break;
    }
    }
}

Block readBlockFromPayload(schema::ViewPayload::Reader payload)
{
    switch (payload.which()) {
    case schema::ViewPayload::LAYOUT:
        return readViewTopology(payload.getLayout());
    case schema::ViewPayload::SCHEMATIC:
        return readViewTopology(payload.getSchematic());
    case schema::ViewPayload::SYMBOL:
        return readViewTopology(payload.getSymbol());
    case schema::ViewPayload::ABSTRACT:
        return readViewTopology(payload.getAbstract());
    default:
        return Block{};
    }
}

void writeCellContent(schema::CellContent::Builder b,
                      const CellContent &content,
                      const std::vector<LayerSpec> &libLayers,
                      bool compactGeometry)
{
    b.setViewType(toSchemaViewType(content.viewType()));
    b.setDbuPerMicron(content.dbuPerMicron());
    std::vector<Property> properties = content.properties();
    appendSourceInfoProperties(content.sourceInfo(), properties);
    writeProperties(b.initProperties(properties.size()), properties);
    if (content.hasOpaquePayload()) {
        auto opaque = b.initPayload().initOpaque();
        opaque.setMimeType(content.opaqueMimeType());
        opaque.setData(kj::arrayPtr(content.opaqueData().data(), content.opaqueData().size()));
        return;
    }
    const std::vector<LayerSpec> viewLayers = layersForSerialization(content, libLayers);
    writeViewPayload(b.initPayload(), content.viewType(), viewLayers, content.block(), compactGeometry);
}

CellContent readCellContent(schema::CellContent::Reader r)
{
    CellContent content(fromSchemaViewType(r.getViewType()), r.getDbuPerMicron());
    content.properties() = readProperties(r.getProperties());
    content.sourceInfo() = extractSourceInfo(content.properties());

    const auto payload = r.getPayload();
    switch (payload.which()) {
    case schema::ViewPayload::LAYOUT: {
        const auto layout = payload.getLayout();
        content.layers() = readViewLayers(layout);
        content.block() = readViewTopology(layout);
        break;
    }
    case schema::ViewPayload::SCHEMATIC: {
        const auto schematic = payload.getSchematic();
        content.layers() = readViewLayers(schematic);
        content.block() = readViewTopology(schematic);
        break;
    }
    case schema::ViewPayload::SYMBOL: {
        const auto symbol = payload.getSymbol();
        content.layers() = readViewLayers(symbol);
        content.block() = readViewTopology(symbol);
        break;
    }
    case schema::ViewPayload::ABSTRACT: {
        const auto abstract = payload.getAbstract();
        content.layers() = readViewLayers(abstract);
        content.block() = readViewTopology(abstract);
        break;
    }
    case schema::ViewPayload::OPAQUE: {
        const auto opaque = payload.getOpaque();
        const auto data = opaque.getData();
        content.setOpaquePayload(opaque.getMimeType().cStr(),
                                 std::vector<std::uint8_t>(data.begin(), data.end()));
        break;
    }
    default:
        content.block() = readBlockFromPayload(payload);
        break;
    }

    return content;
}

void readLibLayers(Lib &lib, schema::Lib::Reader libReader)
{
    const auto libLayers = libReader.getLayers();
    lib.layers().reserve(libLayers.size());
    for (const auto layer : libLayers) {
        lib.layers().push_back(readLayerSpec(layer));
    }
}

void writeLibIndex(schema::LibIndex::Builder builder, const Lib &lib, const LibIndex &index)
{
    builder.setPlacementCount(index.placementCount);

    auto topCells = builder.initTopCells(index.topCells.size());
    for (std::size_t i = 0; i < index.topCells.size(); ++i) {
        topCells.set(i, index.topCells[i]);
    }

    auto entries = builder.initEntries(lib.cells().size());
    for (std::size_t ci = 0; ci < lib.cells().size(); ++ci) {
        const std::string &name = lib.cells()[ci].name();
        auto entry = entries[ci];
        entry.setName(name);

        const auto bboxIt = index.cellBboxes.find(name);
        writeBox(entry.initBbox(), bboxIt != index.cellBboxes.end() ? bboxIt->second : Box{});

        const auto refsIt = index.childRefs.find(name);
        static const std::vector<std::string> kEmptyChildRefs;
        const std::vector<std::string> &refs =
            refsIt != index.childRefs.end() ? refsIt->second : kEmptyChildRefs;
        auto childRefs = entry.initChildRefs(refs.size());
        for (std::size_t i = 0; i < refs.size(); ++i) {
            childRefs.set(i, refs[i]);
        }

        const auto refCountIt = index.referenceCount.find(name);
        entry.setRefCount(static_cast<std::uint32_t>(
            refCountIt != index.referenceCount.end() ? refCountIt->second : 0));
    }
}

LibIndex readLibIndex(schema::LibIndex::Reader reader)
{
    LibIndex index;
    index.placementCount = static_cast<std::size_t>(reader.getPlacementCount());

    const auto topCells = reader.getTopCells();
    index.topCells.reserve(topCells.size());
    for (const auto name : topCells) {
        index.topCells.emplace_back(name.cStr());
    }

    const auto entries = reader.getEntries();
    for (const auto entry : entries) {
        const std::string name = entry.getName().cStr();
        index.cellBboxes.emplace(name, readBox(entry.getBbox()));
        index.referenceCount.emplace(name, entry.getRefCount());

        std::vector<std::string> refs;
        const auto childRefs = entry.getChildRefs();
        refs.reserve(childRefs.size());
        for (const auto ref : childRefs) {
            refs.emplace_back(ref.cStr());
        }
        index.childRefs.emplace(name, std::move(refs));
    }

    return index;
}

} // namespace

/*!****************************************************************************************
 * \brief Serializes a Database to the Cap'n Proto root message.
 * \param root     Database builder to populate.
 * \param db       Source in-memory database.
 * \param options  Compact vs verbose geometry per view.
 *****************************************************************************************/
void writeDatabase(schema::Database::Builder root, const Database &db, SaveOptions options)
{
    root.setVersion(db.version());
    root.setGenerator(db.generator());
    root.setTechnology(db.technology());
    writeFileSummary(root.initSummary(), db.fileSummary());

    auto libBuilder = root.initLib();
    libBuilder.setName(db.lib().name());
    writeProperties(libBuilder.initProperties(db.lib().properties().size()), db.lib().properties());

    auto layers = libBuilder.initLayers(db.lib().layers().size());
    for (std::size_t i = 0; i < db.lib().layers().size(); ++i) {
        writeLayerSpec(layers[i], db.lib().layers()[i]);
    }

    auto cells = libBuilder.initCells(db.lib().cells().size());
    for (std::size_t ci = 0; ci < db.lib().cells().size(); ++ci) {
        const auto &cell = db.lib().cells()[ci];
        auto cellBuilder = cells[ci];
        cellBuilder.setName(cell.name());
        writeProperties(cellBuilder.initProperties(cell.properties().size()), cell.properties());

        auto aliases = cellBuilder.initAliases(cell.aliases().size());
        for (std::size_t ai = 0; ai < cell.aliases().size(); ++ai) {
            aliases.set(ai, cell.aliases()[ai]);
        }
        writePCellInfo(cellBuilder.initPCell(), cell.pCell());

        auto contents = cellBuilder.initContents(cell.contents().size());
        for (std::size_t i = 0; i < cell.contents().size(); ++i) {
            writeCellContent(contents[i], cell.contents()[i], db.lib().layers(), options.compactGeometry);
        }
    }

    if (db.lib().hasIndex()) {
        writeLibIndex(libBuilder.initIndex(), db.lib(), db.lib().index());
    } else {
        writeLibIndex(libBuilder.initIndex(), db.lib(), LibIndex::build(db.lib()));
    }
}

/*!****************************************************************************************
 * \brief Deserializes a Database from the Cap'n Proto root message.
 * \param root     Database reader from a loaded .core file.
 * \return         Reconstructed in-memory database with index refreshed if absent.
 *****************************************************************************************/
Database readDatabase(schema::Database::Reader root)
{
    Database db;
    db.setVersion(root.getVersion().cStr());
    db.setGenerator(root.getGenerator().cStr());
    db.setTechnology(root.getTechnology().cStr());
    if (root.hasSummary()) {
        db.setFileSummary(readFileSummary(root.getSummary()));
        db.setFileView(db.fileSummary().view);
    }

    const auto libReader = root.getLib();
    db.lib() = Lib(libReader.getName().cStr());
    db.lib().properties() = readProperties(libReader.getProperties());
    readLibLayers(db.lib(), libReader);

    const auto cellsReader = libReader.getCells();
    db.lib().cells().reserve(cellsReader.size());
    for (const auto cellReader : cellsReader) {
        Cell cell(cellReader.getName().cStr());
        cell.properties() = readProperties(cellReader.getProperties());
        const auto aliasesReader = cellReader.getAliases();
        cell.aliases().reserve(aliasesReader.size());
        for (const auto alias : aliasesReader) {
            cell.aliases().emplace_back(alias.cStr());
        }
        cell.pCell() = readPCellInfo(cellReader.getPCell());
        const auto contentsReader = cellReader.getContents();
        cell.contents().reserve(contentsReader.size());
        for (const auto contentReader : contentsReader) {
            cell.contents().push_back(readCellContent(contentReader));
        }
        db.lib().cells().push_back(std::move(cell));
    }

    db.lib().recomputeAllBBoxes();
    const auto indexReader = libReader.getIndex();
    if (indexReader.getEntries().size() > 0) {
        db.lib().setIndex(readLibIndex(indexReader));
    }

    const ViewType indexView = db.fileView();
    db.lib().refreshIndex(indexView);

    if (!root.hasSummary()) {
        ViewType detected = ViewType::Layout;
        for (const Cell &cell : db.lib().cells()) {
            for (const CellContent &content : cell.contents()) {
                detected = content.viewType();
            }
        }
        db.setFileSummary(FileSummary::fromLib(db.lib(), detected));
    }

    return db;
}

} // namespace core
