#include "Environment.h"
#include "BindingsFile.h"
#include "LuaVM.h"
#include "Osc.h"
#include "FrameServer.h"
#include "Runtime.h"

#include <lua.hpp>

#include <unistd.h>

#include <cmath>
#include <map>
#include <string>

#define LOG_TAG "loafcore"
#include "../Log.h"

namespace loaf {

namespace {

const char *kMessageMetatable = "loaf.OscMessage";
const char *kBundleMetatable = "loaf.OscBundle";
const char *kSenderMetatable = "loaf.OscSender";
const char *kReceiverMetatable = "loaf.OscReceiver";
const char *kSyncMetatable = "loaf.OscParameterSync";

struct LuaParamSync {
	std::map<std::string, OscArg> params;
	int callback = LUA_NOREF;
};

struct LuaMessage {
	OscMessage message;
};

struct LuaBundle {
	OscBundle bundle;
};

struct LuaSender {
	std::string host = "127.0.0.1";
	int port = 8880;
	int fd = -1;
};

struct LuaReceiver {
	Osc *osc = nullptr;
	int pendingPort = 0;
};

float number(lua_State *L, int index, float fallback = 0.0f) {
	return lua_isnumber(L, index) ? (float)lua_tonumber(L, index) : fallback;
}

bool boolean(lua_State *L, int index, bool fallback = false) {
	if(lua_isboolean(L, index)) {
		return lua_toboolean(L, index) != 0;
	}
	if(lua_isnumber(L, index)) {
		return lua_tonumber(L, index) != 0.0;
	}
	return fallback;
}

LuaMessage *checkMessage(lua_State *L, int index) {
	return static_cast<LuaMessage *>(luaL_checkudata(L, index, kMessageMetatable));
}

LuaBundle *checkBundle(lua_State *L, int index) {
	return static_cast<LuaBundle *>(luaL_checkudata(L, index, kBundleMetatable));
}

LuaSender *checkSender(lua_State *L, int index) {
	return static_cast<LuaSender *>(luaL_checkudata(L, index, kSenderMetatable));
}

LuaReceiver *checkReceiver(lua_State *L, int index) {
	return static_cast<LuaReceiver *>(luaL_checkudata(L, index, kReceiverMetatable));
}

LuaParamSync *checkSync(lua_State *L, int index) {
	return static_cast<LuaParamSync *>(luaL_checkudata(L, index, kSyncMetatable));
}

LuaMessage *newMessage(lua_State *L) {
	LuaMessage *message = static_cast<LuaMessage *>(lua_newuserdatauv(L, sizeof(LuaMessage), 0));
	new(message) LuaMessage();
	luaL_getmetatable(L, kMessageMetatable);
	lua_setmetatable(L, -2);
	return message;
}

OscArg *argAt(LuaMessage *message, int index) {
	if(index < 0 || index >= (int)message->message.args.size()) {
		return nullptr;
	}
	return &message->message.args[index];
}

//--------------------------------------------------------------
int msg_gc(lua_State *L) {
	LuaMessage *message = checkMessage(L, 1);
	message->~LuaMessage();
	return 0;
}

int msg_new(lua_State *L) {
	LuaMessage *message = newMessage(L);
	if(lua_gettop(L) >= 1 && lua_isstring(L, 1)) {
		message->message.address = lua_tostring(L, 1);
	}
	return 1;
}

int msg_setAddress(lua_State *L) {
	checkMessage(L, 1)->message.address = luaL_checkstring(L, 2);
	return 0;
}

int msg_getAddress(lua_State *L) {
	lua_pushstring(L, checkMessage(L, 1)->message.address.c_str());
	return 1;
}

int msg_getNumArgs(lua_State *L) {
	lua_pushinteger(L, (lua_Integer)checkMessage(L, 1)->message.args.size());
	return 1;
}

int msg_getTypeString(lua_State *L) {
	lua_pushstring(L, checkMessage(L, 1)->message.typeString().c_str());
	return 1;
}

int msg_getArgType(lua_State *L) {
	OscArg *arg = argAt(checkMessage(L, 1), (int)number(L, 2, 0.0f));
	if(arg == nullptr) {
		lua_pushnil(L);
		return 1;
	}
	char tag[2] = {(char)arg->type, '\0'};
	lua_pushstring(L, tag);
	return 1;
}

int msg_getArgAsInt32(lua_State *L) {
	OscArg *arg = argAt(checkMessage(L, 1), (int)number(L, 2, 0.0f));
	if(arg == nullptr) {
		lua_pushinteger(L, 0);
		return 1;
	}
	if(arg->type == OscType::Float32 || arg->type == OscType::Double) {
		lua_pushinteger(L, (lua_Integer)std::lround(arg->d));
	}
	else {
		lua_pushinteger(L, (lua_Integer)arg->i);
	}
	return 1;
}

int msg_getArgAsInt64(lua_State *L) {
	return msg_getArgAsInt32(L);
}

int msg_getArgAsFloat(lua_State *L) {
	OscArg *arg = argAt(checkMessage(L, 1), (int)number(L, 2, 0.0f));
	if(arg == nullptr) {
		lua_pushnumber(L, 0.0);
		return 1;
	}
	if(arg->type == OscType::Float32 || arg->type == OscType::Double) {
		lua_pushnumber(L, arg->d);
	}
	else {
		lua_pushnumber(L, (double)arg->i);
	}
	return 1;
}

int msg_getArgAsDouble(lua_State *L) {
	return msg_getArgAsFloat(L);
}

int msg_getArgAsString(lua_State *L) {
	OscArg *arg = argAt(checkMessage(L, 1), (int)number(L, 2, 0.0f));
	if(arg == nullptr) {
		lua_pushstring(L, "");
	}
	else {
		lua_pushlstring(L, arg->s.data(), arg->s.size());
	}
	return 1;
}

int msg_getArgAsSymbol(lua_State *L) {
	return msg_getArgAsString(L);
}

int msg_getArgAsChar(lua_State *L) {
	OscArg *arg = argAt(checkMessage(L, 1), (int)number(L, 2, 0.0f));
	char text[2] = {(char)(arg != nullptr ? arg->i : 0), '\0'};
	lua_pushstring(L, text);
	return 1;
}

int msg_getArgAsBool(lua_State *L) {
	OscArg *arg = argAt(checkMessage(L, 1), (int)number(L, 2, 0.0f));
	bool value = arg != nullptr &&
	             (arg->type == OscType::True || (arg->type != OscType::False && arg->i != 0));
	lua_pushboolean(L, value);
	return 1;
}

int msg_getArgAsBlob(lua_State *L) {
	return msg_getArgAsString(L);
}

int msg_getArgAsTimetag(lua_State *L) {
	return msg_getArgAsInt32(L);
}

int msg_getArgAsMidi(lua_State *L) {
	return msg_getArgAsInt32(L);
}

int msg_getArgAsRgba(lua_State *L) {
	return msg_getArgAsInt32(L);
}

int msg_getArgAsTrigger(lua_State *L) {
	OscArg *arg = argAt(checkMessage(L, 1), (int)number(L, 2, 0.0f));
	lua_pushboolean(L, arg != nullptr && arg->type == OscType::Trigger);
	return 1;
}

int msg_getArgAsNone(lua_State *L) {
	OscArg *arg = argAt(checkMessage(L, 1), (int)number(L, 2, 0.0f));
	lua_pushboolean(L, arg != nullptr && arg->type == OscType::None);
	return 1;
}

int msg_addInt32(lua_State *L) {
	checkMessage(L, 1)->message.args.push_back(OscArg::int32((int32_t)number(L, 2)));
	return 0;
}

int msg_addInt64(lua_State *L) {
	checkMessage(L, 1)->message.args.push_back(OscArg::int64((int64_t)number(L, 2)));
	return 0;
}

int msg_addFloat(lua_State *L) {
	checkMessage(L, 1)->message.args.push_back(OscArg::float32(number(L, 2)));
	return 0;
}

int msg_addDouble(lua_State *L) {
	checkMessage(L, 1)->message.args.push_back(OscArg::double64(number(L, 2)));
	return 0;
}

int msg_addString(lua_State *L) {
	checkMessage(L, 1)->message.args.push_back(OscArg::string(luaL_checkstring(L, 2)));
	return 0;
}

int msg_addSymbol(lua_State *L) {
	checkMessage(L, 1)->message.args.push_back(OscArg::symbol(luaL_checkstring(L, 2)));
	return 0;
}

int msg_addChar(lua_State *L) {
	const char *text = luaL_checkstring(L, 2);
	checkMessage(L, 1)->message.args.push_back(OscArg::character(text[0]));
	return 0;
}

int msg_addBool(lua_State *L) {
	checkMessage(L, 1)->message.args.push_back(OscArg::boolean(boolean(L, 2)));
	return 0;
}

int msg_addBlob(lua_State *L) {
	std::string data;
	if(getLuaBuffer(L, 2, data)) {
		checkMessage(L, 1)->message.args.push_back(OscArg::blob(data));
		return 0;
	}
	size_t length = 0;
	const char *text = luaL_checklstring(L, 2, &length);
	checkMessage(L, 1)->message.args.push_back(OscArg::blob(std::string(text != nullptr ? text : "", length)));
	return 0;
}

int msg_addTimetag(lua_State *L) {
	checkMessage(L, 1)->message.args.push_back(OscArg::timetag((uint64_t)number(L, 2)));
	return 0;
}

int msg_addMidi(lua_State *L) {
	checkMessage(L, 1)->message.args.push_back(OscArg::midi((uint32_t)number(L, 2)));
	return 0;
}

int msg_addRgba(lua_State *L) {
	checkMessage(L, 1)->message.args.push_back(OscArg::rgba((uint32_t)number(L, 2)));
	return 0;
}

int msg_addTrigger(lua_State *L) {
	checkMessage(L, 1)->message.args.push_back(OscArg::trigger());
	return 0;
}

int msg_addNone(lua_State *L) {
	checkMessage(L, 1)->message.args.push_back(OscArg::none());
	return 0;
}

int msg_clear(lua_State *L) {
	LuaMessage *message = checkMessage(L, 1);
	message->message.address.clear();
	message->message.args.clear();
	return 0;
}

int msg_copy(lua_State *L) {
	LuaMessage *source = checkMessage(L, 1);
	LuaMessage *target = newMessage(L);
	target->message = source->message;
	return 1;
}

int msg_setRemoteEndpoint(lua_State *L) {
	LuaMessage *message = checkMessage(L, 1);
	message->message.remoteHost = luaL_checkstring(L, 2);
	message->message.remotePort = (int)number(L, 3);
	return 0;
}

int msg_getRemoteHost(lua_State *L) {
	lua_pushstring(L, checkMessage(L, 1)->message.remoteHost.c_str());
	return 1;
}

int msg_getRemotePort(lua_State *L) {
	lua_pushinteger(L, checkMessage(L, 1)->message.remotePort);
	return 1;
}

int msg_toString(lua_State *L) {
	lua_pushstring(L, checkMessage(L, 1)->message.toString().c_str());
	return 1;
}

int msg_index(lua_State *L) {
	LuaMessage *message = checkMessage(L, 1);
	const char *key = lua_tostring(L, 2);
	if(key == nullptr) {
		return 0;
	}
	std::string name(key);
	if(name == "address") {
		lua_pushstring(L, message->message.address.c_str());
	}
	else if(name == "numArgs") {
		lua_pushinteger(L, (lua_Integer)message->message.args.size());
	}
	else if(name == "typeString") {
		lua_pushstring(L, message->message.typeString().c_str());
	}
	else if(name == "remoteHost") {
		lua_pushstring(L, message->message.remoteHost.c_str());
	}
	else if(name == "remotePort") {
		lua_pushinteger(L, message->message.remotePort);
	}
	else {
		luaL_getmetatable(L, kMessageMetatable);
		lua_getfield(L, -1, "__methods");
		lua_getfield(L, -1, key);
		lua_remove(L, -2);
		lua_remove(L, -2);
	}
	return 1;
}

int bundle_index(lua_State *L) {
	LuaBundle *bundle = checkBundle(L, 1);
	const char *key = lua_tostring(L, 2);
	if(key == nullptr) {
		return 0;
	}
	std::string name(key);
	if(name == "messageCount") {
		lua_pushinteger(L, (lua_Integer)bundle->bundle.messages.size());
		return 1;
	}
	if(name == "bundleCount") {
		lua_pushinteger(L, (lua_Integer)bundle->bundle.bundles.size());
		return 1;
	}
	luaL_getmetatable(L, kBundleMetatable);
	lua_getfield(L, -1, "__methods");
	lua_getfield(L, -1, key);
	lua_remove(L, -2);
	lua_remove(L, -2);
	return 1;
}

int sender_index(lua_State *L) {
	LuaSender *sender = checkSender(L, 1);
	const char *key = lua_tostring(L, 2);
	if(key == nullptr) {
		return 0;
	}
	std::string name(key);
	if(name == "host") {
		lua_pushstring(L, sender->host.c_str());
		return 1;
	}
	if(name == "port") {
		lua_pushinteger(L, sender->port);
		return 1;
	}
	luaL_getmetatable(L, kSenderMetatable);
	lua_getfield(L, -1, "__methods");
	lua_getfield(L, -1, key);
	lua_remove(L, -2);
	lua_remove(L, -2);
	return 1;
}

int receiver_index(lua_State *L) {
	LuaReceiver *receiver = checkReceiver(L, 1);
	const char *key = lua_tostring(L, 2);
	if(key == nullptr) {
		return 0;
	}
	std::string name(key);
	if(name == "port") {
		lua_pushinteger(L, receiver->osc->listenPort());
		return 1;
	}
	if(name == "listening") {
		lua_pushboolean(L, receiver->osc->isListening());
		return 1;
	}
	luaL_getmetatable(L, kReceiverMetatable);
	lua_getfield(L, -1, "__methods");
	lua_getfield(L, -1, key);
	lua_remove(L, -2);
	lua_remove(L, -2);
	return 1;
}

int msg_newindex(lua_State *L) {
	LuaMessage *message = checkMessage(L, 1);
	const char *key = lua_tostring(L, 2);
	if(key == nullptr) {
		return 0;
	}
	if(std::string(key) == "address") {
		message->message.address = luaL_checkstring(L, 3);
	}
	else if(std::string(key) == "remoteHost") {
		message->message.remoteHost = luaL_checkstring(L, 3);
	}
	else if(std::string(key) == "remotePort") {
		message->message.remotePort = (int)number(L, 3);
	}
	return 0;
}

//--------------------------------------------------------------
int bundle_new(lua_State *L) {
	LuaBundle *bundle = static_cast<LuaBundle *>(lua_newuserdatauv(L, sizeof(LuaBundle), 0));
	new(bundle) LuaBundle();
	luaL_getmetatable(L, kBundleMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int bundle_gc(lua_State *L) {
	checkBundle(L, 1)->~LuaBundle();
	return 0;
}

int bundle_addMessage(lua_State *L) {
	LuaBundle *bundle = checkBundle(L, 1);
	LuaMessage *message = checkMessage(L, 2);
	bundle->bundle.messages.push_back(message->message);
	return 0;
}

int bundle_addBundle(lua_State *L) {
	LuaBundle *bundle = checkBundle(L, 1);
	LuaBundle *child = checkBundle(L, 2);
	bundle->bundle.bundles.push_back(child->bundle);
	return 0;
}

int bundle_clear(lua_State *L) {
	LuaBundle *bundle = checkBundle(L, 1);
	bundle->bundle.messages.clear();
	bundle->bundle.bundles.clear();
	return 0;
}

int bundle_getMessageCount(lua_State *L) {
	lua_pushinteger(L, (lua_Integer)checkBundle(L, 1)->bundle.messages.size());
	return 1;
}

int bundle_getBundleCount(lua_State *L) {
	lua_pushinteger(L, (lua_Integer)checkBundle(L, 1)->bundle.bundles.size());
	return 1;
}

int bundle_getMessageAt(lua_State *L) {
	LuaBundle *bundle = checkBundle(L, 1);
	int index = (int)number(L, 2, 0.0f);
	if(index < 0 || index >= (int)bundle->bundle.messages.size()) {
		lua_pushnil(L);
		return 1;
	}
	LuaMessage *message = newMessage(L);
	message->message = bundle->bundle.messages[index];
	return 1;
}

int bundle_getBundleAt(lua_State *L) {
	LuaBundle *bundle = checkBundle(L, 1);
	int index = (int)number(L, 2, 0.0f);
	if(index < 0 || index >= (int)bundle->bundle.bundles.size()) {
		lua_pushnil(L);
		return 1;
	}
	LuaBundle *child = static_cast<LuaBundle *>(lua_newuserdatauv(L, sizeof(LuaBundle), 0));
	new(child) LuaBundle();
	child->bundle = bundle->bundle.bundles[index];
	luaL_getmetatable(L, kBundleMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int bundle_toString(lua_State *L) {
	LuaBundle *bundle = checkBundle(L, 1);
	std::string text = "bundle[" + std::to_string(bundle->bundle.messages.size()) + "]:";
	for(const auto &message : bundle->bundle.messages) {
		text += "\n  " + message.toString();
	}
	lua_pushstring(L, text.c_str());
	return 1;
}

//--------------------------------------------------------------
int sender_new(lua_State *L) {
	LuaSender *sender = static_cast<LuaSender *>(lua_newuserdatauv(L, sizeof(LuaSender), 0));
	new(sender) LuaSender();
	sender->fd = Osc::openSocket();
	luaL_getmetatable(L, kSenderMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int sender_gc(lua_State *L) {
	LuaSender *sender = checkSender(L, 1);
	if(sender->fd >= 0) {
		close(sender->fd);
		sender->fd = -1;
	}
	return 0;
}

int sender_setup(lua_State *L) {
	LuaSender *sender = checkSender(L, 1);
	sender->host = luaL_optstring(L, 2, "127.0.0.1");
	sender->port = (int)number(L, 3, 8880.0f);
	return 0;
}

int sender_sendMessage(lua_State *L) {
	LuaSender *sender = checkSender(L, 1);
	LuaMessage *message = checkMessage(L, 2);
	Osc::sendTo(sender->fd, sender->host, sender->port, Osc::encode(message->message));
	return 0;
}

int sender_sendBundle(lua_State *L) {
	LuaSender *sender = checkSender(L, 1);
	LuaBundle *bundle = checkBundle(L, 2);
	Osc::sendTo(sender->fd, sender->host, sender->port, Osc::encode(bundle->bundle));
	return 0;
}

int sender_getHost(lua_State *L) {
	lua_pushstring(L, checkSender(L, 1)->host.c_str());
	return 1;
}

int sender_getPort(lua_State *L) {
	lua_pushinteger(L, checkSender(L, 1)->port);
	return 1;
}

//--------------------------------------------------------------
int receiver_new(lua_State *L) {
	LuaReceiver *receiver = static_cast<LuaReceiver *>(lua_newuserdatauv(L, sizeof(LuaReceiver), 0));
	new(receiver) LuaReceiver();
	receiver->osc = new Osc();
	luaL_getmetatable(L, kReceiverMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int receiver_gc(lua_State *L) {
	LuaReceiver *receiver = checkReceiver(L, 1);
	if(receiver->osc != nullptr) {
		receiver->osc->stopListening();
		delete receiver->osc;
		receiver->osc = nullptr;
	}
	return 0;
}

int receiver_setup(lua_State *L) {
	LuaReceiver *receiver = checkReceiver(L, 1);
	int port = 0;
	bool start = true;
	if(lua_istable(L, 2)) {
		lua_getfield(L, 2, "port");
		port = (int)number(L, -1, 0.0f);
		lua_pop(L, 1);
		lua_getfield(L, 2, "start");
		if(lua_isboolean(L, -1)) {
			start = lua_toboolean(L, -1) != 0;
		}
		lua_pop(L, 1);
	}
	else {
		port = (int)number(L, 2, 0.0f);
	}
	receiver->pendingPort = port;
	if(start) {
		lua_pushboolean(L, receiver->osc->startListening(port));
	}
	else {
		lua_pushboolean(L, 1);
	}
	return 1;
}

int receiver_start(lua_State *L) {
	LuaReceiver *receiver = checkReceiver(L, 1);
	lua_pushboolean(L, receiver->osc->startListening(receiver->pendingPort));
	return 1;
}

int receiver_stop(lua_State *L) {
	checkReceiver(L, 1)->osc->stopListening();
	return 0;
}

int receiver_hasWaitingMessages(lua_State *L) {
	lua_pushboolean(L, checkReceiver(L, 1)->osc->hasWaiting());
	return 1;
}

int receiver_getNextMessage(lua_State *L) {
	LuaReceiver *receiver = checkReceiver(L, 1);
	OscMessage message;
	if(lua_gettop(L) >= 2 && lua_isuserdata(L, 2)) {
		LuaMessage *target = checkMessage(L, 2);
		if(!receiver->osc->poll(target->message)) {
			lua_pushboolean(L, 0);
			return 1;
		}
		lua_pushboolean(L, 1);
		return 1;
	}
	if(!receiver->osc->poll(message)) {
		lua_pushnil(L);
		return 1;
	}
	LuaMessage *result = newMessage(L);
	result->message = message;
	return 1;
}

int receiver_toString(lua_State *L) {
	LuaReceiver *receiver = checkReceiver(L, 1);
	std::string text = "osc.Receiver(port=" + std::to_string(receiver->osc->listenPort()) +
	                   (receiver->osc->isListening() ? ", listening)" : ", stopped)");
	lua_pushstring(L, text.c_str());
	return 1;
}

int sender_toString(lua_State *L) {
	LuaSender *sender = checkSender(L, 1);
	std::string text = "osc.Sender(" + sender->host + ":" + std::to_string(sender->port) + ")";
	lua_pushstring(L, text.c_str());
	return 1;
}

int receiver_isListening(lua_State *L) {
	lua_pushboolean(L, checkReceiver(L, 1)->osc->isListening());
	return 1;
}

int receiver_getPort(lua_State *L) {
	lua_pushinteger(L, checkReceiver(L, 1)->osc->listenPort());
	return 1;
}

int receiver_close(lua_State *L) {
	checkReceiver(L, 1)->osc->stopListening();
	return 0;
}

//--------------------------------------------------------------
int l_setVerbose(lua_State *L) {
	env().verbose = lua_toboolean(L, 1) != 0;
	return 0;
}

int l_isVerbose(lua_State *L) {
	lua_pushboolean(L, env().verbose);
	return 1;
}

int l_setUseDefaultKeys(lua_State *L) {
	env().defaultKeys = lua_toboolean(L, 1) != 0;
	return 0;
}

int l_useDefaultKeys(lua_State *L) {
	lua_pushboolean(L, env().defaultKeys);
	return 1;
}

int l_isBundled(lua_State *L) {
	lua_pushboolean(L, 0);
	return 1;
}

int l_getStartDirectory(lua_State *L) {
	lua_pushstring(L, env().scriptDir.c_str());
	return 1;
}

int l_getCurrentDirectory(lua_State *L) {
	char buffer[4096] = {0};
	if(getcwd(buffer, sizeof(buffer)) != nullptr) {
		lua_pushstring(L, buffer);
	}
	else {
		lua_pushstring(L, env().scriptDir.c_str());
	}
	return 1;
}

int l_setCurrentDirectory(lua_State *L) {
	lua_pushboolean(L, chdir(luaL_checkstring(L, 1)) == 0);
	return 1;
}

int l_getHostname(lua_State *L) {
	char buffer[256] = {0};
	if(gethostname(buffer, sizeof(buffer)) == 0) {
		lua_pushstring(L, buffer);
	}
	else {
		lua_pushstring(L, "");
	}
	return 1;
}

int l_setResolution(lua_State *L) {
	int width = (int)number(L, 1, 0.0f);
	int height = (int)number(L, 2, 0.0f);
	if(env().renderer == nullptr || width <= 0 || height <= 0) {
		lua_pushboolean(L, 0);
		return 1;
	}
	env().renderer->setDesignResolution(width, height);
	lua_pushboolean(L, 1);
	return 1;
}

int l_clearResolution(lua_State *L) {
	if(env().renderer != nullptr) {
		env().renderer->clearDesignResolution();
	}
	return 0;
}

int l_getResolution(lua_State *L) {
	lua_pushinteger(L, env().renderer != nullptr ? env().renderer->designWidth() : 0);
	lua_pushinteger(L, env().renderer != nullptr ? env().renderer->designHeight() : 0);
	return 2;
}

int l_reloadScript(lua_State *L) {
	env().runtime->queueReload();
	return 0;
}

int l_loadScript(lua_State *L) {
	const char *path = luaL_checkstring(L, 1);
	env().runtime->loadScript(path);
	return 0;
}

int l_clearScript(lua_State *L) {
	env().runtime->queueClear();
	return 0;
}

//--------------------------------------------------------------
int l_setListenPort(lua_State *L) {
	int port = (int)number(L, 1, 9990.0f);
	if(port <= 0 || port > 65535) {
		lua_pushboolean(L, 0);
		return 1;
	}
	env().osc->setListenPort(port);
	lua_pushboolean(L, 1);
	return 1;
}

int l_getListenPort(lua_State *L) {
	lua_pushinteger(L, env().osc->listenPort());
	return 1;
}

int l_startListening(lua_State *L) {
	lua_pushboolean(L, env().osc->startListening(env().osc->listenPort()));
	return 1;
}

int l_stopListening(lua_State *L) {
	env().osc->stopListening();
	return 0;
}

int l_isListening(lua_State *L) {
	lua_pushboolean(L, env().osc->isListening());
	return 1;
}

int l_setSendHost(lua_State *L) {
	env().osc->setSendHost(luaL_checkstring(L, 1));
	return 0;
}

int l_getSendHost(lua_State *L) {
	lua_pushstring(L, env().osc->sendHost().c_str());
	return 1;
}

int l_setSendPort(lua_State *L) {
	int port = (int)number(L, 1, 8880.0f);
	if(port <= 0 || port > 65535) {
		lua_pushboolean(L, 0);
		return 1;
	}
	env().osc->setSendPort(port);
	lua_pushboolean(L, 1);
	return 1;
}

int l_getSendPort(lua_State *L) {
	lua_pushinteger(L, env().osc->sendPort());
	return 1;
}

int l_sendFloat(lua_State *L) {
	env().osc->sendFloat(luaL_checkstring(L, 1), number(L, 2));
	return 0;
}

int l_sendInt(lua_State *L) {
	env().osc->sendInt(luaL_checkstring(L, 1), (int32_t)number(L, 2));
	return 0;
}

int l_sendString(lua_State *L) {
	env().osc->sendString(luaL_checkstring(L, 1), luaL_checkstring(L, 2));
	return 0;
}

int l_sendBang(lua_State *L) {
	env().osc->sendBang(luaL_checkstring(L, 1));
	return 0;
}

int l_sendMessage(lua_State *L) {
	if(lua_isuserdata(L, 1)) {
		env().osc->sendMessage(checkMessage(L, 1)->message);
	}
	else if(lua_isstring(L, 1)) {
		env().osc->sendBang(luaL_checkstring(L, 1));
	}
	return 0;
}

int l_sendBundle(lua_State *L) {
	env().osc->sendBundle(checkBundle(L, 1)->bundle);
	return 0;
}

// Syphon has no HarmonyOS equivalent, so the same calls publish the canvas over
// the loaf frame service (TCP) instead.
int syphonPort = 9999;
int syphonFps = 10;
int syphonScale = 2;

int l_startSyphon(lua_State *L) {
	const char *name = luaL_optstring(L, 1, nullptr);
	std::string serverName = name != nullptr ? name : FrameServer::instance().name();
	lua_pushboolean(L, FrameServer::instance().start(serverName, syphonPort, syphonFps, syphonScale));
	return 1;
}

int l_stopSyphon(lua_State *L) {
	FrameServer::instance().stop();
	return 0;
}

int l_isSyphonPublishing(lua_State *L) {
	lua_pushboolean(L, FrameServer::instance().isPublishing());
	return 1;
}

int l_setSyphonName(lua_State *L) {
	FrameServer::instance().setName(luaL_checkstring(L, 1));
	return 0;
}

int l_getSyphonName(lua_State *L) {
	lua_pushstring(L, FrameServer::instance().name().c_str());
	return 1;
}

int l_setSyphonPort(lua_State *L) {
	int port = (int)number(L, 1, 9999.0f);
	if(port > 0 && port < 65536) {
		syphonPort = port;
	}
	return 0;
}

int l_getSyphonPort(lua_State *L) {
	lua_pushinteger(L, syphonPort);
	return 1;
}

int l_getSyphonFrameCount(lua_State *L) {
	lua_pushinteger(L, (lua_Integer)FrameServer::instance().framesPublished());
	return 1;
}

int l_getSyphonServer(lua_State *L) {
	lua_pushnil(L);
	return 1;
}

int receiverSettings_new(lua_State *L) {
	lua_newtable(L);
	lua_pushinteger(L, 0);
	lua_setfield(L, -2, "port");
	lua_pushboolean(L, 1);
	lua_setfield(L, -2, "start");
	return 1;
}

int senderSettings_new(lua_State *L) {
	lua_newtable(L);
	lua_pushstring(L, "127.0.0.1");
	lua_setfield(L, -2, "host");
	lua_pushinteger(L, 8880);
	lua_setfield(L, -2, "port");
	return 1;
}

//--------------------------------------------------------------
// simplified ofxOscParameterSync: named parameters published over OSC
//--------------------------------------------------------------
int sync_new(lua_State *L) {
	LuaParamSync *sync = static_cast<LuaParamSync *>(lua_newuserdatauv(L, sizeof(LuaParamSync), 0));
	new(sync) LuaParamSync();
	sync->callback = LUA_NOREF;
	luaL_getmetatable(L, kSyncMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int sync_gc(lua_State *L) {
	LuaParamSync *sync = checkSync(L, 1);
	if(sync->callback != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, sync->callback);
		sync->callback = LUA_NOREF;
	}
	sync->~LuaParamSync();
	return 0;
}

int sync_addFloat(lua_State *L) {
	LuaParamSync *sync = checkSync(L, 1);
	sync->params[luaL_checkstring(L, 2)] = OscArg::float32(number(L, 3));
	return 0;
}

int sync_addInt(lua_State *L) {
	LuaParamSync *sync = checkSync(L, 1);
	sync->params[luaL_checkstring(L, 2)] = OscArg::int32((int32_t)number(L, 3));
	return 0;
}

int sync_addBool(lua_State *L) {
	LuaParamSync *sync = checkSync(L, 1);
	sync->params[luaL_checkstring(L, 2)] = OscArg::boolean(boolean(L, 3));
	return 0;
}

int sync_addString(lua_State *L) {
	LuaParamSync *sync = checkSync(L, 1);
	sync->params[luaL_checkstring(L, 2)] = OscArg::string(luaL_optstring(L, 3, ""));
	return 0;
}

int sync_set(lua_State *L) {
	LuaParamSync *sync = checkSync(L, 1);
	const char *address = luaL_checkstring(L, 2);
	auto found = sync->params.find(address);
	if(found == sync->params.end()) {
		sync->params[address] = OscArg::float32(number(L, 3));
		return 0;
	}
	switch(found->second.type) {
		case OscType::Int32:
		case OscType::Int64:
			found->second.i = (int64_t)number(L, 3);
			break;
		case OscType::String:
			found->second.s = luaL_checkstring(L, 3);
			break;
		case OscType::True:
		case OscType::False:
			found->second = OscArg::boolean(boolean(L, 3));
			break;
		default:
			found->second.d = number(L, 3);
			break;
	}
	return 0;
}

int sync_get(lua_State *L) {
	LuaParamSync *sync = checkSync(L, 1);
	auto found = sync->params.find(luaL_checkstring(L, 2));
	if(found == sync->params.end()) {
		lua_pushnil(L);
		return 1;
	}
	switch(found->second.type) {
		case OscType::Int32:
		case OscType::Int64:
			lua_pushinteger(L, (lua_Integer)found->second.i);
			break;
		case OscType::String:
			lua_pushlstring(L, found->second.s.data(), found->second.s.size());
			break;
		case OscType::True:
		case OscType::False:
			lua_pushboolean(L, found->second.type == OscType::True);
			break;
		default:
			lua_pushnumber(L, found->second.d);
			break;
	}
	return 1;
}

int sync_getCount(lua_State *L) {
	lua_pushinteger(L, (lua_Integer)checkSync(L, 1)->params.size());
	return 1;
}

int sync_send(lua_State *L) {
	LuaParamSync *sync = checkSync(L, 1);
	for(const auto &entry : sync->params) {
		OscMessage message;
		message.address = entry.first;
		message.args.push_back(entry.second);
		env().osc->sendMessage(message);
	}
	return 0;
}

int sync_setCallback(lua_State *L) {
	LuaParamSync *sync = checkSync(L, 1);
	if(sync->callback != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, sync->callback);
		sync->callback = LUA_NOREF;
	}
	if(lua_isfunction(L, 2)) {
		lua_pushvalue(L, 2);
		sync->callback = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	return 0;
}

int sync_handle(lua_State *L) {
	LuaParamSync *sync = checkSync(L, 1);
	LuaMessage *message = checkMessage(L, 2);
	auto found = sync->params.find(message->message.address);
	if(found == sync->params.end() || message->message.args.empty()) {
		lua_pushboolean(L, 0);
		return 1;
	}
	const OscArg &incoming = message->message.args[0];
	switch(found->second.type) {
		case OscType::Int32:
		case OscType::Int64:
			found->second.i = incoming.type == OscType::Float32 || incoming.type == OscType::Double
			                  ? (int64_t)incoming.d : incoming.i;
			break;
		case OscType::String:
			found->second.s = incoming.s;
			break;
		case OscType::True:
		case OscType::False:
			found->second = OscArg::boolean(incoming.type == OscType::True ||
			                                (incoming.type != OscType::False && incoming.i != 0));
			break;
		default:
			found->second.d = incoming.type == OscType::Float32 || incoming.type == OscType::Double
			                  ? incoming.d : (double)incoming.i;
			break;
	}
	lua_pushboolean(L, 1);
	if(sync->callback != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, sync->callback);
		lua_pushvalue(L, 2);
		if(lua_pcall(L, 1, 0, 0) != LUA_OK) {
			lua_pop(L, 1);
		}
	}
	return 1;
}

//--------------------------------------------------------------
void setFunction(lua_State *L, int table, const char *name, lua_CFunction function);
void setConstant(lua_State *L, int table, const char *name, const char *value);

void registerMessage(lua_State *L) {
	luaL_newmetatable(L, kMessageMetatable);
	lua_pushcfunction(L, msg_gc);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, msg_index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, msg_newindex);
	lua_setfield(L, -2, "__newindex");
	lua_pushcfunction(L, msg_toString);
	lua_setfield(L, -2, "__tostring");

	lua_newtable(L);
	setFunction(L, lua_gettop(L), "setAddress", msg_setAddress);
	setFunction(L, lua_gettop(L), "getAddress", msg_getAddress);
	setFunction(L, lua_gettop(L), "getNumArgs", msg_getNumArgs);
	setFunction(L, lua_gettop(L), "getTypeString", msg_getTypeString);
	setFunction(L, lua_gettop(L), "getArgType", msg_getArgType);
	setFunction(L, lua_gettop(L), "getArgAsInt32", msg_getArgAsInt32);
	setFunction(L, lua_gettop(L), "getArgAsInt64", msg_getArgAsInt64);
	setFunction(L, lua_gettop(L), "getArgAsFloat", msg_getArgAsFloat);
	setFunction(L, lua_gettop(L), "getArgAsDouble", msg_getArgAsDouble);
	setFunction(L, lua_gettop(L), "getArgAsString", msg_getArgAsString);
	setFunction(L, lua_gettop(L), "getArgAsSymbol", msg_getArgAsSymbol);
	setFunction(L, lua_gettop(L), "getArgAsChar", msg_getArgAsChar);
	setFunction(L, lua_gettop(L), "getArgAsBool", msg_getArgAsBool);
	setFunction(L, lua_gettop(L), "getArgAsBlob", msg_getArgAsBlob);
	setFunction(L, lua_gettop(L), "getArgAsTimetag", msg_getArgAsTimetag);
	setFunction(L, lua_gettop(L), "getArgAsMidiMessage", msg_getArgAsMidi);
	setFunction(L, lua_gettop(L), "getArgAsRgbaColor", msg_getArgAsRgba);
	setFunction(L, lua_gettop(L), "getArgAsTrigger", msg_getArgAsTrigger);
	setFunction(L, lua_gettop(L), "getArgAsNone", msg_getArgAsNone);
	setFunction(L, lua_gettop(L), "addInt32Arg", msg_addInt32);
	setFunction(L, lua_gettop(L), "addIntArg", msg_addInt32);
	setFunction(L, lua_gettop(L), "addInt64Arg", msg_addInt64);
	setFunction(L, lua_gettop(L), "addFloatArg", msg_addFloat);
	setFunction(L, lua_gettop(L), "addDoubleArg", msg_addDouble);
	setFunction(L, lua_gettop(L), "addStringArg", msg_addString);
	setFunction(L, lua_gettop(L), "addSymbolArg", msg_addSymbol);
	setFunction(L, lua_gettop(L), "addCharArg", msg_addChar);
	setFunction(L, lua_gettop(L), "addBoolArg", msg_addBool);
	setFunction(L, lua_gettop(L), "addBlobArg", msg_addBlob);
	setFunction(L, lua_gettop(L), "addTimetagArg", msg_addTimetag);
	setFunction(L, lua_gettop(L), "addMidiMessageArg", msg_addMidi);
	setFunction(L, lua_gettop(L), "addRgbaColorArg", msg_addRgba);
	setFunction(L, lua_gettop(L), "addTriggerArg", msg_addTrigger);
	setFunction(L, lua_gettop(L), "addNoneArg", msg_addNone);
	setFunction(L, lua_gettop(L), "clear", msg_clear);
	setFunction(L, lua_gettop(L), "copy", msg_copy);
	setFunction(L, lua_gettop(L), "setRemoteEndpoint", msg_setRemoteEndpoint);
	setFunction(L, lua_gettop(L), "getRemoteHost", msg_getRemoteHost);
	setFunction(L, lua_gettop(L), "getRemotePort", msg_getRemotePort);
	lua_setfield(L, -2, "__methods");
	lua_pop(L, 1);
}

void registerBundle(lua_State *L) {
	luaL_newmetatable(L, kBundleMetatable);
	lua_pushcfunction(L, bundle_gc);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, bundle_toString);
	lua_setfield(L, -2, "__tostring");
	lua_pushcfunction(L, bundle_index);
	lua_setfield(L, -2, "__index");
	lua_newtable(L);
	setFunction(L, lua_gettop(L), "addMessage", bundle_addMessage);
	setFunction(L, lua_gettop(L), "addBundle", bundle_addBundle);
	setFunction(L, lua_gettop(L), "clear", bundle_clear);
	setFunction(L, lua_gettop(L), "getMessageCount", bundle_getMessageCount);
	setFunction(L, lua_gettop(L), "getBundleCount", bundle_getBundleCount);
	setFunction(L, lua_gettop(L), "getMessageAt", bundle_getMessageAt);
	setFunction(L, lua_gettop(L), "getBundleAt", bundle_getBundleAt);
	lua_setfield(L, -2, "__methods");
	lua_pop(L, 1);
}

void registerSender(lua_State *L) {
	luaL_newmetatable(L, kSenderMetatable);
	lua_pushcfunction(L, sender_gc);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, sender_index);
	lua_setfield(L, -2, "__index");
	lua_newtable(L);
	setFunction(L, lua_gettop(L), "setup", sender_setup);
	setFunction(L, lua_gettop(L), "sendMessage", sender_sendMessage);
	setFunction(L, lua_gettop(L), "sendBundle", sender_sendBundle);
	setFunction(L, lua_gettop(L), "getHost", sender_getHost);
	setFunction(L, lua_gettop(L), "getPort", sender_getPort);
	lua_pushcfunction(L, sender_toString);
	lua_setfield(L, -2, "__tostring");
	lua_setfield(L, -2, "__methods");
	lua_pop(L, 1);
}

void registerReceiver(lua_State *L) {
	luaL_newmetatable(L, kReceiverMetatable);
	lua_pushcfunction(L, receiver_gc);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, receiver_index);
	lua_setfield(L, -2, "__index");
	lua_newtable(L);
	setFunction(L, lua_gettop(L), "setup", receiver_setup);
	setFunction(L, lua_gettop(L), "getNextMessage", receiver_getNextMessage);
	setFunction(L, lua_gettop(L), "isListening", receiver_isListening);
	setFunction(L, lua_gettop(L), "getPort", receiver_getPort);
	setFunction(L, lua_gettop(L), "close", receiver_close);
	setFunction(L, lua_gettop(L), "start", receiver_start);
	setFunction(L, lua_gettop(L), "stop", receiver_stop);
	setFunction(L, lua_gettop(L), "hasWaitingMessages", receiver_hasWaitingMessages);
	lua_pushcfunction(L, receiver_toString);
	lua_setfield(L, -2, "__tostring");
	lua_setfield(L, -2, "__methods");
	lua_pop(L, 1);
}

void registerSync(lua_State *L) {
	luaL_newmetatable(L, kSyncMetatable);
	lua_pushcfunction(L, sync_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, lua_gettop(L), "addFloat", sync_addFloat);
	setFunction(L, lua_gettop(L), "addInt", sync_addInt);
	setFunction(L, lua_gettop(L), "addBool", sync_addBool);
	setFunction(L, lua_gettop(L), "addString", sync_addString);
	setFunction(L, lua_gettop(L), "set", sync_set);
	setFunction(L, lua_gettop(L), "get", sync_get);
	setFunction(L, lua_gettop(L), "getCount", sync_getCount);
	setFunction(L, lua_gettop(L), "send", sync_send);
	setFunction(L, lua_gettop(L), "setCallback", sync_setCallback);
	setFunction(L, lua_gettop(L), "handle", sync_handle);
	setFunction(L, lua_gettop(L), "update", sync_handle);
	lua_setfield(L, -2, "__methods");
	lua_getfield(L, -1, "__methods");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

void setFunction(lua_State *L, int table, const char *name, lua_CFunction function) {
	lua_pushcfunction(L, function);
	lua_setfield(L, table, name);
}

void setConstant(lua_State *L, int table, const char *name, const char *value) {
	lua_pushstring(L, value);
	lua_setfield(L, table, name);
}

} // namespace

//--------------------------------------------------------------
int pushOscMessage(lua_State *L, const OscMessage &message) {
	LuaMessage *userdata = newMessage(L);
	userdata->message = message;
	return 1;
}

//--------------------------------------------------------------
void registerOscBindings(lua_State *L) {
	registerMessage(L);
	registerBundle(L);
	registerSender(L);
	registerReceiver(L);
	registerSync(L);

	lua_newtable(L);
	int table = lua_gettop(L);
	lua_pushstring(L, "osc");
	lua_setfield(L, table, "name");

	setConstant(L, table, "TYPE_INT32", "i");
	setConstant(L, table, "TYPE_INT64", "h");
	setConstant(L, table, "TYPE_FLOAT", "f");
	setConstant(L, table, "TYPE_DOUBLE", "d");
	setConstant(L, table, "TYPE_STRING", "s");
	setConstant(L, table, "TYPE_SYMBOL", "S");
	setConstant(L, table, "TYPE_CHAR", "c");
	setConstant(L, table, "TYPE_MIDI", "m");
	setConstant(L, table, "TYPE_TRUE", "T");
	setConstant(L, table, "TYPE_FALSE", "F");
	setConstant(L, table, "TYPE_NONE", "N");
	setConstant(L, table, "TYPE_TRIGGER", "I");
	setConstant(L, table, "TYPE_TIMETAG", "t");
	setConstant(L, table, "TYPE_BLOB", "b");
	setConstant(L, table, "TYPE_RGBA", "r");

	lua_setglobal(L, "osc");

	// osc.Message / osc.Bundle / osc.Sender / osc.Receiver as constructor tables
	lua_getglobal(L, "osc");
	int oscTable = lua_gettop(L);
	lua_pushcfunction(L, msg_new);
	lua_setfield(L, oscTable, "Message");
	lua_pushcfunction(L, bundle_new);
	lua_setfield(L, oscTable, "Bundle");
	lua_pushcfunction(L, sender_new);
	lua_setfield(L, oscTable, "Sender");
	lua_pushcfunction(L, receiver_new);
	lua_setfield(L, oscTable, "Receiver");
	lua_pushcfunction(L, receiverSettings_new);
	lua_setfield(L, oscTable, "ReceiverSettings");
	lua_pushcfunction(L, senderSettings_new);
	lua_setfield(L, oscTable, "SenderSettings");
	lua_pushcfunction(L, sync_new);
	lua_setfield(L, oscTable, "ParameterSync");
	lua_pop(L, 1);
}

//--------------------------------------------------------------
void registerLoafBindings(lua_State *L) {
	lua_newtable(L);
	int table = lua_gettop(L);

	setFunction(L, table, "setVerbose", l_setVerbose);
	setFunction(L, table, "isVerbose", l_isVerbose);
	setFunction(L, table, "setUseDefaultKeys", l_setUseDefaultKeys);
	setFunction(L, table, "useDefaultKeys", l_useDefaultKeys);
	setFunction(L, table, "isBundled", l_isBundled);
	setFunction(L, table, "getStartDirectory", l_getStartDirectory);
	setFunction(L, table, "getCurrentDirectory", l_getCurrentDirectory);
	setFunction(L, table, "setCurrentDirectory", l_setCurrentDirectory);
	setFunction(L, table, "getHostname", l_getHostname);
	setFunction(L, table, "setResolution", l_setResolution);
	setFunction(L, table, "clearResolution", l_clearResolution);
	setFunction(L, table, "getResolution", l_getResolution);
	setFunction(L, table, "reloadScript", l_reloadScript);
	setFunction(L, table, "loadScript", l_loadScript);
	setFunction(L, table, "clearScript", l_clearScript);

	setFunction(L, table, "setListenPort", l_setListenPort);
	setFunction(L, table, "getListenPort", l_getListenPort);
	setFunction(L, table, "startListening", l_startListening);
	setFunction(L, table, "stopListening", l_stopListening);
	setFunction(L, table, "isListening", l_isListening);
	setFunction(L, table, "setSendHost", l_setSendHost);
	setFunction(L, table, "getSendHost", l_getSendHost);
	setFunction(L, table, "setSendPort", l_setSendPort);
	setFunction(L, table, "getSendPort", l_getSendPort);
	setFunction(L, table, "sendFloat", l_sendFloat);
	setFunction(L, table, "sendInt", l_sendInt);
	setFunction(L, table, "sendString", l_sendString);
	setFunction(L, table, "sendBang", l_sendBang);
	setFunction(L, table, "sendMessage", l_sendMessage);
	setFunction(L, table, "sendBundle", l_sendBundle);

	setFunction(L, table, "startSyphon", l_startSyphon);
	setFunction(L, table, "stopSyphon", l_stopSyphon);
	setFunction(L, table, "isSyphonPublishing", l_isSyphonPublishing);
	setFunction(L, table, "setSyphonName", l_setSyphonName);
	setFunction(L, table, "getSyphonName", l_getSyphonName);
	setFunction(L, table, "getSyphonServer", l_getSyphonServer);
	setFunction(L, table, "setSyphonPort", l_setSyphonPort);
	setFunction(L, table, "getSyphonPort", l_getSyphonPort);
	setFunction(L, table, "getSyphonFrameCount", l_getSyphonFrameCount);

	lua_pushinteger(L, 0);
	lua_setfield(L, table, "modKey");

	lua_setglobal(L, "loaf");

	// convenience vararg sender, mirrors loaf.i
	luaL_dostring(L, R"LUA(
function loaf.send(address, ...)
	if type(address) ~= "string" or address:sub(1,1) ~= "/" then
		print("send: error, first argument is not an address string")
		return
	end
	local msg = osc.Message()
	msg:setAddress(address)
	for _, v in ipairs({...}) do
		local t = type(v)
		if t == "number" then
			if v == math.floor(v) and math.abs(v) < 2147483648 then
				msg:addInt32Arg(v)
			else
				msg:addFloatArg(v)
			end
		elseif t == "string" then
			msg:addStringArg(v)
		elseif t == "boolean" then
			msg:addBoolArg(v)
		else
			print("send: skipping " .. t .. " argument")
		end
	end
	loaf.sendMessage(msg)
end
)LUA");
}

} // namespace loaf
