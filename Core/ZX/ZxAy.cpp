#include "pch.h"
#include "ZX/ZxAy.h"
#include "Utilities/Serializer.h"

//Output level of a channel for each volume (measured on an AY-3-8910, normalized), scaled so that all 3 channels at
//full volume stay well below the int16 range
static constexpr int32_t ChannelMax = 7000;
static const int32_t _volumeTable[16] = {
	0, (int32_t)(0.0137 * ChannelMax), (int32_t)(0.0205 * ChannelMax), (int32_t)(0.0291 * ChannelMax),
	(int32_t)(0.0423 * ChannelMax), (int32_t)(0.0618 * ChannelMax), (int32_t)(0.0847 * ChannelMax), (int32_t)(0.1369 * ChannelMax),
	(int32_t)(0.1691 * ChannelMax), (int32_t)(0.2647 * ChannelMax), (int32_t)(0.3527 * ChannelMax), (int32_t)(0.4499 * ChannelMax),
	(int32_t)(0.5704 * ChannelMax), (int32_t)(0.6873 * ChannelMax), (int32_t)(0.8482 * ChannelMax), ChannelMax
};

//Bits that exist in each register (the others read back as 0)
static const uint8_t _registerMasks[16] = {
	0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF
};

void ZxAy::Init(uint32_t cpuClockRate)
{
	_output.Init(cpuClockRate);
	Reset();
}

void ZxAy::Reset()
{
	memset(_registers, 0, sizeof(_registers));
	_selectedRegister = 0;
	_nextTick = 0;
	for(int i = 0; i < 3; i++) {
		_toneCounter[i] = 0;
		_toneOutput[i] = false;
	}
	_noiseCounter = 0;
	_noiseShift = 1;
	RestartEnvelope();
}

void ZxAy::RestartEnvelope()
{
	_envelopeCounter = 0;
	_envelopeStep = 0;
	_envelopeHolding = false;
	_envelopeAttack = (_registers[13] & 0x04) != 0;
	_envelopeVolume = _envelopeAttack ? 0 : 15;
}

void ZxAy::Tick()
{
	//Tones: the output flips every <period> steps (a period of 0 acts like 1)
	for(int i = 0; i < 3; i++) {
		uint16_t period = _registers[i * 2] | ((_registers[i * 2 + 1] & 0x0F) << 8);
		if(++_toneCounter[i] >= std::max<uint16_t>(period, 1)) {
			_toneCounter[i] = 0;
			_toneOutput[i] = !_toneOutput[i];
		}
	}

	//Noise: 17-bit LFSR, shifted every 2 * <period> steps
	uint8_t noisePeriod = _registers[6] & 0x1F;
	if(++_noiseCounter >= std::max<uint8_t>(noisePeriod, 1) * 2) {
		_noiseCounter = 0;
		_noiseShift = (_noiseShift >> 1) | (((_noiseShift ^ (_noiseShift >> 3)) & 0x01) << 16);
	}

	//Envelope: 16 volume steps per ramp, each lasting 2 * <period> steps
	if(!_envelopeHolding) {
		uint16_t envelopePeriod = _registers[11] | (_registers[12] << 8);
		if(++_envelopeCounter >= std::max<uint32_t>(envelopePeriod, 1) * 2) {
			_envelopeCounter = 0;
			if(++_envelopeStep < 16) {
				_envelopeVolume = _envelopeAttack ? _envelopeStep : 15 - _envelopeStep;
			} else {
				//End of a ramp - shape bits: 3 = continue, 2 = attack, 1 = alternate, 0 = hold
				uint8_t shape = _registers[13];
				bool alternate = (shape & 0x02) != 0;
				if(!(shape & 0x08)) {
					_envelopeHolding = true;
					_envelopeVolume = 0;
				} else if(shape & 0x01) {
					_envelopeHolding = true;
					_envelopeVolume = (_envelopeAttack != alternate) ? 15 : 0;
				} else {
					if(alternate) {
						_envelopeAttack = !_envelopeAttack;
					}
					_envelopeStep = 0;
					_envelopeVolume = _envelopeAttack ? 0 : 15;
				}
			}
		}
	}
}

void ZxAy::UpdateOutput(uint64_t cycle)
{
	uint8_t mixer = _registers[7];
	bool noise = (_noiseShift & 0x01) != 0;
	int32_t level = 0;
	for(int i = 0; i < 3; i++) {
		//A disabled tone/noise counts as high: with both disabled, the channel outputs its volume (used to play samples)
		bool toneOn = _toneOutput[i] || (mixer & (0x01 << i));
		bool noiseOn = noise || (mixer & (0x08 << i));
		if(toneOn && noiseOn) {
			uint8_t amplitude = _registers[8 + i];
			level += _volumeTable[(amplitude & 0x10) ? _envelopeVolume : (amplitude & 0x0F)];
		}
	}
	_output.SetLevel(cycle, level);
}

void ZxAy::Update(uint64_t cycle)
{
	while(_nextTick < cycle) {
		Tick();
		UpdateOutput(_nextTick);
		_nextTick += TickLength;
	}
}

void ZxAy::WriteRegister(uint64_t cycle, uint8_t value)
{
	if(_selectedRegister >= 16) {
		return;
	}

	Update(cycle);
	_registers[_selectedRegister] = value & _registerMasks[_selectedRegister];
	if(_selectedRegister == 13) {
		RestartEnvelope();
	}
	//Volume/mixer changes are heard immediately (some programs play samples by writing the volume registers)
	UpdateOutput(cycle);
}

uint8_t ZxAy::ReadRegister()
{
	if(_selectedRegister >= 16) {
		return 0xFF;
	}
	if(_selectedRegister == 14 && !(_registers[7] & 0x40)) {
		//I/O port A in input mode - nothing drives it
		return 0xFF;
	}
	return _registers[_selectedRegister];
}

void ZxAy::EndFrame(uint64_t frameEnd, uint32_t frameLength)
{
	Update(frameEnd);
	_output.EndFrame(frameLength);
}

ZxAyState ZxAy::GetState()
{
	ZxAyState state = {};
	memcpy(state.Registers, _registers, sizeof(_registers));
	state.SelectedRegister = _selectedRegister;
	state.EnvelopeVolume = _envelopeVolume;
	state.Present = true;
	return state;
}

void ZxAy::Serialize(Serializer& s)
{
	SVArray(_registers, 16);
	SV(_selectedRegister);
	SV(_nextTick);
	SVArray(_toneCounter, 3);
	SVArray(_toneOutput, 3);
	SV(_noiseCounter);
	SV(_noiseShift);
	SV(_envelopeCounter);
	SV(_envelopeStep);
	SV(_envelopeAttack);
	SV(_envelopeHolding);
	SV(_envelopeVolume);
}
