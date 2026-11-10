#pragma once

#include <ohaudio/native_audiostream_base.h>
#include <ohaudio/native_audiostreambuilder.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

namespace loaf {

/// Lua-driven output stream: the render thread fills blocks by calling the
/// script's audioOut(soundBuffer), the audio thread consumes them.
class SoundStream {
public:

	~SoundStream();

	bool setup(int outChannels, int inChannels, int sampleRate, int bufferSize);
	void start();
	void stop();
	void close();
	bool isRunning() const { return running; }

	int getSampleRate() const { return sampleRate; }
	int getBufferSize() const { return bufferSize; }
	int getNumOutputChannels() const { return outChannels; }
	int getNumInputChannels() const { return inChannels; }
	void setVolume(float value);

	/// render thread: fill the next block from Lua
	void requestBlock(std::vector<float> &samples);
	void submit(const std::vector<float> &samples);
	int bufferedFrames();

	static SoundStream *active();

private:

	static OH_AudioData_Callback_Result writeData(OH_AudioRenderer *renderer, void *userData,
	                                              void *audioData, int32_t audioDataSize);
	void readBlock(float *output, int frames);

	OH_AudioStreamBuilder *builder = nullptr;
	OH_AudioRenderer *renderer = nullptr;
	std::atomic<bool> running {false};
	int outChannels = 2;
	int inChannels = 0;
	int sampleRate = 44100;
	int bufferSize = 512;
	float volume = 1.0f;

	std::mutex mutex;
	std::deque<std::vector<float>> blocks;
	size_t readIndex = 0;
};

} // namespace loaf
