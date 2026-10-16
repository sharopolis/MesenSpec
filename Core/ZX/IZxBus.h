#pragma once
#include "pch.h"
#include "Shared/MemoryOperationType.h"

//Everything the Z80 sees of the machine around it.
//Every call receives the T-state at which it happens, so the machine (ULA, AY, tape, etc.) can catch up lazily.
class IZxBus
{
public:
	virtual ~IZxBus() = default;

	//Extra T-states the CPU is held for when a cycle starts at 'cycle' with 'addr' on the address bus.
	//mreq is false for the internal (no MREQ) cycles that still place an address on the bus - these are
	//contended on the 48K/128K, but not on the +2A/+3.
	virtual uint8_t GetContention(uint16_t addr, uint64_t cycle, bool mreq) = 0;

	virtual uint8_t Read(uint16_t addr, uint64_t cycle, MemoryOperationType type) = 0;
	virtual void Write(uint16_t addr, uint8_t value, uint64_t cycle) = 0;

	//Side-effect free read, used by the debugger and the dummy CPU
	virtual uint8_t Peek(uint16_t addr) = 0;

	//I/O cycles take 4 T-states, split around the actual access. Each returns the T-states that elapsed,
	//including any contention (the pattern depends on the port's high byte, its low bit and the model).
	virtual uint8_t ContendPortPre(uint16_t port, uint64_t cycle) = 0;
	virtual uint8_t ContendPortPost(uint16_t port, uint64_t cycle) = 0;

	virtual uint8_t ReadPort(uint16_t port, uint64_t cycle) = 0;
	virtual void WritePort(uint16_t port, uint8_t value, uint64_t cycle) = 0;

	//Level of the /INT line at the given T-state (the ULA asserts it for ~32 T-states per frame)
	virtual bool IsIrqActive(uint64_t cycle) = 0;

	//Value on the data bus during an interrupt acknowledge (used as the low byte of the IM 2 vector)
	virtual uint8_t GetIrqDataBus() { return 0xFF; }
};
