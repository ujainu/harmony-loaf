#pragma once

#include <multimedia/player_framework/avplayer.h>
#include <native_image/native_image.h>

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <atomic>
#include <mutex>
#include <string>

namespace loaf {

class GLRenderer;

/// video playback through AVPlayer rendered into a GL external texture
class VideoPlayer {
public:

	~VideoPlayer();

	bool load(const std::string &path, GLRenderer &renderer);
	void play();
	void pause();
	void stop();
	void setLoop(bool loop);
	bool isLooping() const;
	bool isPlaying() const { return playing.load(); }
	bool isLoaded() const { return player != nullptr; }
	void setVolume(float volume);
	double getDuration() const { return duration; }
	double getPosition();
	void setPosition(double seconds);
	int getWidth() const { return width; }
	int getHeight() const { return height; }
	unsigned long frameCount() const { return frames.load(); }
	const std::string &getError() const { return errorMessage; }
	void draw(GLRenderer &renderer, float x, float y, float w, float h);
	void release(GLRenderer *renderer);

private:

	static void onInfo(OH_AVPlayer *player, AVPlayerOnInfoType type, int32_t extra);
	static void onError(OH_AVPlayer *player, int32_t errorCode, const char *errorMsg);
	static void onFrame(void *context);

	OH_AVPlayer *player = nullptr;
	OH_NativeImage *image = nullptr;
	int sourceFd = -1;
	GLuint texture = 0;
	int width = 0;
	int height = 0;
	double duration = 0.0;
	bool looping = false;
	bool pendingPlay = false;
	std::atomic<bool> playing {false};
	std::atomic<bool> frameAvailable {false};
	std::atomic<unsigned long> frames {0};
	mutable std::mutex mutex;
	float textureMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	std::string errorMessage;
};

} // namespace loaf
