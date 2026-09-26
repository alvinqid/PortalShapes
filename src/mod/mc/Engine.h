#pragma once

#include "mod/mc/Types.h"

namespace portal_shapes::engine {

bool initialize() noexcept;

void* getBlock(void* blockSource, const BlockPos& position) noexcept;
void* blockTypeIdentity(void* block) noexcept;

bool isAir(void* block) noexcept;
bool isInterior(void* block) noexcept;
bool isPortal(void* block) noexcept;
bool isObsidian(void* block) noexcept;

bool getPortalAxis(void* block, PortalAxis& axis) noexcept;

} // namespace portal_shapes::engine
