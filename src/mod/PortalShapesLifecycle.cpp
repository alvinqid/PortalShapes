#include "mod/PortalShapes.h"
#include "mod/mc/Engine.h"
#include "mod/PortalShapesHooks.h"

namespace portal_shapes {

PortalShapes& PortalShapes::getInstance() {
    static PortalShapes instance;
    return instance;
}

PortalShapes::PortalShapes()
    : mSelf(*ll::mod::NativeMod::current()) {}

bool PortalShapes::load() {
    getSelf().getLogger().info("Loading PortalShapes...");
    return true;
}

bool PortalShapes::enable() {
    auto& self = getSelf();
    self.getLogger().info("Enabling PortalShapes...");

    if (!portal_shapes::installHooks()) {
        self.getLogger().error("Failed to install PortalShapes hooks");
        return false;
    }

    self.getLogger().info("PortalShapes enabled");
    return true;
}

bool PortalShapes::disable() {
    getSelf().getLogger().info("Disabling PortalShapes...");
    portal_shapes::removeHooks();
    portal_shapes::releaseThreadState();
    return true;
}

bool PortalShapes::unload() {
    getSelf().getLogger().info("Unloading PortalShapes...");
    portal_shapes::removeHooks();
    portal_shapes::releaseThreadState();
    return true;
}

} // namespace portal_shapes
