#pragma once
#include "pch.h"
#include "ZX/ZxTypes.h"
#include "ZX/ZxCpu.h"
#include "ZX/ZxUla.h"
#include "ZX/ZxMemoryManager.h"
#include "ZX/ZxKeyboard.h"
#include "ZX/ZxBeeper.h"
#include "ZX/ZxTape.h"
#include "Utilities/ISerializable.h"

//A complete Spectrum, independent of the Mesen emulator/UI (which will drive it through ZxConsole).
//Used directly by the headless tools in ZxTests.
class ZxMachine final : public ISerializable
{
private:
	ZxModel _model = ZxModel::Spectrum48K;
	ZxCpu _cpu;
	ZxUla _ula;
	ZxMemoryManager _memoryManager;
	ZxKeyboard _keyboard;
	ZxBeeper _beeper;
	ZxTape _tape;

public:
	ZxMachine();

	bool LoadRom(const vector<uint8_t>& rom);

	//extension is lowercase, with the dot (".sna", ".z80")
	bool LoadSnapshot(const vector<uint8_t>& data, const string& extension, string& error);

	//extension is lowercase, with the dot (".sna", ".z80"). Returns an empty vector if the format isn't supported.
	vector<uint8_t> SaveSnapshot(const string& extension);

	//Inserts a tape (.tap) - it is read when the ROM's LOAD routine runs
	bool LoadTape(const vector<uint8_t>& data, string& error);

	void Reset();

	//Runs the CPU until the end of the current frame (the last instruction may end in the next frame)
	void RunFrame();

	ZxCpu& GetCpu() { return _cpu; }
	ZxUla& GetUla() { return _ula; }
	ZxMemoryManager& GetMemoryManager() { return _memoryManager; }
	ZxKeyboard& GetKeyboard() { return _keyboard; }
	ZxBeeper& GetBeeper() { return _beeper; }
	ZxTape& GetTape() { return _tape; }

	//Palette indexes, ZxConstants::ScreenWidth x ScreenHeight
	uint16_t* GetFrameBuffer() { return _ula.GetFrameBuffer(); }
	static uint32_t GetPaletteColor(uint16_t index);

	void Serialize(Serializer& s) override;
};
