#pragma once
#include "pch.h"

//Beeper output (EAR/MIC bits of port 0xFE). Level changes are recorded with their T-state and box-filtered
//down to the output sample rate at the end of each frame.
class ZxBeeper
{
private:
	static constexpr uint32_t SampleRate = 44100;

	uint32_t _cpuClockRate = 3500000;
	uint64_t _frameStartCycle = 0;

	//Current output level, and the level at the start of the current frame
	int16_t _level = 0;
	int16_t _startLevel = 0;

	//Frame-relative T-state of each level change (can be past the end of the frame, for an instruction that
	//finishes after the frame's last T-state)
	vector<std::pair<uint32_t, int16_t>> _changes;

	//Position of the next sample within the frame, in units of T-states * SampleRate
	uint64_t _samplePos = 0;
	vector<int16_t> _samples;

public:
	void Init(uint32_t cpuClockRate)
	{
		_cpuClockRate = cpuClockRate;
		_frameStartCycle = 0;
		_level = 0;
		_startLevel = 0;
		_samplePos = 0;
		_changes.clear();
		_samples.clear();
	}

	void WritePort(uint64_t cycle, uint8_t value)
	{
		//EAR (bit 4) drives the speaker, MIC (bit 3) leaks into it at a much lower level
		int16_t level = ((value & 0x10) ? 12000 : 0) + ((value & 0x08) ? 1500 : 0);
		if(level != _level) {
			_changes.push_back({ (uint32_t)(cycle - _frameStartCycle), level });
			_level = level;
		}
	}

	//Generates this frame's samples (mono, 44.1kHz)
	void EndFrame(uint32_t frameLength)
	{
		_samples.clear();

		int16_t level = _startLevel;
		size_t changeIndex = 0;

		//Each output sample covers _cpuClockRate / SampleRate T-states (~79.4)
		uint64_t end = (uint64_t)frameLength * SampleRate;
		while(_samplePos + _cpuClockRate <= end) {
			uint32_t from = (uint32_t)(_samplePos / SampleRate);
			uint32_t to = (uint32_t)((_samplePos + _cpuClockRate) / SampleRate);
			int64_t sum = 0;
			uint32_t t = from;
			while(changeIndex < _changes.size() && _changes[changeIndex].first < to) {
				uint32_t changeAt = std::max(_changes[changeIndex].first, from);
				sum += (int64_t)level * (changeAt - t);
				t = changeAt;
				level = _changes[changeIndex].second;
				changeIndex++;
			}
			sum += (int64_t)level * (to - t);
			_samples.push_back((int16_t)(sum / std::max<uint32_t>(1, to - from)));
			_samplePos += _cpuClockRate;
		}
		_samplePos -= end;

		//Apply the changes in the frame's last partial sample, keep the ones that belong to the next frame
		vector<std::pair<uint32_t, int16_t>> nextFrame;
		for(; changeIndex < _changes.size(); changeIndex++) {
			if(_changes[changeIndex].first < frameLength) {
				level = _changes[changeIndex].second;
			} else {
				nextFrame.push_back({ _changes[changeIndex].first - frameLength, _changes[changeIndex].second });
			}
		}
		_startLevel = level;
		_changes = std::move(nextFrame);
		_frameStartCycle += frameLength;
	}

	const vector<int16_t>& GetSamples() { return _samples; }
};
