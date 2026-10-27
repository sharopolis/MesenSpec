#include "pch.h"
#include "ZX/Debugger/DummyZxCpu.h"

#define DUMMYCPU
#define ZxCpu DummyZxCpu
#include "ZX/ZxCpu.cpp"
#undef ZxCpu
#undef DUMMYCPU

void DummyZxCpu::SetDummyState(ZxCpuState& state)
{
	_memOpCounter = 0;
	_state = state;
	_state.Halted = false;
	_state.NmiLevel = false;
	_state.NmiPending = false;
}

uint32_t DummyZxCpu::GetOperationCount()
{
	return _memOpCounter;
}

void DummyZxCpu::LogMemoryOperation(uint32_t addr, uint8_t value, MemoryOperationType type, MemoryType memType)
{
	if(_memOpCounter < sizeof(_memOperations) / sizeof(_memOperations[0])) {
		_memOperations[_memOpCounter] = {
			addr,
			(int32_t)value,
			type,
			memType
		};
		_memOpCounter++;
	}
}

MemoryOperationInfo DummyZxCpu::GetOperationInfo(uint32_t index)
{
	return _memOperations[index];
}
