#pragma once
#include "pch.h"
#include "Shared/BaseState.h"
#include "Shared/MemoryType.h"
#include "Shared/SettingTypes.h"

//Frame/ULA timing for a model, in T-states (CPU clock cycles)
struct ZxTiming
{
	uint32_t CpuClockRate;
	uint32_t FrameLength;
	uint16_t LineLength;
	//Frame line on which the first line of the paper (bitmap) area starts - pixel (0,0) is displayed at FirstPaperLine * LineLength
	uint16_t FirstPaperLine;
	//First T-state at which memory contention applies (the floating bus returns the first bitmap byte 3 T-states later)
	uint32_t ContentionStart;
	//Number of T-states the ULA holds /INT low at the start of each frame
	uint8_t IrqLength;

	static ZxTiming Get(ZxModel model)
	{
		switch(model) {
			default:
			case ZxModel::Spectrum48K: return { 3500000, 69888, 224, 64, 14335, 32 };
			//Paper starts at 14364 (line 63), contention 3 T-states earlier
			case ZxModel::Spectrum128K: return { 3546900, 70908, 228, 63, 14361, 36 };
		}
	}
};

namespace ZxConstants
{
	//Output picture: 256x192 paper area surrounded by a 48 pixel border
	constexpr uint32_t BorderWidth = 48;
	constexpr uint32_t BorderHeight = 48;
	constexpr uint32_t ScreenWidth = 256 + BorderWidth * 2;
	constexpr uint32_t ScreenHeight = 192 + BorderHeight * 2;
	constexpr uint32_t PixelCount = ScreenWidth * ScreenHeight;
}

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

//ULA state as seen by the debugger (scanline/cycle are computed from the CPU's cycle count)
struct ZxUlaState : public BaseState
{
	uint32_t FrameCount;
	//T-state within the frame (0 = start of the /INT pulse)
	uint32_t FrameCycle;
	//FrameCycle / line length - the paper area starts at line 64 on the 48K
	uint16_t Scanline;
	//FrameCycle % line length - the paper of each line starts at cycle 0
	uint16_t Cycle;
	uint8_t BorderColor;
};

//Port 0xFE and the keyboard, as seen by the debugger
struct ZxIoState : public BaseState
{
	//Last value written to port 0xFE (bits 0-2: border, bit 3: MIC, bit 4: EAR/speaker)
	uint8_t PortFe;
	//Level of the EAR input (tape signal)
	bool EarInput;
	//Keyboard matrix, one half-row per entry (bits 0-4, 0 = pressed), selected by address lines A8-A15
	uint8_t KeyboardRows[8];
	//128K paging register (bits 0-2 RAM bank at 0xC000, bit 3 screen in bank 7, bit 4 ROM 1, bit 5 lock)
	uint8_t Port7ffd;
	bool HasPaging;
};

//Not derived from BaseState (like SmsState etc.): Cpu, which is, could then not be at offset 0, and the layout would
//no longer match the UI's ZxState
struct ZxState
{
	ZxCpuState Cpu;
	ZxUlaState Ula;
	ZxIoState Io;
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
