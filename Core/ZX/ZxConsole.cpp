#include "pch.h"
#include "ZX/ZxConsole.h"
#include "ZX/ZxMachine.h"
#include "ZX/ZxSnapshot.h"
#include "ZX/ZxControlManager.h"
#include "ZX/ZxDefaultVideoFilter.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/FirmwareHelper.h"
#include "Shared/MessageManager.h"
#include "Shared/NotificationManager.h"
#include "Shared/RewindManager.h"
#include "Shared/Video/VideoDecoder.h"
#include "Shared/Audio/SoundMixer.h"
#include "Shared/BaseControlManager.h"
#include "Shared/EventType.h"
#include "Utilities/VirtualFile.h"
#include "Utilities/Serializer.h"

ZxConsole::ZxConsole(Emulator* emu)
{
	_emu = emu;
}

ZxConsole::~ZxConsole()
{
}

LoadRomResult ZxConsole::LoadRom(VirtualFile& romFile)
{
	vector<uint8_t> fileData;
	if(!romFile.ReadFile(fileData)) {
		return LoadRomResult::Failure;
	}

	vector<uint8_t> rom;
	if(!FirmwareHelper::LoadZxRom(_emu, rom)) {
		return LoadRomResult::Failure;
	}

	_machine.reset(new ZxMachine());
	_controlManager.reset(new ZxControlManager(_emu));
	if(!_machine->LoadRom(rom)) {
		return LoadRomResult::Failure;
	}

	string ext = romFile.GetFileExtension();
	string error;
	if(ZxSnapshot::IsSnapshot(ext)) {
		_romFormat = RomFormat::ZxSnapshot;
		if(!_machine->LoadSnapshot(fileData, ext, error)) {
			MessageManager::DisplayMessage("Error", error);
			return LoadRomResult::Failure;
		}
	} else if(ZxTape::IsTape(ext)) {
		_romFormat = RomFormat::ZxTape;
		if(!_machine->LoadTape(fileData, error)) {
			MessageManager::DisplayMessage("Error", error);
			return LoadRomResult::Failure;
		}
		ScheduleTapeAutoLoad();
	} else {
		return LoadRomResult::UnknownType;
	}

	//Mesen's keyboard shortcuts are disabled while running (except pause) - the whole keyboard belongs to the Spectrum
	MessageManager::DisplayMessage("Input", "KeyboardModeEnabled");

	_emu->RegisterMemory(MemoryType::ZxRom, _machine->GetMemoryManager().GetRom(), 0x4000);
	_emu->RegisterMemory(MemoryType::ZxRam, _machine->GetMemoryManager().GetRam(), 0xC000);

	return LoadRomResult::Success;
}

void ZxConsole::ScheduleTapeAutoLoad()
{
	//Once the ROM has booted (~2 seconds), type LOAD "" ENTER: J (LOAD in K mode), SYMBOL SHIFT+P twice, ENTER
	_autoKeys.clear();
	uint32_t frame = _machine->GetUla().GetFrameCount() + 100;
	auto press = [&](std::initializer_list<std::pair<uint8_t, uint8_t>> keys) {
		for(auto& k : keys) {
			for(uint32_t i = 0; i < 4; i++) {
				_autoKeys.push_back({ frame + i, k.first, k.second });
			}
		}
		frame += 8;
	};
	press({ { 6, 3 } }); //J
	press({ { 7, 1 }, { 5, 0 } }); //SYMBOL SHIFT + P = "
	press({ { 7, 1 }, { 5, 0 } });
	press({ { 6, 0 } }); //ENTER
}

void ZxConsole::ApplyAutoKeys()
{
	uint32_t frame = _machine->GetUla().GetFrameCount();
	for(AutoKey& k : _autoKeys) {
		if(k.Frame == frame) {
			_machine->GetKeyboard().SetKeyState(k.Row, k.Key, true);
		}
	}
	if(!_autoKeys.empty() && _autoKeys.back().Frame < frame) {
		_autoKeys.clear();
	}
}

void ZxConsole::Reset()
{
	//Like pressing the reset button (or a power cycle - the ROM clears the RAM either way). The tape stays inserted.
	_machine->Reset();
	_machine->GetTape().Rewind();
	_autoKeys.clear();
}

void ZxConsole::RunFrame()
{
	_controlManager->ApplyToMatrix(_machine->GetKeyboard());
	ApplyAutoKeys();

	_machine->RunFrame();

	_emu->ProcessEvent(EventType::EndFrame, CpuType::Zx);
	_emu->GetNotificationManager()->SendNotification(ConsoleNotificationType::PpuFrameDone);

	uint16_t* outputBuffer = _outputBuffers[_currentBuffer];
	_currentBuffer ^= 1;
	memcpy(outputBuffer, _machine->GetFrameBuffer(), sizeof(_outputBuffers[0]));
	RenderedFrame frame(outputBuffer, ZxConstants::ScreenWidth, ZxConstants::ScreenHeight, 1.0, _machine->GetUla().GetFrameCount(), _controlManager->GetPortStates());
	bool rewinding = _emu->GetRewindManager()->IsRewinding();
	_emu->GetVideoDecoder()->UpdateFrame(frame, rewinding, rewinding);

	_emu->ProcessEndOfFrame();
	_controlManager->UpdateControlDevices();
	_controlManager->UpdateInputState();

	//The mixer expects interleaved stereo samples
	const vector<int16_t>& samples = _machine->GetBeeper().GetSamples();
	uint32_t volume = _emu->GetSettings()->GetZxConfig().BeeperVolume;
	_stereoSamples.resize(samples.size() * 2);
	for(size_t i = 0; i < samples.size(); i++) {
		int16_t sample = (int16_t)(samples[i] * (int32_t)volume / 100);
		_stereoSamples[i * 2] = sample;
		_stereoSamples[i * 2 + 1] = sample;
	}
	_emu->GetSoundMixer()->PlayAudioBuffer(_stereoSamples.data(), (uint32_t)samples.size(), ZxBeeper::SampleRate);
}

BaseControlManager* ZxConsole::GetControlManager()
{
	return _controlManager.get();
}

ConsoleRegion ZxConsole::GetRegion()
{
	return ConsoleRegion::Pal;
}

ConsoleType ZxConsole::GetConsoleType()
{
	return ConsoleType::Zx;
}

vector<CpuType> ZxConsole::GetCpuTypes()
{
	return { CpuType::Zx };
}

RomFormat ZxConsole::GetRomFormat()
{
	return _romFormat;
}

double ZxConsole::GetFps()
{
	const ZxTiming& timing = _machine->GetUla().GetTiming();
	return (double)timing.CpuClockRate / timing.FrameLength;
}

PpuFrameInfo ZxConsole::GetPpuFrame()
{
	const ZxTiming& timing = _machine->GetUla().GetTiming();
	PpuFrameInfo frame = {};
	frame.FirstScanline = 0;
	frame.FrameCount = _machine->GetUla().GetFrameCount();
	frame.Width = ZxConstants::ScreenWidth;
	frame.Height = ZxConstants::ScreenHeight;
	frame.ScanlineCount = timing.FrameLength / timing.LineLength;
	frame.CycleCount = timing.LineLength;
	frame.FrameBufferSize = sizeof(_outputBuffers[0]);
	frame.FrameBuffer = (uint8_t*)_outputBuffers[_currentBuffer ^ 1];
	return frame;
}

uint32_t ZxConsole::GetFrameCount()
{
	return _machine->GetUla().GetFrameCount();
}

BaseVideoFilter* ZxConsole::GetVideoFilter(bool getDefaultFilter)
{
	return new ZxDefaultVideoFilter(_emu);
}

uint64_t ZxConsole::GetMasterClock()
{
	return _machine->GetCpu().GetCycleCount();
}

uint32_t ZxConsole::GetMasterClockRate()
{
	return _machine->GetUla().GetTiming().CpuClockRate;
}

AudioTrackInfo ZxConsole::GetAudioTrackInfo()
{
	return AudioTrackInfo();
}

void ZxConsole::ProcessAudioPlayerAction(AudioPlayerActionParams p)
{
}

AddressInfo ZxConsole::GetAbsoluteAddress(AddressInfo& relAddress)
{
	uint16_t addr = (uint16_t)relAddress.Address;
	if(addr < 0x4000) {
		return { addr, MemoryType::ZxRom };
	}
	return { addr - 0x4000, MemoryType::ZxRam };
}

AddressInfo ZxConsole::GetRelativeAddress(AddressInfo& absAddress, CpuType cpuType)
{
	switch(absAddress.Type) {
		case MemoryType::ZxRom: return { absAddress.Address, MemoryType::ZxMemory };
		case MemoryType::ZxRam: return { absAddress.Address + 0x4000, MemoryType::ZxMemory };
		default: return { -1, MemoryType::None };
	}
}

ZxState ZxConsole::GetState()
{
	ZxState state;
	state.Cpu = _machine->GetCpu().GetState();
	return state;
}

void ZxConsole::GetConsoleState(BaseState& state, ConsoleType consoleType)
{
	(ZxState&)state = GetState();
}

void ZxConsole::Serialize(Serializer& s)
{
	SV(_machine);
	SV(_controlManager);
}
