#pragma once

#include "GLRenderer.h"
#include "Osc.h"

#include <functional>
#include <string>
#include <vector>

namespace loaf {

class Runtime;

struct InputState {
	float mouseX = 0.0f;
	float mouseY = 0.0f;
	bool mouseButton[3] = {false, false, false};
	int lastKey = 0;
};

struct Environment {
	GLRenderer *renderer = nullptr;
	Runtime *runtime = nullptr;
	Osc *osc = nullptr;

	std::string scriptPath;
	std::string scriptDir;
	std::string windowTitle = "loaf";
	int windowWidth = 0;
	int windowHeight = 0;
	int screenWidth = 0;
	int screenHeight = 0;
	bool fullscreen = false;
	bool verbose = false;
	bool defaultKeys = true;
	int targetFrameRate = 60;

	int frameNum = 0;
	double time = 0.0;
	double deltaTime = 0.0;
	double lastTime = 0.0;

	InputState input;
	Color background {0.0f, 0.0f, 0.0f, 1.0f};

	std::function<void(const std::string &)> emit;
	std::vector<std::string> pendingMessages;
};

Environment &env();

} // namespace loaf
