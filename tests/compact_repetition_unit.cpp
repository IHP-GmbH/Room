#include "block.h"
#include "compact_codec.h"
#include "instance.h"
#include "room_file_io.h"

#include <capnp/message.h>
#include <capnp/serialize.h>

#include <cstdlib>
#include <iostream>
#include <vector>

int main()
{
    room::Block block;

    room::Shape::RectData arrayRect;
    arrayRect.layerId = 1;
    arrayRect.box = room::Box{0, 0, 100, 200};
    block.shapes().emplace_back(arrayRect);

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 3; ++row) {
            if (col == 0 && row == 0) {
                continue;
            }
            room::Shape::RectData data;
            data.layerId = 1;
            data.box = room::Box{col * 500, row * 300, col * 500 + 100, row * 300 + 200};
            block.shapes().emplace_back(data);
        }
    }

    std::vector<room::Point> diamond{{0, 0}, {10, 5}, {0, 10}, {-10, 5}};
    for (int i = 0; i < 3; ++i) {
        room::Shape::PolygonData polygon;
        polygon.layerId = 2;
        polygon.points = diamond;
        for (room::Point &pt : polygon.points) {
            pt.x += i * 1000;
            pt.y += i * 500;
        }
        block.shapes().emplace_back(polygon);
    }

    room::Shape::PathData flush;
    flush.layerId = 3;
    flush.width = 100;
    flush.points = {{0, 0}, {1000, 0}};
    block.shapes().emplace_back(flush);
    room::Shape::PathData flushMoved = flush;
    for (room::Point &pt : flushMoved.points) {
        pt.x += 5000;
    }
    block.shapes().emplace_back(flushMoved);

    room::Shape::PathData extended;
    extended.layerId = 3;
    extended.width = 200;
    extended.bgnExt = 40;
    extended.endExt = 80;
    extended.round = true;
    extended.points = {{0, 500}, {2000, 500}, {2000, 1500}};
    block.shapes().emplace_back(extended);
    block.shapes().emplace_back(extended);

    for (int i = 0; i < 5; ++i) {
        room::Shape::RectData line;
        line.layerId = 4;
        line.box = room::Box{i * 1000, 40, i * 1000 + 10, 60};
        block.shapes().emplace_back(line);
    }
    room::Instance placed("child", room::Transform{10, 20});
    placed.setOffsets({{0, 0}, {100, 0}, {5000, -3}});
    block.instances().push_back(std::move(placed));

    capnp::MallocMessageBuilder message;
    room::writeCompactBlock(message.initRoot<room::schema::CompactBlock>(), block);
    const auto written = message.getRoot<room::schema::CompactBlock>().asReader();
    int diamondRepeats = 0;
    int diamondOrigins = 0;
    int movedPathRepeats = 0;
    int movedPathOrigins = 0;
    for (const auto layer : written.getLayerShapes()) {
        if (layer.getLayerId() == 2) {
            for (const auto repeat : layer.getPolygonRepeats()) {
                ++diamondRepeats;
                diamondOrigins += static_cast<int>(repeat.getOriginX().size());
            }
        }
        if (layer.getLayerId() == 3) {
            for (const auto repeat : layer.getPathRepeats()) {
                if (repeat.getWidth() == 100 && repeat.getOriginX().size() == 2) {
                    ++movedPathRepeats;
                    movedPathOrigins += static_cast<int>(repeat.getOriginX().size());
                }
            }
        }
    }
    if (diamondRepeats != 1 || diamondOrigins != 3) {
        std::cerr << "translated polygons did not share one prototype: records " << diamondRepeats
                  << " origins " << diamondOrigins << '\n';
        return 5;
    }
    if (movedPathRepeats != 1 || movedPathOrigins != 2) {
        std::cerr << "translated paths did not share one prototype: records " << movedPathRepeats
                  << " origins " << movedPathOrigins << '\n';
        return 6;
    }
    const room::Block rebuilt = room::readCompactBlock(written);

    if (rebuilt.shapes().size() != block.shapes().size()) {
        std::cerr << "shape count mismatch: " << rebuilt.shapes().size() << " vs " << block.shapes().size() << '\n';
        return 1;
    }
    int roundPaths = 0;
    for (const room::Shape &shape : rebuilt.shapes()) {
        if (const room::Shape::PathData *path = shape.path()) {
            if (path->round) {
                ++roundPaths;
                if (path->width != 200 || path->bgnExt != 40 || path->endExt != 80 || path->points.size() != 3) {
                    std::cerr << "round path ends were not restored\n";
                    return 2;
                }
            } else if (path->bgnExt != 0 || path->endExt != 0) {
                std::cerr << "flush path gained an extension\n";
                return 3;
            }
        }
    }
    if (roundPaths != 2) {
        std::cerr << "expected 2 round paths, got " << roundPaths << '\n';
        return 4;
    }

    int packedGroups = 0;
    for (const auto layer : written.getLayerShapes()) {
        if (layer.getLayerId() != 4) {
            continue;
        }
        for (const auto group : layer.getRectGroups()) {
            if (group.getLlxPacked().size() > 0 && group.getLlyPacked().size() > 0 && group.getLlx().size() == 0) {
                ++packedGroups;
            }
        }
    }
    if (packedGroups != 1) {
        std::cerr << "rect group corners were not delta-packed\n";
        return 7;
    }
    if (written.getInstances().size() != 1 || written.getInstances()[0].getOffsetXPacked().size() == 0) {
        std::cerr << "instance offsets were not delta-packed\n";
        return 8;
    }
    if (rebuilt.instances().size() != 1 || rebuilt.instances()[0].offsets().size() != 3 ||
        rebuilt.instances()[0].offsets()[2].x != 5000 || rebuilt.instances()[0].offsets()[2].y != -3) {
        std::cerr << "instance offsets did not round-trip\n";
        return 9;
    }

    const std::vector<std::uint8_t> framed = room::compressRoomMessage(message, 1);
    const std::vector<std::uint8_t> raw = room::decodeRoomFileBytes(framed.data(), framed.size());
    capnp::FlatArrayMessageReader decoded(
        kj::arrayPtr(reinterpret_cast<const capnp::word *>(raw.data()), raw.size() / sizeof(capnp::word)));
    const room::Block fromFrame = room::readCompactBlock(decoded.getRoot<room::schema::CompactBlock>());
    if (fromFrame.shapes().size() != block.shapes().size() || fromFrame.instances().size() != 1 ||
        fromFrame.instances()[0].offsets().size() != 3) {
        std::cerr << "streaming ROOMZ frame did not round-trip\n";
        return 10;
    }

    std::cout << "compact repetition unit OK (" << rebuilt.shapes().size() << " shapes)\n";
    return 0;
}
