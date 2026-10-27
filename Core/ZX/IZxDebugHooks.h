#pragma once
#include "pch.h"
#include "Shared/MemoryOperationType.h"

enum class EventType;

//Notifications from the machine to the debugger. ZxMachine doesn't depend on the Mesen emulator (so the headless
//tests can use it on its own) - ZxConsole implements this and forwards everything to Mesen's debugger.
//When no hooks are set (headless), nothing is called.
class IZxDebugHooks
{
public:
	virtual ~IZxDebugHooks() = default;

	//Called before each instruction (PC points to its first byte), or instead of it while the CPU is halted
	virtual void ProcessInstruction() = 0;
	virtual void ProcessHaltedCpu() = 0;

	//Called after an interrupt was taken: originalPc is the return address, currentPc the handler's address
	virtual void ProcessInterrupt(uint16_t originalPc, uint16_t currentPc, bool forNmi) = 0;

	virtual void ProcessMemoryRead(uint16_t addr, uint8_t& value, MemoryOperationType type) = 0;
	//Returns false if the write must not happen (frozen address)
	virtual bool ProcessMemoryWrite(uint16_t addr, uint8_t& value) = 0;

	virtual void ProcessPortRead(uint16_t port, uint8_t& value) = 0;
	virtual void ProcessPortWrite(uint16_t port, uint8_t& value) = 0;

	virtual void ProcessEvent(EventType type) = 0;
};
