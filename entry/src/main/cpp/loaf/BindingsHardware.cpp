#include "BindingsHardware.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <ohaudio/native_audio_device_base.h>
#include <ohaudio/native_audio_routing_manager.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#define LOG_TAG "loafcore"
#include "../Log.h"

namespace loaf {

namespace {

const char *kSerialMeta = "loaf.Serial";
const char *kArduinoMeta = "loaf.Arduino";
const char *kSoundDeviceMeta = "loaf.SoundDevice";
const char *kSerialDeviceInfoMeta = "loaf.SerialDeviceInfo";

speed_t baudToSpeed(int baud) {
	switch(baud) {
		case 1200: return B1200;
		case 2400: return B2400;
		case 4800: return B4800;
		case 9600: return B9600;
		case 19200: return B19200;
		case 38400: return B38400;
		case 57600: return B57600;
		case 115200: return B115200;
		case 230400: return B230400;
		default: return B9600;
	}
}

float number(lua_State *L, int index, float fallback = 0.0f) {
	return lua_isnumber(L, index) ? (float)lua_tonumber(L, index) : fallback;
}

struct LuaSerial {
	int fd = -1;
	std::string port;
};

struct LuaArduino {
	int fd = -1;
	std::string port;
	unsigned char portState[8] = {0};
	int pinMode[128] = {0};
	bool enabled = false;
};

struct LuaSoundDevice {
	std::string name;
	std::string id;
	int type = 0;
	bool input = false;
	bool output = false;
};

struct LuaSerialDeviceInfo {
	std::string path;
	std::string name;
};

std::vector<LuaArduino *> g_arduinos;

LuaSerial *checkSerial(lua_State *L, int index) {
	return static_cast<LuaSerial *>(luaL_checkudata(L, index, kSerialMeta));
}

LuaArduino *checkArduino(lua_State *L, int index) {
	return static_cast<LuaArduino *>(luaL_checkudata(L, index, kArduinoMeta));
}

LuaSoundDevice *checkSoundDevice(lua_State *L, int index) {
	return static_cast<LuaSoundDevice *>(luaL_checkudata(L, index, kSoundDeviceMeta));
}

void setFunction(lua_State *L, const char *name, lua_CFunction function) {
	lua_pushcfunction(L, function);
	lua_setfield(L, -2, name);
}

//--------------------------------------------------------------
bool openSerial(const std::string &port, int baud, int &fd) {
	fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
	if(fd < 0) {
		LOGI("serial: could not open %{public}s", port.c_str());
		return false;
	}
	struct termios options {};
	if(tcgetattr(fd, &options) != 0) {
		close(fd);
		fd = -1;
		return false;
	}
	cfmakeraw(&options);
	speed_t speed = baudToSpeed(baud);
	cfsetispeed(&options, speed);
	cfsetospeed(&options, speed);
	options.c_cflag |= (CLOCAL | CREAD);
	options.c_cflag &= ~CRTSCTS;
	options.c_cc[VMIN] = 0;
	options.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &options);
	return true;
}

//--------------------------------------------------------------
int serial_new(lua_State *L) {
	LuaSerial *serial = static_cast<LuaSerial *>(lua_newuserdatauv(L, sizeof(LuaSerial), 0));
	new(serial) LuaSerial();
	luaL_getmetatable(L, kSerialMeta);
	lua_setmetatable(L, -2);
	return 1;
}

int serial_gc(lua_State *L) {
	LuaSerial *serial = checkSerial(L, 1);
	if(serial->fd >= 0) {
		close(serial->fd);
		serial->fd = -1;
	}
	return 0;
}

int serial_setup(lua_State *L) {
	LuaSerial *serial = checkSerial(L, 1);
	const char *port = luaL_checkstring(L, 2);
	int baud = (int)number(L, 3, 9600.0f);
	if(serial->fd >= 0) {
		close(serial->fd);
	}
	serial->port = port;
	lua_pushboolean(L, openSerial(port, baud, serial->fd));
	return 1;
}

int serial_readByte(lua_State *L) {
	LuaSerial *serial = checkSerial(L, 1);
	unsigned char byte = 0;
	if(serial->fd >= 0 && read(serial->fd, &byte, 1) == 1) {
		lua_pushinteger(L, byte);
	}
	else {
		lua_pushinteger(L, -1);
	}
	return 1;
}

int serial_writeByte(lua_State *L) {
	LuaSerial *serial = checkSerial(L, 1);
	unsigned char byte = (unsigned char)number(L, 2, 0.0f);
	lua_pushboolean(L, serial->fd >= 0 && write(serial->fd, &byte, 1) == 1);
	return 1;
}

int serial_available(lua_State *L) {
	LuaSerial *serial = checkSerial(L, 1);
	int bytes = 0;
	if(serial->fd >= 0) {
		ioctl(serial->fd, FIONREAD, &bytes);
	}
	lua_pushinteger(L, bytes);
	return 1;
}

int serial_close(lua_State *L) {
	LuaSerial *serial = checkSerial(L, 1);
	if(serial->fd >= 0) {
		close(serial->fd);
		serial->fd = -1;
	}
	return 0;
}

int serial_isInitialized(lua_State *L) {
	lua_pushboolean(L, checkSerial(L, 1)->fd >= 0);
	return 1;
}

int serial_getPort(lua_State *L) {
	lua_pushstring(L, checkSerial(L, 1)->port.c_str());
	return 1;
}

//--------------------------------------------------------------
int arduino_new(lua_State *L) {
	LuaArduino *arduino = static_cast<LuaArduino *>(lua_newuserdatauv(L, sizeof(LuaArduino), 0));
	new(arduino) LuaArduino();
	luaL_getmetatable(L, kArduinoMeta);
	lua_setmetatable(L, -2);
	g_arduinos.push_back(arduino);
	return 1;
}

int arduino_gc(lua_State *L) {
	LuaArduino *arduino = checkArduino(L, 1);
	if(arduino->fd >= 0) {
		close(arduino->fd);
		arduino->fd = -1;
	}
	g_arduinos.erase(std::remove(g_arduinos.begin(), g_arduinos.end(), arduino), g_arduinos.end());
	return 0;
}

int arduino_setup(lua_State *L) {
	LuaArduino *arduino = checkArduino(L, 1);
	const char *port = luaL_checkstring(L, 2);
	int baud = (int)number(L, 3, 57600.0f);
	if(arduino->fd >= 0) {
		close(arduino->fd);
	}
	arduino->port = port;
	bool ok = openSerial(port, baud, arduino->fd);
	if(ok) {
		usleep(1200000); // firmata reset
		tcflush(arduino->fd, TCIFLUSH);
		arduino->enabled = true;
	}
	lua_pushboolean(L, ok);
	return 1;
}

int arduino_close(lua_State *L) {
	LuaArduino *arduino = checkArduino(L, 1);
	if(arduino->fd >= 0) {
		close(arduino->fd);
		arduino->fd = -1;
	}
	arduino->enabled = false;
	return 0;
}

int arduino_isInitialized(lua_State *L) {
	lua_pushboolean(L, checkArduino(L, 1)->fd >= 0);
	return 1;
}

void writeBytes(LuaArduino *arduino, const unsigned char *data, size_t length) {
	if(arduino->fd >= 0) {
		write(arduino->fd, data, length);
	}
}

int arduino_sendDigitalPinMode(lua_State *L) {
	LuaArduino *arduino = checkArduino(L, 1);
	int pin = (int)number(L, 2, 0.0f);
	int mode = (int)number(L, 3, 0.0f);
	unsigned char message[3] = {0xF4, (unsigned char)pin, (unsigned char)mode};
	writeBytes(arduino, message, 3);
	arduino->pinMode[pin & 0x7F] = mode;
	return 0;
}

int arduino_sendDigitalPin(lua_State *L) {
	LuaArduino *arduino = checkArduino(L, 1);
	int pin = (int)number(L, 2, 0.0f);
	int value = number(L, 3, 0.0f) > 0 ? 1 : 0;
	int port = pin / 8;
	int bit = pin % 8;
	if(value) {
		arduino->portState[port] |= (unsigned char)(1 << bit);
	}
	else {
		arduino->portState[port] &= (unsigned char)~(1 << bit);
	}
	unsigned char message[3] = {(unsigned char)(0x90 | port),
	                            (unsigned char)(arduino->portState[port] & 0x7F),
	                            (unsigned char)((arduino->portState[port] >> 7) & 0x7F)};
	writeBytes(arduino, message, 3);
	return 0;
}

int arduino_sendPwmPin(lua_State *L) {
	LuaArduino *arduino = checkArduino(L, 1);
	int pin = (int)number(L, 2, 0.0f);
	int value = std::max(0, std::min(16383, (int)number(L, 3, 0.0f)));
	unsigned char message[3] = {(unsigned char)(0xE0 | (pin & 0x0F)),
	                            (unsigned char)(value & 0x7F),
	                            (unsigned char)((value >> 7) & 0x7F)};
	writeBytes(arduino, message, 3);
	return 0;
}

int arduino_reportAnalogPin(lua_State *L) {
	LuaArduino *arduino = checkArduino(L, 1);
	int pin = (int)number(L, 2, 0.0f);
	bool enable = lua_toboolean(L, 3) != 0;
	unsigned char message[2] = {(unsigned char)(0xC0 | (pin & 0x0F)), (unsigned char)(enable ? 1 : 0)};
	writeBytes(arduino, message, 2);
	return 0;
}

int arduino_reportDigitalPort(lua_State *L) {
	LuaArduino *arduino = checkArduino(L, 1);
	int port = (int)number(L, 2, 0.0f);
	bool enable = lua_toboolean(L, 3) != 0;
	unsigned char message[2] = {(unsigned char)(0xD0 | (port & 0x0F)), (unsigned char)(enable ? 1 : 0)};
	writeBytes(arduino, message, 2);
	return 0;
}

void callPin(lua_State *L, const char *name, int pin, int value) {
	lua_getglobal(L, name);
	if(!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return;
	}
	lua_pushinteger(L, pin);
	lua_pushinteger(L, value);
	if(lua_pcall(L, 2, 0, 0) != LUA_OK) {
		lua_pop(L, 1);
	}
}

int arduino_update(lua_State *L) {
	// kept for scripts that pump manually; the runtime also polls every frame
	return 0;
}

//--------------------------------------------------------------
int serialDeviceInfo_indexNil(lua_State *L) {
	lua_pushnil(L);
	return 1;
}

int soundDevice_getName(lua_State *L) { lua_pushstring(L, checkSoundDevice(L, 1)->name.c_str()); return 1; }
int soundDevice_getId(lua_State *L) { lua_pushstring(L, checkSoundDevice(L, 1)->id.c_str()); return 1; }
int soundDevice_getType(lua_State *L) { lua_pushinteger(L, checkSoundDevice(L, 1)->type); return 1; }
int soundDevice_isInput(lua_State *L) { lua_pushboolean(L, checkSoundDevice(L, 1)->input); return 1; }
int soundDevice_isOutput(lua_State *L) { lua_pushboolean(L, checkSoundDevice(L, 1)->output); return 1; }

int soundDevice_new(lua_State *L) {
	LuaSoundDevice *device = static_cast<LuaSoundDevice *>(lua_newuserdatauv(L, sizeof(LuaSoundDevice), 0));
	new(device) LuaSoundDevice();
	luaL_getmetatable(L, kSoundDeviceMeta);
	lua_setmetatable(L, -2);
	return 1;
}

int soundDevice_gc(lua_State *L) { checkSoundDevice(L, 1)->~LuaSoundDevice(); return 0; }

//--------------------------------------------------------------
int l_getSerialDeviceList(lua_State *L) {
	lua_newtable(L);
	int index = 1;
	const char *patterns[] = {"/dev/ttyUSB", "/dev/ttyACM", "/dev/ttyS"};
	for(const char *prefix : patterns) {
		for(int i = 0; i < 16; ++i) {
			std::string path = std::string(prefix) + std::to_string(i);
			struct stat info {};
			if(stat(path.c_str(), &info) != 0) {
				continue;
			}
			lua_newtable(L);
			lua_pushstring(L, path.c_str());
			lua_setfield(L, -2, "devicePath");
			lua_pushstring(L, path.c_str());
			lua_setfield(L, -2, "deviceName");
			lua_rawseti(L, -2, index++);
		}
	}
	return 1;
}

int l_getSoundDeviceList(lua_State *L) {
	lua_newtable(L);
	OH_AudioRoutingManager *manager = nullptr;
	if(OH_AudioManager_GetAudioRoutingManager(&manager) != AUDIOCOMMON_RESULT_SUCCESS || manager == nullptr) {
		return 1;
	}
	OH_AudioDeviceDescriptorArray *devices = nullptr;
	if(OH_AudioRoutingManager_GetDevices(manager, AUDIO_DEVICE_FLAG_ALL, &devices) != AUDIOCOMMON_RESULT_SUCCESS ||
	   devices == nullptr) {
		return 1;
	}
	int index = 1;
	for(uint32_t i = 0; i < devices->size; ++i) {
		OH_AudioDeviceDescriptor *descriptor = devices->descriptors[i];
		LuaSoundDevice *device = static_cast<LuaSoundDevice *>(lua_newuserdatauv(L, sizeof(LuaSoundDevice), 0));
		new(device) LuaSoundDevice();
		char *name = nullptr;
		uint32_t id = 0;
		OH_AudioDevice_Type type = AUDIO_DEVICE_TYPE_INVALID;
		OH_AudioDeviceDescriptor_GetDeviceName(descriptor, &name);
		OH_AudioDeviceDescriptor_GetDeviceId(descriptor, &id);
		OH_AudioDeviceDescriptor_GetDeviceType(descriptor, &type);
		if(name != nullptr) device->name = name;
		device->id = std::to_string(id);
		device->type = (int)type;
		device->input = true;
		device->output = true;
		luaL_getmetatable(L, kSoundDeviceMeta);
		lua_setmetatable(L, -2);
		lua_rawseti(L, -2, index++);
	}
	OH_AudioRoutingManager_ReleaseDevices(manager, devices);
	return 1;
}

} // namespace

//--------------------------------------------------------------
void registerHardwareBindings(lua_State *L, int ofTable) {
	luaL_newmetatable(L, kSerialMeta);
	lua_pushcfunction(L, serial_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "setup", serial_setup);
	setFunction(L, "readByte", serial_readByte);
	setFunction(L, "writeByte", serial_writeByte);
	setFunction(L, "available", serial_available);
	setFunction(L, "close", serial_close);
	setFunction(L, "isInitialized", serial_isInitialized);
	setFunction(L, "getPortName", serial_getPort);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	luaL_newmetatable(L, kArduinoMeta);
	lua_pushcfunction(L, arduino_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "setup", arduino_setup);
	setFunction(L, "close", arduino_close);
	setFunction(L, "isInitialized", arduino_isInitialized);
	setFunction(L, "sendDigitalPinMode", arduino_sendDigitalPinMode);
	setFunction(L, "sendDigitalPin", arduino_sendDigitalPin);
	setFunction(L, "sendPwmPin", arduino_sendPwmPin);
	setFunction(L, "sendAnalogPin", arduino_sendPwmPin);
	setFunction(L, "reportAnalogPin", arduino_reportAnalogPin);
	setFunction(L, "reportDigitalPort", arduino_reportDigitalPort);
	setFunction(L, "update", arduino_update);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	luaL_newmetatable(L, kSoundDeviceMeta);
	lua_pushcfunction(L, soundDevice_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "getDeviceName", soundDevice_getName);
	setFunction(L, "getName", soundDevice_getName);
	setFunction(L, "getDeviceID", soundDevice_getId);
	setFunction(L, "getDeviceType", soundDevice_getType);
	setFunction(L, "isInput", soundDevice_isInput);
	setFunction(L, "isOutput", soundDevice_isOutput);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	luaL_newmetatable(L, kSerialDeviceInfoMeta);
	lua_newtable(L);
	lua_pushcfunction(L, serialDeviceInfo_indexNil);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
	lua_pop(L, 1);

	lua_pushcfunction(L, serial_new); lua_setfield(L, ofTable, "Serial");
	lua_pushcfunction(L, arduino_new); lua_setfield(L, ofTable, "Arduino");
	lua_pushcfunction(L, soundDevice_new); lua_setfield(L, ofTable, "SoundDevice");
	lua_pushcfunction(L, l_getSerialDeviceList); lua_setfield(L, ofTable, "getSerialDeviceList");
	lua_pushcfunction(L, l_getSoundDeviceList); lua_setfield(L, ofTable, "getSoundDeviceList");
}

//--------------------------------------------------------------
void pollSerialDevices(lua_State *L) {
	for(LuaArduino *arduino : g_arduinos) {
		if(arduino == nullptr || arduino->fd < 0 || !arduino->enabled) {
			continue;
		}
		unsigned char buffer[256];
		ssize_t count = read(arduino->fd, buffer, sizeof(buffer));
		if(count <= 0) {
			continue;
		}
		int command = -1;
		int needed = 0;
		int data[3] = {0, 0, 0};
		int received = 0;
		for(ssize_t i = 0; i < count; ++i) {
			unsigned char byte = buffer[i];
			if(byte & 0x80) {
				command = byte;
				received = 0;
				if((command & 0xF0) == 0x90 || (command & 0xF0) == 0xE0) {
					needed = 2;
				}
				else if((command & 0xF0) == 0xC0 || (command & 0xF0) == 0xD0) {
					needed = 1;
				}
				else if(command == 0xF4) {
					needed = 2;
				}
				else {
					needed = 0;
				}
				continue;
			}
			if(command < 0 || needed == 0) {
				continue;
			}
			data[received++] = byte;
			if(received < needed) {
				continue;
			}
			if((command & 0xF0) == 0x90) {
				int port = command & 0x0F;
				int mask = data[0] | (data[1] << 7);
				for(int bit = 0; bit < 8; ++bit) {
					int pin = port * 8 + bit;
					int value = (mask >> bit) & 1;
					int previous = (arduino->portState[port] >> bit) & 1;
					if(value != previous) {
						callPin(L, "digitalPinChanged", pin, value);
					}
					if(value) {
						arduino->portState[port] |= (unsigned char)(1 << bit);
					}
					else {
						arduino->portState[port] &= (unsigned char)~(1 << bit);
					}
				}
			}
			else if((command & 0xF0) == 0xE0) {
				int pin = command & 0x0F;
				int value = data[0] | (data[1] << 7);
				callPin(L, "analogPinChanged", pin, value);
			}
			received = 0;
		}
	}
}

} // namespace loaf
