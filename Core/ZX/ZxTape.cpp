#include "pch.h"
#include "ZX/ZxTape.h"
#include "ZX/ZxCpu.h"
#include "ZX/ZxMemoryManager.h"
#include "ZX/ZxBeeper.h"
#include "Utilities/Serializer.h"

void ZxTape::Init(ZxModel model, uint32_t clockRate, ZxCpu* cpu, ZxBeeper* beeper)
{
	_model = model;
	_clockRate = clockRate;
	_cpu = cpu;
	_beeper = beeper;
}

bool ZxTape::Load(const vector<uint8_t>& data, string& error)
{
	ZxTapeImage image;
	if(!image.Load(data, error)) {
		return false;
	}

	Stop();
	_image = std::move(image);
	_loaded = true;
	Rewind();
	return true;
}

void ZxTape::Eject()
{
	Stop();
	_image = {};
	_loaded = false;
	Rewind();
}

void ZxTape::Rewind()
{
	Stop();
	_level = false;
	SetPosition(0);
}

void ZxTape::SetPosition(uint32_t blockIndex)
{
	const vector<ZxTapeBlock>& blocks = _image.GetBlocks();
	_blockIndex = std::min<uint32_t>(blockIndex, (uint32_t)blocks.size());
	_blockStartLevel = _level;
	_pulses.clear();
	if(_blockIndex < blocks.size()) {
		bool level = _level;
		ZxTapeImage::GetPulses(blocks[_blockIndex], level, _clockRate, _pulses);
	}
	_nextPulse = 0;
	_remaining = 0;
	_endReached = false;
	_endReported = false;
}

void ZxTape::Play()
{
	if(_playing || !_loaded) {
		return;
	}
	if(_blockIndex >= GetBlockCount()) {
		_endReached = true;
		return;
	}

	uint64_t cycle = _cpu->GetCycleCount();
	_playing = true;
	_nextEdge = cycle + _remaining;
	_loaderSeen = false;
	_lastLoaderReadCycle = cycle;
	_detectCount = 0;
	_beeper->SetTapeLevel(cycle, _level);
}

void ZxTape::Stop()
{
	if(_playing) {
		uint64_t cycle = _cpu->GetCycleCount();
		Update(cycle);
		StopAt(cycle);
	}
}

void ZxTape::StopAt(uint64_t cycle)
{
	if(_playing) {
		_remaining = _nextEdge > cycle ? _nextEdge - cycle : 0;
		_playing = false;
	}
	_detectCount = 0;
	_beeper->SetTapeLevel(cycle, false);
}

void ZxTape::SetLevel(uint64_t cycle, bool level)
{
	_level = level;
	_beeper->SetTapeLevel(cycle, level);
}

bool ZxTape::EnterBlock(uint64_t cycle)
{
	const vector<ZxTapeBlock>& blocks = _image.GetBlocks();
	_pulses.clear();
	_nextPulse = 0;
	_remaining = 0;
	_blockStartLevel = _level;

	bool stop = false;
	if(_blockIndex >= blocks.size()) {
		_blockIndex = (uint32_t)blocks.size();
		_endReached = true;
		stop = true;
	} else {
		const ZxTapeBlock& block = blocks[_blockIndex];
		if(block.Type == ZxTapeBlockType::Stop || (block.Type == ZxTapeBlockType::Stop48K && _model == ZxModel::Spectrum48K)) {
			//Playing resumes after this block
			stop = true;
		} else {
			bool level = _level;
			ZxTapeImage::GetPulses(block, level, _clockRate, _pulses);
			if(_pulses.empty() && level != _level) {
				SetLevel(cycle, level);
			}
		}
	}

	if(stop) {
		_playing = false;
		_detectCount = 0;
		_beeper->SetTapeLevel(cycle, false);
		return false;
	}
	return true;
}

void ZxTape::Update(uint64_t cycle)
{
	while(_playing && _nextEdge <= cycle) {
		if(_nextPulse >= _pulses.size()) {
			_blockIndex++;
			EnterBlock(_nextEdge);
			continue;
		}

		const ZxTapePulse& pulse = _pulses[_nextPulse++];
		if(pulse.Level != _level) {
			SetLevel(_nextEdge, pulse.Level);
		}
		_nextEdge += pulse.Length;
	}

	if(_playing && _loaderSeen && cycle > _lastLoaderReadCycle + _clockRate) {
		//No loader has read the tape for a second - the program has finished loading (or is waiting for a key between
		//loads), stop the tape like the user would. A loader always reads it continuously while data is playing.
		StopAt(cycle);
	}
}

bool ZxTape::ReadEar(uint64_t cycle, bool basicRomPaged)
{
	if(!_loaded) {
		return false;
	}

	Update(cycle);

	//Loader detection (from Fuse): loaders read the port in tight loops, incrementing B to time the pulses
	uint8_t b = _cpu->GetState().B;
	uint64_t elapsed = cycle - _lastReadCycle;
	uint8_t bDelta = b - _lastReadB;
	_lastReadCycle = cycle;
	_lastReadB = b;

	bool loaderRead = elapsed <= 500 && (bDelta == 1 || bDelta == 0xFF);
	if(_playing) {
		if(loaderRead) {
			//Update() stops the tape when this hasn't happened for a while
			_lastLoaderReadCycle = cycle;
			_loaderSeen = true;
		}
	} else if(loaderRead) {
		//The ROM's loader (LD-BYTES, 0x0556-0x0604) is handled by the trap at LD-START, which loads standard blocks instantly
		uint16_t pc = _cpu->GetState().PC;
		bool romLoader = basicRomPaged && pc >= 0x0556 && pc < 0x0605;
		if(++_detectCount > 10 && _blockIndex < GetBlockCount() && !romLoader) {
			Play();
			_loaderSeen = true;
		}
	} else {
		_detectCount = 0;
	}

	return GetEarLevel();
}

bool ZxTape::ProcessLoadTrap(ZxCpu& cpu, ZxMemoryManager& memoryManager)
{
	Update(cpu.GetCycleCount());

	//The block the ROM is waiting for: the current one if its data hasn't started yet, otherwise the next one
	const vector<ZxTapeBlock>& blocks = _image.GetBlocks();
	size_t index = _blockIndex;
	if(index < blocks.size() && _nextPulse > 0) {
		const ZxTapeBlock& current = blocks[index];
		if(current.Type == ZxTapeBlockType::Data) {
			size_t dataStart = current.PilotCount + (current.Sync1 ? 1 : 0) + (current.Sync2 ? 1 : 0);
			size_t dataEnd = dataStart + (current.Data.empty() ? 0 : ((current.Data.size() - 1) * 8 + current.UsedBits) * 2);
			if(_nextPulse > dataEnd) {
				index++;
			} else if(_nextPulse > dataStart) {
				//The data is playing - let the ROM read it
				return false;
			}
		} else if(current.Type == ZxTapeBlockType::Pause) {
			index++;
		}
	}
	while(index < blocks.size() && (blocks[index].Type == ZxTapeBlockType::Pause || blocks[index].Type == ZxTapeBlockType::Stop ||
		blocks[index].Type == ZxTapeBlockType::Stop48K || blocks[index].Type == ZxTapeBlockType::SetLevel)) {
		index++;
	}

	if(index >= blocks.size()) {
		//End of tape: let the ROM wait for a signal that never comes, like a real tape deck
		if(!_playing) {
			_endReached = true;
		}
		return false;
	}

	const ZxTapeBlock& tapeBlock = blocks[index];
	if(!tapeBlock.IsStandardSpeed()) {
		//Turbo/custom block - play it, the ROM may still be able to read it
		Play();
		return false;
	}

	//LD-BYTES has swapped AF: A' = expected flag byte, carry' set = LOAD (reset = VERIFY). IX = destination, DE = length,
	//and the return address (SA/LD-RET, or the loader's own) is on the stack.
	ZxCpuState& s = cpu.GetState();
	const vector<uint8_t>& block = tapeBlock.Data;
	bool isLoad = (s.AltFlags & ZxCpuFlags::Carry) != 0;
	uint8_t expectedFlag = s.AltA;
	uint16_t addr = (s.IXH << 8) | s.IXL;
	uint16_t length = (s.D << 8) | s.E;

	bool success = false;
	if(!block.empty() && block[0] == expectedFlag) {
		uint8_t parity = block[0];
		uint32_t available = (uint32_t)block.size() - 1;
		uint32_t count = std::min<uint32_t>(length, available);
		bool verifyOk = true;
		for(uint32_t i = 0; i < count; i++) {
			uint8_t value = block[i + 1];
			parity ^= value;
			if(isLoad) {
				memoryManager.DirectWrite(addr, value);
			} else if(memoryManager.Peek(addr) != value) {
				verifyOk = false;
			}
			addr++;
		}
		length -= count;

		//The checksum byte follows the data - the block is only valid if everything XORs to 0
		if(length == 0 && available > count) {
			parity ^= block[count + 1];
			success = parity == 0 && verifyOk;
		}
	}

	s.IXH = addr >> 8;
	s.IXL = (uint8_t)addr;
	s.D = length >> 8;
	s.E = (uint8_t)length;
	if(success) {
		s.Flags |= ZxCpuFlags::Carry;
	} else {
		s.Flags &= ~ZxCpuFlags::Carry;
	}

	//Return like LD-BYTES does - normally to SA/LD-RET, which restores the border, re-enables interrupts and returns to
	//LD-BYTES' caller
	s.PC = memoryManager.Peek(s.SP) | (memoryManager.Peek(s.SP + 1) << 8);
	s.SP += 2;

	//The tape is now after the block, stopped
	Stop();
	SetPosition((uint32_t)index + 1);
	return true;
}

void ZxTape::Serialize(Serializer& s)
{
	uint32_t blockCount = (uint32_t)GetBlockCount();
	SV(blockCount);
	SV(_playing);
	SV(_blockIndex);
	SV(_blockStartLevel);
	SV(_nextPulse);
	SV(_nextEdge);
	SV(_remaining);
	SV(_level);
	SV(_lastReadCycle);
	SV(_lastReadB);
	SV(_detectCount);
	SV(_loaderSeen);
	SV(_lastLoaderReadCycle);
	SV(_endReached);
	SV(_endReported);

	if(!s.IsSaving()) {
		if(!_loaded || blockCount != GetBlockCount()) {
			//Another tape (or none) is inserted now - start it from the beginning
			_playing = false;
			_level = false;
			SetPosition(0);
		} else {
			//Regenerate the current block's signal
			_pulses.clear();
			const vector<ZxTapeBlock>& blocks = _image.GetBlocks();
			if(_blockIndex < blocks.size()) {
				bool level = _blockStartLevel;
				ZxTapeImage::GetPulses(blocks[_blockIndex], level, _clockRate, _pulses);
			}
			_nextPulse = std::min<uint32_t>(_nextPulse, (uint32_t)_pulses.size());
		}
	}
}
