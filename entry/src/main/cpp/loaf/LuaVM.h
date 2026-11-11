#pragma once

#include <lua.hpp>

#include <string>
#include <vector>

namespace loaf { class SoundStream; }

namespace loaf {

void registerOfBindings(lua_State *L);
void registerLoafBindings(lua_State *L);
void registerOscBindings(lua_State *L);
int pushOscMessage(lua_State *L, const struct OscMessage &message);

class LuaVM {
public:

	~LuaVM();

	bool loadFile(const std::string &path);
	bool loadBuffer(const std::string &code, const std::string &name, const std::string &directory);
	bool exec(const std::string &code, const std::string &name = "eval");
	bool reload();
	void clear();

	/// calls a global lua function if it exists
	bool call(const char *name, int nargs = 0, int nresults = 0);
	bool callNumbers(const char *name, const std::vector<double> &args);
	bool callString(const char *name, const std::string &value);
	bool callFiles(const char *name, const std::vector<std::string> &files);
	bool callAudioOut(SoundStream &stream);
	void pollHardware();
	bool hasFunction(const char *name);
	bool callOsc(const struct OscMessage &message);

	void syncGlobals();
	void connectStdout();

	lua_State *state() const { return L; }
	bool valid() const { return L != nullptr; }
	const std::string &error() const { return lastError; }
	const std::string &path() const { return currentPath; }
	bool setupPending = false;

private:

	bool initState();
	void closeState();
	bool runChunk(const char *chunkName);
	void fail(const std::string &message);
	void installPackagePath(const std::string &directory);
	int traceback();

	lua_State *L = nullptr;
	std::string currentPath;
	std::string currentDir;
	std::string lastError;
	std::vector<std::string> arguments;
};

} // namespace loaf
