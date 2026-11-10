#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace loaf {

/// Syphon-style frame publisher: streams the current canvas over TCP.
///
/// protocol per frame (little endian header, raw RGBA payload):
///   char[4] "LOAF" | u32 width | u32 height | u32 sequence | u32 payloadBytes
class FrameServer {
public:

	static FrameServer &instance();

	bool start(const std::string &name, int port, int fps, int scale);
	void stop();
	bool isPublishing() const { return running; }
	int port() const { return currentPort; }
	const std::string &name() const { return serverName; }
	void setName(const std::string &value) { serverName = value; }

	void publish(const std::vector<unsigned char> &rgba, int width, int height);
	unsigned long framesPublished() const { return published; }
	int clientCount() const { return clients; }

private:

	FrameServer() = default;
	~FrameServer();

	void acceptLoop();
	void sendLoop();

	int listenFd = -1;
	int clientFd = -1;
	int currentPort = 9999;
	int targetFps = 10;
	int downscale = 2;
	std::string serverName = "loaf";
	std::atomic<bool> running {false};
	std::atomic<unsigned long> published {0};
	std::atomic<int> clients {0};
	std::thread acceptThread;
	std::thread sendThread;

	std::mutex mutex;
	std::vector<unsigned char> frame;
	int frameWidth = 0;
	int frameHeight = 0;
	uint32_t sequence = 0;
};

} // namespace loaf
