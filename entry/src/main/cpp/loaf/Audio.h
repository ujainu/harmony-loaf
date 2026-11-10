#pragma once

#include <ohaudio/native_audiostream_base.h>
#include <ohaudio/native_audiostreambuilder.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace loaf {

/// decoded PCM clip, always resampled to 44100 Hz stereo float32
struct AudioClip {
	std::vector<float> samples;
	int sampleRate = 44100;
	int channels = 2;
	double duration = 0.0;
	bool valid = false;
};

class AudioEngine;

class SoundPlayer {
public:

	~SoundPlayer();

	bool load(const std::string &path);
	void play();
	void stop();
	void pause();
	void setPaused(bool paused);
	bool isPlaying() const { return playing.load(); }
	bool isLoaded() const { return clip.valid; }
	void setLoop(bool loop) { looping = loop; }
	bool isLooping() const { return looping; }
	void setVolume(float volume) { this->volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume); }
	float getVolume() const { return volume; }
	double getDuration() const { return clip.duration; }
	double getPosition() const;
	void setPosition(double seconds);

private:

	friend class AudioEngine;

	AudioClip clip;
	std::atomic<bool> playing {false};
	std::atomic<bool> paused {false};
	bool looping = false;
	float volume = 1.0f;
	size_t position = 0; // frame index
	mutable std::mutex mutex;
};

class AudioEngine {
public:

	static AudioEngine &instance();

	bool start();
	void stop();
	bool isRunning() const { return running; }

	void attach(SoundPlayer *player);
	void detach(SoundPlayer *player);

	static constexpr int kSampleRate = 44100;
	static constexpr int kChannels = 2;

private:

	AudioEngine() = default;
	~AudioEngine();

	static OH_AudioData_Callback_Result writeData(OH_AudioRenderer *renderer, void *userData,
	                                              void *audioData, int32_t audioDataSize);
	void mix(float *output, int frames);

	OH_AudioStreamBuilder *builder = nullptr;
	OH_AudioRenderer *renderer = nullptr;
	std::atomic<bool> running {false};

	std::mutex mutex;
	std::vector<SoundPlayer *> players;
};

} // namespace loaf
