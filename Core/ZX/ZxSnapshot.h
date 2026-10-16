#pragma once
#include "pch.h"

class ZxCpu;
class ZxMemoryManager;
class ZxUla;

//Loads and saves .sna and .z80 snapshots (48K only for now)
class ZxSnapshot
{
private:
	static bool LoadSna(const vector<uint8_t>& data, ZxCpu& cpu, ZxMemoryManager& memoryManager, ZxUla& ula, string& error);
	static bool LoadZ80(const vector<uint8_t>& data, ZxCpu& cpu, ZxMemoryManager& memoryManager, ZxUla& ula, string& error);
	static bool DecompressZ80(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize, bool hasEndMarker);
	static void CompressZ80(const uint8_t* src, size_t size, vector<uint8_t>& out);

	static vector<uint8_t> SaveSna(ZxCpu& cpu, ZxMemoryManager& memoryManager, ZxUla& ula);
	static vector<uint8_t> SaveZ80(ZxCpu& cpu, ZxMemoryManager& memoryManager, ZxUla& ula);

public:
	static bool IsSnapshot(const string& extension);

	//extension is lowercase, with the dot (".sna", ".z80")
	static bool Load(const vector<uint8_t>& data, const string& extension, ZxCpu& cpu, ZxMemoryManager& memoryManager, ZxUla& ula, string& error);

	//Returns an empty vector if the format isn't supported. Saving a .sna puts PC on the stack (in the file only),
	//so the 2 bytes below SP must not hold anything important - this is a limitation of the format.
	static vector<uint8_t> Save(const string& extension, ZxCpu& cpu, ZxMemoryManager& memoryManager, ZxUla& ula);
};
