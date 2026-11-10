#include "Osc.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#define LOG_TAG "loafcore"
#include "../Log.h"

namespace loaf {

namespace {

void appendString(std::string &out, const std::string &value) {
	out.append(value);
	out.push_back('\0');
	while(out.size() % 4 != 0) {
		out.push_back('\0');
	}
}

void appendInt32(std::string &out, int32_t value) {
	uint32_t network = htonl((uint32_t)value);
	out.append(reinterpret_cast<const char *>(&network), 4);
}

void appendInt64(std::string &out, int64_t value) {
	uint64_t raw = (uint64_t)value;
	for(int i = 7; i >= 0; --i) {
		out.push_back((char)((raw >> (i * 8)) & 0xff));
	}
}

void appendFloat(std::string &out, float value) {
	uint32_t raw = 0;
	std::memcpy(&raw, &value, 4);
	uint32_t network = htonl(raw);
	out.append(reinterpret_cast<const char *>(&network), 4);
}

void appendDouble(std::string &out, double value) {
	uint64_t raw = 0;
	std::memcpy(&raw, &value, 8);
	for(int i = 7; i >= 0; --i) {
		out.push_back((char)((raw >> (i * 8)) & 0xff));
	}
}

void appendBlob(std::string &out, const std::string &value) {
	appendInt32(out, (int32_t)value.size());
	out.append(value);
	while(out.size() % 4 != 0) {
		out.push_back('\0');
	}
}

size_t pad4(size_t value) {
	return (value + 3) & ~((size_t)3);
}

bool readInt32(const char *data, size_t size, size_t &offset, int32_t &value) {
	if(offset + 4 > size) {
		return false;
	}
	value = (int32_t)ntohl(*reinterpret_cast<const uint32_t *>(data + offset));
	offset += 4;
	return true;
}

bool readInt64(const char *data, size_t size, size_t &offset, int64_t &value) {
	if(offset + 8 > size) {
		return false;
	}
	uint64_t raw = 0;
	for(int i = 0; i < 8; ++i) {
		raw = (raw << 8) | (unsigned char)data[offset + i];
	}
	value = (int64_t)raw;
	offset += 8;
	return true;
}

bool readFloat(const char *data, size_t size, size_t &offset, double &value) {
	if(offset + 4 > size) {
		return false;
	}
	uint32_t raw = ntohl(*reinterpret_cast<const uint32_t *>(data + offset));
	float f = 0.0f;
	std::memcpy(&f, &raw, 4);
	value = f;
	offset += 4;
	return true;
}

bool readDouble(const char *data, size_t size, size_t &offset, double &value) {
	if(offset + 8 > size) {
		return false;
	}
	uint64_t raw = 0;
	for(int i = 0; i < 8; ++i) {
		raw = (raw << 8) | (unsigned char)data[offset + i];
	}
	std::memcpy(&value, &raw, 8);
	offset += 8;
	return true;
}

bool readString(const char *data, size_t size, size_t &offset, std::string &value) {
	size_t length = strnlen(data + offset, size - offset);
	if(length >= size - offset) {
		return false;
	}
	value.assign(data + offset, length);
	offset += pad4(length + 1);
	return offset <= size;
}

} // namespace

//--------------------------------------------------------------
OscArg OscArg::float32(float value) {
	OscArg arg;
	arg.type = OscType::Float32;
	arg.d = value;
	return arg;
}

OscArg OscArg::double64(double value) {
	OscArg arg;
	arg.type = OscType::Double;
	arg.d = value;
	return arg;
}

OscArg OscArg::int32(int32_t value) {
	OscArg arg;
	arg.type = OscType::Int32;
	arg.i = value;
	return arg;
}

OscArg OscArg::int64(int64_t value) {
	OscArg arg;
	arg.type = OscType::Int64;
	arg.i = value;
	return arg;
}

OscArg OscArg::string(const std::string &value) {
	OscArg arg;
	arg.type = OscType::String;
	arg.s = value;
	return arg;
}

OscArg OscArg::symbol(const std::string &value) {
	OscArg arg;
	arg.type = OscType::Symbol;
	arg.s = value;
	return arg;
}

OscArg OscArg::character(char value) {
	OscArg arg;
	arg.type = OscType::Char;
	arg.i = (unsigned char)value;
	return arg;
}

OscArg OscArg::boolean(bool value) {
	OscArg arg;
	arg.type = value ? OscType::True : OscType::False;
	arg.i = value ? 1 : 0;
	return arg;
}

OscArg OscArg::blob(const std::string &value) {
	OscArg arg;
	arg.type = OscType::Blob;
	arg.s = value;
	return arg;
}

OscArg OscArg::midi(uint32_t value) {
	OscArg arg;
	arg.type = OscType::Midi;
	arg.i = (int64_t)value;
	return arg;
}

OscArg OscArg::timetag(uint64_t value) {
	OscArg arg;
	arg.type = OscType::Timetag;
	arg.i = (int64_t)value;
	return arg;
}

OscArg OscArg::rgba(uint32_t value) {
	OscArg arg;
	arg.type = OscType::Rgba;
	arg.i = (int64_t)value;
	return arg;
}

OscArg OscArg::none() {
	OscArg arg;
	arg.type = OscType::None;
	return arg;
}

OscArg OscArg::trigger() {
	OscArg arg;
	arg.type = OscType::Trigger;
	return arg;
}

//--------------------------------------------------------------
std::string OscMessage::typeString() const {
	std::string tags = ",";
	for(const auto &arg : args) {
		tags.push_back((char)arg.type);
	}
	return tags;
}

//--------------------------------------------------------------
std::string OscMessage::toString() const {
	std::string text = address + " ";
	for(const auto &arg : args) {
		switch(arg.type) {
			case OscType::Int32:
			case OscType::Int64:
			case OscType::Char:
			case OscType::Midi:
			case OscType::Timetag:
			case OscType::Rgba:
				text += " " + std::to_string(arg.i);
				break;
			case OscType::Float32:
			case OscType::Double: {
				char buffer[64];
				std::snprintf(buffer, sizeof(buffer), " %g", arg.d);
				text += buffer;
				break;
			}
			case OscType::String:
			case OscType::Symbol:
			case OscType::Blob:
				text += " " + arg.s;
				break;
			case OscType::True:
				text += " true";
				break;
			case OscType::False:
				text += " false";
				break;
			case OscType::Trigger:
				text += " trigger";
				break;
			case OscType::None:
				break;
		}
	}
	return text;
}

//--------------------------------------------------------------
std::string Osc::encode(const OscMessage &message) {
	std::string packet;
	appendString(packet, message.address);
	appendString(packet, message.typeString());
	for(const auto &arg : message.args) {
		switch(arg.type) {
			case OscType::Int32:
			case OscType::Char:
			case OscType::Midi:
			case OscType::Rgba:
				appendInt32(packet, (int32_t)arg.i);
				break;
			case OscType::Int64:
			case OscType::Timetag:
				appendInt64(packet, arg.i);
				break;
			case OscType::Float32:
				appendFloat(packet, (float)arg.d);
				break;
			case OscType::Double:
				appendDouble(packet, arg.d);
				break;
			case OscType::String:
			case OscType::Symbol:
				appendString(packet, arg.s);
				break;
			case OscType::Blob:
				appendBlob(packet, arg.s);
				break;
			case OscType::True:
			case OscType::False:
			case OscType::None:
			case OscType::Trigger:
				break;
		}
	}
	return packet;
}

//--------------------------------------------------------------
std::string Osc::encode(const OscBundle &bundle) {
	std::string packet;
	appendString(packet, "#bundle");
	appendInt64(packet, 1); // immediate
	for(const auto &message : bundle.messages) {
		std::string element = encode(message);
		appendInt32(packet, (int32_t)element.size());
		packet.append(element);
		while(packet.size() % 4 != 0) {
			packet.push_back('\0');
		}
	}
	for(const auto &child : bundle.bundles) {
		std::string element = encode(child);
		appendInt32(packet, (int32_t)element.size());
		packet.append(element);
		while(packet.size() % 4 != 0) {
			packet.push_back('\0');
		}
	}
	return packet;
}

//--------------------------------------------------------------
bool Osc::decode(const char *data, size_t size, OscMessage &message, size_t &consumed) {
	size_t offset = 0;
	if(!readString(data, size, offset, message.address)) {
		return false;
	}
	std::string tags;
	if(!readString(data, size, offset, tags) || tags.empty() || tags[0] != ',') {
		return false;
	}
	message.args.clear();
	for(size_t i = 1; i < tags.size(); ++i) {
		OscArg arg;
		switch((OscType)tags[i]) {
			case OscType::Int32:
			case OscType::Char:
			case OscType::Midi:
			case OscType::Rgba: {
				int32_t value = 0;
				if(!readInt32(data, size, offset, value)) return false;
				arg.type = (OscType)tags[i];
				arg.i = value;
				break;
			}
			case OscType::Int64:
			case OscType::Timetag: {
				int64_t value = 0;
				if(!readInt64(data, size, offset, value)) return false;
				arg.type = (OscType)tags[i];
				arg.i = value;
				break;
			}
			case OscType::Float32: {
				double value = 0.0;
				if(!readFloat(data, size, offset, value)) return false;
				arg = OscArg::float32((float)value);
				break;
			}
			case OscType::Double: {
				double value = 0.0;
				if(!readDouble(data, size, offset, value)) return false;
				arg = OscArg::double64(value);
				break;
			}
			case OscType::String:
			case OscType::Symbol: {
				std::string value;
				if(!readString(data, size, offset, value)) return false;
				arg.type = (OscType)tags[i];
				arg.s = value;
				break;
			}
			case OscType::Blob: {
				int32_t length = 0;
				if(!readInt32(data, size, offset, length) || length < 0 || offset + (size_t)length > size) {
					return false;
				}
				arg = OscArg::blob(std::string(data + offset, (size_t)length));
				offset += pad4((size_t)length);
				break;
			}
			case OscType::True:
				arg = OscArg::boolean(true);
				break;
			case OscType::False:
				arg = OscArg::boolean(false);
				break;
			case OscType::None:
				arg = OscArg::none();
				break;
			case OscType::Trigger:
				arg = OscArg::trigger();
				break;
			default:
				return false;
		}
		message.args.push_back(arg);
	}
	consumed = offset;
	return true;
}

//--------------------------------------------------------------
bool Osc::decodeBundle(const char *data, size_t size, OscBundle &bundle) {
	if(size < 16 || std::memcmp(data, "#bundle", 7) != 0) {
		return false;
	}
	size_t offset = 16;
	while(offset + 4 <= size) {
		int32_t elementSize = 0;
		if(!readInt32(data, size, offset, elementSize) || elementSize <= 0 ||
		   offset + (size_t)elementSize > size) {
			return false;
		}
		const char *element = data + offset;
		if(elementSize >= 16 && std::memcmp(element, "#bundle", 7) == 0) {
			OscBundle child;
			if(decodeBundle(element, (size_t)elementSize, child)) {
				bundle.bundles.push_back(child);
			}
		}
		else {
			OscMessage message;
			size_t consumed = 0;
			if(decode(element, (size_t)elementSize, message, consumed)) {
				bundle.messages.push_back(message);
			}
		}
		offset += pad4((size_t)elementSize);
	}
	return true;
}

//--------------------------------------------------------------
Osc::~Osc() {
	stopListening();
	if(sendFd >= 0) {
		close(sendFd);
		sendFd = -1;
	}
}

//--------------------------------------------------------------
int Osc::openSocket() {
	return socket(AF_INET, SOCK_DGRAM, 0);
}

//--------------------------------------------------------------
void Osc::sendTo(int fd, const std::string &host, int port, const std::string &packet) {
	if(fd < 0) {
		return;
	}
	sockaddr_in address {};
	address.sin_family = AF_INET;
	address.sin_port = htons((uint16_t)port);
	if(inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	}
	ssize_t sent = sendto(fd, packet.data(), packet.size(), 0,
	                      reinterpret_cast<sockaddr *>(&address), sizeof(address));
	if(sent < 0) {
		LOGW("osc: send to %s:%d failed", host.c_str(), port);
	}
}

//--------------------------------------------------------------
bool Osc::startListening(int newPort) {
	stopListening();
	if(newPort <= 0 || newPort > 65535) {
		return false;
	}
	socketFd = openSocket();
	if(socketFd < 0) {
		LOGE("osc: socket failed");
		return false;
	}
	int reuse = 1;
	setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	timeval timeout {0, 200000};
	setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	sockaddr_in address {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons((uint16_t)newPort);
	if(bind(socketFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
		LOGE("osc: bind %d failed", newPort);
		close(socketFd);
		socketFd = -1;
		return false;
	}
	currentPort = newPort;
	requestedPort = newPort;
	running = true;
	listening = true;
	thread = std::thread(&Osc::receiveLoop, this);
	OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "loafcore", "osc: listening on %{public}d", newPort);
	return true;
}

//--------------------------------------------------------------
void Osc::stopListening() {
	if(!running) {
		return;
	}
	running = false;
	if(thread.joinable()) {
		thread.join();
	}
	if(socketFd >= 0) {
		close(socketFd);
		socketFd = -1;
	}
	listening = false;
	LOGI("osc: stopped listening");
}

//--------------------------------------------------------------
void Osc::setSendHost(const std::string &newHost) {
	host = newHost.empty() ? "127.0.0.1" : newHost;
}

//--------------------------------------------------------------
void Osc::setSendPort(int newPort) {
	if(newPort > 0 && newPort <= 65535) {
		port = newPort;
	}
}

//--------------------------------------------------------------
int Osc::sendSocket() {
	if(sendFd < 0) {
		sendFd = openSocket();
	}
	return sendFd;
}

//--------------------------------------------------------------
void Osc::sendPacket(const std::string &packet) {
	sendTo(sendSocket(), host, port, packet);
}

//--------------------------------------------------------------
void Osc::sendMessage(const OscMessage &message) {
	sendPacket(encode(message));
}

//--------------------------------------------------------------
void Osc::sendBundle(const OscBundle &bundle) {
	sendPacket(encode(bundle));
}

//--------------------------------------------------------------
void Osc::sendFloat(const std::string &address, float value) {
	OscMessage message;
	message.address = address;
	message.args.push_back(OscArg::float32(value));
	sendMessage(message);
}

//--------------------------------------------------------------
void Osc::sendInt(const std::string &address, int32_t value) {
	OscMessage message;
	message.address = address;
	message.args.push_back(OscArg::int32(value));
	sendMessage(message);
}

//--------------------------------------------------------------
void Osc::sendString(const std::string &address, const std::string &value) {
	OscMessage message;
	message.address = address;
	message.args.push_back(OscArg::string(value));
	sendMessage(message);
}

//--------------------------------------------------------------
void Osc::sendBang(const std::string &address) {
	OscMessage message;
	message.address = address;
	sendMessage(message);
}

//--------------------------------------------------------------
void Osc::receiveLoop() {
	char buffer[65536];
	while(running) {
		ssize_t size = recv(socketFd, buffer, sizeof(buffer), 0);
		if(size <= 0) {
			continue;
		}
		handlePacket(buffer, (size_t)size);
	}
}

//--------------------------------------------------------------
void Osc::handlePacket(const char *data, size_t size) {
	if(size < 4) {
		return;
	}
	if(size >= 8 && std::memcmp(data, "#bundle", 7) == 0) {
		OscBundle bundle;
		if(!decodeBundle(data, size, bundle)) {
			return;
		}
		std::lock_guard<std::mutex> lock(mutex);
		for(auto &message : bundle.messages) {
			if(queue.size() > 512) {
				queue.pop_front();
			}
			queue.push_back(message);
		}
		return;
	}
	OscMessage message;
	size_t consumed = 0;
	if(decode(data, size, message, consumed)) {
		std::lock_guard<std::mutex> lock(mutex);
		if(queue.size() > 512) {
			queue.pop_front();
		}
		queue.push_back(message);
	}
}

//--------------------------------------------------------------
bool Osc::poll(OscMessage &message) {
	std::lock_guard<std::mutex> lock(mutex);
	if(queue.empty()) {
		return false;
	}
	message = queue.front();
	queue.pop_front();
	return true;
}

} // namespace loaf
