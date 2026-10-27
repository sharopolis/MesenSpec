#include "pch.h"
#include "ZX/Debugger/ZxDebugger.h"
#include "ZX/Debugger/DummyZxCpu.h"
#include "ZX/Debugger/ZxDisUtils.h"
#include "ZX/Debugger/ZxEventManager.h"
#include "ZX/Debugger/ZxPpuTools.h"
#include "ZX/Debugger/ZxTraceLogger.h"
#include "ZX/ZxConsole.h"
#include "ZX/ZxMachine.h"
#include "ZX/ZxCpu.h"
#include "ZX/ZxUla.h"
#include "SMS/Debugger/SmsAssembler.h"
#include "Debugger/DisassemblyInfo.h"
#include "Debugger/Disassembler.h"
#include "Debugger/CallstackManager.h"
#include "Debugger/BreakpointManager.h"
#include "Debugger/Debugger.h"
#include "Debugger/MemoryAccessCounter.h"
#include "Debugger/MemoryDumper.h"
#include "Debugger/CodeDataLogger.h"
#include "Debugger/BaseEventManager.h"
#include "Debugger/StepBackManager.h"
#include "Utilities/HexUtilities.h"
#include "Shared/EmuSettings.h"
#include "Shared/Emulator.h"
#include "Shared/MemoryOperationType.h"

ZxDebugger::ZxDebugger(Debugger* debugger) : IDebugger(debugger->GetEmulator())
{
	_debugger = debugger;
	_emu = debugger->GetEmulator();

	_disassembler = debugger->GetDisassembler();
	_memoryAccessCounter = debugger->GetMemoryAccessCounter();

	_console = (ZxConsole*)debugger->GetConsole();
	_machine = _console->GetMachine();
	_cpu = &_machine->GetCpu();
	_ula = &_machine->GetUla();

	_settings = debugger->GetEmulator()->GetSettings();

	_romCdl.reset(new CodeDataLogger(debugger, MemoryType::ZxRom, _emu->GetMemory(MemoryType::ZxRom).Size, CpuType::Zx, _emu->GetCrc32()));
	_ramCdl.reset(new CodeDataLogger(debugger, MemoryType::ZxRam, _emu->GetMemory(MemoryType::ZxRam).Size, CpuType::Zx, _emu->GetCrc32()));
	//The RAM's log is specific to the program that was loaded - saved next to its other debugger files
	_cdlFile = _ramCdl->GetCdlFilePath(_emu->GetRomInfo().RomFile.GetFileName());
	_ramCdl->LoadCdlFile(_cdlFile, _settings->GetDebugConfig().AutoResetCdl);

	_traceLogger.reset(new ZxTraceLogger(debugger, this, _console));
	_ppuTools.reset(new ZxPpuTools(debugger, _emu));

	_stepBackManager.reset(new StepBackManager(_emu, this));
	_eventManager.reset(new ZxEventManager(debugger, _console));
	_callstackManager.reset(new CallstackManager(debugger, this));
	_breakpointManager.reset(new BreakpointManager(debugger, this, CpuType::Zx, _eventManager.get()));
	_step.reset(new StepRequest());
	_assembler.reset(new SmsAssembler(debugger->GetLabelManager(), CpuType::Zx));

	_dummyCpu.reset(new DummyZxCpu());
	_dummyCpu->Init(&_machine->GetMemoryManager());

	_prevUlaCycle = _cpu->GetCycleCount();
}

ZxDebugger::~ZxDebugger()
{
	_ramCdl->SaveCdlFile(_cdlFile);
}

void ZxDebugger::Reset()
{
	_callstackManager->Clear();
	ResetPrevOpCode();
	_prevUlaCycle = _cpu->GetCycleCount();
}

CodeDataLogger* ZxDebugger::GetCdl(MemoryType memType)
{
	switch(memType) {
		case MemoryType::ZxRom: return _romCdl.get();
		case MemoryType::ZxRam: return _ramCdl.get();
		default: return nullptr;
	}
}

AddressInfo ZxDebugger::GetAbsoluteAddress(uint16_t addr)
{
	AddressInfo relAddr = { addr, MemoryType::ZxMemory };
	return _console->GetAbsoluteAddress(relAddr);
}

uint16_t ZxDebugger::ReadOpCode(uint16_t pc)
{
	MemoryDumper* dumper = _debugger->GetMemoryDumper();
	uint8_t value = dumper->GetMemoryValue(MemoryType::ZxMemory, pc);
	if(value == 0xED || value == 0xDD || value == 0xFD) {
		return value | (dumper->GetMemoryValue(MemoryType::ZxMemory, (uint16_t)(pc + 1)) << 8);
	}
	return value;
}

void ZxDebugger::ProcessInstruction()
{
	ZxCpuState& state = _cpu->GetState();
	uint16_t pc = state.PC;
	AddressInfo addressInfo = GetAbsoluteAddress(pc);
	uint8_t value = _debugger->GetMemoryDumper()->GetMemoryValue(MemoryType::ZxMemory, pc);
	MemoryOperationInfo operation(pc, value, MemoryOperationType::ExecOpCode, MemoryType::ZxMemory);
	InstructionProgress.LastMemOperation = operation;
	InstructionProgress.StartCycle = state.CycleCount;

	if(addressInfo.Address >= 0) {
		CodeDataLogger* cdl = GetCdl(addressInfo.Type);
		if(cdl) {
			cdl->SetCode(addressInfo.Address, ZxDisUtils::GetOpFlags(_prevOpCode, pc, _prevProgramCounter));
		}
		_disassembler->BuildCache(addressInfo, 0, CpuType::Zx);
	}

	ProcessCallStackUpdates(addressInfo, pc, state.SP);

	if(_step->BreakAddress == (int32_t)pc && _step->BreakStackPointer == state.SP) {
		//Step over/out: the routine returned without a RET (e.g. it popped its return address and jumped)
		_step->Break(BreakSource::CpuStep);
	}

	_prevOpCode = ReadOpCode(pc);
	_prevProgramCounter = pc;
	_prevStackPointer = state.SP;

	_step->ProcessCpuExec();

	if(_step->StepCount != 0 && _breakpointManager->HasBreakpoints() && _settings->GetDebugConfig().UsePredictiveBreakpoints) {
		_dummyCpu->SetDummyState(state);
		_dummyCpu->Exec();
		for(uint32_t i = 1; i < _dummyCpu->GetOperationCount(); i++) {
			MemoryOperationInfo memOp = _dummyCpu->GetOperationInfo(i);
			if(_breakpointManager->HasBreakpointForType(memOp.Type)) {
				AddressInfo memAddr = { (int32_t)memOp.Address, memOp.MemType };
				AddressInfo absAddr = memOp.MemType == MemoryType::ZxMemory ? _console->GetAbsoluteAddress(memAddr) : memAddr;
				_debugger->ProcessPredictiveBreakpoint(CpuType::Zx, _breakpointManager.get(), memOp, absAddr);
			}
		}
	}

	_debugger->ProcessBreakConditions(CpuType::Zx, *_step.get(), _breakpointManager.get(), operation, addressInfo);
}

void ZxDebugger::ProcessRead(uint32_t addr, uint8_t value, MemoryOperationType type)
{
	AddressInfo addressInfo = GetAbsoluteAddress((uint16_t)addr);
	MemoryOperationInfo operation(addr, value, type, MemoryType::ZxMemory);
	InstructionProgress.LastMemOperation = operation;
	CodeDataLogger* cdl = GetCdl(addressInfo.Type);

	if(type == MemoryOperationType::ExecOpCode) {
		if(_traceLogger->IsEnabled()) {
			DisassemblyInfo disInfo = _disassembler->GetDisassemblyInfo(addressInfo, addr, 0, CpuType::Zx);
			_traceLogger->Log(_cpu->GetState(), disInfo, operation, addressInfo);
		}
		_memoryAccessCounter->ProcessMemoryExec(addressInfo, _console->GetMasterClock());
		if(_step->ProcessCpuCycle()) {
			_debugger->SleepUntilResume(CpuType::Zx, BreakSource::CpuStep, &operation);
		}
	} else if(type == MemoryOperationType::ExecOperand) {
		if(cdl && addressInfo.Address >= 0) {
			cdl->SetCode(addressInfo.Address);
		}

		if(_traceLogger->IsEnabled()) {
			_traceLogger->LogNonExec(operation, addressInfo);
		}

		_memoryAccessCounter->ProcessMemoryExec(addressInfo, _console->GetMasterClock());
		_step->ProcessCpuCycle();
		_debugger->ProcessBreakConditions(CpuType::Zx, *_step.get(), _breakpointManager.get(), operation, addressInfo);
	} else {
		if(cdl && addressInfo.Address >= 0) {
			cdl->SetData(addressInfo.Address);
		}

		if(_traceLogger->IsEnabled()) {
			_traceLogger->LogNonExec(operation, addressInfo);
		}

		ReadResult result = _memoryAccessCounter->ProcessMemoryRead(addressInfo, _console->GetMasterClock());
		if(result != ReadResult::Normal) {
			//Memory access was a read on an uninitialized memory address
			if(result == ReadResult::FirstUninitRead) {
				//Only warn the first time
				_debugger->Log("[ZX] Uninitialized memory read: $" + HexUtilities::ToHex((uint16_t)addr));
			}
			if(_settings->CheckDebuggerFlag(DebuggerFlags::ZxDebuggerEnabled) && _settings->GetDebugConfig().BreakOnUninitRead) {
				_step->Break(BreakSource::BreakOnUninitMemoryRead);
			}
		}

		_step->ProcessCpuCycle();
		_debugger->ProcessBreakConditions(CpuType::Zx, *_step.get(), _breakpointManager.get(), operation, addressInfo);
	}
}

void ZxDebugger::ProcessWrite(uint32_t addr, uint8_t value, MemoryOperationType type)
{
	AddressInfo addressInfo = GetAbsoluteAddress((uint16_t)addr);
	MemoryOperationInfo operation(addr, value, type, MemoryType::ZxMemory);
	InstructionProgress.LastMemOperation = operation;

	if(addressInfo.Type == MemoryType::ZxRam) {
		_disassembler->InvalidateCache(addressInfo, CpuType::Zx);

		if(addr >= 0x4000 && addr < 0x5B00 && _eventManager->IsScreenWriteEnabled()) {
			_eventManager->AddEvent(DebugEventType::Register, operation);
		}
	}

	if(_traceLogger->IsEnabled()) {
		_traceLogger->LogNonExec(operation, addressInfo);
	}

	_memoryAccessCounter->ProcessMemoryWrite(addressInfo, _console->GetMasterClock());
	_step->ProcessCpuCycle();
	_debugger->ProcessBreakConditions(CpuType::Zx, *_step.get(), _breakpointManager.get(), operation, addressInfo);
}

template<MemoryOperationType opType>
void ZxDebugger::ProcessMemoryAccess(uint32_t addr, uint8_t value, MemoryType memType)
{
	MemoryOperationInfo operation(addr, value, opType, memType);
	_eventManager->AddEvent(DebugEventType::Register, operation);
}

void ZxDebugger::Run()
{
	_step.reset(new StepRequest());
}

void ZxDebugger::Step(int32_t stepCount, StepType type)
{
	StepRequest step(type);
	const ZxTiming& timing = _ula->GetTiming();

	switch(type) {
		case StepType::Step: step.StepCount = stepCount; break;
		case StepType::StepOut:
			step.BreakAddress = _callstackManager->GetReturnAddress();
			step.BreakStackPointer = _callstackManager->GetReturnStackPointer();
			break;

		case StepType::StepOver:
			if(ZxDisUtils::IsJumpToSub((uint8_t)_prevOpCode)) {
				step.BreakAddress = _prevProgramCounter + GetPrevOpCodeSize();
				step.BreakStackPointer = _prevStackPointer;
			} else {
				//For any other instruction, step over is the same as step into
				step.StepCount = 1;
			}
			break;

		case StepType::CpuCycleStep: step.CpuCycleStepCount = stepCount; break;

		//The ULA's "cycles" are T-states (2 pixels each)
		case StepType::PpuStep: step.PpuStepCount = stepCount; break;
		case StepType::PpuScanline: step.PpuStepCount = timing.LineLength * stepCount; break;
		case StepType::PpuFrame: step.PpuStepCount = timing.FrameLength * stepCount; break;
		case StepType::SpecificScanline: step.BreakScanline = stepCount; break;
	}

	_step.reset(new StepRequest(step));
}

void ZxDebugger::ProcessPpuCycle()
{
	uint64_t cycle = _cpu->GetCycleCount();
	uint64_t prevCycle = _prevUlaCycle;
	_prevUlaCycle = cycle;

	if(cycle <= prevCycle) {
		//After a reset or a state load
		return;
	}

	bool hasViewers = _ppuTools->HasOpenedViewer();
	if(!_step->HasRequest && !hasViewers) {
		return;
	}

	const ZxTiming& timing = _ula->GetTiming();
	int64_t frameStart = (int64_t)_ula->GetFrameStartCycle();
	auto getFrameCycle = [&](uint64_t c) -> uint32_t {
		int64_t t = ((int64_t)c - frameStart) % timing.FrameLength;
		return (uint32_t)(t < 0 ? t + timing.FrameLength : t);
	};
	uint32_t prevFrameCycle = getFrameCycle(prevCycle);
	uint32_t frameCycle = getFrameCycle(cycle);

	if(hasViewers) {
		_ppuTools->UpdateViewers(prevFrameCycle, frameCycle, timing.LineLength);
	}

	if(_step->HasScanlineBreakRequest()) {
		//Break on the first instruction that starts at or after the beginning of the scanline
		uint32_t target = (uint32_t)_step->BreakScanline * timing.LineLength;
		bool crossed = frameCycle >= prevFrameCycle ? (prevFrameCycle < target && target <= frameCycle) : (target > prevFrameCycle || target <= frameCycle);
		if(crossed || cycle - prevCycle >= timing.FrameLength) {
			_debugger->SleepUntilResume(CpuType::Zx, _step->GetBreakSource());
		}
	} else if(_step->PpuStepCount > 0) {
		uint64_t elapsed = cycle - prevCycle;
		if(elapsed >= (uint64_t)_step->PpuStepCount) {
			_step->PpuStepCount = 0;
			_debugger->SleepUntilResume(CpuType::Zx, _step->GetBreakSource());
		} else {
			_step->PpuStepCount -= (int32_t)elapsed;
		}
	}
}

StepBackConfig ZxDebugger::GetStepBackConfig()
{
	const ZxTiming& timing = _ula->GetTiming();
	return {
		_cpu->GetCycleCount(),
		timing.LineLength,
		timing.FrameLength
	};
}

void ZxDebugger::DrawPartialFrame()
{
	_console->DebugSendFrame();
}

uint8_t ZxDebugger::GetPrevOpCodeSize()
{
	return ZxDisUtils::GetOpSize((uint8_t)_prevOpCode, _prevProgramCounter, MemoryType::ZxMemory, _debugger->GetMemoryDumper());
}

void ZxDebugger::ProcessCallStackUpdates(AddressInfo& destAddr, uint16_t destPc, uint16_t sp)
{
	if(ZxDisUtils::IsJumpToSub((uint8_t)_prevOpCode) && destPc != _prevProgramCounter + GetPrevOpCodeSize()) {
		//CALL and RST, and PC doesn't match the next instruction, so the call was (probably) done
		uint8_t opSize = GetPrevOpCodeSize();
		uint16_t returnPc = _prevProgramCounter + opSize;
		AddressInfo src = GetAbsoluteAddress(_prevProgramCounter);
		AddressInfo ret = GetAbsoluteAddress(returnPc);
		_callstackManager->Push(src, _prevProgramCounter, destAddr, destPc, ret, returnPc, _prevStackPointer, StackFrameFlags::None);
	} else if(ZxDisUtils::IsReturnInstruction(_prevOpCode)) {
		if(destPc != _prevProgramCounter + GetPrevOpCodeSize()) {
			//RET used, and PC doesn't match the next instruction, so the ret was (probably) taken
			_callstackManager->Pop(destAddr, destPc, sp);
		}

		if(_step->BreakAddress >= 0 && _step->BreakStackPointer == sp) {
			//RET/RETI back to the caller's stack level - break (for step over/step out).
			//The address isn't checked: ROM routines like RST 28 (the calculator) return past the data that follows them.
			_step->Break(BreakSource::CpuStep);
		}
	}
}

void ZxDebugger::ProcessInterrupt(uint32_t originalPc, uint32_t currentPc, bool forNmi)
{
	AddressInfo ret = GetAbsoluteAddress((uint16_t)originalPc);
	AddressInfo dest = GetAbsoluteAddress((uint16_t)currentPc);

	CodeDataLogger* cdl = GetCdl(dest.Type);
	if(cdl && dest.Address >= 0) {
		cdl->SetCode(dest.Address, CdlFlags::SubEntryPoint);
	}

	uint16_t originalSp = _cpu->GetState().SP + 2;
	_prevStackPointer = originalSp;

	//If a call/return occurred just before IRQ, it needs to be processed now
	ProcessCallStackUpdates(ret, originalPc, originalSp);
	ResetPrevOpCode();

	_debugger->InternalProcessInterrupt(CpuType::Zx, *this, *_step.get(), ret, originalPc, dest, currentPc, ret, originalPc, originalSp, forNmi);
}

DebuggerFeatures ZxDebugger::GetSupportedFeatures()
{
	DebuggerFeatures features = {};
	features.RunToIrq = true;
	features.RunToNmi = true;
	features.StepOver = true;
	features.StepOut = true;
	features.StepBack = true;
	features.CallStack = true;
	features.CpuCycleStep = true;
	features.ChangeProgramCounter = AllowChangeProgramCounter;

	features.CpuVectors[0] = { "IRQ", 0x38, VectorType::Direct };
	features.CpuVectors[1] = { "NMI", 0x66, VectorType::Direct };
	features.CpuVectorCount = 2;

	return features;
}

void ZxDebugger::SetProgramCounter(uint32_t addr, bool updateDebuggerOnly)
{
	if(!updateDebuggerOnly) {
		_cpu->GetState().PC = (uint16_t)addr;
	}
	_prevOpCode = ReadOpCode((uint16_t)addr);
	_prevProgramCounter = (uint16_t)addr;
	_prevStackPointer = _cpu->GetState().SP;
}

uint32_t ZxDebugger::GetProgramCounter(bool getInstPc)
{
	return getInstPc ? _prevProgramCounter : _cpu->GetState().PC;
}

uint64_t ZxDebugger::GetCpuCycleCount(bool forProfiler)
{
	return _cpu->GetCycleCount();
}

void ZxDebugger::ResetPrevOpCode()
{
	_prevOpCode = 0;
}

BaseEventManager* ZxDebugger::GetEventManager()
{
	return _eventManager.get();
}

IAssembler* ZxDebugger::GetAssembler()
{
	return _assembler.get();
}

CallstackManager* ZxDebugger::GetCallstackManager()
{
	return _callstackManager.get();
}

BreakpointManager* ZxDebugger::GetBreakpointManager()
{
	return _breakpointManager.get();
}

BaseState& ZxDebugger::GetState()
{
	return _cpu->GetState();
}

void ZxDebugger::GetPpuState(BaseState& state)
{
	(ZxUlaState&)state = _ula->GetState(_cpu->GetCycleCount());
}

void ZxDebugger::SetPpuState(BaseState& srcState)
{
	//The ULA's position is derived from the CPU's cycle count - nothing to change
}

PpuTools* ZxDebugger::GetPpuTools()
{
	return _ppuTools.get();
}

ITraceLogger* ZxDebugger::GetTraceLogger()
{
	return _traceLogger.get();
}

ISerializable* ZxDebugger::GetSerializableCpu()
{
	return _cpu;
}

template void ZxDebugger::ProcessMemoryAccess<MemoryOperationType::Read>(uint32_t addr, uint8_t value, MemoryType memType);
template void ZxDebugger::ProcessMemoryAccess<MemoryOperationType::Write>(uint32_t addr, uint8_t value, MemoryType memType);
