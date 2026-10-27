#pragma once
#include "pch.h"
#include "Debugger/DebugTypes.h"

class ZxConsole;
class LabelManager;
class EmuSettings;
class MemoryDumper;
struct ZxCpuState;

enum class ZxHlRegType
{
	HL,
	IX,
	IY
};

struct ZxOpInfo
{
	ZxHlRegType HlType = ZxHlRegType::HL;
	const char* Op = nullptr;
	int16_t IndexOffset = -1;
	uint8_t* ByteCode = nullptr;
	bool IsEdPrefix = false;
	bool IsCbPrefix = false;
};

class ZxDisUtils
{
public:
	static void GetDisassembly(DisassemblyInfo& info, string& out, uint32_t memoryAddr, LabelManager* labelManager, EmuSettings* settings);
	static ZxOpInfo GetZxOpInfo(DisassemblyInfo& info);
	static EffectiveAddressInfo GetEffectiveAddress(DisassemblyInfo& info, ZxConsole* console, ZxCpuState& state);

	static uint8_t GetOpSize(uint8_t opCode, uint32_t cpuAddress, MemoryType memType, MemoryDumper* memoryDumper);

	static bool IsJumpToSub(uint8_t opCode);
	static bool IsReturnInstruction(uint16_t opCode);
	//opCode includes the byte after a DD/FD prefix in its high byte (for JP (IX)/(IY))
	static bool IsUnconditionalJump(uint16_t opCode);
	static bool IsConditionalJump(uint8_t opCode);
	//opCode includes the byte after a DD/FD/ED prefix in its high byte
	static CdlFlags::CdlFlags GetOpFlags(uint16_t opCode, uint16_t pc, uint16_t prevPc);
	static string GetOpTemplate(uint8_t op, bool cbPrefix, bool edPrefix);
};
