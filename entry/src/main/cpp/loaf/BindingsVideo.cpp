#include "BindingsVideo.h"

#include "Environment.h"
#include "Video.h"

namespace loaf {

namespace {

const char *kVideoMetatable = "loaf.VideoPlayer";

struct LuaVideo {
	VideoPlayer *player = nullptr;
};

LuaVideo *checkVideo(lua_State *L, int index) {
	return static_cast<LuaVideo *>(luaL_checkudata(L, index, kVideoMetatable));
}

float number(lua_State *L, int index, float fallback = 0.0f) {
	return lua_isnumber(L, index) ? (float)lua_tonumber(L, index) : fallback;
}

int video_new(lua_State *L) {
	std::string path = luaL_optstring(L, 1, "");
	LuaVideo *video = static_cast<LuaVideo *>(lua_newuserdatauv(L, sizeof(LuaVideo), 0));
	new(video) LuaVideo();
	video->player = new VideoPlayer();
	luaL_getmetatable(L, kVideoMetatable);
	lua_setmetatable(L, -2);
	if(!path.empty() && env().renderer != nullptr) {
		video->player->load(path, *env().renderer);
	}
	return 1;
}

int video_gc(lua_State *L) {
	LuaVideo *video = checkVideo(L, 1);
	delete video->player;
	video->player = nullptr;
	return 0;
}

int video_load(lua_State *L) {
	LuaVideo *video = checkVideo(L, 1);
	const char *path = luaL_checkstring(L, 2);
	lua_pushboolean(L, env().renderer != nullptr && video->player->load(path, *env().renderer));
	return 1;
}

int video_play(lua_State *L) {
	checkVideo(L, 1)->player->play();
	return 0;
}

int video_pause(lua_State *L) {
	checkVideo(L, 1)->player->pause();
	return 0;
}

int video_stop(lua_State *L) {
	checkVideo(L, 1)->player->stop();
	return 0;
}

int video_setLoop(lua_State *L) {
	checkVideo(L, 1)->player->setLoop(lua_toboolean(L, 2) != 0);
	return 0;
}

int video_isLooping(lua_State *L) {
	lua_pushboolean(L, checkVideo(L, 1)->player->isLooping());
	return 1;
}

int video_isPlaying(lua_State *L) {
	lua_pushboolean(L, checkVideo(L, 1)->player->isPlaying());
	return 1;
}

int video_isLoaded(lua_State *L) {
	lua_pushboolean(L, checkVideo(L, 1)->player->isLoaded());
	return 1;
}

int video_setVolume(lua_State *L) {
	checkVideo(L, 1)->player->setVolume(number(L, 2, 1.0f));
	return 0;
}

int video_getDuration(lua_State *L) {
	lua_pushnumber(L, checkVideo(L, 1)->player->getDuration());
	return 1;
}

int video_getPosition(lua_State *L) {
	lua_pushnumber(L, checkVideo(L, 1)->player->getPosition());
	return 1;
}

int video_setPosition(lua_State *L) {
	checkVideo(L, 1)->player->setPosition((double)number(L, 2));
	return 0;
}

int video_getWidth(lua_State *L) {
	lua_pushinteger(L, checkVideo(L, 1)->player->getWidth());
	return 1;
}

int video_getHeight(lua_State *L) {
	lua_pushinteger(L, checkVideo(L, 1)->player->getHeight());
	return 1;
}

int video_getFrameCount(lua_State *L) {
	lua_pushinteger(L, (lua_Integer)checkVideo(L, 1)->player->frameCount());
	return 1;
}

int video_getError(lua_State *L) {
	lua_pushstring(L, checkVideo(L, 1)->player->getError().c_str());
	return 1;
}

int video_draw(lua_State *L) {
	LuaVideo *video = checkVideo(L, 1);
	if(env().renderer == nullptr) {
		return 0;
	}
	float x = number(L, 2);
	float y = number(L, 3);
	float w = number(L, 4, (float)video->player->getWidth());
	float h = number(L, 5, (float)video->player->getHeight());
	video->player->draw(*env().renderer, x, y, w, h);
	return 0;
}

} // namespace

//--------------------------------------------------------------
void registerVideoBindings(lua_State *L, int ofTable) {
	luaL_newmetatable(L, kVideoMetatable);
	lua_pushcfunction(L, video_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	lua_pushcfunction(L, video_load);
	lua_setfield(L, -2, "load");
	lua_pushcfunction(L, video_play);
	lua_setfield(L, -2, "play");
	lua_pushcfunction(L, video_pause);
	lua_setfield(L, -2, "pause");
	lua_pushcfunction(L, video_stop);
	lua_setfield(L, -2, "stop");
	lua_pushcfunction(L, video_setLoop);
	lua_setfield(L, -2, "setLoop");
	lua_pushcfunction(L, video_isLooping);
	lua_setfield(L, -2, "isLooping");
	lua_pushcfunction(L, video_isPlaying);
	lua_setfield(L, -2, "isPlaying");
	lua_pushcfunction(L, video_isLoaded);
	lua_setfield(L, -2, "isLoaded");
	lua_pushcfunction(L, video_setVolume);
	lua_setfield(L, -2, "setVolume");
	lua_pushcfunction(L, video_getDuration);
	lua_setfield(L, -2, "getDuration");
	lua_pushcfunction(L, video_getPosition);
	lua_setfield(L, -2, "getPosition");
	lua_pushcfunction(L, video_setPosition);
	lua_setfield(L, -2, "setPosition");
	lua_pushcfunction(L, video_getWidth);
	lua_setfield(L, -2, "getWidth");
	lua_pushcfunction(L, video_getHeight);
	lua_setfield(L, -2, "getHeight");
	lua_pushcfunction(L, video_getFrameCount);
	lua_setfield(L, -2, "getFrameCount");
	lua_pushcfunction(L, video_getError);
	lua_setfield(L, -2, "getError");
	lua_pushcfunction(L, video_draw);
	lua_setfield(L, -2, "draw");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	lua_pushcfunction(L, video_new);
	lua_setfield(L, ofTable, "VideoPlayer");
}

} // namespace loaf
