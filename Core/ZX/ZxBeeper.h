#pragma once
#include "pch.h"
#include "Utilities/Audio/blip_buf.h"

//Beeper output (EAR/MIC bits of port 0xFE). Level changes are fed to a band-limited synthesizer (blip_buf) at the
//T-state they happen, and turned into 44.1kHz samples at the end of each frame.
class ZxBeeper
{
private:
	static constexpr uint32_t MaxSamples = 4000;

	blip_t* _blip = nullptr;
	uint64_t _frameStartCycle = 0;
	int16_t _level = 0;
	vector<int16_t> _samples;

public:
	static constexpr uint32_t SampleRate = 44100;

	ZxBeeper() { _blip = blip_new(MaxSamples); }
	~ZxBeeper() { blip_delete(_blip); }
	ZxBeeper(const ZxBeeper&) = delete;
	ZxBeeper& operator=(const ZxBeeper&) = delete;

	void Init(uint32_t cpuClockRate)
	{
		blip_set_rates(_blip, cpuClockRate, SampleRate);
		blip_clear(_blip);
		_frameStartCycle = 0;
		_level = 0;
		_samples.clear();
	}

	void WritePort(uint64_t cycle, uint8_t value)
	{
		//EAR (bit 4) drives the speaker, MIC (bit 3) leaks into it at a much lower level
		int16_t level = ((value & 0x10) ? 12000 : 0) + ((value & 0x08) ? 1500 : 0);
		if(level != _level) {
			//The time can be a few T-states past the end of the frame (for the instruction that ends it) - blip_buf allows that
			blip_add_delta(_blip, (uint32_t)(cycle - _frameStartCycle), level - _level);
			_level = level;
		}
	}

	//Generates this frame's samples (mono, 44.1kHz)
	void EndFrame(uint32_t frameLength)
	{
		blip_end_frame(_blip, frameLength);
		_samples.resize(blip_samples_avail(_blip));
		blip_read_samples(_blip, _samples.data(), (int)_samples.size(), 0);
		_frameStartCycle += frameLength;
	}

	const vector<int16_t>& GetSamples() { return _samples; }
};
