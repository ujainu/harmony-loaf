#pragma once

#include <lua.hpp>

#include <string>
#include <vector>

namespace loaf {

void registerGfx2Bindings(lua_State *L, int ofTable);

/// helpers so other binding files can exchange pixel data with of.Pixels
int pushPixelsUserdata(lua_State *L, int width, int height, int channels,
                       const std::vector<unsigned char> &data);
bool readPixelsUserdata(lua_State *L, int index, int &width, int &height,
                        int &channels, std::vector<unsigned char> &data);

} // namespace loaf
