#pragma once
#include "pch.h"
#include "ZX/ZxTypes.h"
#include "ZX/ZxTapeImage.h"
#include "Utilities/ISerializable.h"

class ZxCpu;
class ZxMemoryManager;
class ZxBeeper;

//The tape deck. The tape (.tap or .tzx) plays in real time through the EAR input, and the ROM's loader is trapped to
//load standard speed blocks instantly.
//- Playing starts automatically when a loader is detected (tight loops reading port 0xFE while counting in B, like the
//  ROM's LD-EDGE - same heuristic as Fuse) or when the ROM loader waits for a block it can't load instantly, and stops
//  when no loader has read the tape for a second (or at a TZX "stop the tape" block). It can also be started/stopped
//  by hand.
//- The signal is computed lazily, up to the T-state of each port read (and to the end of each frame), and is heard
//  through the beeper like on the real machine.
class ZxTape final : public ISerializable
{
private:
	ZxTapeImage _image;
	bool _loaded = false;

	ZxCpu* _cpu = nullptr;
	ZxBeeper* _beeper = nullptr;
	ZxModel _model = ZxModel::Spectrum48K;
	uint32_t _clockRate = 3500000;

	bool _playing = false;
	//Current block (== block count at the end of the tape), and its signal
	uint32_t _blockIndex = 0;
	vector<ZxTapePulse> _pulses;
	//Signal level before the current block (to regenerate its pulses after loading a save state)
	bool _blockStartLevel = false;
	//Next pulse to start, and when it starts (while playing) or how many T-states are left before it starts (stopped)
	uint32_t _nextPulse = 0;
	uint64_t _nextEdge = 0;
	uint64_t _remaining = 0;
	//Current signal level
	bool _level = false;

	//Loader detection: the last read of port 0xFE, and the number of consecutive reads that look like a loader
	uint64_t _lastReadCycle = 0;
	uint8_t _lastReadB = 0;
	uint32_t _detectCount = 0;
	//A loader has read the signal since the tape started (only then is it stopped automatically), and when it last did
	bool _loaderSeen = false;
	uint64_t _lastLoaderReadCycle = 0;

	//The tape stopped at its end - reported once, until the tape is rewound or replaced
	bool _endReached = false;
	bool _endReported = false;

	//Sets up the current block (skipping the blocks without a signal). Returns false if the tape stops there.
	bool EnterBlock(uint64_t cycle);
	void SetLevel(uint64_t cycle, bool level);
	void StopAt(uint64_t cycle);
	void SetPosition(uint32_t blockIndex);

public:
	void Init(ZxModel model, uint32_t clockRate, ZxCpu* cpu, ZxBeeper* beeper);

	bool Load(const vector<uint8_t>& data, string& error);
	void Eject();
	void Rewind();

	void Play();
	void Stop();

	bool IsLoaded() { return _loaded; }
	bool IsPlaying() { return _playing; }
	size_t GetBlockCount() { return _image.GetBlocks().size(); }
	size_t GetCurrentBlock() { return _blockIndex; }

	//Runs the tape up to the given cycle
	void Update(uint64_t cycle);

	//The CPU reads port 0xFE: returns the EAR input (after running the tape up to that cycle) and runs the loader detection.
	//basicRomPaged: the 48K BASIC ROM is paged in (its loader is left to ProcessLoadTrap).
	bool ReadEar(uint64_t cycle, bool basicRomPaged);
	//Current EAR input level (no side effects)
	bool GetEarLevel() { return _playing && _level; }

	//True once when the tape reaches its end (or the ROM waits for a block after it) - e.g. a multi-load game asking for
	//a rewind or the other side
	bool TakeEndReached()
	{
		if(_endReached && !_endReported) {
			_endReported = true;
			return true;
		}
		return false;
	}

	//LD-START, inside the ROM's LD-BYTES routine (0x0556). Call ProcessLoadTrap when the CPU is about to execute it (with
	//the 48K ROM paged in). Loaders that set up their own flag and return address and jump into LD-BYTES past its
	//entry point (e.g. to 0x0562) all get here. Standard speed blocks are loaded instantly, other blocks are played.
	static constexpr uint16_t LoadTrapAddress = 0x056C;
	bool ProcessLoadTrap(ZxCpu& cpu, ZxMemoryManager& memoryManager);

	void Serialize(Serializer& s) override;
};
