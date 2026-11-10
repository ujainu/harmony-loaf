#include "BindingsShader.h"

#include "Environment.h"
#include "Shader.h"

#include <string>

namespace loaf {

namespace {

const char *kShaderMetatable = "loaf.Shader";

struct LuaShader {
	Shader *shader = nullptr;
};

LuaShader *checkShader(lua_State *L, int index) {
	return static_cast<LuaShader *>(luaL_checkudata(L, index, kShaderMetatable));
}

int shader_new(lua_State *L) {
	LuaShader *userdata = static_cast<LuaShader *>(lua_newuserdatauv(L, sizeof(LuaShader), 0));
	new(userdata) LuaShader();
	userdata->shader = new Shader();
	luaL_getmetatable(L, kShaderMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int shader_gc(lua_State *L) {
	LuaShader *userdata = checkShader(L, 1);
	delete userdata->shader;
	userdata->shader = nullptr;
	return 0;
}

int shader_load(lua_State *L) {
	LuaShader *userdata = checkShader(L, 1);
	const char *vertex = luaL_optstring(L, 2, "");
	const char *fragment = luaL_optstring(L, 3, "");
	if(fragment[0] == '\0') {
		lua_pushboolean(L, userdata->shader->loadFragmentOnly(vertex));
	}
	else {
		lua_pushboolean(L, userdata->shader->loadFiles(vertex, fragment));
	}
	return 1;
}

int shader_loadSource(lua_State *L) {
	LuaShader *userdata = checkShader(L, 1);
	const char *vertex = luaL_checkstring(L, 2);
	const char *fragment = luaL_checkstring(L, 3);
	lua_pushboolean(L, userdata->shader->loadSources(vertex, fragment));
	return 1;
}

int shader_loadFragmentSource(lua_State *L) {
	LuaShader *userdata = checkShader(L, 1);
	lua_pushboolean(L, userdata->shader->loadFragmentOnly(luaL_checkstring(L, 2)));
	return 1;
}

int shader_begin(lua_State *L) {
	checkShader(L, 1)->shader->begin(*env().renderer);
	return 0;
}

int shader_end(lua_State *L) {
	checkShader(L, 1)->shader->end(*env().renderer);
	return 0;
}

int shader_setUniform1f(lua_State *L) {
	LuaShader *userdata = checkShader(L, 1);
	userdata->shader->setUniform1f(luaL_checkstring(L, 2), (float)luaL_checknumber(L, 3));
	return 0;
}

int shader_setUniform2f(lua_State *L) {
	LuaShader *userdata = checkShader(L, 1);
	userdata->shader->setUniform2f(luaL_checkstring(L, 2), (float)luaL_checknumber(L, 3),
	                               (float)luaL_checknumber(L, 4));
	return 0;
}

int shader_setUniform3f(lua_State *L) {
	LuaShader *userdata = checkShader(L, 1);
	userdata->shader->setUniform3f(luaL_checkstring(L, 2), (float)luaL_checknumber(L, 3),
	                               (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5));
	return 0;
}

int shader_setUniform4f(lua_State *L) {
	LuaShader *userdata = checkShader(L, 1);
	userdata->shader->setUniform4f(luaL_checkstring(L, 2), (float)luaL_checknumber(L, 3),
	                               (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
	                               (float)luaL_checknumber(L, 6));
	return 0;
}

int shader_setUniform1i(lua_State *L) {
	LuaShader *userdata = checkShader(L, 1);
	userdata->shader->setUniform1i(luaL_checkstring(L, 2), (int)luaL_checknumber(L, 3));
	return 0;
}

int shader_isValid(lua_State *L) {
	lua_pushboolean(L, checkShader(L, 1)->shader->valid());
	return 1;
}

} // namespace

//--------------------------------------------------------------
void registerShaderBindings(lua_State *L, int ofTable) {
	luaL_newmetatable(L, kShaderMetatable);
	lua_pushcfunction(L, shader_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	lua_pushcfunction(L, shader_load);
	lua_setfield(L, -2, "load");
	lua_pushcfunction(L, shader_loadSource);
	lua_setfield(L, -2, "loadFromSource");
	lua_pushcfunction(L, shader_loadFragmentSource);
	lua_setfield(L, -2, "loadFragmentSource");
	lua_pushcfunction(L, shader_begin);
	lua_setfield(L, -2, "begin");
	lua_pushcfunction(L, shader_end);
	lua_setfield(L, -2, "endShader");
	lua_pushcfunction(L, shader_end);
	lua_setfield(L, -2, "finish");
	lua_pushcfunction(L, shader_setUniform1f);
	lua_setfield(L, -2, "setUniform1f");
	lua_pushcfunction(L, shader_setUniform2f);
	lua_setfield(L, -2, "setUniform2f");
	lua_pushcfunction(L, shader_setUniform3f);
	lua_setfield(L, -2, "setUniform3f");
	lua_pushcfunction(L, shader_setUniform4f);
	lua_setfield(L, -2, "setUniform4f");
	lua_pushcfunction(L, shader_setUniform1i);
	lua_setfield(L, -2, "setUniform1i");
	lua_pushcfunction(L, shader_isValid);
	lua_setfield(L, -2, "isValid");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	lua_pushcfunction(L, shader_new);
	lua_setfield(L, ofTable, "Shader");
}

} // namespace loaf
