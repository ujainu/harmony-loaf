#include "BindingsFile.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace loaf {

namespace {

const char *kBufferMetatable = "loaf.FileBuffer";
const char *kFileMetatable = "loaf.File";
const char *kDirectoryMetatable = "loaf.Directory";

struct LuaBuffer {
	std::string data;
};

struct LuaFile {
	std::string path;
};

struct LuaDirectory {
	std::string path;
};

std::string absolutePath(const std::string &path) {
	char resolved[PATH_MAX] = {0};
	if(realpath(path.c_str(), resolved) != nullptr) {
		return resolved;
	}
	return path;
}

std::string fileName(const std::string &path) {
	size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
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

std::string baseName(const std::string &path) {
	std::string name = fileName(path);
	size_t dot = name.find_last_of('.');
	if(dot == std::string::npos || dot == 0) {
		return name;
	}
	return name.substr(0, dot);
}

std::string extension(const std::string &path) {
	std::string name = fileName(path);
	size_t dot = name.find_last_of('.');
	if(dot == std::string::npos || dot == 0) {
		return "";
	}
	return name.substr(dot + 1);
}

bool statPath(const std::string &path, struct stat &info) {
	return stat(path.c_str(), &info) == 0;
}

bool isDirectoryPath(const std::string &path) {
	struct stat info {};
	return statPath(path, info) && S_ISDIR(info.st_mode);
}

bool isFilePath(const std::string &path) {
	struct stat info {};
	return statPath(path, info) && S_ISREG(info.st_mode);
}

void setMethod(lua_State *L, int table, const char *name, lua_CFunction function) {
	lua_pushcfunction(L, function);
	lua_setfield(L, table, name);
}

//--------------------------------------------------------------
LuaBuffer *checkBuffer(lua_State *L, int index) {
	return static_cast<LuaBuffer *>(luaL_checkudata(L, index, kBufferMetatable));
}

LuaFile *checkFile(lua_State *L, int index) {
	return static_cast<LuaFile *>(luaL_checkudata(L, index, kFileMetatable));
}

LuaDirectory *checkDirectory(lua_State *L, int index) {
	return static_cast<LuaDirectory *>(luaL_checkudata(L, index, kDirectoryMetatable));
}

//--------------------------------------------------------------
int buffer_new(lua_State *L) {
	size_t length = 0;
	const char *data = luaL_optlstring(L, 1, "", &length);
	std::string initial(data != nullptr ? data : "", length);
	LuaBuffer *buffer = static_cast<LuaBuffer *>(lua_newuserdatauv(L, sizeof(LuaBuffer), 0));
	new(buffer) LuaBuffer();
	buffer->data = initial;
	luaL_getmetatable(L, kBufferMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int buffer_gc(lua_State *L) {
	checkBuffer(L, 1)->~LuaBuffer();
	return 0;
}

int buffer_size(lua_State *L) {
	lua_pushinteger(L, (lua_Integer)checkBuffer(L, 1)->data.size());
	return 1;
}

int buffer_clear(lua_State *L) {
	checkBuffer(L, 1)->data.clear();
	return 0;
}

int buffer_set(lua_State *L) {
	size_t length = 0;
	const char *data = luaL_checklstring(L, 2, &length);
	checkBuffer(L, 1)->data.assign(data, length);
	return 0;
}

int buffer_append(lua_State *L) {
	size_t length = 0;
	const char *data = luaL_checklstring(L, 2, &length);
	checkBuffer(L, 1)->data.append(data, length);
	return 0;
}

int buffer_getData(lua_State *L) {
	LuaBuffer *buffer = checkBuffer(L, 1);
	lua_pushlstring(L, buffer->data.data(), buffer->data.size());
	return 1;
}

int buffer_getText(lua_State *L) {
	LuaBuffer *buffer = checkBuffer(L, 1);
	lua_pushlstring(L, buffer->data.data(), buffer->data.size());
	return 1;
}

int buffer_isEmpty(lua_State *L) {
	lua_pushboolean(L, checkBuffer(L, 1)->data.empty());
	return 1;
}

//--------------------------------------------------------------
int file_new(lua_State *L) {
	std::string path = luaL_optstring(L, 1, "");
	LuaFile *file = static_cast<LuaFile *>(lua_newuserdatauv(L, sizeof(LuaFile), 0));
	new(file) LuaFile();
	file->path = path;
	luaL_getmetatable(L, kFileMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int file_gc(lua_State *L) {
	checkFile(L, 1)->~LuaFile();
	return 0;
}

int file_path(lua_State *L) {
	lua_pushstring(L, checkFile(L, 1)->path.c_str());
	return 1;
}

int file_setPath(lua_State *L) {
	checkFile(L, 1)->path = luaL_checkstring(L, 2);
	return 0;
}

int file_getAbsolutePath(lua_State *L) {
	lua_pushstring(L, absolutePath(checkFile(L, 1)->path).c_str());
	return 1;
}

int file_getFileName(lua_State *L) {
	lua_pushstring(L, fileName(checkFile(L, 1)->path).c_str());
	return 1;
}

int file_getBaseName(lua_State *L) {
	lua_pushstring(L, baseName(checkFile(L, 1)->path).c_str());
	return 1;
}

int file_getExtension(lua_State *L) {
	lua_pushstring(L, extension(checkFile(L, 1)->path).c_str());
	return 1;
}

int file_getEnclosingDirectory(lua_State *L) {
	lua_pushstring(L, directoryOf(checkFile(L, 1)->path).c_str());
	return 1;
}

int file_exists(lua_State *L) {
	struct stat info {};
	lua_pushboolean(L, statPath(checkFile(L, 1)->path, info));
	return 1;
}

int file_isFile(lua_State *L) {
	lua_pushboolean(L, isFilePath(checkFile(L, 1)->path));
	return 1;
}

int file_isDirectory(lua_State *L) {
	lua_pushboolean(L, isDirectoryPath(checkFile(L, 1)->path));
	return 1;
}

int file_isLink(lua_State *L) {
	struct stat info {};
	bool link = lstat(checkFile(L, 1)->path.c_str(), &info) == 0 && S_ISLNK(info.st_mode);
	lua_pushboolean(L, link);
	return 1;
}

int file_isDevice(lua_State *L) {
	struct stat info {};
	bool device = statPath(checkFile(L, 1)->path, info) &&
	              (S_ISCHR(info.st_mode) || S_ISBLK(info.st_mode));
	lua_pushboolean(L, device);
	return 1;
}

int file_isHidden(lua_State *L) {
	std::string name = fileName(checkFile(L, 1)->path);
	lua_pushboolean(L, !name.empty() && name[0] == '.');
	return 1;
}

int file_canRead(lua_State *L) {
	lua_pushboolean(L, access(checkFile(L, 1)->path.c_str(), R_OK) == 0);
	return 1;
}

int file_canWrite(lua_State *L) {
	lua_pushboolean(L, access(checkFile(L, 1)->path.c_str(), W_OK) == 0);
	return 1;
}

int file_canExecute(lua_State *L) {
	lua_pushboolean(L, access(checkFile(L, 1)->path.c_str(), X_OK) == 0);
	return 1;
}

int file_getSize(lua_State *L) {
	struct stat info {};
	lua_pushnumber(L, statPath(checkFile(L, 1)->path, info) ? (double)info.st_size : 0.0);
	return 1;
}

int file_remove(lua_State *L) {
	lua_pushboolean(L, std::remove(checkFile(L, 1)->path.c_str()) == 0);
	return 1;
}

int file_copyTo(lua_State *L) {
	LuaFile *file = checkFile(L, 1);
	const char *target = luaL_checkstring(L, 2);
	std::ifstream source(file->path, std::ios::binary);
	std::ofstream destination(target, std::ios::binary);
	if(!source || !destination) {
		lua_pushboolean(L, 0);
		return 1;
	}
	destination << source.rdbuf();
	lua_pushboolean(L, destination.good());
	return 1;
}

int file_moveTo(lua_State *L) {
	LuaFile *file = checkFile(L, 1);
	const char *target = luaL_checkstring(L, 2);
	lua_pushboolean(L, std::rename(file->path.c_str(), target) == 0);
	return 1;
}

int file_readToString(lua_State *L) {
	LuaFile *file = checkFile(L, 1);
	std::ifstream stream(file->path, std::ios::binary);
	if(!stream) {
		lua_pushnil(L);
		return 1;
	}
	std::ostringstream text;
	text << stream.rdbuf();
	std::string content = text.str();
	lua_pushlstring(L, content.data(), content.size());
	return 1;
}

int file_writeString(lua_State *L) {
	LuaFile *file = checkFile(L, 1);
	size_t length = 0;
	const char *data = luaL_checklstring(L, 2, &length);
	std::ofstream stream(file->path, std::ios::binary | std::ios::trunc);
	if(!stream) {
		lua_pushboolean(L, 0);
		return 1;
	}
	stream.write(data, (std::streamsize)length);
	lua_pushboolean(L, stream.good());
	return 1;
}

//--------------------------------------------------------------
int directory_new(lua_State *L) {
	std::string path = luaL_optstring(L, 1, ".");
	LuaDirectory *directory = static_cast<LuaDirectory *>(lua_newuserdatauv(L, sizeof(LuaDirectory), 0));
	new(directory) LuaDirectory();
	directory->path = path;
	luaL_getmetatable(L, kDirectoryMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int directory_gc(lua_State *L) {
	checkDirectory(L, 1)->~LuaDirectory();
	return 0;
}

int directory_path(lua_State *L) {
	lua_pushstring(L, checkDirectory(L, 1)->path.c_str());
	return 1;
}

int directory_getAbsolutePath(lua_State *L) {
	lua_pushstring(L, absolutePath(checkDirectory(L, 1)->path).c_str());
	return 1;
}

int directory_exists(lua_State *L) {
	struct stat info {};
	lua_pushboolean(L, statPath(checkDirectory(L, 1)->path, info));
	return 1;
}

int directory_isDirectory(lua_State *L) {
	lua_pushboolean(L, isDirectoryPath(checkDirectory(L, 1)->path));
	return 1;
}

int directory_create(lua_State *L) {
	LuaDirectory *directory = checkDirectory(L, 1);
	bool recursive = lua_gettop(L) >= 2 && lua_toboolean(L, 2) != 0;
	if(!recursive) {
		lua_pushboolean(L, mkdir(directory->path.c_str(), 0755) == 0);
		return 1;
	}
	std::string current;
	std::stringstream stream(directory->path);
	std::string part;
	if(!directory->path.empty() && directory->path[0] == '/') {
		current = "/";
	}
	while(std::getline(stream, part, '/')) {
		if(part.empty()) {
			continue;
		}
		current += part + "/";
		mkdir(current.c_str(), 0755);
	}
	lua_pushboolean(L, isDirectoryPath(directory->path));
	return 1;
}

int directory_listDir(lua_State *L) {
	LuaDirectory *directory = checkDirectory(L, 1);
	DIR *handle = opendir(directory->path.c_str());
	if(handle == nullptr) {
		lua_pushnil(L);
		return 1;
	}
	lua_newtable(L);
	int index = 1;
	struct dirent *entry = nullptr;
	while((entry = readdir(handle)) != nullptr) {
		if(std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		LuaFile *file = static_cast<LuaFile *>(lua_newuserdatauv(L, sizeof(LuaFile), 0));
		new(file) LuaFile();
		file->path = directory->path + "/" + entry->d_name;
		luaL_getmetatable(L, kFileMetatable);
		lua_setmetatable(L, -2);
		lua_rawseti(L, -2, index++);
	}
	closedir(handle);
	return 1;
}

//--------------------------------------------------------------
int filepath_join(lua_State *L) {
	std::string a = luaL_checkstring(L, 1);
	std::string b = luaL_checkstring(L, 2);
	if(a.empty()) {
		lua_pushstring(L, b.c_str());
	}
	else if(b.empty()) {
		lua_pushstring(L, a.c_str());
	}
	else if(a.back() == '/') {
		lua_pushstring(L, (a + b).c_str());
	}
	else {
		lua_pushstring(L, (a + "/" + b).c_str());
	}
	return 1;
}

int filepath_getCurrentWorkingDirectory(lua_State *L) {
	char buffer[PATH_MAX] = {0};
	lua_pushstring(L, getcwd(buffer, sizeof(buffer)) != nullptr ? buffer : "");
	return 1;
}

int filepath_getFileName(lua_State *L) {
	lua_pushstring(L, fileName(luaL_checkstring(L, 1)).c_str());
	return 1;
}

int filepath_getBaseName(lua_State *L) {
	lua_pushstring(L, baseName(luaL_checkstring(L, 1)).c_str());
	return 1;
}

int filepath_getFileExt(lua_State *L) {
	lua_pushstring(L, extension(luaL_checkstring(L, 1)).c_str());
	return 1;
}

int filepath_getEnclosingDirectory(lua_State *L) {
	lua_pushstring(L, directoryOf(luaL_checkstring(L, 1)).c_str());
	return 1;
}

int filepath_getAbsolutePath(lua_State *L) {
	lua_pushstring(L, absolutePath(luaL_checkstring(L, 1)).c_str());
	return 1;
}

//--------------------------------------------------------------
void registerBuffer(lua_State *L) {
	luaL_newmetatable(L, kBufferMetatable);
	lua_pushcfunction(L, buffer_gc);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, buffer_getData);
	lua_setfield(L, -2, "__tostring");
	lua_newtable(L);
	setMethod(L, lua_gettop(L), "size", buffer_size);
	setMethod(L, lua_gettop(L), "clear", buffer_clear);
	setMethod(L, lua_gettop(L), "set", buffer_set);
	setMethod(L, lua_gettop(L), "append", buffer_append);
	setMethod(L, lua_gettop(L), "getData", buffer_getData);
	setMethod(L, lua_gettop(L), "getText", buffer_getText);
	setMethod(L, lua_gettop(L), "isEmpty", buffer_isEmpty);
	lua_setfield(L, -2, "__methods");
	lua_getfield(L, -1, "__methods");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

void registerFile(lua_State *L) {
	luaL_newmetatable(L, kFileMetatable);
	lua_pushcfunction(L, file_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setMethod(L, lua_gettop(L), "path", file_path);
	setMethod(L, lua_gettop(L), "setPath", file_setPath);
	setMethod(L, lua_gettop(L), "getAbsolutePath", file_getAbsolutePath);
	setMethod(L, lua_gettop(L), "getFileName", file_getFileName);
	setMethod(L, lua_gettop(L), "getBaseName", file_getBaseName);
	setMethod(L, lua_gettop(L), "getExtension", file_getExtension);
	setMethod(L, lua_gettop(L), "getEnclosingDirectory", file_getEnclosingDirectory);
	setMethod(L, lua_gettop(L), "exists", file_exists);
	setMethod(L, lua_gettop(L), "isFile", file_isFile);
	setMethod(L, lua_gettop(L), "isDirectory", file_isDirectory);
	setMethod(L, lua_gettop(L), "isLink", file_isLink);
	setMethod(L, lua_gettop(L), "isDevice", file_isDevice);
	setMethod(L, lua_gettop(L), "isHidden", file_isHidden);
	setMethod(L, lua_gettop(L), "canRead", file_canRead);
	setMethod(L, lua_gettop(L), "canWrite", file_canWrite);
	setMethod(L, lua_gettop(L), "canExecute", file_canExecute);
	setMethod(L, lua_gettop(L), "getSize", file_getSize);
	setMethod(L, lua_gettop(L), "remove", file_remove);
	setMethod(L, lua_gettop(L), "copyTo", file_copyTo);
	setMethod(L, lua_gettop(L), "moveTo", file_moveTo);
	setMethod(L, lua_gettop(L), "readToString", file_readToString);
	setMethod(L, lua_gettop(L), "writeString", file_writeString);
	lua_setfield(L, -2, "__methods");
	lua_getfield(L, -1, "__methods");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

void registerDirectory(lua_State *L) {
	luaL_newmetatable(L, kDirectoryMetatable);
	lua_pushcfunction(L, directory_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setMethod(L, lua_gettop(L), "path", directory_path);
	setMethod(L, lua_gettop(L), "getAbsolutePath", directory_getAbsolutePath);
	setMethod(L, lua_gettop(L), "exists", directory_exists);
	setMethod(L, lua_gettop(L), "isDirectory", directory_isDirectory);
	setMethod(L, lua_gettop(L), "create", directory_create);
	setMethod(L, lua_gettop(L), "listDir", directory_listDir);
	setMethod(L, lua_gettop(L), "getFiles", directory_listDir);
	lua_setfield(L, -2, "__methods");
	lua_getfield(L, -1, "__methods");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

} // namespace

//--------------------------------------------------------------
void registerFileBindings(lua_State *L, int ofTable) {
	registerBuffer(L);
	registerFile(L);
	registerDirectory(L);

	lua_pushcfunction(L, buffer_new);
	lua_setfield(L, ofTable, "Buffer");
	lua_pushcfunction(L, file_new);
	lua_setfield(L, ofTable, "File");
	lua_pushcfunction(L, directory_new);
	lua_setfield(L, ofTable, "Directory");

	lua_newtable(L);
	int filePath = lua_gettop(L);
	setMethod(L, filePath, "join", filepath_join);
	setMethod(L, filePath, "getCurrentWorkingDirectory", filepath_getCurrentWorkingDirectory);
	setMethod(L, filePath, "getFileName", filepath_getFileName);
	setMethod(L, filePath, "getBaseName", filepath_getBaseName);
	setMethod(L, filePath, "getFileExt", filepath_getFileExt);
	setMethod(L, filePath, "getEnclosingDirectory", filepath_getEnclosingDirectory);
	setMethod(L, filePath, "getAbsolutePath", filepath_getAbsolutePath);
	lua_setfield(L, ofTable, "FilePath");
}

//--------------------------------------------------------------
bool getLuaBuffer(lua_State *L, int index, std::string &data) {
	LuaBuffer *buffer = static_cast<LuaBuffer *>(luaL_testudata(L, index, kBufferMetatable));
	if(buffer == nullptr) {
		return false;
	}
	data = buffer->data;
	return true;
}

} // namespace loaf
