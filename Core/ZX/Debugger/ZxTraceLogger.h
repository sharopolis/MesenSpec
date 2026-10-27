#pragma once
#include "pch.h"
#include "Debugger/BaseTraceLogger.h"
#include "ZX/ZxTypes.h"

class DisassemblyInfo;
class Debugger;
class ZxConsole;

class ZxTraceLogger : public BaseTraceLogger<ZxTraceLogger, ZxCpuState>
{
private:
	ZxConsole* _console = nullptr;

protected:
	RowDataType GetFormatTagType(string& tag) override;

public:
	ZxTraceLogger(Debugger* debugger, IDebugger* cpuDebugger, ZxConsole* console);

	void GetTraceRow(string& output, ZxCpuState& cpuState, TraceLogPpuState& vdpState, DisassemblyInfo& disassemblyInfo);
	void LogPpuState();

	__forceinline uint32_t GetProgramCounter(ZxCpuState& state) { return state.PC; }
	__forceinline uint64_t GetCycleCount(ZxCpuState& state) { return state.CycleCount; }
	__forceinline uint8_t GetStackPointer(ZxCpuState& state) { return (uint8_t)state.SP; }
};
