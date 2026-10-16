#pragma once
#include "pch.h"
#include "Shared/BaseState.h"
#include "Shared/MemoryType.h"

struct ZxCpuState : public BaseState
{
	//Total number of T-states executed since power on
	uint64_t CycleCount;
	uint16_t PC;
	uint16_t SP;

	uint8_t A;
	uint8_t Flags;

	uint8_t B;
	uint8_t C;
	uint8_t D;
	uint8_t E;

	uint8_t H;
	uint8_t L;

	uint8_t IXL;
	uint8_t IXH;

	uint8_t IYL;
	uint8_t IYH;

	uint8_t I;
	uint8_t R;

	uint8_t AltA;
	uint8_t AltFlags;
	uint8_t AltB;
	uint8_t AltC;
	uint8_t AltD;
	uint8_t AltE;
	uint8_t AltH;
	uint8_t AltL;

	bool NmiLevel;
	bool NmiPending;

	//While halted, PC points to the HALT opcode itself (it is re-fetched every 4 T-states), like Fuse/SZX
	bool Halted;

	bool IFF1;
	bool IFF2;

	uint8_t IM;

	//Internal flags needed to properly emulate the behavior of the undocumented F3/F5 flags
	uint8_t FlagsChanged;
	uint16_t WZ;

	//Set when the last instruction was LD A,I or LD A,R (NMOS Z80s copy IFF2 to P/V before an accepted IRQ clears it)
	bool Iff2Read;
};

namespace ZxCpuFlags
{
	enum ZxCpuFlags
	{
		Carry = 0x01,
		AddSub = 0x02,
		Parity = 0x04,
		F3 = 0x08,
		HalfCarry = 0x10,
		F5 = 0x20,
		Zero = 0x40,
		Sign = 0x80,
	};
}

class ZxCpuParityTable
{
private:
	uint8_t _parityTable[0x100];

public:
	ZxCpuParityTable()
	{
		for(int i = 0; i < 256; i++) {
			int count = 0;
			for(int j = 0; j < 8; j++) {
				count += (i & (1 << j)) ? 1 : 0;
			}
			_parityTable[i] = count & 0x01 ? 0 : 1;
		}
	}

	__forceinline bool CheckParity(uint8_t val)
	{
		return _parityTable[val];
	}
};
