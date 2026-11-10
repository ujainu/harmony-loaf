#pragma once

#include <lua.hpp>

#include <string>

namespace loaf {

/// reads a of.Buffer userdata, returns false if the value is not a buffer
bool getLuaBuffer(lua_State *L, int index, std::string &data);

/// registers of.Buffer, of.File, of.Directory and of.FilePath on the "of" table
void registerFileBindings(lua_State *L, int ofTable);

} // namespace loaf
