#include "mod/PortalShapesHooks.h"

#include "mod/mc/Engine.h"
#include "mod/mc/Hook.h"
#include "mod/mc/Signatures.h"

#include <android/log.h>
#include <cstdint>
#include <cstdlib>

namespace portal_shapes {
namespace {

constexpr char kLogTag[] = "PortalShapes";

constexpr std::size_t kContextSlotCount = 16;
constexpr std::size_t kInitialCellCapacity = 256;
constexpr std::size_t kInitialSetCapacity = 1024;
constexpr std::size_t kMaxScannedCells = 65536;
constexpr int32_t kMaximumEdgeProbeDistance = 8192;

bool gHorizontalEnabled = false;

struct Context {
    const PortalShapeLayout* shape = nullptr;
    void* blockSource = nullptr;
    PortalAxis axis = PortalAxis::Unknown;

    BlockPos* cells = nullptr;
    std::size_t cellCount = 0;
    std::size_t cellCapacity = 0;

    bool custom = false;
    bool horizontal = false;
    bool outsideSkin = false;
};

thread_local Context gContexts[kContextSlotCount];
thread_local unsigned gNextContext = 0;

void clearContext(Context& context) noexcept {
    std::free(context.cells);
    context = {};
}

void resetContextCells(Context& context) noexcept {
    context.cellCount = 0;
    context.outsideSkin = false;
}

Context* findContext(const PortalShapeLayout* shape) noexcept {
    for (Context& context : gContexts) {
        if (context.shape == shape) {
            return &context;
        }
    }

    return nullptr;
}

Context* beginContext(const PortalShapeLayout* shape) noexcept {
    if (Context* existing = findContext(shape)) {
        clearContext(*existing);
        existing->shape = shape;
        return existing;
    }

    Context& context = gContexts[gNextContext++ % kContextSlotCount];
    clearContext(context);
    context.shape = shape;
    return &context;
}

uint64_t hashPosition(const BlockPos& position) noexcept {
    const uint64_t x = static_cast<uint32_t>(position.x);
    const uint64_t y = static_cast<uint32_t>(position.y);
    const uint64_t z = static_cast<uint32_t>(position.z);

    uint64_t hash = 0x9E3779B97F4A7C15ULL;
    hash ^= x + 0x9E3779B9 + (hash << 6) + (hash >> 2);
    hash ^= y + 0x85EBCA6B + (hash << 6) + (hash >> 2);
    hash ^= z + 0xC2B2AE35 + (hash << 6) + (hash >> 2);
    hash ^= hash >> 30;
    hash *= 0xBF58476D1CE4E5B9ULL;
    hash ^= hash >> 27;
    hash *= 0x94D049BB133111EBULL;
    return hash ^ (hash >> 31);
}

bool samePosition(const BlockPos& lhs, const BlockPos& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

struct PositionSetSlot {
    BlockPos position{};
    uint8_t occupied = 0;
};

struct PositionSet {
    PositionSetSlot* slots = nullptr;
    std::size_t capacity = 0;
    std::size_t size = 0;

    ~PositionSet() {
        std::free(slots);
    }

    bool initialize(std::size_t initialCapacity) noexcept {
        capacity = initialCapacity;
        slots = static_cast<PositionSetSlot*>(
            std::calloc(capacity, sizeof(PositionSetSlot))
        );
        return slots != nullptr;
    }

    bool grow() noexcept {
        const std::size_t newCapacity = capacity ? capacity * 2 : kInitialSetCapacity;
        auto* newSlots = static_cast<PositionSetSlot*>(
            std::calloc(newCapacity, sizeof(PositionSetSlot))
        );
        if (!newSlots) {
            return false;
        }

        for (std::size_t index = 0; index < capacity; ++index) {
            const PositionSetSlot& oldSlot = slots[index];
            if (!oldSlot.occupied) {
                continue;
            }

            std::size_t target = hashPosition(oldSlot.position) & (newCapacity - 1);
            while (newSlots[target].occupied) {
                target = (target + 1) & (newCapacity - 1);
            }

            newSlots[target] = oldSlot;
        }

        std::free(slots);
        slots = newSlots;
        capacity = newCapacity;
        return true;
    }

    bool add(const BlockPos& position) noexcept {
        if (!capacity && !initialize(kInitialSetCapacity)) {
            return false;
        }

        if ((size + 1) * 10 >= capacity * 7 && !grow()) {
            return false;
        }

        std::size_t index = hashPosition(position) & (capacity - 1);
        while (slots[index].occupied) {
            if (samePosition(slots[index].position, position)) {
                return true;
            }
            index = (index + 1) & (capacity - 1);
        }

        slots[index].occupied = 1;
        slots[index].position = position;
        ++size;
        return true;
    }

    bool contains(const BlockPos& position) const noexcept {
        if (!capacity) {
            return false;
        }

        std::size_t index = hashPosition(position) & (capacity - 1);
        while (slots[index].occupied) {
            if (samePosition(slots[index].position, position)) {
                return true;
            }
            index = (index + 1) & (capacity - 1);
        }

        return false;
    }
};

bool appendCell(Context& context, const BlockPos& position) noexcept {
    if (context.cellCount == context.cellCapacity) {
        const std::size_t newCapacity = context.cellCapacity
            ? context.cellCapacity * 2
            : kInitialCellCapacity;

        auto* newCells = static_cast<BlockPos*>(
            std::realloc(context.cells, newCapacity * sizeof(BlockPos))
        );
        if (!newCells) {
            return false;
        }

        context.cells = newCells;
        context.cellCapacity = newCapacity;
    }

    context.cells[context.cellCount++] = position;
    return true;
}

enum class CellKind : uint8_t {
    Interior,
    Frame,
    Invalid,
};

bool isFrameCell(CellKind kind) noexcept {
    return kind == CellKind::Frame;
}

CellKind classifyCell(void* blockSource, const BlockPos& position) noexcept {
    void* block = engine::getBlock(blockSource, position);
    if (!block) {
        return CellKind::Invalid;
    }

    if (engine::isInterior(block)) {
        return CellKind::Interior;
    }

    if (engine::isObsidian(block)) {
        return CellKind::Frame;
    }

    return CellKind::Invalid;
}

bool hasVerticalEdges(
    void* blockSource,
    const BlockPos& seed,
    PortalAxis axis
) noexcept {
    constexpr int signs[] = {-1, 1};

    for (int sign : signs) {
        bool foundFrame = false;

        for (int32_t distance = 1; distance <= kMaximumEdgeProbeDistance; ++distance) {
            BlockPos position = seed;
            if (axis == PortalAxis::X) {
                position.x += sign * distance;
            } else {
                position.z += sign * distance;
            }

            const CellKind kind = classifyCell(blockSource, position);
            if (isFrameCell(kind)) {
                foundFrame = true;
                break;
            }

            if (kind == CellKind::Invalid) {
                return false;
            }
        }

        if (!foundFrame) {
            return false;
        }
    }

    return true;
}

bool hasHorizontalEdges(void* blockSource, const BlockPos& seed) noexcept {
    constexpr int xDirections[] = {-1, 1, 0, 0};
    constexpr int zDirections[] = {0, 0, -1, 1};

    for (int direction = 0; direction < 4; ++direction) {
        bool foundFrame = false;

        for (int32_t distance = 1; distance <= kMaximumEdgeProbeDistance; ++distance) {
            const BlockPos position{
                seed.x + xDirections[direction] * distance,
                seed.y,
                seed.z + zDirections[direction] * distance,
            };

            const CellKind kind = classifyCell(blockSource, position);
            if (isFrameCell(kind)) {
                foundFrame = true;
                break;
            }

            if (kind == CellKind::Invalid) {
                return false;
            }
        }

        if (!foundFrame) {
            return false;
        }
    }

    return true;
}

bool scanVertical(Context& context, const BlockPos& seed) noexcept {
    resetContextCells(context);

    if (context.axis != PortalAxis::X && context.axis != PortalAxis::Z) {
        return false;
    }

    if (classifyCell(context.blockSource, seed) != CellKind::Interior) {
        return false;
    }

    if (!hasVerticalEdges(context.blockSource, seed, context.axis)) {
        return false;
    }

    PositionSet seen;
    if (!seen.initialize(kInitialSetCapacity) ||
        !seen.add(seed) ||
        !appendCell(context, seed)) {
        return false;
    }

    std::size_t head = 0;
    while (head < context.cellCount) {
        if (context.cellCount > kMaxScannedCells) {
            resetContextCells(context);
            return false;
        }

        const BlockPos position = context.cells[head++];

        BlockPos neighbors[4] = {
            {position.x, position.y + 1, position.z},
            {position.x, position.y - 1, position.z},
            {position.x, position.y, position.z},
            {position.x, position.y, position.z},
        };

        if (context.axis == PortalAxis::X) {
            --neighbors[2].x;
            ++neighbors[3].x;
        } else {
            --neighbors[2].z;
            ++neighbors[3].z;
        }

        for (const BlockPos& neighbor : neighbors) {
            const CellKind kind = classifyCell(context.blockSource, neighbor);

            if (isFrameCell(kind)) {
                continue;
            }

            if (kind != CellKind::Interior) {
                resetContextCells(context);
                return false;
            }

            if (!seen.contains(neighbor)) {
                if (!seen.add(neighbor) || !appendCell(context, neighbor)) {
                    resetContextCells(context);
                    return false;
                }
            }
        }
    }

    return context.cellCount > 0;
}

bool scanHorizontal(Context& context, const BlockPos& seed) noexcept {
    resetContextCells(context);

    if (classifyCell(context.blockSource, seed) != CellKind::Interior) {
        return false;
    }

    if (!hasHorizontalEdges(context.blockSource, seed)) {
        return false;
    }

    PositionSet seen;
    if (!seen.initialize(kInitialSetCapacity) ||
        !seen.add(seed) ||
        !appendCell(context, seed)) {
        return false;
    }

    std::size_t head = 0;
    while (head < context.cellCount) {
        if (context.cellCount > kMaxScannedCells) {
            resetContextCells(context);
            return false;
        }

        const BlockPos position = context.cells[head++];
        const BlockPos neighbors[4] = {
            {position.x - 1, position.y, position.z},
            {position.x + 1, position.y, position.z},
            {position.x, position.y, position.z - 1},
            {position.x, position.y, position.z + 1},
        };

        for (const BlockPos& neighbor : neighbors) {
            const CellKind kind = classifyCell(context.blockSource, neighbor);

            if (isFrameCell(kind)) {
                continue;
            }

            if (kind != CellKind::Interior) {
                resetContextCells(context);
                return false;
            }

            if (!seen.contains(neighbor)) {
                if (!seen.add(neighbor) || !appendCell(context, neighbor)) {
                    resetContextCells(context);
                    return false;
                }
            }
        }
    }

    return context.cellCount > 0;
}

bool scanHorizontalNear(
    Context& context,
    const BlockPos& ignition,
    BlockPos& acceptedSeed
) noexcept {
    constexpr int yOffsets[] = {0, -1, 1};
    constexpr int xOffsets[] = {0, -1, 1, 0, 0};
    constexpr int zOffsets[] = {0, 0, 0, -1, 1};

    for (int yIndex = 0; yIndex < 3; ++yIndex) {
        for (int offsetIndex = 0; offsetIndex < 5; ++offsetIndex) {
            const BlockPos seed{
                ignition.x + xOffsets[offsetIndex],
                ignition.y + yOffsets[yIndex],
                ignition.z + zOffsets[offsetIndex],
            };

            if (scanHorizontal(context, seed)) {
                acceptedSeed = seed;
                return true;
            }
        }
    }

    resetContextCells(context);
    return false;
}

enum class SkinPlane : uint8_t {
    VerticalX,
    VerticalZ,
    HorizontalXZ,
};

struct RegionCells {
    BlockPos* cells = nullptr;
    std::size_t count = 0;
    std::size_t capacity = 0;
};

void clearRegion(RegionCells& region) noexcept {
    std::free(region.cells);
    region = {};
}

bool appendRegionCell(RegionCells& region, const BlockPos& position) noexcept {
    if (region.count == region.capacity) {
        const std::size_t newCapacity = region.capacity
            ? region.capacity * 2
            : kInitialCellCapacity;

        auto* newCells = static_cast<BlockPos*>(
            std::realloc(region.cells, newCapacity * sizeof(BlockPos))
        );
        if (!newCells) {
            return false;
        }

        region.cells = newCells;
        region.capacity = newCapacity;
    }

    region.cells[region.count++] = position;
    return true;
}

bool isCoreBlock(void* blockSource, const BlockPos& position) noexcept {
    void* block = engine::getBlock(blockSource, position);
    return block && engine::isObsidian(block);
}

bool isOutsideTarget(void* blockSource, const BlockPos& position) noexcept {
    return classifyCell(blockSource, position) == CellKind::Interior;
}

void getPlaneNeighbors4(
    const BlockPos& position,
    SkinPlane plane,
    BlockPos output[4]
) noexcept {
    if (plane == SkinPlane::VerticalX) {
        output[0] = {position.x - 1, position.y, position.z};
        output[1] = {position.x + 1, position.y, position.z};
        output[2] = {position.x, position.y - 1, position.z};
        output[3] = {position.x, position.y + 1, position.z};
        return;
    }

    if (plane == SkinPlane::VerticalZ) {
        output[0] = {position.x, position.y, position.z - 1};
        output[1] = {position.x, position.y, position.z + 1};
        output[2] = {position.x, position.y - 1, position.z};
        output[3] = {position.x, position.y + 1, position.z};
        return;
    }

    output[0] = {position.x - 1, position.y, position.z};
    output[1] = {position.x + 1, position.y, position.z};
    output[2] = {position.x, position.y, position.z - 1};
    output[3] = {position.x, position.y, position.z + 1};
}

void getPlaneNeighbors8(
    const BlockPos& position,
    SkinPlane plane,
    BlockPos output[8]
) noexcept {
    int index = 0;

    if (plane == SkinPlane::VerticalX) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                output[index++] = {position.x + dx, position.y + dy, position.z};
            }
        }
        return;
    }

    if (plane == SkinPlane::VerticalZ) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (dz == 0 && dy == 0) {
                    continue;
                }
                output[index++] = {position.x, position.y + dy, position.z + dz};
            }
        }
        return;
    }

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) {
                continue;
            }
            output[index++] = {position.x + dx, position.y, position.z + dz};
        }
    }
}

bool gatherCore(
    void* blockSource,
    const BlockPos& seed,
    SkinPlane plane,
    RegionCells& core,
    PositionSet& coreSet
) noexcept {
    if (!isCoreBlock(blockSource, seed) ||
        !coreSet.initialize(kInitialSetCapacity) ||
        !coreSet.add(seed) ||
        !appendRegionCell(core, seed)) {
        return false;
    }

    std::size_t head = 0;
    while (head < core.count) {
        if (core.count > kMaxScannedCells) {
            return false;
        }

        BlockPos neighbors[4];
        getPlaneNeighbors4(core.cells[head++], plane, neighbors);

        for (const BlockPos& neighbor : neighbors) {
            if (!isCoreBlock(blockSource, neighbor) || coreSet.contains(neighbor)) {
                continue;
            }

            if (!coreSet.add(neighbor) || !appendRegionCell(core, neighbor)) {
                return false;
            }
        }
    }

    return core.count >= 4;
}

bool isFilledCoreRectangle(
    const RegionCells& core,
    const PositionSet& coreSet,
    SkinPlane plane
) noexcept {
    if (core.count < 4) {
        return false;
    }

    int minU = 0;
    int maxU = 0;
    int minV = 0;
    int maxV = 0;
    int fixed = 0;

    for (std::size_t index = 0; index < core.count; ++index) {
        const BlockPos& position = core.cells[index];
        const int u = plane == SkinPlane::VerticalZ ? position.z : position.x;
        const int v = plane == SkinPlane::HorizontalXZ ? position.z : position.y;
        const int fixedCoordinate = plane == SkinPlane::VerticalX
            ? position.z
            : (plane == SkinPlane::VerticalZ ? position.x : position.y);

        if (index == 0) {
            minU = maxU = u;
            minV = maxV = v;
            fixed = fixedCoordinate;
            continue;
        }

        if (fixedCoordinate != fixed) {
            return false;
        }

        if (u < minU) minU = u;
        if (u > maxU) maxU = u;
        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
    }

    const std::size_t width = static_cast<std::size_t>(maxU - minU + 1);
    const std::size_t height = static_cast<std::size_t>(maxV - minV + 1);

    if (
        width < 2 ||
        height < 2 ||
        width > kMaxScannedCells ||
        height > kMaxScannedCells ||
        width * height != core.count
    ) {
        return false;
    }

    for (int v = minV; v <= maxV; ++v) {
        for (int u = minU; u <= maxU; ++u) {
            BlockPos position{};

            if (plane == SkinPlane::VerticalX) {
                position = {u, v, fixed};
            } else if (plane == SkinPlane::VerticalZ) {
                position = {fixed, v, u};
            } else {
                position = {u, fixed, v};
            }

            if (!coreSet.contains(position)) {
                return false;
            }
        }
    }

    return true;
}

bool buildSkin(
    Context& context,
    const BlockPos& ignition,
    const BlockPos& coreSeed,
    SkinPlane plane,
    BlockPos& acceptedSeed
) noexcept {
    RegionCells core;
    PositionSet coreSet;
    PositionSet shellSet;

    if (!gatherCore(context.blockSource, coreSeed, plane, core, coreSet)) {
        clearRegion(core);
        return false;
    }

    if (!isFilledCoreRectangle(core, coreSet, plane)) {
        clearRegion(core);
        return false;
    }

    if (!shellSet.initialize(kInitialSetCapacity)) {
        clearRegion(core);
        return false;
    }

    resetContextCells(context);

    for (std::size_t index = 0; index < core.count; ++index) {
        BlockPos neighbors[8];
        getPlaneNeighbors8(core.cells[index], plane, neighbors);

        for (const BlockPos& neighbor : neighbors) {
            if (coreSet.contains(neighbor) || shellSet.contains(neighbor)) {
                continue;
            }

            if (!isOutsideTarget(context.blockSource, neighbor)) {
                continue;
            }

            if (!shellSet.add(neighbor) || !appendCell(context, neighbor)) {
                clearRegion(core);
                resetContextCells(context);
                return false;
            }

            if (context.cellCount > kMaxScannedCells) {
                clearRegion(core);
                resetContextCells(context);
                return false;
            }
        }
    }

    bool relatedToIgnition = false;

    for (std::size_t index = 0; index < core.count && !relatedToIgnition; ++index) {
        const BlockPos& position = core.cells[index];

        const int xDistance = std::abs(ignition.x - position.x);
        const int yDistance = std::abs(ignition.y - position.y);
        const int zDistance = std::abs(ignition.z - position.z);

        relatedToIgnition = xDistance <= 1 && yDistance <= 1 && zDistance <= 1;
    }

    clearRegion(core);

    if (!relatedToIgnition || context.cellCount < 3) {
        resetContextCells(context);
        return false;
    }

    context.outsideSkin = true;
    context.axis = plane == SkinPlane::VerticalX
        ? PortalAxis::X
        : (plane == SkinPlane::VerticalZ ? PortalAxis::Z : PortalAxis::Unknown);

    acceptedSeed = context.cells[0];
    return true;
}

bool scanOutsideAnyNear(
    Context& context,
    const BlockPos& ignition,
    BlockPos& seed
) noexcept {
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                const BlockPos coreSeed{
                    ignition.x + dx,
                    ignition.y + dy,
                    ignition.z + dz,
                };

                if (!isCoreBlock(context.blockSource, coreSeed)) {
                    continue;
                }

                if (buildSkin(context, ignition, coreSeed, SkinPlane::VerticalX, seed)) {
                    return true;
                }

                if (buildSkin(context, ignition, coreSeed, SkinPlane::VerticalZ, seed)) {
                    return true;
                }

                if (
                    gHorizontalEnabled &&
                    buildSkin(context, ignition, coreSeed, SkinPlane::HorizontalXZ, seed)
                ) {
                    return true;
                }
            }
        }
    }

    resetContextCells(context);
    return false;
}

bool isCustomPortalComplete(Context& context) noexcept {
    for (std::size_t index = 0; index < context.cellCount; ++index) {
        void* block = engine::getBlock(context.blockSource, context.cells[index]);
        if (!block || !engine::isPortal(block)) {
            return false;
        }
    }

    return context.cellCount > 0;
}

void acceptCustomPortal(
    Context& context,
    PortalShapeLayout* shape,
    const BlockPos& seed,
    bool horizontal
) noexcept {
    context.custom = true;
    context.horizontal = horizontal;

    shape->axis = context.axis;
    shape->bottomLeft = seed;
    shape->bottomLeftValid = 1;
    shape->numPortalBlocks = 0;
    shape->width = 1;
    shape->height = 1;

    __android_log_print(
        ANDROID_LOG_DEBUG,
        kLogTag,
        "Accepted %s custom portal: cells=%zu axis=%d outsideSkin=%d",
        horizontal ? "horizontal" : "vertical",
        context.cellCount,
        static_cast<int>(context.axis),
        context.outsideSkin ? 1 : 0
    );
}

struct AABBLayout {
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
};

static_assert(sizeof(AABBLayout) == 24);

using PortalShapeValidFunction = bool (*)(const PortalShapeLayout*);
PortalShapeValidFunction gVanillaValid = nullptr;

LL_STATIC_HOOK(
    PortalShapeCtorHook,
    memory::HookPriority::Normal,
    signatures::kPortalShapeCtorBody.signature.data(),
    "libminecraftpe.so",
    void,
    PortalShapeLayout* self,
    const BlockPos* position,
    void* blockSource
) {
    origin(self, position, blockSource);

    Context* context = beginContext(self);
    context->blockSource = blockSource;
    context->axis = self->axis;

    if (!position) {
        return;
    }

    if (gVanillaValid && gVanillaValid(self)) {
        return;
    }

    if (context->axis == PortalAxis::X) {
        BlockPos outerSeed{};
        if (scanOutsideAnyNear(*context, *position, outerSeed)) {
            acceptCustomPortal(
                *context,
                self,
                outerSeed,
                context->axis == PortalAxis::Unknown
            );
            return;
        }
    }

    if (
        (context->axis == PortalAxis::X || context->axis == PortalAxis::Z) &&
        scanVertical(*context, *position)
    ) {
        acceptCustomPortal(*context, self, *position, false);
        return;
    }

    if (gHorizontalEnabled && context->axis == PortalAxis::Unknown) {
        if (scanHorizontal(*context, *position)) {
            acceptCustomPortal(*context, self, *position, true);
            return;
        }
    }

    if (gHorizontalEnabled && context->axis == PortalAxis::X) {
        BlockPos seed{};
        if (scanHorizontalNear(*context, *position, seed)) {
            acceptCustomPortal(*context, self, seed, true);
        }
    }
}

LL_STATIC_HOOK(
    PortalShapeValidHook,
    memory::HookPriority::Normal,
    signatures::kPortalShapeIsValid.signature.data(),
    "libminecraftpe.so",
    bool,
    const PortalShapeLayout* self
) {
    if (auto* context = findContext(self); context && context->custom) {
        return true;
    }

    return origin(self);
}

LL_STATIC_HOOK(
    PortalShapeCompleteHook,
    memory::HookPriority::Normal,
    signatures::kPortalShapeIsComplete.signature.data(),
    "libminecraftpe.so",
    bool,
    const PortalShapeLayout* self
) {
    if (auto* context = findContext(self); context && context->custom) {
        return isCustomPortalComplete(*context);
    }

    return origin(self);
}

LL_STATIC_HOOK(
    PortalShapeCreateHook,
    memory::HookPriority::Normal,
    signatures::kPortalShapeCreateBlocks.signature.data(),
    "libminecraftpe.so",
    void,
    PortalShapeLayout* self,
    void* transaction
) {
    Context* context = findContext(self);
    if (!context || !context->custom) {
        origin(self, transaction);
        return;
    }

    PortalShapeLayout oneBlockShape = *self;
    oneBlockShape.bottomLeftValid = 1;
    oneBlockShape.width = 1;
    oneBlockShape.height = 1;
    oneBlockShape.numPortalBlocks = 0;

    if (context->horizontal) {
        oneBlockShape.axis = PortalAxis::Unknown;
    }

    for (std::size_t index = 0; index < context->cellCount; ++index) {
        oneBlockShape.bottomLeft = context->cells[index];
        origin(&oneBlockShape, transaction);
    }

    self->numPortalBlocks = static_cast<int32_t>(context->cellCount);

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "Lit %s custom Nether portal: cells=%zu outsideSkin=%d",
        context->horizontal ? "horizontal" : "vertical",
        context->cellCount,
        context->outsideSkin ? 1 : 0
    );
}

LL_STATIC_HOOK(
    PortalVisualHook,
    memory::HookPriority::Normal,
    signatures::kPortalBlockVisualShape.signature.data(),
    "libminecraftpe.so",
    void*,
    void* self,
    void* block,
    void* region,
    const BlockPos* position,
    void* buffer
) {
    PortalAxis axis{};

    if (
        buffer &&
        engine::getPortalAxis(block, axis) &&
        axis == PortalAxis::Unknown
    ) {
        auto* box = reinterpret_cast<AABBLayout*>(buffer);
        box->minX = 0.0f;
        box->minY = 0.375f;
        box->minZ = 0.0f;
        box->maxX = 1.0f;
        box->maxY = 0.625f;
        box->maxZ = 1.0f;
        return buffer;
    }

    return origin(self, block, region, position, buffer);
}

bool gCtorHooked = false;
bool gValidHooked = false;
bool gCompleteHooked = false;
bool gCreateHooked = false;
bool gVisualHooked = false;

} // namespace

bool installHooks() noexcept {
    if (!engine::initialize()) {
        return false;
    }

    gCtorHooked = PortalShapeCtorHook::hook() == 0;
    if (!gCtorHooked) {
        goto fail;
    }

    gValidHooked = PortalShapeValidHook::hook() == 0;
    if (!gValidHooked) {
        goto fail;
    }

    gVanillaValid = reinterpret_cast<PortalShapeValidFunction>(
        PortalShapeValidHook::originFunc
    );

    gCompleteHooked = PortalShapeCompleteHook::hook() == 0;
    if (!gCompleteHooked) {
        goto fail;
    }

    gCreateHooked = PortalShapeCreateHook::hook() == 0;
    if (!gCreateHooked) {
        goto fail;
    }

    gHorizontalEnabled = false;

    if (
        signatures::hasSignature(signatures::kPortalBlockVisualShape) &&
        signatures::hasSignature(signatures::kPortalAxisStateId) &&
        PortalVisualHook::hook() == 0
    ) {
        gVisualHooked = true;
        gHorizontalEnabled = true;
    } else {
        gVisualHooked = false;
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "Horizontal visual hook is unavailable; vertical portal support remains active"
        );
    }

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "Hooks active: Nether=1 horizontal=%d",
        gHorizontalEnabled ? 1 : 0
    );

    return true;

fail:
    removeHooks();
    return false;
}

void removeHooks() noexcept {
    gHorizontalEnabled = false;

    if (gVisualHooked) {
        PortalVisualHook::unhook();
        gVisualHooked = false;
    }

    if (gCreateHooked) {
        PortalShapeCreateHook::unhook();
        gCreateHooked = false;
    }

    if (gCompleteHooked) {
        PortalShapeCompleteHook::unhook();
        gCompleteHooked = false;
    }

    if (gValidHooked) {
        PortalShapeValidHook::unhook();
        gVanillaValid = nullptr;
        gValidHooked = false;
    }

    if (gCtorHooked) {
        PortalShapeCtorHook::unhook();
        gCtorHooked = false;
    }
}

void releaseThreadState() noexcept {
    for (Context& context : gContexts) {
        clearContext(context);
    }
}

} // namespace portal_shapes
