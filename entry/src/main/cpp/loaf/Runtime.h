#pragma once

#include "Environment.h"
#include "GLRenderer.h"
#include "LuaVM.h"
#include "Osc.h"
#include "FrameServer.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace loaf {

/// owns the EGL surface, the render thread and the Lua interpreter
class Runtime {
public:

	static Runtime &instance();

	void surfaceCreated(void *nativeWindow, int width, int height);
	void surfaceChanged(void *nativeWindow, int width, int height);
	void surfaceDestroyed();

	void setDensity(float density);

	void setEventSink(std::function<void(const std::string &)> sink) {
		std::lock_guard<std::mutex> lock(eventMutex);
		eventSink = std::move(sink);
	}
	void emit(const std::string &json);

	bool isRunning() const { return running; }
	bool hasScript() const;
	std::string infoJson();

	/// thread safe entry points, executed on the render thread
	void loadScript(const std::string &path);
	void loadScriptText(const std::string &name, const std::string &text, const std::string &directory);
	void reloadScript();
	void clearScript();
	void evaluate(const std::string &code);
	void queueReload();
	void queueClear();

	void pointerEvent(float x, float y, int button, int action);
	void pointerEventPixels(float x, float y, int button, int action);
	void keyEvent(int key, bool down);
	void dragEvent(const std::vector<std::string> &files);

	void setOscSendHost(const std::string &host);
	void setOscSendPort(int port);
	void setOscListenPort(int port);
	void startOsc();
	void stopOsc();
	void sendOsc(const std::string &address, const std::string &type, const std::string &value);

private:

	Runtime();
	~Runtime();

	void post(std::function<void()> command);
	void processCommands();
	void startThread(void *nativeWindow, int width, int height);
	void stopThread();
	void loop();
	void drawDemo();
	void drawErrorOverlay();
	void pumpOsc();
	void watchScript();
	void loadNow(const std::string &path);
	void clearNow();
	std::string scriptDirectory(const std::string &path) const;

	std::thread renderThread;
	std::mutex mutex;
	std::condition_variable condition;
	std::atomic<bool> running {false};
	std::atomic<bool> stopRequested {false};

	void *window = nullptr;
	int pendingWidth = 0;
	int pendingHeight = 0;
	std::atomic<float> pendingDensity {2.0f};

	GLRenderer renderer;
	LuaVM lua;
	Osc osc;

	double startTime = 0.0;
	double watchTimer = 0.0;
	double frameServerTimer = 0.0;
	long long scriptTimestamp = 0;
	std::string lastReportedError;

	std::mutex commandMutex;
	std::deque<std::function<void()>> commands;

	std::mutex eventMutex;
	std::function<void(const std::string &)> eventSink;
};

} // namespace loaf
