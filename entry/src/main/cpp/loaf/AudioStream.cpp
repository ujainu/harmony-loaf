#include "AudioStream.h"

#include <algorithm>
#include <cstring>

#define LOG_TAG "loafcore"
#include "../Log.h"

namespace loaf {

namespace {
SoundStream *g_activeStream = nullptr;
}

//--------------------------------------------------------------
SoundStream::~SoundStream() {
	close();
}

//--------------------------------------------------------------
SoundStream *SoundStream::active() {
	return g_activeStream;
}

//--------------------------------------------------------------
bool SoundStream::setup(int out, int in, int rate, int size) {
	outChannels = std::max(1, out);
	inChannels = std::max(0, in);
	sampleRate = rate > 0 ? rate : 44100;
	bufferSize = size > 0 ? size : 512;
	{
		std::lock_guard<std::mutex> lock(mutex);
		blocks.clear();
		readIndex = 0;
	}
	g_activeStream = this;
	return true;
}

//--------------------------------------------------------------
void SoundStream::start() {
	if(running) {
		return;
	}
	OH_AudioStream_Result result = OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_RENDERER);
	if(result != AUDIOSTREAM_SUCCESS || builder == nullptr) {
		builder = nullptr;
		return;
	}
	OH_AudioStreamBuilder_SetSamplingRate(builder, sampleRate);
	OH_AudioStreamBuilder_SetChannelCount(builder, outChannels);
	OH_AudioStreamBuilder_SetSampleFormat(builder, AUDIOSTREAM_SAMPLE_F32LE);
	OH_AudioStreamBuilder_SetEncodingType(builder, AUDIOSTREAM_ENCODING_TYPE_RAW);
	OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_NORMAL);
	OH_AudioStreamBuilder_SetRendererWriteDataCallback(builder, writeData, this);
	result = OH_AudioStreamBuilder_GenerateRenderer(builder, &renderer);
	if(result != AUDIOSTREAM_SUCCESS || renderer == nullptr) {
		OH_AudioStreamBuilder_Destroy(builder);
		builder = nullptr;
		renderer = nullptr;
		return;
	}
	OH_AudioRenderer_SetVolume(renderer, volume);
	if(OH_AudioRenderer_Start(renderer) != AUDIOSTREAM_SUCCESS) {
		OH_AudioRenderer_Release(renderer);
		OH_AudioStreamBuilder_Destroy(builder);
		renderer = nullptr;
		builder = nullptr;
		return;
	}
	running = true;
	g_activeStream = this;
	LOGI("soundstream: started %{public}d Hz %{public}d ch buffer %{public}d",
	     sampleRate, outChannels, bufferSize);
}

//--------------------------------------------------------------
void SoundStream::stop() {
	if(renderer != nullptr) {
		OH_AudioRenderer_Stop(renderer);
	}
	running = false;
}

//--------------------------------------------------------------
void SoundStream::close() {
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
	if(g_activeStream == this) {
		g_activeStream = nullptr;
	}
}

//--------------------------------------------------------------
void SoundStream::setVolume(float value) {
	volume = std::max(0.0f, std::min(1.0f, value));
	if(renderer != nullptr) {
		OH_AudioRenderer_SetVolume(renderer, volume);
	}
}

//--------------------------------------------------------------
OH_AudioData_Callback_Result SoundStream::writeData(OH_AudioRenderer *renderer, void *userData,
                                                    void *audioData, int32_t audioDataSize) {
	auto *stream = static_cast<SoundStream *>(userData);
	if(stream == nullptr || audioData == nullptr) {
		return AUDIO_DATA_CALLBACK_RESULT_INVALID;
	}
	float *output = static_cast<float *>(audioData);
	int frames = audioDataSize / (int)(sizeof(float) * stream->outChannels);
	stream->readBlock(output, frames);
	return AUDIO_DATA_CALLBACK_RESULT_VALID;
}

//--------------------------------------------------------------
void SoundStream::readBlock(float *output, int frames) {
	std::lock_guard<std::mutex> lock(mutex);
	int written = 0;
	while(written < frames) {
		if(blocks.empty()) {
			// starving: repeat silence instead of stale audio
			for(int i = written; i < frames; ++i) {
				for(int c = 0; c < outChannels; ++c) {
					output[i * outChannels + c] = 0.0f;
				}
			}
			return;
		}
		std::vector<float> &block = blocks.front();
		int available = (int)((block.size() - readIndex) / outChannels);
		int take = std::min(frames - written, available);
		std::memcpy(output + (size_t)written * outChannels,
		            block.data() + readIndex, (size_t)take * outChannels * sizeof(float));
		readIndex += (size_t)take * outChannels;
		written += take;
		if(readIndex >= block.size()) {
			blocks.pop_front();
			readIndex = 0;
		}
	}
}

//--------------------------------------------------------------
void SoundStream::requestBlock(std::vector<float> &samples) {
	samples.assign((size_t)bufferSize * outChannels, 0.0f);
}

//--------------------------------------------------------------
void SoundStream::submit(const std::vector<float> &samples) {
	std::lock_guard<std::mutex> lock(mutex);
	size_t expected = (size_t)bufferSize * outChannels;
	std::vector<float> block(expected, 0.0f);
	std::memcpy(block.data(), samples.data(),
	            std::min(samples.size(), expected) * sizeof(float));
	blocks.push_back(std::move(block));
	while(blocks.size() > 32) {
		blocks.pop_front();
	}
}

//--------------------------------------------------------------
int SoundStream::bufferedFrames() {
	std::lock_guard<std::mutex> lock(mutex);
	size_t total = 0;
	for(const auto &block : blocks) {
		total += block.size();
	}
	total = total > readIndex ? total - readIndex : 0;
	return (int)(total / outChannels);
}

} // namespace loaf
