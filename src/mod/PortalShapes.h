#pragma once

#include <pl/Mod.hpp>

namespace portal_shapes {

class PortalShapes {
public:
    static PortalShapes& getInstance();

    PortalShapes();

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();
    bool unload();

private:
    ll::mod::NativeMod& mSelf;
};

} // namespace portal_shapes
