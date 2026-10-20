#pragma once
#include "pch.h"
#include "Shared/Interfaces/IConsole.h"
#include "Shared/SettingTypes.h"
#include "ZX/ZxTypes.h"

class Emulator;
class ZxMachine;
class ZxControlManager;

class ZxConsole final : public IConsole
{
private:
	Emulator* _emu = nullptr;
	unique_ptr<ZxMachine> _machine;
	unique_ptr<ZxControlManager> _controlManager;
	RomFormat _romFormat = RomFormat::ZxSnapshot;

	//The frame buffer handed to the video decoder alternates between these (the decoder runs on another thread)
	uint16_t _outputBuffers[2][ZxConstants::PixelCount] = {};
	uint8_t _currentBuffer = 0;
	vector<int16_t> _stereoSamples;

	//Keys typed automatically after the machine boots (LOAD "" when a tape is opened), as {frame, row, key} presses
	struct AutoKey
	{
		uint32_t Frame;
		uint8_t Row;
		uint8_t Key;
	};
	vector<AutoKey> _autoKeys;

	void ScheduleTapeAutoLoad();
	void ApplyAutoKeys();

public:
	static vector<string> GetSupportedExtensions() { return { ".sna", ".z80", ".tap" }; }
	static vector<string> GetSupportedSignatures() { return {}; }

	ZxConsole(Emulator* emu);
	virtual ~ZxConsole();

	Emulator* GetEmulator() { return _emu; }
	ZxMachine* GetMachine() { return _machine.get(); }

	LoadRomResult LoadRom(VirtualFile& romFile) override;

	void Reset() override;
	void RunFrame() override;

	void SaveBattery() override {}

	BaseControlManager* GetControlManager() override;
	ConsoleRegion GetRegion() override;
	ConsoleType GetConsoleType() override;
	vector<CpuType> GetCpuTypes() override;
	RomFormat GetRomFormat() override;
	double GetFps() override;
	PpuFrameInfo GetPpuFrame() override;
	uint32_t GetFrameCount() override;
	BaseVideoFilter* GetVideoFilter(bool getDefaultFilter) override;

	uint64_t GetMasterClock() override;
	uint32_t GetMasterClockRate() override;

	AudioTrackInfo GetAudioTrackInfo() override;
	void ProcessAudioPlayerAction(AudioPlayerActionParams p) override;

	AddressInfo GetAbsoluteAddress(AddressInfo& relAddress) override;
	AddressInfo GetRelativeAddress(AddressInfo& absAddress, CpuType cpuType) override;

	ZxState GetState();
	void GetConsoleState(BaseState& state, ConsoleType consoleType) override;

	void Serialize(Serializer& s) override;
};
