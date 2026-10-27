#pragma once
#include "pch.h"
#include "ZX/ZxAudioChannel.h"
#include "ZX/ZxTypes.h"
#include "Utilities/ISerializable.h"

//AY-3-8912 sound chip (128K): 3 square wave channels, a noise generator and an envelope generator, clocked at half
//the CPU clock. Port 0xFFFD selects a register (and reads it back), port 0xBFFD writes it.
//The chip's state is advanced in steps of 8 AY clocks (16 T-states) up to the T-state of each access, and each
//output change goes to its own audio channel at the T-state it happens.
class ZxAy final : public ISerializable
{
private:
	//T-states per step: the tone/noise/envelope counters all count in units of 8 AY clocks (AY clock = CPU clock / 2)
	static constexpr uint32_t TickLength = 16;

	ZxAudioChannel _output;

	uint8_t _registers[16] = {};
	//Register selected through port 0xFFFD (>= 16: none - writes are ignored and reads return 0xFF)
	uint8_t _selectedRegister = 0;

	//Cycle of the next step
	uint64_t _nextTick = 0;

	uint16_t _toneCounter[3] = {};
	bool _toneOutput[3] = {};

	uint16_t _noiseCounter = 0;
	uint32_t _noiseShift = 1;

	uint32_t _envelopeCounter = 0;
	uint8_t _envelopeStep = 0;
	//Direction of the current ramp (up when attacking)
	bool _envelopeAttack = false;
	bool _envelopeHolding = false;
	uint8_t _envelopeVolume = 0;

	void Tick();
	void UpdateOutput(uint64_t cycle);
	void RestartEnvelope();

public:
	void Init(uint32_t cpuClockRate);
	void Reset();

	//Runs the chip up to (not including) the given cycle
	void Update(uint64_t cycle);

	void SelectRegister(uint8_t value) { _selectedRegister = value; }
	void WriteRegister(uint64_t cycle, uint8_t value);
	uint8_t ReadRegister();

	//Generates this frame's samples (mono, 44.1kHz) - frameEnd is the first cycle of the next frame
	void EndFrame(uint64_t frameEnd, uint32_t frameLength);
	const vector<int16_t>& GetSamples() { return _output.GetSamples(); }

	ZxAyState GetState();

	void Serialize(Serializer& s) override;
};
