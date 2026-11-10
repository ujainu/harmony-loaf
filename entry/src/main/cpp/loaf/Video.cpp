#include "Video.h"

#include "GLRenderer.h"
#include "Environment.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <limits.h>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "loafcore"
#include "../Log.h"

namespace loaf {

namespace {

std::mutex registryMutex;
std::map<OH_AVPlayer *, VideoPlayer *> registry;

} // namespace

//--------------------------------------------------------------
bool VideoPlayer::load(const std::string &path, GLRenderer &renderer) {
	release(&renderer);

	texture = renderer.createExternalTexture();
	if(texture == 0) {
		LOGE("video: could not create external texture");
		return false;
	}
	image = OH_NativeImage_Create(texture, GL_TEXTURE_EXTERNAL_OES);
	if(image == nullptr) {
		LOGE("video: could not create native image");
		renderer.deleteTexture(texture);
		texture = 0;
		return false;
	}
	OH_OnFrameAvailableListener listener;
	listener.context = this;
	listener.onFrameAvailable = onFrame;
	OH_NativeImage_SetOnFrameAvailableListener(image, listener);

	OHNativeWindow *window = OH_NativeImage_AcquireNativeWindow(image);
	if(window == nullptr) {
		LOGE("video: could not acquire native window");
		OH_NativeImage_Destroy(&image);
		renderer.deleteTexture(texture);
		texture = 0;
		return false;
	}

	player = OH_AVPlayer_Create();
	if(player == nullptr) {
		LOGE("video: could not create player");
		OH_NativeImage_Destroy(&image);
		renderer.deleteTexture(texture);
		texture = 0;
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(registryMutex);
		registry[player] = this;
	}
	AVPlayerCallback callback {onInfo, onError};
	OH_AVPlayer_SetPlayerCallback(player, callback);
	OH_AVPlayer_SetVideoSurface(player, window);

	std::string absolute = path;
	char resolved[PATH_MAX] = {0};
	if(realpath(path.c_str(), resolved) != nullptr) {
		absolute = resolved;
	}
	sourceFd = open(absolute.c_str(), O_RDONLY);
	if(sourceFd < 0) {
		LOGE("video: could not open %{public}s", absolute.c_str());
		release(&renderer);
		return false;
	}
	struct stat info {};
	if(fstat(sourceFd, &info) != 0) {
		LOGE("video: could not stat %{public}s", absolute.c_str());
		release(&renderer);
		return false;
	}
	OH_AVErrCode result = OH_AVPlayer_SetFDSource(player, sourceFd, 0, (int64_t)info.st_size);
	if(result != AV_ERR_OK) {
		LOGE("video: could not set source %{public}s (%{public}d)", absolute.c_str(), (int)result);
		release(&renderer);
		return false;
	}
	result = OH_AVPlayer_Prepare(player);
	if(result != AV_ERR_OK) {
		LOGE("video: prepare failed (%{public}d)", (int)result);
		release(&renderer);
		return false;
	}
	LOGI("video: loading %{public}s", absolute.c_str());
	return true;
}

//--------------------------------------------------------------
VideoPlayer::~VideoPlayer() {
	release(nullptr);
}

//--------------------------------------------------------------
void VideoPlayer::release(GLRenderer *renderer) {
	if(player != nullptr) {
		OH_AVPlayer_Stop(player);
		OH_AVPlayer_Release(player);
		std::lock_guard<std::mutex> lock(registryMutex);
		registry.erase(player);
		player = nullptr;
	}
	if(sourceFd >= 0) {
		close(sourceFd);
		sourceFd = -1;
	}
	if(image != nullptr) {
		OH_NativeImage_Destroy(&image);
		image = nullptr;
	}
	if(texture != 0 && renderer != nullptr) {
		renderer->deleteTexture(texture);
		texture = 0;
	}
	playing = false;
	pendingPlay = false;
}

//--------------------------------------------------------------
void VideoPlayer::onInfo(OH_AVPlayer *player, AVPlayerOnInfoType type, int32_t extra) {
	VideoPlayer *self = nullptr;
	{
		std::lock_guard<std::mutex> lock(registryMutex);
		auto found = registry.find(player);
		if(found != registry.end()) {
			self = found->second;
		}
	}
	if(self == nullptr) {
		return;
	}
	if(type == AV_INFO_TYPE_STATE_CHANGE) {
		if(extra == AV_PREPARED) {
			int32_t value = 0;
			if(OH_AVPlayer_GetDuration(player, &value) == AV_ERR_OK) {
				self->duration = value / 1000.0;
			}
			int32_t videoWidth = 0;
			int32_t videoHeight = 0;
			if(OH_AVPlayer_GetVideoWidth(player, &videoWidth) == AV_ERR_OK &&
			   OH_AVPlayer_GetVideoHeight(player, &videoHeight) == AV_ERR_OK) {
				self->width = videoWidth;
				self->height = videoHeight;
			}
			if(self->looping) {
				OH_AVPlayer_SetLooping(player, true);
			}
			LOGI("video: prepared duration %{public}.3f", self->duration);
			if(self->pendingPlay) {
				self->pendingPlay = false;
				OH_AVPlayer_Play(player);
				self->playing = true;
			}
		}
		else if(extra == AV_PLAYING) {
			self->playing = true;
		}
		else if(extra == AV_PAUSED) {
			self->playing = false;
		}
		else if(extra == AV_COMPLETED) {
			if(self->looping) {
				OH_AVPlayer_Seek(player, 0, AV_SEEK_NEXT_SYNC);
				OH_AVPlayer_Play(player);
			}
			else {
				self->playing = false;
			}
		}
		else if(extra == AV_STOPPED || extra == AV_IDLE) {
			self->playing = false;
		}
	}
}

//--------------------------------------------------------------
void VideoPlayer::onError(OH_AVPlayer *player, int32_t errorCode, const char *errorMsg) {
	LOGE("video: error %{public}d %{public}s", errorCode, errorMsg != nullptr ? errorMsg : "");
	VideoPlayer *self = nullptr;
	{
		std::lock_guard<std::mutex> lock(registryMutex);
		auto found = registry.find(player);
		if(found != registry.end()) {
			self = found->second;
		}
	}
	if(self != nullptr) {
		self->errorMessage = errorMsg != nullptr ? errorMsg : "video decode error";
		self->playing = false;
		self->pendingPlay = false;
		if(env().emit) {
			env().emit("{\"type\":\"error\",\"message\":\"video: " +
			           std::string(errorMsg != nullptr ? errorMsg : "decode error") + "\"}");
		}
	}
}

//--------------------------------------------------------------
void VideoPlayer::onFrame(void *context) {
	auto *self = static_cast<VideoPlayer *>(context);
	if(self != nullptr) {
		self->frameAvailable = true;
	}
}

//--------------------------------------------------------------
void VideoPlayer::play() {
	if(player == nullptr) {
		return;
	}
	pendingPlay = true;
	OH_AVErrCode result = OH_AVPlayer_Play(player);
	if(result == AV_ERR_OK) {
		playing = true;
	}
}

//--------------------------------------------------------------
void VideoPlayer::pause() {
	if(player != nullptr) {
		OH_AVPlayer_Pause(player);
		playing = false;
	}
}

//--------------------------------------------------------------
void VideoPlayer::stop() {
	if(player != nullptr) {
		OH_AVPlayer_Stop(player);
		playing = false;
	}
}

//--------------------------------------------------------------
void VideoPlayer::setLoop(bool loop) {
	looping = loop;
	if(player != nullptr) {
		OH_AVPlayer_SetLooping(player, loop);
	}
}

//--------------------------------------------------------------
bool VideoPlayer::isLooping() const {
	return looping;
}

//--------------------------------------------------------------
void VideoPlayer::setVolume(float volume) {
	if(player != nullptr) {
		float value = std::max(0.0f, std::min(1.0f, volume));
		OH_AVPlayer_SetVolume(player, value, value);
	}
}

//--------------------------------------------------------------
double VideoPlayer::getPosition() {
	int32_t value = 0;
	if(player != nullptr && OH_AVPlayer_GetCurrentTime(player, &value) == AV_ERR_OK) {
		return value / 1000.0;
	}
	return 0.0;
}

//--------------------------------------------------------------
void VideoPlayer::setPosition(double seconds) {
	if(player != nullptr) {
		OH_AVPlayer_Seek(player, (int32_t)(seconds * 1000.0), AV_SEEK_NEXT_SYNC);
	}
}

//--------------------------------------------------------------
void VideoPlayer::draw(GLRenderer &renderer, float x, float y, float w, float h) {
	if(image == nullptr || texture == 0) {
		return;
	}
	if(frameAvailable.exchange(false)) {
		OH_NativeImage_UpdateSurfaceImage(image);
		OH_NativeImage_GetTransformMatrix(image, textureMatrix);
		frames++;
		if(frames == 1) {
			LOGI("video: first frame %{public}dx%{public}d", width, height);
		}
	}
	renderer.drawExternalTexture(texture, textureMatrix, x, y, w, h);
}

} // namespace loaf
