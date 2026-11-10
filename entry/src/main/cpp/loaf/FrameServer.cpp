#include "FrameServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

#define LOG_TAG "loafcore"
#include "../Log.h"

namespace loaf {

namespace {

void putU32(std::vector<unsigned char> &out, uint32_t value) {
	out.push_back((unsigned char)(value & 0xff));
	out.push_back((unsigned char)((value >> 8) & 0xff));
	out.push_back((unsigned char)((value >> 16) & 0xff));
	out.push_back((unsigned char)((value >> 24) & 0xff));
}

} // namespace

//--------------------------------------------------------------
FrameServer &FrameServer::instance() {
	static FrameServer server;
	return server;
}

//--------------------------------------------------------------
FrameServer::~FrameServer() {
	stop();
}

//--------------------------------------------------------------
bool FrameServer::start(const std::string &name, int newPort, int fps, int scale) {
	stop();
	serverName = name;
	currentPort = newPort > 0 && newPort < 65536 ? newPort : 9999;
	targetFps = fps > 0 && fps <= 60 ? fps : 10;
	downscale = scale >= 1 && scale <= 8 ? scale : 2;

	listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if(listenFd < 0) {
		LOGE("frameserver: socket failed");
		return false;
	}
	int reuse = 1;
	setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	sockaddr_in address {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons((uint16_t)currentPort);
	if(bind(listenFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
	   listen(listenFd, 4) != 0) {
		LOGE("frameserver: bind %{public}d failed", currentPort);
		close(listenFd);
		listenFd = -1;
		return false;
	}
	running = true;
	acceptThread = std::thread(&FrameServer::acceptLoop, this);
	sendThread = std::thread(&FrameServer::sendLoop, this);
	LOGI("frameserver: publishing '%{public}s' on tcp %{public}d (%{public}dfps, 1/%{public}d)",
	     serverName.c_str(), currentPort, targetFps, downscale);
	return true;
}

//--------------------------------------------------------------
void FrameServer::stop() {
	if(!running) {
		return;
	}
	running = false;
	if(listenFd >= 0) {
		shutdown(listenFd, SHUT_RDWR);
		close(listenFd);
		listenFd = -1;
	}
	{
		std::lock_guard<std::mutex> lock(mutex);
		if(clientFd >= 0) {
			shutdown(clientFd, SHUT_RDWR);
			close(clientFd);
			clientFd = -1;
		}
	}
	if(acceptThread.joinable()) {
		acceptThread.join();
	}
	if(sendThread.joinable()) {
		sendThread.join();
	}
	clients = 0;
	LOGI("frameserver: stopped");
}

//--------------------------------------------------------------
void FrameServer::acceptLoop() {
	while(running) {
		int client = accept(listenFd, nullptr, nullptr);
		if(client < 0) {
			if(!running) {
				break;
			}
			continue;
		}
		int timeoutMs = 500;
		setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeoutMs, sizeof(timeoutMs));
		std::lock_guard<std::mutex> lock(mutex);
		if(clientFd >= 0) {
			close(clientFd);
		}
		clientFd = client;
		clients = 1;
		LOGI("frameserver: client connected");
	}
}

//--------------------------------------------------------------
void FrameServer::sendLoop() {
	const auto interval = std::chrono::milliseconds(1000 / targetFps);
	while(running) {
		std::this_thread::sleep_for(interval);
		std::vector<unsigned char> payload;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t seq = 0;
		int fd = -1;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if(clientFd < 0 || frame.empty()) {
				continue;
			}
			fd = clientFd;
			payload = frame;
			width = (uint32_t)frameWidth;
			height = (uint32_t)frameHeight;
			seq = sequence;
		}

		std::vector<unsigned char> packet;
		packet.reserve(20 + payload.size());
		packet.insert(packet.end(), {'L', 'O', 'A', 'F'});
		putU32(packet, width);
		putU32(packet, height);
		putU32(packet, seq);
		putU32(packet, (uint32_t)payload.size());
		packet.insert(packet.end(), payload.begin(), payload.end());

		size_t sent = 0;
		bool failed = false;
		while(sent < packet.size()) {
			ssize_t chunk = send(fd, packet.data() + sent, packet.size() - sent, MSG_NOSIGNAL);
			if(chunk <= 0) {
				failed = true;
				break;
			}
			sent += (size_t)chunk;
		}
		if(failed) {
			std::lock_guard<std::mutex> lock(mutex);
			if(clientFd == fd) {
				close(clientFd);
				clientFd = -1;
			}
			clients = 0;
			LOGW("frameserver: client disconnected");
		}
	}
}

//--------------------------------------------------------------
void FrameServer::publish(const std::vector<unsigned char> &rgba, int width, int height) {
	if(!running || width <= 0 || height <= 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex);
	if(clientFd < 0) {
		return; // nothing to do without a consumer
	}
	int outWidth = width / downscale;
	int outHeight = height / downscale;
	if(outWidth <= 0 || outHeight <= 0) {
		return;
	}
	frame.resize((size_t)outWidth * outHeight * 4);
	frameWidth = outWidth;
	frameHeight = outHeight;
	for(int y = 0; y < outHeight; ++y) {
		int sourceRow = height - 1 - y * downscale; // flip to top-left origin
		for(int x = 0; x < outWidth; ++x) {
			size_t source = ((size_t)sourceRow * width + x * downscale) * 4;
			size_t target = ((size_t)y * outWidth + x) * 4;
			if(source + 3 < rgba.size()) {
				frame[target] = rgba[source];
				frame[target + 1] = rgba[source + 1];
				frame[target + 2] = rgba[source + 2];
				frame[target + 3] = rgba[source + 3];
			}
		}
	}
	sequence++;
	published++;
}

} // namespace loaf
