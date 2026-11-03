#pragma once
#include "pch.h"
#include "ZX/ZxTypes.h"

class ZxMachine;

//Loads and saves .sna and .z80 snapshots, 48K and 128K (the 128K ones include the paging register; .z80 also has the
//AY's registers)
class ZxSnapshot
{
private:
	static bool LoadSna(const vector<uint8_t>& data, ZxMachine& machine, string& error);
	static bool LoadZ80(const vector<uint8_t>& data, ZxMachine& machine, string& error);
	static bool DecompressZ80(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize, bool hasEndMarker);
	static void CompressZ80(const uint8_t* src, size_t size, vector<uint8_t>& out);

	static vector<uint8_t> SaveSna(ZxMachine& machine);
	static vector<uint8_t> SaveZ80(ZxMachine& machine);

public:
	static bool IsSnapshot(const string& extension);

	//The model the snapshot was taken on - false (with the reason in error) if it isn't a supported one
	static bool GetModel(const vector<uint8_t>& data, const string& extension, ZxModel& model, string& error);

	//extension is lowercase, with the dot (".sna", ".z80"). The machine must be the model given by GetModel.
	static bool Load(const vector<uint8_t>& data, const string& extension, ZxMachine& machine, string& error);

	//Returns an empty vector if the format isn't supported. Saving a 48K .sna puts PC on the stack (in the file only),
	//so the 2 bytes below SP must not hold anything important - this is a limitation of the format.
	static vector<uint8_t> Save(const string& extension, ZxMachine& machine);
};
