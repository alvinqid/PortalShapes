#include "mod/mc/Engine.h"
#include "mod/mc/Signatures.h"

#include <android/log.h>
#include <cstdint>
#include <pl/memory/Signature.hpp>

namespace portal_shapes::engine {
namespace {

constexpr char kLogTag[] = "PortalShapes";
constexpr char kMinecraftModule[] = "libminecraftpe.so";

using IsAirFunction = bool (*)(void*);
using ResolveGlobalFunction = void* (*)(void*);

bool gInitialized = false;
IsAirFunction gIsAir = nullptr;
ResolveGlobalFunction gResolveGlobal = nullptr;

void* gInteriorType = nullptr;
void* gPortalType = nullptr;
void* gObsidianType = nullptr;

const uint64_t* gPortalAxisStateId = nullptr;

void* resolveTarget(const signatures::Target& target) noexcept {
    if (!signatures::hasSignature(target)) {
        return nullptr;
    }

    return reinterpret_cast<void*>(
        pl::memory::resolveSignature(target.signature.data(), kMinecraftModule)
    );
}

void logMissingSignature(const char* name) noexcept {
    __android_log_print(
        ANDROID_LOG_ERROR,
        kLogTag,
        "Missing native signature: %s = %s",
        name,
        signatures::kUnknownSignature.data()
    );
}

bool resolveBlockTypeGlobals() noexcept {
    const auto& interiorTarget = signatures::kInteriorGlobalA;
    const auto& portalTarget = signatures::kInteriorGlobalB;
    const auto& obsidianTarget = signatures::kObsidianGlobal;

    if (!signatures::hasSignature(interiorTarget)) {
        logMissingSignature("InteriorGlobalA");
    }
    if (!signatures::hasSignature(portalTarget)) {
        logMissingSignature("InteriorGlobalB");
    }
    if (!signatures::hasSignature(obsidianTarget)) {
        logMissingSignature("ObsidianGlobal");
    }

    if (!signatures::hasSignature(interiorTarget) ||
        !signatures::hasSignature(portalTarget) ||
        !signatures::hasSignature(obsidianTarget)) {
        return false;
    }

    void* interiorAddress = resolveTarget(interiorTarget);
    void* portalAddress = resolveTarget(portalTarget);
    void* obsidianAddress = resolveTarget(obsidianTarget);

    if (!interiorAddress || !portalAddress || !obsidianAddress) {
        __android_log_print(
            ANDROID_LOG_ERROR,
            kLogTag,
            "One or more block-type signatures could not be resolved"
        );
        return false;
    }

    if (!gResolveGlobal) {
        return false;
    }

    gInteriorType = gResolveGlobal(interiorAddress);
    gPortalType = gResolveGlobal(portalAddress);
    gObsidianType = gResolveGlobal(obsidianAddress);

    return gInteriorType && gPortalType && gObsidianType;
}

void resolveOptionalPortalAxisState() noexcept {
    if (!signatures::hasSignature(signatures::kPortalAxisStateId)) {
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "Portal axis state signature is unknown; horizontal visual override will be disabled"
        );
        return;
    }

    gPortalAxisStateId = reinterpret_cast<const uint64_t*>(
        resolveTarget(signatures::kPortalAxisStateId)
    );

    if (!gPortalAxisStateId) {
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "Portal axis state signature could not be resolved"
        );
    }
}

bool readPortalAxisState(
    void* block,
    uint32_t& stateValue
) noexcept {
    if (!block || !gPortalAxisStateId) {
        return false;
    }

    auto* legacy = *reinterpret_cast<uint8_t**>(
        reinterpret_cast<uint8_t*>(block) + 0x68
    );
    if (!legacy) {
        return false;
    }

    const uint64_t stateId = *gPortalAxisStateId;
    if (!stateId) {
        return false;
    }

    auto* sentinel = legacy + 0x1B0;
    void* node = *reinterpret_cast<void**>(sentinel);
    void* candidate = sentinel;

    while (node) {
        const uint64_t key = *reinterpret_cast<const uint64_t*>(
            reinterpret_cast<uint8_t*>(node) + 0x20
        );

        if (key < stateId) {
            node = *reinterpret_cast<void**>(
                reinterpret_cast<uint8_t*>(node) + 0x08
            );
        } else {
            candidate = node;
            node = *reinterpret_cast<void**>(node);
        }
    }

    if (
        candidate == sentinel ||
        *reinterpret_cast<const uint64_t*>(
            reinterpret_cast<uint8_t*>(candidate) + 0x20
        ) != stateId
    ) {
        return false;
    }

    const auto* state = reinterpret_cast<const uint8_t*>(candidate) + 0x28;
    const uint32_t variationCount = *reinterpret_cast<const uint32_t*>(
        state + 0x00
    );
    const uint32_t bitCount = *reinterpret_cast<const uint32_t*>(
        state + 0x04
    );
    const uint32_t endBit = *reinterpret_cast<const uint32_t*>(
        state + 0x08
    );

    if (
        variationCount == 0 ||
        bitCount == 0 ||
        bitCount > 16 ||
        endBit >= 16 ||
        endBit + 1 < bitCount
    ) {
        return false;
    }

    const uint32_t shift = endBit - bitCount + 1;
    const uint32_t mask = bitCount == 16
        ? 0xFFFFu
        : ((1u << bitCount) - 1u);

    const uint16_t data = *reinterpret_cast<const uint16_t*>(
        reinterpret_cast<uint8_t*>(block) + 0x128
    );

    stateValue = (data >> shift) & mask;
    return stateValue < variationCount;
}

} // namespace

bool initialize() noexcept {
    if (gInitialized) {
        return true;
    }

    void* isAirAddress = resolveTarget(signatures::kBlockIsAirLike);
    void* resolveGlobalAddress = resolveTarget(signatures::kResolveBlockGlobal);

    if (!isAirAddress) {
        logMissingSignature("BlockIsAirLike");
    }
    if (!resolveGlobalAddress) {
        logMissingSignature("ResolveBlockGlobal");
    }

    if (!isAirAddress || !resolveGlobalAddress) {
        return false;
    }

    gIsAir = reinterpret_cast<IsAirFunction>(isAirAddress);
    gResolveGlobal = reinterpret_cast<ResolveGlobalFunction>(resolveGlobalAddress);

    if (!resolveBlockTypeGlobals()) {
        __android_log_print(
            ANDROID_LOG_ERROR,
            kLogTag,
            "Required vanilla block type signatures are not available"
        );
        return false;
    }

    resolveOptionalPortalAxisState();

    gInitialized = true;

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "Engine helpers and vanilla block types resolved from signatures"
    );

    return true;
}

void* getBlock(void* blockSource, const BlockPos& position) noexcept {
    if (!blockSource) {
        return nullptr;
    }

    auto** vtable = *reinterpret_cast<void***>(blockSource);
    if (!vtable) {
        return nullptr;
    }

    using GetBlockFunction = void* (*)(void*, const BlockPos*);
    const auto getBlockFunction = reinterpret_cast<GetBlockFunction>(vtable[2]);

    return getBlockFunction
        ? getBlockFunction(blockSource, &position)
        : nullptr;
}

void* blockTypeIdentity(void* block) noexcept {
    if (!block || !gResolveGlobal) {
        return nullptr;
    }

    auto* legacy = *reinterpret_cast<void**>(
        reinterpret_cast<uint8_t*>(block) + 0x68
    );
    if (!legacy) {
        return nullptr;
    }

    return gResolveGlobal(
        reinterpret_cast<uint8_t*>(legacy) + 0xC8
    );
}

bool isAir(void* block) noexcept {
    return block && gIsAir && gIsAir(block);
}

bool isPortal(void* block) noexcept {
    return block && blockTypeIdentity(block) == gPortalType;
}

bool isInterior(void* block) noexcept {
    if (!block) {
        return false;
    }

    if (isAir(block)) {
        return true;
    }

    const void* type = blockTypeIdentity(block);
    return type == gInteriorType || type == gPortalType;
}

bool isObsidian(void* block) noexcept {
    return block && blockTypeIdentity(block) == gObsidianType;
}

bool getPortalAxis(void* block, PortalAxis& axis) noexcept {
    uint32_t stateValue = 0;
    if (!readPortalAxisState(block, stateValue)) {
        return false;
    }

    if (stateValue > static_cast<uint32_t>(PortalAxis::Z)) {
        return false;
    }

    axis = static_cast<PortalAxis>(stateValue);
    return true;
}

} // namespace portal_shapes::engine
