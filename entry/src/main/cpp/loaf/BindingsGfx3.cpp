#include "BindingsGfx3.h"

#include "Environment.h"

#include <GLES3/gl3.h>

#include "stb_image.h"

#include <cstring>
#include <string>
#include <vector>

namespace loaf {

namespace {

const char *kCubeMapMeta = "loaf.CubeMap";
const char *kBufferObjectMeta = "loaf.BufferObject";

struct LuaCubeMap {
	GLuint texture = 0;
	int size = 0;
};

struct LuaBufferObject {
	GLuint buffer = 0;
	int target = GL_ARRAY_BUFFER;
	size_t size = 0;
};

LuaCubeMap *checkCubeMap(lua_State *L, int index) {
	return static_cast<LuaCubeMap *>(luaL_checkudata(L, index, kCubeMapMeta));
}

LuaBufferObject *checkBufferObject(lua_State *L, int index) {
	return static_cast<LuaBufferObject *>(luaL_checkudata(L, index, kBufferObjectMeta));
}

float number(lua_State *L, int index, float fallback = 0.0f) {
	return lua_isnumber(L, index) ? (float)lua_tonumber(L, index) : fallback;
}

void setFunction(lua_State *L, const char *name, lua_CFunction function) {
	lua_pushcfunction(L, function);
	lua_setfield(L, -2, name);
}

//--------------------------------------------------------------
int cube_new(lua_State *L) {
	LuaCubeMap *cube = static_cast<LuaCubeMap *>(lua_newuserdatauv(L, sizeof(LuaCubeMap), 0));
	new(cube) LuaCubeMap();
	luaL_getmetatable(L, kCubeMapMeta);
	lua_setmetatable(L, -2);
	return 1;
}

int cube_gc(lua_State *L) {
	LuaCubeMap *cube = checkCubeMap(L, 1);
	if(cube->texture != 0 && env().renderer != nullptr) {
		env().renderer->deleteTexture(cube->texture);
	}
	cube->texture = 0;
	return 0;
}

int cube_loadImages(lua_State *L) {
	LuaCubeMap *cube = checkCubeMap(L, 1);
	if(env().renderer == nullptr) {
		lua_pushboolean(L, 0);
		return 1;
	}
	std::vector<std::string> paths;
	if(lua_istable(L, 2)) {
		int count = (int)lua_rawlen(L, 2);
		for(int i = 1; i <= count; ++i) {
			lua_rawgeti(L, 2, i);
			paths.emplace_back(luaL_checkstring(L, -1));
			lua_pop(L, 1);
		}
	}
	else {
		for(int i = 2; i <= 7; ++i) {
			paths.emplace_back(luaL_checkstring(L, i));
		}
	}
	if(paths.size() != 6) {
		lua_pushboolean(L, 0);
		return 1;
	}

	std::vector<std::vector<unsigned char>> faces;
	int size = 0;
	for(const auto &path : paths) {
		int width = 0, height = 0, channels = 0;
		unsigned char *pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
		if(pixels == nullptr) {
			lua_pushboolean(L, 0);
			return 1;
		}
		if(size == 0) {
			size = width;
		}
		std::vector<unsigned char> face((size_t)size * size * 4, 0);
		int copyWidth = width < size ? width : size;
		int copyHeight = height < size ? height : size;
		for(int y = 0; y < copyHeight; ++y) {
			std::memcpy(face.data() + (size_t)y * size * 4,
			            pixels + (size_t)y * width * 4, (size_t)copyWidth * 4);
		}
		stbi_image_free(pixels);
		faces.push_back(std::move(face));
	}
	if(cube->texture != 0) {
		env().renderer->deleteTexture(cube->texture);
	}
	cube->texture = env().renderer->createCubeMapTexture(faces, size);
	cube->size = size;
	lua_pushboolean(L, cube->texture != 0);
	return 1;
}

int cube_getWidth(lua_State *L) { lua_pushinteger(L, checkCubeMap(L, 1)->size); return 1; }
int cube_getHeight(lua_State *L) { lua_pushinteger(L, checkCubeMap(L, 1)->size); return 1; }
int cube_isLoaded(lua_State *L) { lua_pushboolean(L, checkCubeMap(L, 1)->texture != 0); return 1; }

int cube_draw(lua_State *L) {
	LuaCubeMap *cube = checkCubeMap(L, 1);
	if(cube->texture != 0 && env().renderer != nullptr) {
		env().renderer->drawCubeMap(cube->texture, number(L, 2, 500.0f));
	}
	return 0;
}

//--------------------------------------------------------------
int buffer_new(lua_State *L) {
	LuaBufferObject *object = static_cast<LuaBufferObject *>(lua_newuserdatauv(L, sizeof(LuaBufferObject), 0));
	new(object) LuaBufferObject();
	luaL_getmetatable(L, kBufferObjectMeta);
	lua_setmetatable(L, -2);
	return 1;
}

int buffer_gc(lua_State *L) {
	LuaBufferObject *object = checkBufferObject(L, 1);
	if(object->buffer != 0) {
		glDeleteBuffers(1, &object->buffer);
		object->buffer = 0;
	}
	return 0;
}

int buffer_allocate(lua_State *L) {
	LuaBufferObject *object = checkBufferObject(L, 1);
	size_t size = (size_t)number(L, 2, 0.0f);
	if(lua_gettop(L) >= 3) {
		object->target = (int)number(L, 3, (float)GL_ARRAY_BUFFER);
	}
	if(object->buffer == 0) {
		glGenBuffers(1, &object->buffer);
	}
	glBindBuffer(object->target, object->buffer);
	glBufferData(object->target, size, nullptr, GL_DYNAMIC_DRAW);
	object->size = size;
	return 0;
}

int buffer_updateData(lua_State *L) {
	LuaBufferObject *object = checkBufferObject(L, 1);
	size_t length = 0;
	const char *data = luaL_checklstring(L, 2, &length);
	if(object->buffer == 0) {
		glGenBuffers(1, &object->buffer);
	}
	glBindBuffer(object->target, object->buffer);
	glBufferData(object->target, length, data, GL_DYNAMIC_DRAW);
	object->size = length;
	return 0;
}

int buffer_bind(lua_State *L) {
	LuaBufferObject *object = checkBufferObject(L, 1);
	if(object->buffer != 0) {
		glBindBuffer(object->target, object->buffer);
	}
	return 0;
}

int buffer_unbind(lua_State *L) {
	LuaBufferObject *object = checkBufferObject(L, 1);
	glBindBuffer(object->target, 0);
	return 0;
}

int buffer_getSize(lua_State *L) { lua_pushinteger(L, (lua_Integer)checkBufferObject(L, 1)->size); return 1; }
int buffer_isAllocated(lua_State *L) { lua_pushboolean(L, checkBufferObject(L, 1)->buffer != 0); return 1; }

} // namespace

//--------------------------------------------------------------
void registerGfx3Bindings(lua_State *L, int ofTable) {
	luaL_newmetatable(L, kCubeMapMeta);
	lua_pushcfunction(L, cube_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "loadImages", cube_loadImages);
	setFunction(L, "getWidth", cube_getWidth);
	setFunction(L, "getHeight", cube_getHeight);
	setFunction(L, "isLoaded", cube_isLoaded);
	setFunction(L, "draw", cube_draw);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	luaL_newmetatable(L, kBufferObjectMeta);
	lua_pushcfunction(L, buffer_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "allocate", buffer_allocate);
	setFunction(L, "updateData", buffer_updateData);
	setFunction(L, "bind", buffer_bind);
	setFunction(L, "unbind", buffer_unbind);
	setFunction(L, "getSize", buffer_getSize);
	setFunction(L, "isAllocated", buffer_isAllocated);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	lua_pushcfunction(L, cube_new); lua_setfield(L, ofTable, "CubeMap");
	lua_pushcfunction(L, buffer_new); lua_setfield(L, ofTable, "BufferObject");
}

} // namespace loaf
