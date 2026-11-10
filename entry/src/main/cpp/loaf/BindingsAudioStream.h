#pragma once

#include <lua.hpp>

#include <vector>

namespace loaf {

void registerAudioStreamBindings(lua_State *L, int ofTable);

/// pushes a sound buffer userdata, returns it on the stack
int pushSoundBuffer(lua_State *L, int frames, int channels, int sampleRate,
                    std::vector<float> &samples);

/// returns the backing sample vector of a sound buffer userdata
std::vector<float> *soundBufferSamples(lua_State *L, int index);

} // namespace loaf
