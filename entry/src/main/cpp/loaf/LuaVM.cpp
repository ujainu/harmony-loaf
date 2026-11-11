#include "LuaVM.h"

#include "Environment.h"
#include "CompatStubs.h"
#include "LuaExtras.h"
#include "AudioStream.h"
#include "BindingsAudioStream.h"
#include "BindingsHardware.h"
#include "../Log.h"

#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace loaf {

namespace {

int luaPrint(lua_State *L) {
	int count = lua_gettop(L);
	luaL_Buffer buffer;
	luaL_buffinit(L, &buffer);
	for(int i = 1; i <= count; ++i) {
		size_t length = 0;
		const char *text = luaL_tolstring(L, i, &length);
		if(text != nullptr) {
			luaL_addlstring(&buffer, text, length);
		}
		if(i < count) {
			luaL_addchar(&buffer, '\t');
		}
		lua_pop(L, 1);
	}
	luaL_pushresult(&buffer);
	std::string line = lua_tostring(L, -1);
	lua_pop(L, 1);
	OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "loafcore", "print: %{public}s", line.c_str());
	if(env().emit) {
		std::string escaped;
		for(char c : line) {
			if(c == '"' || c == '\\') {
				escaped.push_back('\\');
				escaped.push_back(c);
			}
			else if(c == '\n') {
				escaped += "\\n";
			}
			else if((unsigned char)c >= 0x20) {
				escaped.push_back(c);
			}
		}
		env().emit("{\"type\":\"info\",\"message\":\"" + escaped + "\"}");
	}
	return 0;
}

std::string directoryOf(const std::string &path) {
	size_t slash = path.find_last_of('/');
	if(slash == std::string::npos) {
		return ".";
	}
	if(slash == 0) {
		return "/";
	}
	return path.substr(0, slash);
}

} // namespace

//--------------------------------------------------------------
LuaVM::~LuaVM() {
	closeState();
}

//--------------------------------------------------------------
bool LuaVM::initState() {
	closeState();
	L = luaL_newstate();
	if(L == nullptr) {
		fail("could not create lua state");
		return false;
	}
	luaL_openlibs(L);
	lua_pushcfunction(L, luaPrint);
	lua_setglobal(L, "print");
	registerOfBindings(L);
	registerLoafBindings(L);
	registerOscBindings(L);
	if(luaL_dostring(L, kCompatLua) != LUA_OK) {
		const char *message = lua_tostring(L, -1);
		OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0000, "loafcore",
		             "compat surface failed: %{public}s", message != nullptr ? message : "");
		lua_pop(L, 1);
	}
	if(luaL_dostring(L, kLuaExtras) != LUA_OK) {
		const char *message = lua_tostring(L, -1);
		OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0000, "loafcore",
		             "lua extras failed: %{public}s", message != nullptr ? message : "");
		lua_pop(L, 1);
	}

	// script arguments
	lua_newtable(L);
	lua_pushstring(L, currentPath.c_str());
	lua_rawseti(L, -2, 0);
	lua_setglobal(L, "arg");

	lua_pushnumber(L, 0.0);
	lua_setglobal(L, "mouseX");
	lua_pushnumber(L, 0.0);
	lua_setglobal(L, "mouseY");
	return true;
}

//--------------------------------------------------------------
void LuaVM::closeState() {
	if(L != nullptr) {
		lua_close(L);
		L = nullptr;
	}
}

//--------------------------------------------------------------
void LuaVM::fail(const std::string &message) {
	lastError = message;
	OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0000, "loafcore", "lua error: %{public}s", message.c_str());
}

//--------------------------------------------------------------
void LuaVM::installPackagePath(const std::string &directory) {
	lua_getglobal(L, "package");
	if(!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	lua_getfield(L, -1, "path");
	std::string path = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
	lua_pop(L, 1);
	path = directory + "/?.lua;" + directory + "/?/init.lua;" + path;
	lua_pushstring(L, path.c_str());
	lua_setfield(L, -2, "path");
	lua_pop(L, 1);
}

//--------------------------------------------------------------
bool LuaVM::runChunk(const char *chunkName) {
	if(L == nullptr) {
		return false;
	}
	if(lua_pcall(L, 0, 0, 0) != LUA_OK) {
		const char *message = lua_tostring(L, -1);
		fail(std::string(chunkName) + ": " + (message ? message : "unknown error"));
		lua_pop(L, 1);
		return false;
	}
	return true;
}

//--------------------------------------------------------------
bool LuaVM::loadFile(const std::string &path) {
	if(!initState()) {
		return false;
	}
	currentPath = path;
	currentDir = directoryOf(path);
	if(chdir(currentDir.c_str()) != 0) {
		LOGW("could not chdir to %s", currentDir.c_str());
	}
	installPackagePath(currentDir);
	syncGlobals();
	if(luaL_loadfilex(L, path.c_str(), nullptr) != LUA_OK) {
		const char *message = lua_tostring(L, -1);
		fail(message ? message : "could not load script");
		lua_pop(L, 1);
		return false;
	}
	if(!runChunk(path.c_str())) {
		return false;
	}
	lastError.clear();
	setupPending = true;
	return true;
}

//--------------------------------------------------------------
bool LuaVM::loadBuffer(const std::string &code, const std::string &name, const std::string &directory) {
	if(!initState()) {
		return false;
	}
	currentPath = directory + "/" + name;
	currentDir = directory;
	if(chdir(currentDir.c_str()) != 0) {
		LOGW("could not chdir to %s", currentDir.c_str());
	}
	installPackagePath(currentDir);
	syncGlobals();
	if(luaL_loadbufferx(L, code.c_str(), code.size(), name.c_str(), nullptr) != LUA_OK) {
		const char *message = lua_tostring(L, -1);
		fail(message ? message : "could not parse script");
		lua_pop(L, 1);
		return false;
	}
	if(!runChunk(name.c_str())) {
		return false;
	}
	lastError.clear();
	setupPending = true;
	return true;
}

//--------------------------------------------------------------
bool LuaVM::exec(const std::string &code, const std::string &name) {
	if(L == nullptr) {
		return false;
	}
	syncGlobals();
	if(luaL_loadbufferx(L, code.c_str(), code.size(), name.c_str(), nullptr) != LUA_OK) {
		const char *message = lua_tostring(L, -1);
		fail(message ? message : "could not parse code");
		lua_pop(L, 1);
		return false;
	}
	if(lua_pcall(L, 0, 0, 0) != LUA_OK) {
		const char *message = lua_tostring(L, -1);
		fail(std::string(name) + ": " + (message ? message : "unknown error"));
		lua_pop(L, 1);
		return false;
	}
	return true;
}

//--------------------------------------------------------------
bool LuaVM::callOsc(const OscMessage &message) {
	if(L == nullptr) {
		return false;
	}
	lua_pushcfunction(L, [](lua_State *state) -> int {
		const char *text = lua_tostring(state, 1);
		luaL_traceback(state, state, text, 1);
		return 1;
	});
	int handler = lua_gettop(L);
	lua_getglobal(L, "oscReceived");
	if(!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return false;
	}
	pushOscMessage(L, message);
	if(lua_pcall(L, 1, 0, handler) != LUA_OK) {
		const char *error = lua_tostring(L, -1);
		fail(std::string("oscReceived(): ") + (error ? error : "unknown error"));
		lua_pop(L, 1);
		lua_remove(L, handler);
		return false;
	}
	lua_remove(L, handler);
	return true;
}

//--------------------------------------------------------------
bool LuaVM::reload() {
	if(currentPath.empty()) {
		return false;
	}
	call("exit");
	return loadFile(currentPath);
}

//--------------------------------------------------------------
void LuaVM::clear() {
	if(L == nullptr) {
		return;
	}
	call("exit");
	closeState();
	lastError.clear();
	setupPending = false;
	currentPath.clear();
}

//--------------------------------------------------------------
bool LuaVM::hasFunction(const char *name) {
	if(L == nullptr) {
		return false;
	}
	lua_getglobal(L, name);
	bool function = lua_isfunction(L, -1);
	lua_pop(L, 1);
	return function;
}

//--------------------------------------------------------------
bool LuaVM::call(const char *name, int nargs, int nresults) {
	if(L == nullptr) {
		return false;
	}
	syncGlobals();
	lua_pushcfunction(L, [](lua_State *state) -> int {
		const char *message = lua_tostring(state, 1);
		luaL_traceback(state, state, message, 1);
		return 1;
	});
	int handler = lua_gettop(L);
	lua_getglobal(L, name);
	if(!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return false;
	}
	if(lua_pcall(L, 0, nresults, handler) != LUA_OK) {
		const char *message = lua_tostring(L, -1);
		fail(std::string(name) + "(): " + (message ? message : "unknown error"));
		lua_pop(L, 1);
		lua_remove(L, handler);
		return false;
	}
	lua_remove(L, handler);
	return true;
}

//--------------------------------------------------------------
bool LuaVM::callNumbers(const char *name, const std::vector<double> &args) {
	if(L == nullptr) {
		return false;
	}
	syncGlobals();
	lua_pushcfunction(L, [](lua_State *state) -> int {
		const char *message = lua_tostring(state, 1);
		luaL_traceback(state, state, message, 1);
		return 1;
	});
	int handler = lua_gettop(L);
	lua_getglobal(L, name);
	if(!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return false;
	}
	for(double value : args) {
		lua_pushnumber(L, value);
	}
	if(lua_pcall(L, (int)args.size(), 0, handler) != LUA_OK) {
		const char *message = lua_tostring(L, -1);
		fail(std::string(name) + "(): " + (message ? message : "unknown error"));
		lua_pop(L, 1);
		lua_remove(L, handler);
		return false;
	}
	lua_remove(L, handler);
	return true;
}

//--------------------------------------------------------------
bool LuaVM::callString(const char *name, const std::string &value) {
	if(L == nullptr) {
		return false;
	}
	syncGlobals();
	lua_pushcfunction(L, [](lua_State *state) -> int {
		const char *message = lua_tostring(state, 1);
		luaL_traceback(state, state, message, 1);
		return 1;
	});
	int handler = lua_gettop(L);
	lua_getglobal(L, name);
	if(!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return false;
	}
	lua_pushlstring(L, value.data(), value.size());
	if(lua_pcall(L, 1, 0, handler) != LUA_OK) {
		const char *message = lua_tostring(L, -1);
		fail(std::string(name) + "(): " + (message ? message : "unknown error"));
		lua_pop(L, 1);
		lua_remove(L, handler);
		return false;
	}
	lua_remove(L, handler);
	return true;
}

//--------------------------------------------------------------
bool LuaVM::callFiles(const char *name, const std::vector<std::string> &files) {
	if(L == nullptr) {
		return false;
	}
	syncGlobals();
	lua_pushcfunction(L, [](lua_State *state) -> int {
		const char *message = lua_tostring(state, 1);
		luaL_traceback(state, state, message, 1);
		return 1;
	});
	int handler = lua_gettop(L);
	lua_getglobal(L, name);
	if(!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return false;
	}
	lua_newtable(L);
	lua_newtable(L);
	for(size_t i = 0; i < files.size(); ++i) {
		lua_pushlstring(L, files[i].data(), files[i].size());
		lua_rawseti(L, -2, (lua_Integer)i + 1);
	}
	lua_setfield(L, -2, "files");
	if(lua_pcall(L, 1, 0, handler) != LUA_OK) {
		const char *message = lua_tostring(L, -1);
		fail(std::string(name) + "(): " + (message ? message : "unknown error"));
		lua_pop(L, 1);
		lua_remove(L, handler);
		return false;
	}
	lua_remove(L, handler);
	return true;
}

//--------------------------------------------------------------
bool LuaVM::callAudioOut(SoundStream &stream) {
	if(L == nullptr) {
		return false;
	}
	std::vector<float> block;
	stream.requestBlock(block);
	pushSoundBuffer(L, stream.getBufferSize(), stream.getNumOutputChannels(),
	                stream.getSampleRate(), block);
	int bufferIndex = lua_gettop(L);

	lua_pushcfunction(L, [](lua_State *state) -> int {
		const char *message = lua_tostring(state, 1);
		luaL_traceback(state, state, message, 1);
		return 1;
	});
	int handler = lua_gettop(L);
	lua_getglobal(L, "audioOut");
	if(!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		lua_pop(L, 1);
		return false;
	}
	lua_pushvalue(L, bufferIndex);
	if(lua_pcall(L, 1, 0, handler) != LUA_OK) {
		const char *message = lua_tostring(L, -1);
		fail(std::string("audioOut(): ") + (message ? message : "unknown error"));
		lua_pop(L, 1);
		lua_remove(L, handler);
		lua_pop(L, 1);
		return false;
	}
	lua_remove(L, handler);
	std::vector<float> *result = soundBufferSamples(L, bufferIndex);
	stream.submit(result != nullptr ? *result : block);
	lua_pop(L, 1);
	return true;
}

//--------------------------------------------------------------
void LuaVM::pollHardware() {
	if(L != nullptr) {
		pollSerialDevices(L);
	}
}

//--------------------------------------------------------------
void LuaVM::syncGlobals() {
	if(L == nullptr) {
		return;
	}
	Environment &e = env();
	auto setNumber = [this](const char *name, double value) {
		lua_pushnumber(L, value);
		lua_setglobal(L, name);
	};
	setNumber("mouseX", e.input.mouseX);
	setNumber("mouseY", e.input.mouseY);
	setNumber("width", e.renderer ? e.renderer->width() : 0);
	setNumber("height", e.renderer ? e.renderer->height() : 0);
	setNumber("windowWidth", e.windowWidth);
	setNumber("windowHeight", e.windowHeight);
	setNumber("frameNum", e.frameNum);
	setNumber("time", e.time);
	setNumber("deltaTime", e.deltaTime);
	setNumber("frameRate", e.targetFrameRate);
}

} // namespace loaf
