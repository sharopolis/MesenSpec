#include "pch.h"
#include "ZX/Debugger/ZxEventManager.h"
#include "ZX/ZxConsole.h"
#include "ZX/ZxMachine.h"
#include "Debugger/DebugTypes.h"
#include "Debugger/Debugger.h"
#include "Debugger/DebugBreakHelper.h"
#include "Debugger/BaseEventManager.h"

ZxEventManager::ZxEventManager(Debugger* debugger, ZxConsole* console)
{
	_debugger = debugger;
	_console = console;
	_timing = console->GetMachine()->GetUla().GetTiming();
	_scanlineCount = console->GetMachine()->GetUla().GetScanlineCount();
}

void ZxEventManager::AddEvent(DebugEventType type, MemoryOperationInfo& operation, int32_t breakpointId)
{
	ZxMachine* machine = _console->GetMachine();
	ZxUlaState ula = machine->GetUla().GetState(machine->GetCpu().GetCycleCount());

	DebugEventInfo evt = {};
	evt.Type = type;
	evt.Flags = (uint32_t)EventFlags::ReadWriteOp;
	evt.Operation = operation;
	evt.Scanline = (int16_t)ula.Scanline;
	evt.Cycle = ula.Cycle;
	evt.BreakpointId = breakpointId;
	evt.DmaChannel = -1;
	evt.ProgramCounter = _debugger->GetProgramCounter(CpuType::Zx, true);
	_debugEvents.push_back(evt);
}

void ZxEventManager::AddEvent(DebugEventType type)
{
	ZxMachine* machine = _console->GetMachine();
	ZxUlaState ula = machine->GetUla().GetState(machine->GetCpu().GetCycleCount());

	DebugEventInfo evt = {};
	evt.Type = type;
	evt.Scanline = (int16_t)ula.Scanline;
	evt.Cycle = ula.Cycle;
	evt.BreakpointId = -1;
	evt.DmaChannel = -1;
	evt.ProgramCounter = machine->GetCpu().GetState().PC;
	_debugEvents.push_back(evt);
}

DebugEventInfo ZxEventManager::GetEvent(uint16_t y, uint16_t x)
{
	auto lock = _lock.AcquireSafe();

	//Look for an exact match first, then for an event whose dot (drawn larger) covers the position
	for(int distance = 0; distance <= 2; distance += 2) {
		for(int i = (int)_sentEvents.size() - 1; i >= 0; i--) {
			DebugEventInfo& evt = _sentEvents[i];
			int32_t evtX = evt.Cycle;
			int32_t evtY = evt.Scanline;
			ConvertScanlineCycleToRowColumn(evtX, evtY);
			if(std::abs(evtX - (int)x) <= distance + 1 && std::abs(evtY - (int)y) <= distance + 1) {
				return evt;
			}
		}
	}

	DebugEventInfo empty = {};
	empty.ProgramCounter = 0xFFFFFFFF;
	return empty;
}

bool ZxEventManager::ShowPreviousFrameEvents()
{
	return _config.ShowPreviousFrameEvents;
}

void ZxEventManager::SetConfiguration(BaseEventViewerConfig& config)
{
	_config = (ZxEventViewerConfig&)config;
}

EventViewerCategoryCfg ZxEventManager::GetEventConfig(DebugEventInfo& evt)
{
	switch(evt.Type) {
		default: return {};
		case DebugEventType::Breakpoint: return _config.MarkedBreakpoints;
		case DebugEventType::Irq: return _config.Irq;
		case DebugEventType::Nmi: return _config.Nmi;
		case DebugEventType::Register:
			if(evt.Operation.MemType == MemoryType::ZxPort) {
				bool isUlaPort = (evt.Operation.Address & 0x01) == 0;
				if(evt.Operation.Type == MemoryOperationType::Write) {
					return isUlaPort ? _config.UlaPortWrite : _config.OtherPortWrite;
				} else {
					return isUlaPort ? _config.UlaPortRead : _config.OtherPortRead;
				}
			}
			return _config.ScreenWrite;
	}
}

void ZxEventManager::ConvertScanlineCycleToRowColumn(int32_t& x, int32_t& y)
{
	//Shift the timeline by the left border's width, so that each row starts with the left border
	int32_t t = y * _timing.LineLength + x + ZxConstants::BorderWidth / 2;
	int32_t frameLength = _scanlineCount * _timing.LineLength;
	t = ((t % frameLength) + frameLength) % frameLength;
	y = (t / _timing.LineLength) * 2;
	x = (t % _timing.LineLength) * 2;
}

uint32_t ZxEventManager::TakeEventSnapshot(bool forAutoRefresh)
{
	DebugBreakHelper breakHelper(_debugger);
	auto lock = _lock.AcquireSafe();

	ZxMachine* machine = _console->GetMachine();
	uint64_t cycle = machine->GetCpu().GetCycleCount();
	ZxUlaState ula = machine->GetUla().GetState(cycle);

	//The ULA draws into its buffer as the beam moves: after drawing up to now, the part of the picture below the
	//beam still shows the previous frame
	machine->GetUla().Render(cycle);
	memcpy(_ppuBuffer, machine->GetFrameBuffer(), sizeof(_ppuBuffer));

	_snapshotCurrentFrame = _debugEvents;
	_snapshotPrevFrame = _prevDebugEvents;
	_snapshotScanline = (int16_t)ula.Scanline;
	_snapshotCycle = ula.Cycle;
	_forAutoRefresh = forAutoRefresh;
	return _scanlineCount;
}

FrameInfo ZxEventManager::GetDisplayBufferSize()
{
	FrameInfo size;
	size.Width = GetDisplayWidth();
	size.Height = _scanlineCount * 2;
	return size;
}

void ZxEventManager::DrawScreen(uint32_t* buffer)
{
	//Picture row 0 is on frame line FirstPaperLine - BorderHeight, and (after the shift) each picture pixel is
	//at the column matching its T-state
	uint32_t width = GetDisplayWidth();
	uint32_t firstLine = _timing.FirstPaperLine - ZxConstants::BorderHeight;
	for(uint32_t y = 0; y < ZxConstants::ScreenHeight; y++) {
		uint32_t row = (firstLine + y) * 2;
		if(row + 1 >= _scanlineCount * 2u) {
			break;
		}
		for(uint32_t x = 0; x < ZxConstants::ScreenWidth && x < width; x++) {
			uint32_t color = ZxMachine::GetPaletteColor(_ppuBuffer[y * ZxConstants::ScreenWidth + x]);
			buffer[row * width + x] = color;
			buffer[(row + 1) * width + x] = color;
		}
	}
}
