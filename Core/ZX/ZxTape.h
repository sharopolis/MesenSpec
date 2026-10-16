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

public:
	static bool IsTape(const string& extension) { return extension == ".tap"; }

	bool Load(const vector<uint8_t>& data, string& error);
	void Eject();
	void Rewind() { _nextBlock = 0; }

	bool IsLoaded() { return !_blocks.empty(); }
	size_t GetBlockCount() { return _blocks.size(); }
	size_t GetNextBlock() { return _nextBlock; }

	//ROM address of LD-BYTES. Call ProcessLoadTrap when the CPU is about to execute it (with the 48K ROM paged in).
	static constexpr uint16_t LdBytesAddress = 0x0556;
	bool ProcessLoadTrap(ZxCpu& cpu, ZxMemoryManager& memoryManager);
};
