#pragma once

#include <string_view>

namespace portal_shapes::signatures {

inline constexpr std::string_view kUnknownSignature = "34 30 34 20 4E 6F 74 20 46 6F 75 6E 64";

struct Target {
    std::string_view signature;
};

inline constexpr Target kPortalShapeCtorEntry{
    "E8 03 01 AA E1 03 02 AA 03"
};

inline constexpr Target kPortalShapeCtorBody{
    "FF 83 02 D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 01 91 5A D0 3B D5 F4 03 02 AA F3"
};

inline constexpr Target kPortalShapeIsValid{
    "? ? ? 39 ? ? ? 34 ? ? ? B9 08 09"
};

inline constexpr Target kPortalShapeIsComplete{
    "? ? ? 29 28 7D 08 1B 09 08"
};

inline constexpr Target kPortalShapeCreateBlocks{
    "FF 03 02 D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 83 00 91 48 D0 3B D5 F3 03 01 AA F4 03 00 AA E8 03"
};

inline constexpr Target kPortalBlockVisualShape{
    "FF C3 01 D1 ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? A9 FD C3 00 91 57 D0 3B D5 E9 03"
};

inline constexpr Target kPortalBlockTrySpawnPortal{
    "FF 43 05 D1 ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 04 91 57 D0 3B D5 F3 03 00 AA F5"
};

inline constexpr Target kBlockIsAirLike{
    "? ? ? F9 66 1D FD"
};

inline constexpr Target kResolveBlockGlobal{
    "? ? ? F9 C0 03 5F D6 ? ? ? B9 C0 03 5F D6 00 20 00 91 C0 03 5F D6 08"
};

inline constexpr Target kPortalAxisStateId{
    kUnknownSignature
};

inline constexpr Target kInteriorGlobalA{
    kUnknownSignature
};

inline constexpr Target kInteriorGlobalB{
    kUnknownSignature
};

inline constexpr Target kObsidianGlobal{
    kUnknownSignature
};

constexpr bool hasSignature(const Target& target) noexcept {
    return !target.signature.empty() && target.signature != kUnknownSignature;
}

} // namespace portal_shapes::signatures
