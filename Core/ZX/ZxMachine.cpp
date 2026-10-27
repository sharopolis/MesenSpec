#include "pch.h"
#include "ZX/ZxMachine.h"
#include "ZX/ZxSnapshot.h"
#include "ZX/IZxDebugHooks.h"
#include "Utilities/Serializer.h"

ZxMachine::ZxMachine()
{
	const ZxTiming& timing = ZxTiming::Get(_model);
	_memoryManager.Init(&_ula, &_keyboard, &_beeper);
	_ula.Init(_model, _memoryManager.GetVideoRam());
	_beeper.Init(timing.CpuClockRate);
	_cpu.Init(&_memoryManager);
}

bool ZxMachine::LoadRom(const vector<uint8_t>& rom)
{
	return _memoryManager.LoadRom(rom);
}

bool ZxMachine::LoadSnapshot(const vector<uint8_t>& data, const string& extension, string& error)
{
	return ZxSnapshot::Load(data, extension, _cpu, _memoryManager, _ula, error);
}

vector<uint8_t> ZxMachine::SaveSnapshot(const string& extension)
{
	return ZxSnapshot::Save(extension, _cpu, _memoryManager, _ula);
}

bool ZxMachine::LoadTape(const vector<uint8_t>& data, string& error)
{
	return _tape.Load(data, error);
}

void ZxMachine::Reset()
{
	_memoryManager.Reset();
	_ula.Reset();
	_beeper.Init(ZxTiming::Get(_model).CpuClockRate);
	_keyboard.ReleaseAll();
	_cpu.Reset();
}

void ZxMachine::RunFrame()
{
	uint64_t frameEnd = _ula.GetFrameStartCycle() + _ula.GetTiming().FrameLength;
	bool tapeLoaded = _tape.IsLoaded();
	while(_cpu.GetCycleCount() < frameEnd) {
		if(tapeLoaded && _cpu.GetState().PC == ZxTape::LdBytesAddress) {
			//The trap replaces the instruction at LD-BYTES, so let the debugger see it (e.g. for breakpoints) first
			if(IZxDebugHooks* hooks = _cpu.GetDebugHooks()) {
				hooks->ProcessInstruction();
			}
			_tape.ProcessLoadTrap(_cpu, _memoryManager);
		}
		_cpu.Exec();
	}
	_ula.EndFrame();
	_beeper.EndFrame(_ula.GetTiming().FrameLength);
}

uint32_t ZxMachine::GetPaletteColor(uint16_t index)
{
	//GRB colour bits, normal colours at ~84% intensity
	uint8_t level = (index & 0x08) ? 0xFF : 0xD7;
	uint8_t r = (index & 0x02) ? level : 0;
	uint8_t g = (index & 0x04) ? level : 0;
	uint8_t b = (index & 0x01) ? level : 0;
	return 0xFF000000 | (r << 16) | (g << 8) | b;
}

void ZxMachine::Serialize(Serializer& s)
{
	SV(_cpu);
	SV(_ula);
	SV(_memoryManager);
}
