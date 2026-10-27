#pragma once
#include "pch.h"

class ZxCpu;
class ZxMemoryManager;

//Tape images. For now only .tap files, loaded instantly by trapping the ROM's LD-BYTES routine
//(TODOZX: real-time playback through the EAR input, needed for TZX and custom loaders).
class ZxTape
{
private:
	//Each block includes its flag byte and checksum
	vector<vector<uint8_t>> _blocks;
	size_t _nextBlock = 0;
	//The ROM tried to load past the last block - reported once, until the tape is rewound or replaced
	bool _endReached = false;
	bool _endReported = false;

public:
	static bool IsTape(const string& extension) { return extension == ".tap"; }

	bool Load(const vector<uint8_t>& data, string& error);
	void Eject();
	void Rewind()
	{
		_nextBlock = 0;
		_endReached = false;
		_endReported = false;
	}

	bool IsLoaded() { return !_blocks.empty(); }
	size_t GetBlockCount() { return _blocks.size(); }
	size_t GetNextBlock() { return _nextBlock; }

	//True once when the ROM starts waiting for a block after the end of the tape (e.g. a multi-load game asking for a
	//rewind or the other side)
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
	//entry point (e.g. to 0x0562) all get here.
	static constexpr uint16_t LoadTrapAddress = 0x056C;
	bool ProcessLoadTrap(ZxCpu& cpu, ZxMemoryManager& memoryManager);
};
