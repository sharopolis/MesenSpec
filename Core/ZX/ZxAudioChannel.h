#pragma once
#include "pch.h"
#include "Utilities/Audio/blip_buf.h"

//A mono sound output: level changes are fed to a band-limited synthesizer (blip_buf) at the T-state they happen, and
//turned into 44.1kHz samples at the end of each frame. Each sound source (beeper, AY) has its own, so they can be
//mixed with separate volumes - they all produce the same number of samples per frame.
class ZxAudioChannel
{
private:
	static constexpr uint32_t MaxSamples = 4000;

	blip_t* _blip = nullptr;
	uint64_t _frameStartCycle = 0;
	int32_t _level = 0;
	vector<int16_t> _samples;

public:
	static constexpr uint32_t SampleRate = 44100;

	ZxAudioChannel() { _blip = blip_new(MaxSamples); }
	~ZxAudioChannel() { blip_delete(_blip); }
	ZxAudioChannel(const ZxAudioChannel&) = delete;
	ZxAudioChannel& operator=(const ZxAudioChannel&) = delete;

	void Init(uint32_t cpuClockRate)
	{
		blip_set_rates(_blip, cpuClockRate, SampleRate);
		blip_clear(_blip);
		_frameStartCycle = 0;
		_level = 0;
		_samples.clear();
	}

	void SetLevel(uint64_t cycle, int32_t level)
	{
		if(level != _level) {
			//The time can be a few T-states past the end of the frame (for the instruction that ends it) - blip_buf allows that
			blip_add_delta(_blip, (uint32_t)(cycle - _frameStartCycle), level - _level);
			_level = level;
		}
	}

	//Generates this frame's samples
	void EndFrame(uint32_t frameLength)
	{
		blip_end_frame(_blip, frameLength);
		_samples.resize(blip_samples_avail(_blip));
		blip_read_samples(_blip, _samples.data(), (int)_samples.size(), 0);
		_frameStartCycle += frameLength;
	}

	const vector<int16_t>& GetSamples() { return _samples; }
};
