#include "block.h"
#include "compact_codec.h"

#include <capnp/message.h>

#include <cstdlib>
#include <iostream>
#include <vector>

int main()
{
    core::Block block;

    core::Shape::RectData arrayRect;
    arrayRect.layerId = 1;
    arrayRect.box = core::Box{0, 0, 100, 200};
    block.shapes().emplace_back(arrayRect);

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 3; ++row) {
            if (col == 0 && row == 0) {
                continue;
            }
            core::Shape::RectData data;
            data.layerId = 1;
            data.box = core::Box{col * 500, row * 300, col * 500 + 100, row * 300 + 200};
            block.shapes().emplace_back(data);
        }
    }

    std::vector<core::Point> diamond{{0, 0}, {10, 5}, {0, 10}, {-10, 5}};
    for (int i = 0; i < 3; ++i) {
        core::Shape::PolygonData polygon;
        polygon.layerId = 2;
        polygon.points = diamond;
        for (core::Point &pt : polygon.points) {
            pt.x += i * 1000;
            pt.y += i * 500;
        }
        block.shapes().emplace_back(polygon);
    }

    capnp::MallocMessageBuilder message;
    core::writeCompactBlock(message.initRoot<core::schema::CompactBlock>(), block);
    const core::Block rebuilt = core::readCompactBlock(message.getRoot<core::schema::CompactBlock>());

    if (rebuilt.shapes().size() != block.shapes().size()) {
        std::cerr << "shape count mismatch: " << rebuilt.shapes().size() << " vs " << block.shapes().size() << '\n';
        return 1;
    }

    std::cout << "compact repetition unit OK (" << rebuilt.shapes().size() << " shapes)\n";
    return 0;
}
