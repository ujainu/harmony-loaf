#include "BindingsAudio.h"

#include "Audio.h"

namespace loaf {

namespace {

const char *kSoundMetatable = "loaf.SoundPlayer";

struct LuaSound {
	SoundPlayer *player = nullptr;
};

LuaSound *checkSound(lua_State *L, int index) {
	return static_cast<LuaSound *>(luaL_checkudata(L, index, kSoundMetatable));
}

float number(lua_State *L, int index, float fallback = 0.0f) {
	return lua_isnumber(L, index) ? (float)lua_tonumber(L, index) : fallback;
}

int sound_new(lua_State *L) {
	std::string path = luaL_optstring(L, 1, "");
	LuaSound *sound = static_cast<LuaSound *>(lua_newuserdatauv(L, sizeof(LuaSound), 0));
	new(sound) LuaSound();
	sound->player = new SoundPlayer();
	luaL_getmetatable(L, kSoundMetatable);
	lua_setmetatable(L, -2);
	if(!path.empty()) {
		sound->player->load(path);
	}
	return 1;
}

int sound_gc(lua_State *L) {
	LuaSound *sound = checkSound(L, 1);
	delete sound->player;
	sound->player = nullptr;
	return 0;
}

int sound_load(lua_State *L) {
	lua_pushboolean(L, checkSound(L, 1)->player->load(luaL_checkstring(L, 2)));
	return 1;
}

int sound_play(lua_State *L) {
	checkSound(L, 1)->player->play();
	return 0;
}

int sound_stop(lua_State *L) {
	checkSound(L, 1)->player->stop();
	return 0;
}

int sound_pause(lua_State *L) {
	checkSound(L, 1)->player->setPaused(true);
	return 0;
}

int sound_resume(lua_State *L) {
	checkSound(L, 1)->player->setPaused(false);
	return 0;
}

int sound_setPaused(lua_State *L) {
	checkSound(L, 1)->player->setPaused(lua_toboolean(L, 2) != 0);
	return 0;
}

int sound_isPlaying(lua_State *L) {
	lua_pushboolean(L, checkSound(L, 1)->player->isPlaying());
	return 1;
}

int sound_isLoaded(lua_State *L) {
	lua_pushboolean(L, checkSound(L, 1)->player->isLoaded());
	return 1;
}

int sound_setLoop(lua_State *L) {
	checkSound(L, 1)->player->setLoop(lua_toboolean(L, 2) != 0);
	return 0;
}

int sound_isLooping(lua_State *L) {
	lua_pushboolean(L, checkSound(L, 1)->player->isLooping());
	return 1;
}

int sound_setVolume(lua_State *L) {
	checkSound(L, 1)->player->setVolume(number(L, 2, 1.0f));
	return 0;
}

int sound_getVolume(lua_State *L) {
	lua_pushnumber(L, checkSound(L, 1)->player->getVolume());
	return 1;
}

int sound_getDuration(lua_State *L) {
	lua_pushnumber(L, checkSound(L, 1)->player->getDuration());
	return 1;
}

int sound_getPosition(lua_State *L) {
	lua_pushnumber(L, checkSound(L, 1)->player->getPosition());
	return 1;
}

int sound_setPosition(lua_State *L) {
	checkSound(L, 1)->player->setPosition((double)number(L, 2));
	return 0;
}

int audio_getSampleRate(lua_State *L) {
	lua_pushinteger(L, AudioEngine::kSampleRate);
	return 1;
}

} // namespace

//--------------------------------------------------------------
void registerAudioBindings(lua_State *L, int ofTable) {
	luaL_newmetatable(L, kSoundMetatable);
	lua_pushcfunction(L, sound_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	lua_pushcfunction(L, sound_load);
	lua_setfield(L, -2, "load");
	lua_pushcfunction(L, sound_play);
	lua_setfield(L, -2, "play");
	lua_pushcfunction(L, sound_stop);
	lua_setfield(L, -2, "stop");
	lua_pushcfunction(L, sound_pause);
	lua_setfield(L, -2, "pause");
	lua_pushcfunction(L, sound_resume);
	lua_setfield(L, -2, "resume");
	lua_pushcfunction(L, sound_setPaused);
	lua_setfield(L, -2, "setPaused");
	lua_pushcfunction(L, sound_isPlaying);
	lua_setfield(L, -2, "isPlaying");
	lua_pushcfunction(L, sound_isLoaded);
	lua_setfield(L, -2, "isLoaded");
	lua_pushcfunction(L, sound_setLoop);
	lua_setfield(L, -2, "setLoop");
	lua_pushcfunction(L, sound_isLooping);
	lua_setfield(L, -2, "isLooping");
	lua_pushcfunction(L, sound_setVolume);
	lua_setfield(L, -2, "setVolume");
	lua_pushcfunction(L, sound_getVolume);
	lua_setfield(L, -2, "getVolume");
	lua_pushcfunction(L, sound_getDuration);
	lua_setfield(L, -2, "getDuration");
	lua_pushcfunction(L, sound_getPosition);
	lua_setfield(L, -2, "getPosition");
	lua_pushcfunction(L, sound_setPosition);
	lua_setfield(L, -2, "setPosition");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	lua_pushcfunction(L, sound_new);
	lua_setfield(L, ofTable, "SoundPlayer");
	lua_pushcfunction(L, audio_getSampleRate);
	lua_setfield(L, ofTable, "getAudioSampleRate");
}

} // namespace loaf
