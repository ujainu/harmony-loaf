#include "Audio.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <fstream>

#define LOG_TAG "loafcore"
#include "../Log.h"

namespace loaf {

namespace {

uint32_t readU32(const unsigned char *data) {
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

uint16_t readU16(const unsigned char *data) {
	return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

bool tagEquals(const unsigned char *data, const char *tag) {
	return std::memcmp(data, tag, 4) == 0;
}

float sampleAt(const unsigned char *data, int bits, bool floating, size_t index) {
	if(floating && bits == 32) {
		float value = 0.0f;
		std::memcpy(&value, data + index * 4, 4);
		return value;
	}
	if(bits == 16) {
		int16_t value = (int16_t)readU16(data + index * 2);
		return value / 32768.0f;
	}
	if(bits == 24) {
		int32_t value = (int32_t)((uint32_t)data[index * 3] |
		                          ((uint32_t)data[index * 3 + 1] << 8) |
		                          ((uint32_t)data[index * 3 + 2] << 16));
		if(value & 0x800000) {
			value |= (int32_t)0xff000000;
		}
		return (float)value / 8388608.0f;
	}
	if(bits == 32) {
		int32_t value = (int32_t)readU32(data + index * 4);
		return (float)((double)value / 2147483648.0);
	}
	if(bits == 8) {
		return (data[index] - 128) / 128.0f;
	}
	return 0.0f;
}

} // namespace

//--------------------------------------------------------------
bool SoundPlayer::load(const std::string &path) {
	std::ifstream stream(path, std::ios::binary);
	if(!stream) {
		LOGE("audio: could not open %{public}s", path.c_str());
		return false;
	}
	std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)),
	                                 std::istreambuf_iterator<char>());
	if(bytes.size() < 44 || !tagEquals(bytes.data(), "RIFF") || !tagEquals(bytes.data() + 8, "WAVE")) {
		LOGE("audio: %{public}s is not a wav file", path.c_str());
		return false;
	}

	int format = 1;
	int channels = 1;
	int sampleRate = 44100;
	int bits = 16;
	const unsigned char *pcm = nullptr;
	size_t pcmBytes = 0;

	size_t offset = 12;
	while(offset + 8 <= bytes.size()) {
		const unsigned char *chunk = bytes.data() + offset;
		uint32_t size = readU32(chunk + 4);
		if(tagEquals(chunk, "fmt ") && offset + 8 + size <= bytes.size()) {
			const unsigned char *fmt = chunk + 8;
			format = readU16(fmt);
			channels = readU16(fmt + 2);
			sampleRate = (int)readU32(fmt + 4);
			bits = readU16(fmt + 14);
			if(format == 0xFFFE && size >= 26) {
				format = readU16(fmt + 24);
			}
		}
		else if(tagEquals(chunk, "data")) {
			pcm = chunk + 8;
			pcmBytes = size;
			if(offset + 8 + size > bytes.size()) {
				pcmBytes = bytes.size() - offset - 8;
			}
			break;
		}
		offset += 8 + size + (size & 1);
	}
	if(pcm == nullptr || pcmBytes == 0 || channels <= 0 || sampleRate <= 0) {
		LOGE("audio: %{public}s has no usable data", path.c_str());
		return false;
	}

	bool floating = (format == 3);
	int bytesPerSample = bits / 8;
	size_t frames = pcmBytes / (bytesPerSample * channels);
	double sourceDuration = (double)frames / sampleRate;
	size_t outputFrames = (size_t)std::ceil(sourceDuration * AudioEngine::kSampleRate);

	std::vector<float> output(outputFrames * AudioEngine::kChannels, 0.0f);
	for(size_t frame = 0; frame < outputFrames; ++frame) {
		double sourcePosition = (double)frame * sampleRate / AudioEngine::kSampleRate;
		size_t index = (size_t)sourcePosition;
		if(index >= frames) {
			break;
		}
		double fraction = sourcePosition - (double)index;
		float left = 0.0f;
		float right = 0.0f;
		for(int c = 0; c < channels && c < 2; ++c) {
			size_t sampleIndex = index * channels + c;
			if(sampleIndex >= frames * channels) {
				break;
			}
			float value = sampleAt(pcm, bits, floating, sampleIndex);
			if(fraction > 0.0 && sampleIndex + channels < frames * channels) {
				float next = sampleAt(pcm, bits, floating, sampleIndex + channels);
				value = (float)(value + (next - value) * fraction);
			}
			if(c == 0) left = value;
			right = value;
		}
		if(channels == 1) {
			right = left;
		}
		output[frame * 2] = left;
		output[frame * 2 + 1] = right;
	}

	{
		std::lock_guard<std::mutex> lock(mutex);
		clip.samples = std::move(output);
		clip.sampleRate = AudioEngine::kSampleRate;
		clip.channels = AudioEngine::kChannels;
		clip.duration = sourceDuration;
		clip.valid = true;
		position = 0;
	}
	playing = false;
	paused = false;
	return true;
}

//--------------------------------------------------------------
SoundPlayer::~SoundPlayer() {
	AudioEngine::instance().detach(this);
}

//--------------------------------------------------------------
void SoundPlayer::play() {
	if(!clip.valid) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(mutex);
		if(position >= clip.samples.size() / 2) {
			position = 0;
		}
	}
	paused = false;
	playing = true;
	AudioEngine::instance().attach(this);
	AudioEngine::instance().start();
}

//--------------------------------------------------------------
void SoundPlayer::stop() {
	playing = false;
	paused = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		position = 0;
	}
}

//--------------------------------------------------------------
void SoundPlayer::pause() {
	paused = true;
}

//--------------------------------------------------------------
void SoundPlayer::setPaused(bool value) {
	paused = value;
}

//--------------------------------------------------------------
double SoundPlayer::getPosition() const {
	std::lock_guard<std::mutex> lock(mutex);
	return (double)position / AudioEngine::kSampleRate;
}

//--------------------------------------------------------------
void SoundPlayer::setPosition(double seconds) {
	std::lock_guard<std::mutex> lock(mutex);
	size_t frame = (size_t)(seconds * AudioEngine::kSampleRate);
	size_t frames = clip.samples.size() / 2;
	position = frame < frames ? frame : frames;
}

//--------------------------------------------------------------
AudioEngine &AudioEngine::instance() {
	static AudioEngine engine;
	return engine;
}

//--------------------------------------------------------------
AudioEngine::~AudioEngine() {
	stop();
}

//--------------------------------------------------------------
OH_AudioData_Callback_Result AudioEngine::writeData(OH_AudioRenderer *renderer, void *userData,
                                                    void *audioData, int32_t audioDataSize) {
	auto *engine = static_cast<AudioEngine *>(userData);
	if(engine == nullptr || audioData == nullptr) {
		return AUDIO_DATA_CALLBACK_RESULT_INVALID;
	}
	float *output = static_cast<float *>(audioData);
	int frames = audioDataSize / (int)(sizeof(float) * engine->kChannels);
	engine->mix(output, frames);
	return AUDIO_DATA_CALLBACK_RESULT_VALID;
}

//--------------------------------------------------------------
void AudioEngine::mix(float *output, int frames) {
	static int callbacks = 0;
	std::memset(output, 0, (size_t)frames * kChannels * sizeof(float));

	std::lock_guard<std::mutex> lock(mutex);
	for(SoundPlayer *player : players) {
		if(player == nullptr || !player->playing.load() || player->paused.load()) {
			continue;
		}
		std::lock_guard<std::mutex> playerLock(player->mutex);
		const std::vector<float> &samples = player->clip.samples;
		size_t totalFrames = samples.size() / kChannels;
		for(int frame = 0; frame < frames; ++frame) {
			if(player->position >= totalFrames) {
				if(player->looping) {
					player->position = 0;
				}
				else {
					player->playing = false;
					break;
				}
			}
			size_t index = player->position * kChannels;
			output[frame * kChannels] += samples[index] * player->volume;
			output[frame * kChannels + 1] += samples[index + 1] * player->volume;
			player->position++;
		}
	}
	if(callbacks++ % 200 == 0) {
		LOGI("audio: callback %{public}d frames %{public}d players %{public}d",
		     callbacks, frames, (int)players.size());
	}
}

//--------------------------------------------------------------
bool AudioEngine::start() {
	if(running) {
		return true;
	}
	OH_AudioStream_Result result = OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_RENDERER);
	if(result != AUDIOSTREAM_SUCCESS || builder == nullptr) {
		LOGE("audio: could not create builder (%{public}d)", (int)result);
		builder = nullptr;
		return false;
	}
	OH_AudioStreamBuilder_SetSamplingRate(builder, kSampleRate);
	OH_AudioStreamBuilder_SetChannelCount(builder, kChannels);
	OH_AudioStreamBuilder_SetSampleFormat(builder, AUDIOSTREAM_SAMPLE_F32LE);
	OH_AudioStreamBuilder_SetEncodingType(builder, AUDIOSTREAM_ENCODING_TYPE_RAW);
	OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_FAST);
	OH_AudioStreamBuilder_SetRendererWriteDataCallback(builder, writeData, this);

	result = OH_AudioStreamBuilder_GenerateRenderer(builder, &renderer);
	if(result != AUDIOSTREAM_SUCCESS || renderer == nullptr) {
		LOGE("audio: could not create renderer (%{public}d)", (int)result);
		OH_AudioStreamBuilder_Destroy(builder);
		builder = nullptr;
		renderer = nullptr;
		return false;
	}
	result = OH_AudioRenderer_Start(renderer);
	if(result != AUDIOSTREAM_SUCCESS) {
		LOGE("audio: could not start renderer (%{public}d)", (int)result);
		OH_AudioRenderer_Release(renderer);
		OH_AudioStreamBuilder_Destroy(builder);
		renderer = nullptr;
		builder = nullptr;
		return false;
	}
	running = true;
	LOGI("audio: renderer started %{public}d Hz %{public}d ch", kSampleRate, kChannels);
	return true;
}

//--------------------------------------------------------------
void AudioEngine::stop() {
	if(renderer != nullptr) {
		OH_AudioRenderer_Stop(renderer);
		OH_AudioRenderer_Release(renderer);
		renderer = nullptr;
	}
	if(builder != nullptr) {
		OH_AudioStreamBuilder_Destroy(builder);
		builder = nullptr;
	}
	running = false;
}

//--------------------------------------------------------------
void AudioEngine::attach(SoundPlayer *player) {
	std::lock_guard<std::mutex> lock(mutex);
	if(std::find(players.begin(), players.end(), player) == players.end()) {
		players.push_back(player);
	}
}

//--------------------------------------------------------------
void AudioEngine::detach(SoundPlayer *player) {
	std::lock_guard<std::mutex> lock(mutex);
	players.erase(std::remove(players.begin(), players.end(), player), players.end());
}

} // namespace loaf
