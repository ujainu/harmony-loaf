#include "BindingsAudioStream.h"

#include "AudioStream.h"

#include <string>

namespace loaf {

namespace {

const char *kStreamMeta = "loaf.SoundStream";
const char *kBufferMeta = "loaf.SoundBuffer";

struct LuaStream {
	SoundStream *stream = nullptr;
};

struct LuaSoundBuffer {
	int frames = 0;
	int channels = 2;
	int sampleRate = 44100;
	std::vector<float> samples;
};

LuaStream *checkStream(lua_State *L, int index) {
	return static_cast<LuaStream *>(luaL_checkudata(L, index, kStreamMeta));
}

LuaSoundBuffer *checkBuffer(lua_State *L, int index) {
	return static_cast<LuaSoundBuffer *>(luaL_checkudata(L, index, kBufferMeta));
}

int stream_new(lua_State *L) {
	LuaStream *s = static_cast<LuaStream *>(lua_newuserdatauv(L, sizeof(LuaStream), 0));
	new(s) LuaStream();
	s->stream = new SoundStream();
	luaL_getmetatable(L, kStreamMeta);
	lua_setmetatable(L, -2);
	return 1;
}

int stream_gc(lua_State *L) {
	LuaStream *s = checkStream(L, 1);
	delete s->stream;
	s->stream = nullptr;
	return 0;
}

int stream_setup(lua_State *L) {
	LuaStream *s = checkStream(L, 1);
	int out = (int)luaL_optnumber(L, 2, 2);
	int in = (int)luaL_optnumber(L, 3, 0);
	int rate = (int)luaL_optnumber(L, 4, 44100);
	int size = (int)luaL_optnumber(L, 5, 512);
	lua_pushboolean(L, s->stream->setup(out, in, rate, size));
	return 1;
}

int stream_start(lua_State *L) { checkStream(L, 1)->stream->start(); return 0; }
int stream_stop(lua_State *L) { checkStream(L, 1)->stream->stop(); return 0; }
int stream_close(lua_State *L) { checkStream(L, 1)->stream->close(); return 0; }
int stream_isRunning(lua_State *L) { lua_pushboolean(L, checkStream(L, 1)->stream->isRunning()); return 1; }
int stream_getSampleRate(lua_State *L) { lua_pushinteger(L, checkStream(L, 1)->stream->getSampleRate()); return 1; }
int stream_getBufferSize(lua_State *L) { lua_pushinteger(L, checkStream(L, 1)->stream->getBufferSize()); return 1; }
int stream_getNumOutputChannels(lua_State *L) { lua_pushinteger(L, checkStream(L, 1)->stream->getNumOutputChannels()); return 1; }
int stream_getNumInputChannels(lua_State *L) { lua_pushinteger(L, checkStream(L, 1)->stream->getNumInputChannels()); return 1; }
int stream_setVolume(lua_State *L) { checkStream(L, 1)->stream->setVolume((float)luaL_checknumber(L, 2)); return 0; }

//--------------------------------------------------------------
int buffer_getNumFrames(lua_State *L) { lua_pushinteger(L, checkBuffer(L, 1)->frames); return 1; }
int buffer_getNumChannels(lua_State *L) { lua_pushinteger(L, checkBuffer(L, 1)->channels); return 1; }
int buffer_getSampleRate(lua_State *L) { lua_pushinteger(L, checkBuffer(L, 1)->sampleRate); return 1; }

int buffer_setSample(lua_State *L) {
	LuaSoundBuffer *b = checkBuffer(L, 1);
	int index = (int)luaL_checknumber(L, 2);
	float value = (float)luaL_checknumber(L, 3);
	if(index >= 0 && index < (int)b->samples.size()) {
		b->samples[index] = value;
	}
	return 0;
}

int buffer_getSample(lua_State *L) {
	LuaSoundBuffer *b = checkBuffer(L, 1);
	int index = (int)luaL_checknumber(L, 2);
	lua_pushnumber(L, index >= 0 && index < (int)b->samples.size() ? b->samples[index] : 0.0f);
	return 1;
}

int buffer_fill(lua_State *L) {
	LuaSoundBuffer *b = checkBuffer(L, 1);
	float value = (float)luaL_optnumber(L, 2, 0.0);
	for(float &sample : b->samples) {
		sample = value;
	}
	return 0;
}

} // namespace

//--------------------------------------------------------------
int pushSoundBuffer(lua_State *L, int frames, int channels, int sampleRate,
                    std::vector<float> &samples) {
	LuaSoundBuffer *b = static_cast<LuaSoundBuffer *>(lua_newuserdatauv(L, sizeof(LuaSoundBuffer), 0));
	new(b) LuaSoundBuffer();
	b->frames = frames;
	b->channels = channels;
	b->sampleRate = sampleRate;
	b->samples = samples;
	luaL_getmetatable(L, kBufferMeta);
	lua_setmetatable(L, -2);
	lua_pushvalue(L, -1);
	// keep a pointer for the caller to read the mutating buffer
	lua_pop(L, 1);
	return 1;
}

//--------------------------------------------------------------
std::vector<float> *soundBufferSamples(lua_State *L, int index) {
	LuaSoundBuffer *b = static_cast<LuaSoundBuffer *>(luaL_testudata(L, index, kBufferMeta));
	return b != nullptr ? &b->samples : nullptr;
}

//--------------------------------------------------------------
void registerAudioStreamBindings(lua_State *L, int ofTable) {
	luaL_newmetatable(L, kStreamMeta);
	lua_pushcfunction(L, stream_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	lua_pushcfunction(L, stream_setup); lua_setfield(L, -2, "setup");
	lua_pushcfunction(L, stream_start); lua_setfield(L, -2, "start");
	lua_pushcfunction(L, stream_stop); lua_setfield(L, -2, "stop");
	lua_pushcfunction(L, stream_close); lua_setfield(L, -2, "close");
	lua_pushcfunction(L, stream_isRunning); lua_setfield(L, -2, "isRunning");
	lua_pushcfunction(L, stream_getSampleRate); lua_setfield(L, -2, "getSampleRate");
	lua_pushcfunction(L, stream_getBufferSize); lua_setfield(L, -2, "getBufferSize");
	lua_pushcfunction(L, stream_getNumOutputChannels); lua_setfield(L, -2, "getNumOutputChannels");
	lua_pushcfunction(L, stream_getNumInputChannels); lua_setfield(L, -2, "getNumInputChannels");
	lua_pushcfunction(L, stream_setVolume); lua_setfield(L, -2, "setVolume");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	luaL_newmetatable(L, kBufferMeta);
	lua_newtable(L);
	lua_pushcfunction(L, buffer_getNumFrames); lua_setfield(L, -2, "getNumFrames");
	lua_pushcfunction(L, buffer_getNumChannels); lua_setfield(L, -2, "getNumChannels");
	lua_pushcfunction(L, buffer_getSampleRate); lua_setfield(L, -2, "getSampleRate");
	lua_pushcfunction(L, buffer_setSample); lua_setfield(L, -2, "setSample");
	lua_pushcfunction(L, buffer_getSample); lua_setfield(L, -2, "getSample");
	lua_pushcfunction(L, buffer_fill); lua_setfield(L, -2, "fill");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	lua_pushcfunction(L, stream_new);
	lua_setfield(L, ofTable, "SoundStream");
}

} // namespace loaf
