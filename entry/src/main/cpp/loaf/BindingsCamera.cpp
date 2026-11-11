#include "BindingsCamera.h"

#include "Environment.h"

#include <native_image/native_image.h>
#include <native_window/external_window.h>

#include <GLES2/gl2ext.h>

#include <atomic>
#include <cstdio>
#include <string>

namespace loaf {

namespace {

const char *kGrabberMeta = "loaf.VideoGrabber";

struct LuaGrabber {
	GLuint texture = 0;
	OH_NativeImage *image = nullptr;
	int width = 0;
	int height = 0;
	std::atomic<bool> frameAvailable {false};
	std::atomic<unsigned long> frames {0};
	bool receiving = false;
	float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

LuaGrabber *checkGrabber(lua_State *L, int index) {
	return static_cast<LuaGrabber *>(luaL_checkudata(L, index, kGrabberMeta));
}

float number(lua_State *L, int index, float fallback = 0.0f) {
	return lua_isnumber(L, index) ? (float)lua_tonumber(L, index) : fallback;
}

void setFunction(lua_State *L, const char *name, lua_CFunction function) {
	lua_pushcfunction(L, function);
	lua_setfield(L, -2, name);
}

void onFrameAvailable(void *context) {
	auto *grabber = static_cast<LuaGrabber *>(context);
	if(grabber != nullptr) {
		grabber->frameAvailable = true;
	}
}

//--------------------------------------------------------------
int grabber_new(lua_State *L) {
	LuaGrabber *grabber = static_cast<LuaGrabber *>(lua_newuserdatauv(L, sizeof(LuaGrabber), 0));
	new(grabber) LuaGrabber();
	luaL_getmetatable(L, kGrabberMeta);
	lua_setmetatable(L, -2);
	return 1;
}

int grabber_gc(lua_State *L) {
	LuaGrabber *grabber = checkGrabber(L, 1);
	if(env().emit && grabber->receiving) {
		env().emit("{\"type\":\"camera\",\"action\":\"stop\"}");
	}
	if(grabber->image != nullptr) {
		OH_NativeImage_Destroy(&grabber->image);
		grabber->image = nullptr;
	}
	if(grabber->texture != 0 && env().renderer != nullptr) {
		env().renderer->deleteTexture(grabber->texture);
		grabber->texture = 0;
	}
	grabber->receiving = false;
	return 0;
}

int grabber_setup(lua_State *L) {
	LuaGrabber *grabber = checkGrabber(L, 1);
	int width = (int)number(L, 2, 640.0f);
	int height = (int)number(L, 3, 480.0f);
	if(env().renderer == nullptr || env().emit == nullptr) {
		lua_pushboolean(L, 0);
		return 1;
	}
	if(grabber->image != nullptr) {
		OH_NativeImage_Destroy(&grabber->image);
	}
	if(grabber->texture == 0) {
		grabber->texture = env().renderer->createExternalTexture();
	}
	grabber->image = OH_NativeImage_Create(grabber->texture, GL_TEXTURE_EXTERNAL_OES);
	if(grabber->image == nullptr) {
		lua_pushboolean(L, 0);
		return 1;
	}
	OH_OnFrameAvailableListener listener;
	listener.context = grabber;
	listener.onFrameAvailable = onFrameAvailable;
	OH_NativeImage_SetOnFrameAvailableListener(grabber->image, listener);

	OHNativeWindow *window = OH_NativeImage_AcquireNativeWindow(grabber->image);
	uint64_t surfaceId = 0;
	if(window == nullptr || OH_NativeWindow_GetSurfaceId(window, &surfaceId) != 0) {
		lua_pushboolean(L, 0);
		return 1;
	}
	grabber->width = width;
	grabber->height = height;
	grabber->receiving = true;
	char payload[192];
	std::snprintf(payload, sizeof(payload),
	              "{\"type\":\"camera\",\"action\":\"start\",\"surfaceId\":\"%llu\","
	              "\"width\":%d,\"height\":%d}",
	              (unsigned long long)surfaceId, width, height);
	env().emit(payload);
	lua_pushboolean(L, 1);
	return 1;
}

int grabber_close(lua_State *L) {
	LuaGrabber *grabber = checkGrabber(L, 1);
	if(grabber->receiving && env().emit) {
		env().emit("{\"type\":\"camera\",\"action\":\"stop\"}");
	}
	grabber->receiving = false;
	if(grabber->image != nullptr) {
		OH_NativeImage_Destroy(&grabber->image);
		grabber->image = nullptr;
	}
	return 0;
}

int grabber_update(lua_State *L) {
	LuaGrabber *grabber = checkGrabber(L, 1);
	if(grabber->image != nullptr && grabber->frameAvailable.exchange(false)) {
		OH_NativeImage_UpdateSurfaceImage(grabber->image);
		OH_NativeImage_GetTransformMatrix(grabber->image, grabber->matrix);
		grabber->frames++;
	}
	return 0;
}

int grabber_draw(lua_State *L) {
	LuaGrabber *grabber = checkGrabber(L, 1);
	if(env().renderer == nullptr || grabber->texture == 0) {
		return 0;
	}
	grabber_update(L);
	float x = number(L, 2);
	float y = number(L, 3);
	float w = number(L, 4, (float)grabber->width);
	float h = number(L, 5, (float)grabber->height);
	env().renderer->drawExternalTexture(grabber->texture, grabber->matrix, x, y, w, h);
	return 0;
}

int grabber_isFrameNew(lua_State *L) {
	lua_pushboolean(L, checkGrabber(L, 1)->frameAvailable.load());
	return 1;
}

int grabber_getWidth(lua_State *L) { lua_pushinteger(L, checkGrabber(L, 1)->width); return 1; }
int grabber_getHeight(lua_State *L) { lua_pushinteger(L, checkGrabber(L, 1)->height); return 1; }
int grabber_isInitialized(lua_State *L) { lua_pushboolean(L, checkGrabber(L, 1)->image != nullptr); return 1; }
int grabber_getFrameCount(lua_State *L) { lua_pushinteger(L, (lua_Integer)checkGrabber(L, 1)->frames.load()); return 1; }

int grabber_listDevices(lua_State *L) {
	lua_newtable(L);
	return 1;
}

} // namespace

//--------------------------------------------------------------
void registerCameraBindings(lua_State *L, int ofTable) {
	luaL_newmetatable(L, kGrabberMeta);
	lua_pushcfunction(L, grabber_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "setup", grabber_setup);
	setFunction(L, "close", grabber_close);
	setFunction(L, "update", grabber_update);
	setFunction(L, "draw", grabber_draw);
	setFunction(L, "isFrameNew", grabber_isFrameNew);
	setFunction(L, "isInitialized", grabber_isInitialized);
	setFunction(L, "getWidth", grabber_getWidth);
	setFunction(L, "getHeight", grabber_getHeight);
	setFunction(L, "getFrameCount", grabber_getFrameCount);
	setFunction(L, "listDevices", grabber_listDevices);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	lua_pushcfunction(L, grabber_new);
	lua_setfield(L, ofTable, "VideoGrabber");
}

} // namespace loaf
