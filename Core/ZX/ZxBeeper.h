#pragma once
#include "pch.h"
#include "ZX/ZxAudioChannel.h"

//Beeper output (EAR/MIC bits of port 0xFE)
class ZxBeeper
{
private:
	ZxAudioChannel _output;
	int32_t _portLevel = 0;
	int32_t _tapeLevel = 0;

public:
	static constexpr uint32_t SampleRate = ZxAudioChannel::SampleRate;

	void Init(uint32_t cpuClockRate)
	{
		_output.Init(cpuClockRate);
		_portLevel = 0;
		_tapeLevel = 0;
	}

	void WritePort(uint64_t cycle, uint8_t value)
	{
		//EAR (bit 4) drives the speaker, MIC (bit 3) leaks into it at a much lower level
		_portLevel = ((value & 0x10) ? 12000 : 0) + ((value & 0x08) ? 1500 : 0);
		_output.SetLevel(cycle, _portLevel + _tapeLevel);
	}

	//The tape signal (EAR input) is heard through the speaker too
	void SetTapeLevel(uint64_t cycle, bool level)
	{
		_tapeLevel = level ? 3000 : 0;
		_output.SetLevel(cycle, _portLevel + _tapeLevel);
	}

	//Generates this frame's samples (mono, 44.1kHz)
	void EndFrame(uint32_t frameLength) { _output.EndFrame(frameLength); }

	const vector<int16_t>& GetSamples() { return _output.GetSamples(); }
};
