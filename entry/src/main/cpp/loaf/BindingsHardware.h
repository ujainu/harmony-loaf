#pragma once

#include <lua.hpp>

namespace loaf {

void registerHardwareBindings(lua_State *L, int ofTable);

/// pumps serial/arduino input and dispatches pin callbacks
void pollSerialDevices(lua_State *L);

} // namespace loaf
