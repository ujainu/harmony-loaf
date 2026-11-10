#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace loaf {

/// OSC 1.0 argument types, the tag characters match the wire format
enum class OscType : char {
	Int32 = 'i',
	Int64 = 'h',
	Float32 = 'f',
	Double = 'd',
	String = 's',
	Symbol = 'S',
	Char = 'c',
	Midi = 'm',
	True = 'T',
	False = 'F',
	None = 'N',
	Trigger = 'I',
	Timetag = 't',
	Blob = 'b',
	Rgba = 'r',
};

struct OscArg {
	OscType type = OscType::Float32;
	int64_t i = 0;      ///< int32/int64/char/midi/timetag/rgba
	double d = 0.0;     ///< float/double
	std::string s;      ///< string/symbol/blob

	static OscArg float32(float value);
	static OscArg double64(double value);
	static OscArg int32(int32_t value);
	static OscArg int64(int64_t value);
	static OscArg string(const std::string &value);
	static OscArg symbol(const std::string &value);
	static OscArg character(char value);
	static OscArg boolean(bool value);
	static OscArg blob(const std::string &value);
	static OscArg midi(uint32_t value);
	static OscArg timetag(uint64_t value);
	static OscArg rgba(uint32_t value);
	static OscArg none();
	static OscArg trigger();
};

struct OscMessage {
	std::string address;
	std::vector<OscArg> args;
	std::string remoteHost;
	int remotePort = 0;

	std::string typeString() const;
	std::string toString() const;
};

struct OscBundle {
	std::vector<OscMessage> messages;
	std::vector<OscBundle> bundles;
};

class Osc {
public:

	~Osc();

	bool startListening(int port);
	void stopListening();
	bool isListening() const { return listening; }
	int listenPort() const { return listening ? currentPort : requestedPort; }
	void setListenPort(int port) { requestedPort = port; }

	void setSendHost(const std::string &host);
	void setSendPort(int port);
	const std::string &sendHost() const { return host; }
	int sendPort() const { return port; }

	void sendMessage(const OscMessage &message);
	void sendBundle(const OscBundle &bundle);
	void sendFloat(const std::string &address, float value);
	void sendInt(const std::string &address, int32_t value);
	void sendString(const std::string &address, const std::string &value);
	void sendBang(const std::string &address);

	/// pops one received message, returns false if the queue is empty
	bool poll(OscMessage &message);
	bool hasWaiting() const { return !queue.empty(); }

	/// wire encoding helpers, also used by standalone senders
	static std::string encode(const OscMessage &message);
	static std::string encode(const OscBundle &bundle);
	static bool decode(const char *data, size_t size, OscMessage &message, size_t &consumed);
	static bool decodeBundle(const char *data, size_t size, OscBundle &bundle);
	static void sendTo(int fd, const std::string &host, int port, const std::string &packet);
	static int openSocket();

private:

	void receiveLoop();
	int sendSocket();
	void sendPacket(const std::string &packet);
	void handlePacket(const char *data, size_t size);

	int socketFd = -1;
	int sendFd = -1;
	int currentPort = 0;
	int requestedPort = 9990;
	int port = 8880;
	std::string host = "127.0.0.1";
	std::atomic<bool> listening {false};
	std::atomic<bool> running {false};
	std::thread thread;
	std::mutex mutex;
	std::deque<OscMessage> queue;
};

} // namespace loaf
