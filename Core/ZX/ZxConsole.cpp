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

	string ext = romFile.GetFileExtension();

	//Snapshots are 48K-only for now, tapes run on the model selected in the settings
	ZxModel model = ZxSnapshot::IsSnapshot(ext) ? ZxModel::Spectrum48K : _emu->GetSettings()->GetZxConfig().Model;

	vector<uint8_t> rom;
	if(!FirmwareHelper::LoadZxRom(_emu, model, rom)) {
		return LoadRomResult::Failure;
	}

	_machine.reset(new ZxMachine(model));
	_machine->SetDebugHooks(this);
	_controlManager.reset(new ZxControlManager(_emu));
	if(!_machine->LoadRom(rom)) {
		return LoadRomResult::Failure;
	}

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

	ZxMemoryManager& memoryManager = _machine->GetMemoryManager();
	_emu->RegisterMemory(MemoryType::ZxRom, memoryManager.GetRom(), memoryManager.GetRomSize());
	_emu->RegisterMemory(MemoryType::ZxRam, memoryManager.GetRam(), memoryManager.GetRamSize());

	return LoadRomResult::Success;
}

void ZxConsole::ScheduleTapeAutoLoad()
{
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
	if(_machine->GetModel() == ZxModel::Spectrum128K) {
		//Once the ROM has booted (~2 seconds), choose "Tape Loader" (the first item of the menu) with ENTER
		press({ { 6, 0 } });
		return;
	}

	//Once the ROM has booted (~2 seconds), type LOAD "" ENTER: J (LOAD in K mode), SYMBOL SHIFT+P twice, ENTER
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

	SendFrame(_machine->GetFrameBuffer());

	_emu->ProcessEndOfFrame();
	_emu->ProcessEvent(EventType::StartFrame, CpuType::Zx);
	_controlManager->UpdateControlDevices();
	_controlManager->UpdateInputState();

	//Mix the beeper and the AY (same number of samples per frame) - the mixer expects interleaved stereo samples
	ZxConfig& cfg = _emu->GetSettings()->GetZxConfig();
	const vector<int16_t>& samples = _machine->GetBeeper().GetSamples();
	const vector<int16_t>* aySamples = _machine->HasAy() ? &_machine->GetAy().GetSamples() : nullptr;
	_stereoSamples.resize(samples.size() * 2);
	for(size_t i = 0; i < samples.size(); i++) {
		int32_t sample = samples[i] * (int32_t)cfg.BeeperVolume / 100;
		if(aySamples && i < aySamples->size()) {
			sample += (*aySamples)[i] * (int32_t)cfg.AyVolume / 100;
		}
		int16_t clamped = (int16_t)std::clamp<int32_t>(sample, INT16_MIN, INT16_MAX);
		_stereoSamples[i * 2] = clamped;
		_stereoSamples[i * 2 + 1] = clamped;
	}
	_emu->GetSoundMixer()->PlayAudioBuffer(_stereoSamples.data(), (uint32_t)samples.size(), ZxBeeper::SampleRate);
}

void ZxConsole::SendFrame(uint16_t* frameBuffer)
{
	uint16_t* outputBuffer = _outputBuffers[_currentBuffer];
	_currentBuffer ^= 1;
	memcpy(outputBuffer, frameBuffer, sizeof(_outputBuffers[0]));
	RenderedFrame frame(outputBuffer, ZxConstants::ScreenWidth, ZxConstants::ScreenHeight, 1.0, _machine->GetUla().GetFrameCount(), _controlManager->GetPortStates());
	bool rewinding = _emu->GetRewindManager()->IsRewinding();
	_emu->GetVideoDecoder()->UpdateFrame(frame, rewinding, rewinding);
}

void ZxConsole::DebugSendFrame()
{
	//Draw up to the current T-state, and blank what the beam hasn't reached yet
	ZxUla& ula = _machine->GetUla();
	ula.Render(_machine->GetCpu().GetCycleCount());
	uint16_t frameBuffer[ZxConstants::PixelCount];
	memcpy(frameBuffer, ula.GetFrameBuffer(), sizeof(frameBuffer));
	uint32_t drawn = std::min(ula.GetDrawnPixelCount(), ZxConstants::PixelCount);
	std::fill(frameBuffer + drawn, frameBuffer + ZxConstants::PixelCount, 0);
	SendFrame(frameBuffer);
}

void ZxConsole::ProcessInstruction()
{
	_emu->ProcessPpuCycle<CpuType::Zx>();
	_emu->ProcessInstruction<CpuType::Zx>();
}

void ZxConsole::ProcessHaltedCpu()
{
	_emu->ProcessPpuCycle<CpuType::Zx>();
	_emu->ProcessHaltedCpu<CpuType::Zx>();
}

void ZxConsole::ProcessInterrupt(uint16_t originalPc, uint16_t currentPc, bool forNmi)
{
	_emu->ProcessInterrupt<CpuType::Zx>(originalPc, currentPc, forNmi);
}

void ZxConsole::ProcessMemoryRead(uint16_t addr, uint8_t& value, MemoryOperationType type)
{
	_emu->ProcessMemoryRead<CpuType::Zx>(addr, value, type);
}

bool ZxConsole::ProcessMemoryWrite(uint16_t addr, uint8_t& value)
{
	return _emu->ProcessMemoryWrite<CpuType::Zx>(addr, value, MemoryOperationType::Write);
}

void ZxConsole::ProcessPortRead(uint16_t port, uint8_t& value)
{
	_emu->ProcessMemoryAccess<CpuType::Zx, MemoryType::ZxPort, MemoryOperationType::Read>(port, value);
}

void ZxConsole::ProcessPortWrite(uint16_t port, uint8_t& value)
{
	_emu->ProcessMemoryAccess<CpuType::Zx, MemoryType::ZxPort, MemoryOperationType::Write>(port, value);
}

void ZxConsole::ProcessEvent(EventType type)
{
	_emu->ProcessEvent(type, CpuType::Zx);
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
	uint32_t offset;
	bool isRom = _machine->GetMemoryManager().GetMappedOffset((uint16_t)relAddress.Address, offset);
	return { (int32_t)offset, isRom ? MemoryType::ZxRom : MemoryType::ZxRam };
}

AddressInfo ZxConsole::GetRelativeAddress(AddressInfo& absAddress, CpuType cpuType)
{
	switch(absAddress.Type) {
		case MemoryType::ZxRom:
		case MemoryType::ZxRam: {
			int32_t addr = _machine->GetMemoryManager().GetCpuAddress(absAddress.Type == MemoryType::ZxRom, absAddress.Address);
			return { addr, addr >= 0 ? MemoryType::ZxMemory : MemoryType::None };
		}
		default: return { -1, MemoryType::None };
	}
}

ZxState ZxConsole::GetState()
{
	ZxState state;
	state.Cpu = _machine->GetCpu().GetState();
	state.Ula = _machine->GetUla().GetState(_machine->GetCpu().GetCycleCount());
	state.Io.PortFe = _machine->GetMemoryManager().GetPortFe();
	state.Io.Port7ffd = _machine->GetMemoryManager().GetPort7ffd();
	state.Io.HasPaging = _machine->GetMemoryManager().HasPaging();
	if(_machine->HasAy()) {
		state.Ay = _machine->GetAy().GetState();
	} else {
		state.Ay = {};
	}
	state.Io.EarInput = _machine->GetMemoryManager().GetEarInput();
	for(int i = 0; i < 8; i++) {
		state.Io.KeyboardRows[i] = _machine->GetKeyboard().GetRow(i);
	}
	return state;
}

void ZxConsole::GetConsoleState(BaseState& state, ConsoleType consoleType)
{
	(ZxState&)state = GetState();
}

void ZxConsole::Serialize(Serializer& s)
{
	//The memory layout depends on the model - states can only be loaded on the model that saved them
	ZxModel model = _machine->GetModel();
	SV(model);
	if(!s.IsSaving() && model != _machine->GetModel()) {
		auto getName = [](ZxModel m) { return m == ZxModel::Spectrum128K ? string("128K") : string("48K"); };
		MessageManager::DisplayMessage("SaveStates", "Can't load " + getName(model) + " state in " + getName(_machine->GetModel()) + " mode.");
		s.SetErrorFlag();
		return;
	}

	SV(_machine);
	SV(_controlManager);
}
