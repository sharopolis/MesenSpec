#pragma once
#include "pch.h"
#include "Debugger/DebugTypes.h"
#include "Debugger/IDebugger.h"

class Disassembler;
class Debugger;
class ZxTraceLogger;
class ZxConsole;
class ZxMachine;
class ZxCpu;
class ZxUla;
class CallstackManager;
class MemoryAccessCounter;
class BreakpointManager;
class EmuSettings;
class SmsAssembler;
class Emulator;
class CodeDataLogger;
class ZxEventManager;
class DummyZxCpu;
class ZxPpuTools;

enum class MemoryOperationType;

class ZxDebugger final : public IDebugger
{
	Debugger* _debugger;
	Emulator* _emu;
	ZxConsole* _console;
	ZxMachine* _machine;
	ZxCpu* _cpu;
	ZxUla* _ula;
	Disassembler* _disassembler;
	MemoryAccessCounter* _memoryAccessCounter;
	EmuSettings* _settings;

	unique_ptr<ZxEventManager> _eventManager;
	unique_ptr<ZxPpuTools> _ppuTools;
	unique_ptr<CallstackManager> _callstackManager;
	//Code mostly runs from RAM on the Spectrum, so both the ROM and the RAM are logged
	unique_ptr<CodeDataLogger> _romCdl;
	unique_ptr<CodeDataLogger> _ramCdl;
	unique_ptr<BreakpointManager> _breakpointManager;
	unique_ptr<SmsAssembler> _assembler;
	unique_ptr<ZxTraceLogger> _traceLogger;
	unique_ptr<DummyZxCpu> _dummyCpu;

	//Opcode of the previous instruction - for prefixed instructions (DD/FD/ED), the next byte is in the high byte
	uint16_t _prevOpCode = 0;
	uint16_t _prevStackPointer = 0;
	uint32_t _prevProgramCounter = 0;

	//CPU cycle count when ProcessPpuCycle was last called (to step by T-states, scanlines and frames)
	uint64_t _prevUlaCycle = 0;

	string _cdlFile;

	__forceinline uint8_t GetPrevOpCodeSize();
	__forceinline void ProcessCallStackUpdates(AddressInfo& destAddr, uint16_t destPc, uint16_t sp);
	CodeDataLogger* GetCdl(MemoryType memType);
	uint16_t ReadOpCode(uint16_t pc);
	AddressInfo GetAbsoluteAddress(uint16_t addr);

public:
	ZxDebugger(Debugger* debugger);
	~ZxDebugger();

	void Reset() override;

	void ProcessInstruction();
	void ProcessRead(uint32_t addr, uint8_t value, MemoryOperationType type);
	void ProcessWrite(uint32_t addr, uint8_t value, MemoryOperationType type);

	template<MemoryOperationType opType>
	void ProcessMemoryAccess(uint32_t addr, uint8_t value, MemoryType memType);

	void ProcessInterrupt(uint32_t originalPc, uint32_t currentPc, bool forNmi) override;

	//Called before each instruction (and every 4 T-states while halted) - the ULA has no cycle loop of its own,
	//so stepping by T-states/scanlines/frames is done here, at instruction granularity
	void ProcessPpuCycle();

	void Run() override;
	void Step(int32_t stepCount, StepType type) override;
	StepBackConfig GetStepBackConfig() override;

	void DrawPartialFrame() override;

	void SetProgramCounter(uint32_t addr, bool updateDebuggerOnly = false) override;
	uint32_t GetProgramCounter(bool getInstPc) override;
	uint64_t GetCpuCycleCount(bool forProfiler) override;
	void ResetPrevOpCode() override;

	DebuggerFeatures GetSupportedFeatures() override;

	BaseEventManager* GetEventManager() override;
	IAssembler* GetAssembler() override;
	CallstackManager* GetCallstackManager() override;
	BreakpointManager* GetBreakpointManager() override;
	ITraceLogger* GetTraceLogger() override;
	PpuTools* GetPpuTools() override;
	ISerializable* GetSerializableCpu() override;

	BaseState& GetState() override;
	void GetPpuState(BaseState& state) override;
	void SetPpuState(BaseState& state) override;
};
