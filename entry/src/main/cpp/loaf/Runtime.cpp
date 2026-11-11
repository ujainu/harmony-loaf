#include "Runtime.h"
#include "AudioStream.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

#define LOG_TAG "loafcore"
#include "../Log.h"

namespace loaf {

namespace {

Environment g_environment;

/// HarmonyOS key codes to the values loaf scripts expect (OpenFrameworks)
int mapKeyCode(int code) {
	if(code >= 2017 && code <= 2042) {
		return 'a' + (code - 2017);
	}
	if(code >= 2000 && code <= 2009) {
		return '0' + (code - 2000);
	}
	switch(code) {
		case 2049: return 9;      // tab
		case 2050: return ' ';
		case 2054: return 13;     // enter
		case 2055: return 8;      // backspace
		case 2070: return 27;     // escape
		case 2071: return 127;    // forward delete
		case 2012: return 0xFF01; // up
		case 2013: return 0xFF03; // down
		case 2014: return 0xFF00; // left
		case 2015: return 0xFF02; // right
		case 2047: return 0xFF0D; // shift
		case 2072: return 0xFF0E; // control
		default: return code;
	}
}

double nowSeconds() {
	return (double)std::chrono::duration_cast<std::chrono::microseconds>(
	           std::chrono::steady_clock::now().time_since_epoch()).count() / 1000000.0;
}

long long fileTimestamp(const std::string &path) {
	struct stat info {};
	if(stat(path.c_str(), &info) != 0) {
		return 0;
	}
	return (long long)info.st_mtim.tv_sec * 1000000000LL + (long long)info.st_mtim.tv_nsec;
}

std::string escapeJson(const std::string &text) {
	std::string out;
	out.reserve(text.size() + 16);
	for(char c : text) {
		switch(c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if((unsigned char)c < 0x20) {
					char buffer[8];
					std::snprintf(buffer, sizeof(buffer), "\\u%04x", (unsigned char)c);
					out += buffer;
				}
				else {
					out += c;
				}
		}
	}
	return out;
}

EGLDisplay eglDisplay = EGL_NO_DISPLAY;
EGLSurface eglSurface = EGL_NO_SURFACE;
EGLContext eglContext = EGL_NO_CONTEXT;

} // namespace

//--------------------------------------------------------------
Environment &env() {
	return g_environment;
}

//--------------------------------------------------------------
Runtime &Runtime::instance() {
	static Runtime runtime;
	return runtime;
}

//--------------------------------------------------------------
Runtime::Runtime() {
	env().renderer = &renderer;
	env().runtime = this;
	env().osc = &osc;
	env().emit = [this](const std::string &json) { emit(json); };
}

//--------------------------------------------------------------
Runtime::~Runtime() {
	stopThread();
	osc.stopListening();
}

//--------------------------------------------------------------
bool Runtime::hasScript() const {
	return lua.valid();
}

//--------------------------------------------------------------
std::string Runtime::infoJson() {
	std::ostringstream out;
	out << "{\"running\":" << (running ? "true" : "false")
	    << ",\"script\":\"" << escapeJson(lua.path()) << "\""
	    << ",\"frameRate\":" << env().targetFrameRate
	    << ",\"width\":" << renderer.width()
	    << ",\"height\":" << renderer.height()
	    << ",\"listening\":" << (osc.isListening() ? "true" : "false")
	    << ",\"listenPort\":" << osc.listenPort()
	    << ",\"error\":\"" << escapeJson(lua.error()) << "\"}";
	return out.str();
}

//--------------------------------------------------------------
void Runtime::surfaceCreated(void *nativeWindow, int width, int height) {
	LOGI("surface created %dx%d", width, height);
	startThread(nativeWindow, width, height);
}

//--------------------------------------------------------------
void Runtime::surfaceChanged(void *nativeWindow, int width, int height) {
	LOGI("surface changed %dx%d", width, height);
	if(!running) {
		startThread(nativeWindow, width, height);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(mutex);
		window = nativeWindow;
		pendingWidth = width;
		pendingHeight = height;
	}
	post([this, width, height]() {
		env().windowWidth = width;
		env().windowHeight = height;
		lua.callNumbers("windowResized", {(double)renderer.width(), (double)renderer.height()});
	});
}

//--------------------------------------------------------------
void Runtime::surfaceDestroyed() {
	LOGI("surface destroyed");
	stopThread();
}

//--------------------------------------------------------------
void Runtime::setDensity(float density) {
	pendingDensity = density;
}

//--------------------------------------------------------------
void Runtime::emit(const std::string &json) {
	std::function<void(const std::string &)> sink;
	{
		std::lock_guard<std::mutex> lock(eventMutex);
		sink = eventSink;
	}
	if(sink) {
		sink(json);
	}
}

//--------------------------------------------------------------
void Runtime::post(std::function<void()> command) {
	std::lock_guard<std::mutex> lock(commandMutex);
	commands.push_back(std::move(command));
}

//--------------------------------------------------------------
void Runtime::loadScript(const std::string &path) {
	post([this, path]() { loadNow(path); });
}

//--------------------------------------------------------------
void Runtime::loadScriptText(const std::string &name, const std::string &text, const std::string &directory) {
	post([this, name, text, directory]() {
		if(!lua.loadBuffer(text, name, directory)) {
			return;
		}
		env().scriptPath = directory + "/" + name;
		env().scriptDir = directory;
		env().background = Color{0, 0, 0, 1};
		lastReportedError.clear();
		scriptTimestamp = 0;
		emit("{\"type\":\"script\",\"path\":\"" + escapeJson(env().scriptPath) + "\"}");
	});
}

//--------------------------------------------------------------
void Runtime::reloadScript() {
	post([this]() {
		if(lua.path().empty()) {
			return;
		}
		lua.reload();
		scriptTimestamp = fileTimestamp(lua.path());
		lastReportedError.clear();
		emit("{\"type\":\"script\",\"path\":\"" + escapeJson(lua.path()) + "\",\"reload\":true}");
	});
}

//--------------------------------------------------------------
void Runtime::clearScript() {
	post([this]() { clearNow(); });
}

//--------------------------------------------------------------
void Runtime::queueReload() {
	// called from lua, already on the render thread: defer until the callback returns
	post([this]() {
		if(!lua.path().empty()) {
			lua.reload();
			scriptTimestamp = fileTimestamp(lua.path());
		}
	});
}

//--------------------------------------------------------------
void Runtime::queueClear() {
	post([this]() { clearNow(); });
}

//--------------------------------------------------------------
void Runtime::clearNow() {
	lua.clear();
	env().background = Color{0, 0, 0, 1};
	lastReportedError.clear();
	emit("{\"type\":\"info\",\"message\":\"script cleared\"}");
}

//--------------------------------------------------------------
void Runtime::evaluate(const std::string &code) {
	post([this, code]() { lua.exec(code); });
}

//--------------------------------------------------------------
void Runtime::pointerEvent(float x, float y, int button, int action) {
	post([this, x, y, button, action]() {
		env().input.mouseX = x;
		env().input.mouseY = y;
		if(button >= 0 && button < 3) {
			env().input.mouseButton[button] = action == 1;
		}
		if(action == 1) {
			lua.callNumbers("mousePressed", {x, y, (double)button});
		}
		else if(action == 2) {
			lua.callNumbers("mouseReleased", {x, y, (double)button});
		}
		else {
			lua.callNumbers("mouseMoved", {x, y});
			if(env().input.mouseButton[0]) {
				lua.callNumbers("mouseDragged", {x, y, 0.0});
			}
		}
	});
}

//--------------------------------------------------------------
void Runtime::pointerEventPixels(float x, float y, int button, int action) {
	pointerEvent(renderer.toLogicalX(x), renderer.toLogicalY(y), button, action);
}

//--------------------------------------------------------------
void Runtime::keyEvent(int key, bool down) {
	key = mapKeyCode(key);
	// ArkUI can deliver the same key through more than one focus path; drop
	// duplicates that arrive in the same frame window.
	static int lastKey = -1;
	static bool lastDown = false;
	static double lastKeyTime = 0.0;
	double now = nowSeconds();
	if(down && key == lastKey && lastDown && now - lastKeyTime < 0.06) {
		return;
	}
	lastKey = key;
	lastDown = down;
	lastKeyTime = now;
	post([this, key, down]() {
		env().input.lastKey = key;
		lua.callNumbers(down ? "keyPressed" : "keyReleased", {(double)key});
	});
}

//--------------------------------------------------------------
void Runtime::dragEvent(const std::vector<std::string> &files) {
	post([this, files]() {
		lua.callFiles("dragEvent", files);
	});
}

//--------------------------------------------------------------
void Runtime::setOscSendHost(const std::string &host) {
	post([this, host]() { osc.setSendHost(host); });
}

//--------------------------------------------------------------
void Runtime::setOscSendPort(int port) {
	post([this, port]() { osc.setSendPort(port); });
}

//--------------------------------------------------------------
void Runtime::setOscListenPort(int port) {
	post([this, port]() { osc.setListenPort(port); });
}

//--------------------------------------------------------------
void Runtime::startOsc() {
	post([this]() { osc.startListening(osc.listenPort()); });
}

//--------------------------------------------------------------
void Runtime::stopOsc() {
	post([this]() { osc.stopListening(); });
}

//--------------------------------------------------------------
void Runtime::sendOsc(const std::string &address, const std::string &type, const std::string &value) {
	post([this, address, type, value]() {
		if(type == "f") {
			osc.sendFloat(address, std::strtof(value.c_str(), nullptr));
		}
		else if(type == "i") {
			osc.sendInt(address, (int32_t)std::strtol(value.c_str(), nullptr, 10));
		}
		else if(type == "s") {
			osc.sendString(address, value);
		}
		else {
			osc.sendBang(address);
		}
	});
}

//--------------------------------------------------------------
void Runtime::startThread(void *nativeWindow, int width, int height) {
	stopThread();
	{
		std::lock_guard<std::mutex> lock(mutex);
		window = nativeWindow;
		pendingWidth = width;
		pendingHeight = height;
	}
	stopRequested = false;
	running = true;
	renderThread = std::thread(&Runtime::loop, this);
}

//--------------------------------------------------------------
void Runtime::stopThread() {
	if(!running) {
		return;
	}
	stopRequested = true;
	condition.notify_all();
	if(renderThread.joinable()) {
		renderThread.join();
	}
	running = false;
}

//--------------------------------------------------------------
void Runtime::processCommands() {
	std::deque<std::function<void()>> pending;
	{
		std::lock_guard<std::mutex> lock(commandMutex);
		pending.swap(commands);
	}
	for(auto &command : pending) {
		command();
	}
}

//--------------------------------------------------------------
void Runtime::loop() {
	EGLint configAttributes[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 0,
		EGL_NONE
	};
	EGLConfig config = nullptr;
	EGLint numConfigs = 0;
	EGLint major = 0;
	EGLint minor = 0;

	eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if(eglDisplay == EGL_NO_DISPLAY) {
		LOGE("eglGetDisplay failed");
		running = false;
		return;
	}
	if(!eglInitialize(eglDisplay, &major, &minor)) {
		LOGE("eglInitialize failed 0x%x", eglGetError());
		running = false;
		return;
	}
	if(!eglChooseConfig(eglDisplay, configAttributes, &config, 1, &numConfigs) || numConfigs < 1) {
		LOGE("eglChooseConfig failed 0x%x", eglGetError());
		eglTerminate(eglDisplay);
		eglDisplay = EGL_NO_DISPLAY;
		running = false;
		return;
	}

	EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
	eglContext = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT, contextAttributes);
	if(eglContext == EGL_NO_CONTEXT) {
		LOGE("eglCreateContext failed 0x%x", eglGetError());
		eglTerminate(eglDisplay);
		eglDisplay = EGL_NO_DISPLAY;
		running = false;
		return;
	}

	void *currentWindow = nullptr;
	int width = 0;
	int height = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		currentWindow = window;
		width = pendingWidth;
		height = pendingHeight;
	}
	eglSurface = eglCreateWindowSurface(eglDisplay, config, (EGLNativeWindowType)currentWindow, nullptr);
	if(eglSurface == EGL_NO_SURFACE) {
		LOGE("eglCreateWindowSurface failed 0x%x", eglGetError());
		eglDestroyContext(eglDisplay, eglContext);
		eglContext = EGL_NO_CONTEXT;
		eglTerminate(eglDisplay);
		eglDisplay = EGL_NO_DISPLAY;
		running = false;
		return;
	}
	eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
	eglSwapInterval(eglDisplay, 1);

	if(!renderer.init()) {
		LOGE("renderer init failed");
	}
	renderer.resize(width, height, pendingDensity);
	env().windowWidth = renderer.width();
	env().windowHeight = renderer.height();
	startTime = nowSeconds();
	env().lastTime = startTime;
	emit("{\"type\":\"ready\",\"message\":\"loaf runtime ready\"}");

	while(!stopRequested) {
		double frameStart = nowSeconds();
		processCommands();
		{
			std::lock_guard<std::mutex> lock(mutex);
			if(currentWindow != window && window != nullptr) {
				currentWindow = window;
				eglDestroySurface(eglDisplay, eglSurface);
				eglSurface = eglCreateWindowSurface(eglDisplay, config,
				                                    (EGLNativeWindowType)currentWindow, nullptr);
				eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
			}
			if(pendingWidth > 0 && pendingHeight > 0 &&
			   (pendingWidth != width || pendingHeight != height)) {
				width = pendingWidth;
				height = pendingHeight;
				renderer.resize(width, height, pendingDensity);
				env().windowWidth = renderer.width();
				env().windowHeight = renderer.height();
				lua.callNumbers("windowResized", {(double)renderer.width(), (double)renderer.height()});
			}
		}

		env().deltaTime = frameStart - env().lastTime;
		env().lastTime = frameStart;
		env().time = frameStart - startTime;
		env().frameNum++;

		renderer.beginFrame();

		if(lua.valid()) {
			if(lua.setupPending) {
				lua.setupPending = false;
				lua.call("setup");
			}
			if(lua.error().empty()) {
				lua.call("update");
			}
			renderer.clear(env().background);
			if(lua.error().empty()) {
				lua.call("draw");
			}
			SoundStream *stream = SoundStream::active();
			if(stream != nullptr && stream->isRunning() && lua.error().empty()) {
				int targetBacklog = (int)(stream->getSampleRate() * 0.12);
				for(int i = 0; i < 16 && stream->bufferedFrames() < targetBacklog; ++i) {
					if(!lua.callAudioOut(*stream)) {
						break;
					}
				}
			}
			if(!env().pendingMessages.empty()) {
				std::vector<std::string> messages = env().pendingMessages;
				env().pendingMessages.clear();
				for(const auto &message : messages) {
					lua.callString("gotMessage", message);
				}
			}
			lua.pollHardware();
			pumpOsc();
			watchScript();
			if(!lua.error().empty()) {
				drawErrorOverlay();
			}
			if(lua.error() != lastReportedError) {
				lastReportedError = lua.error();
				if(!lastReportedError.empty()) {
					emit("{\"type\":\"error\",\"message\":\"" + escapeJson(lastReportedError) + "\"}");
				}
			}
		}
		else {
			drawDemo();
		}

		renderer.endFrame();

		if(FrameServer::instance().isPublishing()) {
			frameServerTimer += env().deltaTime;
			if(frameServerTimer >= 0.1) {
				frameServerTimer = 0.0;
				std::vector<unsigned char> pixels;
				renderer.readPixels(renderer.pixelWidth(), renderer.pixelHeight(), pixels);
				FrameServer::instance().publish(pixels, renderer.pixelWidth(), renderer.pixelHeight());
			}
		}
		eglSwapBuffers(eglDisplay, eglSurface);

		double frameTime = nowSeconds() - frameStart;
		double target = 1.0 / std::max(1, env().targetFrameRate);
		double remaining = target - frameTime;
		if(remaining > 0.001) {
			std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
		}
	}

	renderer.shutdown();
	eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if(eglSurface != EGL_NO_SURFACE) {
		eglDestroySurface(eglDisplay, eglSurface);
		eglSurface = EGL_NO_SURFACE;
	}
	if(eglContext != EGL_NO_CONTEXT) {
		eglDestroyContext(eglDisplay, eglContext);
		eglContext = EGL_NO_CONTEXT;
	}
	if(eglDisplay != EGL_NO_DISPLAY) {
		eglTerminate(eglDisplay);
		eglDisplay = EGL_NO_DISPLAY;
	}
	LOGI("render loop stopped");
}

//--------------------------------------------------------------
void Runtime::pumpOsc() {
	OscMessage message;
	while(osc.poll(message)) {
		// loaf control message: /loaf/load <absolute path>
		if(message.address == "/loaf/load" && !message.args.empty() &&
		   message.args[0].type == OscType::String) {
			loadScript(message.args[0].s);
			continue;
		}
		lua.callOsc(message);
	}
}

//--------------------------------------------------------------
void Runtime::watchScript() {
	watchTimer += env().deltaTime;
	if(watchTimer < 0.4) {
		return;
	}
	watchTimer = 0.0;
	if(lua.path().empty()) {
		return;
	}
	long long timestamp = fileTimestamp(lua.path());
	if(timestamp == 0 || timestamp == scriptTimestamp) {
		return;
	}
	scriptTimestamp = timestamp;
	LOGI("script changed, reloading %s", lua.path().c_str());
	lua.reload();
	lastReportedError.clear();
	emit("{\"type\":\"script\",\"path\":\"" + escapeJson(lua.path()) + "\",\"reload\":true}");
}

//--------------------------------------------------------------
void Runtime::drawErrorOverlay() {
	std::string error = lua.error();
	if(error.empty()) {
		return;
	}
	// mirrors loaf's ofDrawBitmapStringHighlight error rendering:
	// white highlight, crimson text, starting at x=8, centered vertically
	std::vector<std::string> lines;
	std::string current;
	for(char c : error) {
		if(c == '\n') {
			lines.push_back(current);
			current.clear();
		}
		else {
			current += c;
		}
	}
	if(!current.empty()) {
		lines.push_back(current);
	}

	int lineChars = std::max(8, renderer.width() / 8 - 2);
	float x = 8.0f;
	float y = renderer.height() * 0.5f - (float)lines.size() * 10.0f;
	float fontSize = 12.0f;
	renderer.setAlphaBlending(true);
	renderer.setFill(true);
	for(const std::string &line : lines) {
		for(size_t offset = 0; offset < line.size(); offset += (size_t)lineChars) {
			std::string chunk = line.substr(offset, (size_t)lineChars);
			float width = renderer.measureText(chunk, fontSize);
			renderer.setColor(Color{1.0f, 1.0f, 1.0f, 0.85f});
			renderer.drawRectangle(x - 2.0f, y - fontSize, width + 4.0f, fontSize + 5.0f);
			renderer.setColor(Color{220.0f / 255.0f, 20.0f / 255.0f, 60.0f / 255.0f, 1.0f});
			renderer.drawText(chunk, x, y, fontSize);
			y += 20.0f;
		}
	}
}

//--------------------------------------------------------------
void Runtime::drawDemo() {
	renderer.clear(Color{0.06f, 0.07f, 0.10f, 1.0f});
	float pulse = (float)(0.5 + 0.5 * std::sin(env().time * 2.0));

	renderer.setColor(Color{0.15f + 0.3f * pulse, 0.45f, 0.9f, 1.0f});
	renderer.setFill(true);
	renderer.drawCircle(renderer.width() * 0.5f, renderer.height() * 0.5f,
	                    110.0f + 40.0f * pulse);
	renderer.setColor(Color{1.0f, 0.75f, 0.1f, 1.0f});
	renderer.setFill(false);
	renderer.setLineWidth(4.0f);
	renderer.drawCircle(renderer.width() * 0.5f, renderer.height() * 0.5f, 220.0f);
	renderer.setFill(true);
	renderer.setLineWidth(1.0f);

	renderer.setColor(Color{0.9f, 0.9f, 0.95f, 1.0f});
	renderer.drawText("loaf on HarmonyOS PC", 40.0f, 70.0f, 28.0f);
	renderer.setColor(Color{0.6f, 0.65f, 0.75f, 1.0f});
	renderer.drawText("打开或拖入 .lua 脚本开始", 40.0f, 104.0f, 16.0f);
}

//--------------------------------------------------------------
std::string Runtime::scriptDirectory(const std::string &path) const {
	size_t slash = path.find_last_of('/');
	if(slash == std::string::npos) {
		return ".";
	}
	return path.substr(0, slash);
}

//--------------------------------------------------------------
void Runtime::loadNow(const std::string &path) {
	// set the script directory before loading: setup() may query it
	env().scriptPath = path;
	env().scriptDir = scriptDirectory(path);
	if(!lua.loadFile(path)) {
		OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0000, "loafcore",
		             "failed to load %{public}s: %{public}s", path.c_str(), lua.error().c_str());
		return;
	}
	env().background = Color{0, 0, 0, 1};
	scriptTimestamp = fileTimestamp(path);
	lastReportedError.clear();
	OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "loafcore", "loaded script: %{public}s", path.c_str());
	emit("{\"type\":\"script\",\"path\":\"" + escapeJson(path) + "\"}");
}

} // namespace loaf
