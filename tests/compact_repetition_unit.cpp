#include "block.h"
#include "compact_codec.h"

#include <capnp/message.h>

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

    room::Shape::PathData extended;
    extended.layerId = 3;
    extended.width = 200;
    extended.bgnExt = 40;
    extended.endExt = 80;
    extended.round = true;
    extended.points = {{0, 500}, {2000, 500}, {2000, 1500}};
    block.shapes().emplace_back(extended);
    block.shapes().emplace_back(extended);

    capnp::MallocMessageBuilder message;
    room::writeCompactBlock(message.initRoot<room::schema::CompactBlock>(), block);
    const room::Block rebuilt = room::readCompactBlock(message.getRoot<room::schema::CompactBlock>());

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

    std::cout << "compact repetition unit OK (" << rebuilt.shapes().size() << " shapes)\n";
    return 0;
}
