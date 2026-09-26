#pragma once

#include <cstddef>
#include <cstdint>

namespace portal_shapes {

struct BlockPos {
    int32_t x;
    int32_t y;
    int32_t z;
};

enum class PortalAxis : int32_t {
    Unknown = 0,
    X = 1,
    Z = 2,
    Count = 3,
};

struct PortalShapeLayout {
    PortalAxis axis;
    uint8_t rightDir;
    uint8_t leftDir;
    uint8_t padding06[2];
    int32_t numPortalBlocks;
    BlockPos bottomLeft;
    uint8_t bottomLeftValid;
    uint8_t padding19[3];
    int32_t height;
    int32_t width;
};

static_assert(sizeof(BlockPos) == 0x0C);
static_assert(offsetof(PortalShapeLayout, axis) == 0x00);
static_assert(offsetof(PortalShapeLayout, rightDir) == 0x04);
static_assert(offsetof(PortalShapeLayout, leftDir) == 0x05);
static_assert(offsetof(PortalShapeLayout, numPortalBlocks) == 0x08);
static_assert(offsetof(PortalShapeLayout, bottomLeft) == 0x0C);
static_assert(offsetof(PortalShapeLayout, bottomLeftValid) == 0x18);
static_assert(offsetof(PortalShapeLayout, height) == 0x1C);
static_assert(offsetof(PortalShapeLayout, width) == 0x20);
static_assert(sizeof(PortalShapeLayout) == 0x24);

} // namespace portal_shapes
