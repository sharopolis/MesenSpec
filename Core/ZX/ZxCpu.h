#if (defined(DUMMYCPU) && !defined(__DUMMYZxCpu__H)) || (!defined(DUMMYCPU) && !defined(__ZxCpu__H))
	#ifdef DUMMYCPU
		#define __DUMMYZxCpu__H
	#else
		#define __ZxCpu__H
	#endif

	#include "pch.h"
	#include "ZX/ZxTypes.h"
	#include "Shared/MemoryOperationType.h"
	#include "Shared/MemoryType.h"
	#include "Utilities/ISerializable.h"

class IZxBus;
class IZxDebugHooks;
class ZxCpuParityTable;

class ZxCpu final : public ISerializable
{
private:
	static ZxCpuParityTable _parity;

	class Register16
	{
		uint8_t* _high;
		uint8_t* _low;

	public:
		Register16(uint8_t* high, uint8_t* low) : _high(high), _low(low) {}

		void Write(uint16_t value)
		{
			*_high = (uint8_t)(value >> 8);
			*_low = (uint8_t)value;
		}

		uint16_t Read() { return (*_high << 8) | *_low; }
		void Inc() { Write(Read() + 1); }
		void Dec() { Write(Read() - 1); }
		operator uint16_t() { return Read(); }
	};

	IZxBus* _bus = nullptr;
	IZxDebugHooks* _hooks = nullptr;

	int32_t _cbAddress = -1;
	ZxCpuState _state = {};

	Register16 _regAF = Register16(&_state.A, &_state.Flags);
	Register16 _regBC = Register16(&_state.B, &_state.C);
	Register16 _regDE = Register16(&_state.D, &_state.E);
	Register16 _regHL = Register16(&_state.H, &_state.L);
	Register16 _regIX = Register16(&_state.IXH, &_state.IXL);
	Register16 _regIY = Register16(&_state.IYH, &_state.IYL);

	template<uint8_t prefix>
	void ExecOpCode(uint8_t opCode);

	//Timing follows Fuse's model: every memory cycle is contended at the T-state it starts on, using the
	//address placed on the bus, and internal cycles that leave an address on the bus are contended per T-state.
	__forceinline void Tick(uint8_t cycles) { _state.CycleCount += cycles; }
	__forceinline void Contend(uint16_t addr, bool mreq);
	__forceinline void InternalCycles(uint16_t addr, uint8_t count);
	__forceinline uint16_t GetIR() { return (_state.I << 8) | _state.R; }

	__forceinline uint8_t ReadOpCode();
	__forceinline uint8_t ReadNextOpCode();
	__forceinline uint8_t ReadCode();
	__forceinline uint16_t ReadCodeWord();
	__forceinline void SkipCode();

	__forceinline uint8_t Read(uint16_t addr);

	__forceinline void Write(uint16_t addr, uint8_t value);

	__forceinline uint8_t ReadPort(uint16_t port);
	__forceinline void WritePort(uint16_t port, uint8_t value);

	template<uint8_t mask>
	void SetStandardFlags(uint8_t value);

	bool CheckFlag(uint8_t flag);
	void SetFlag(uint8_t flag);
	void ClearFlag(uint8_t flag);
	void SetFlagState(uint8_t flag, bool state);

	void PushByte(uint8_t value);
	void PushWord(uint16_t value);
	uint16_t PopWord();

	void LD(uint8_t& dst, uint8_t value);
	void LD_IR(uint8_t& dst, uint8_t value);
	void LD(uint16_t& dst, uint16_t value);
	void LD(Register16& dst, uint16_t value);
	void LD_Indirect(uint16_t dst, uint8_t value);
	void LDA_Address(uint16_t addr);
	void LD_Indirect_A(uint16_t dst);
	void LD_IndirectImm(uint16_t dst);
	void LD_Indirect16(uint16_t dst, uint16_t value);
	void LDReg_Indirect16(uint16_t& dst, uint16_t addr);
	void LDReg_Indirect16(Register16& dst, uint16_t addr);

	void INC(uint8_t& dst);
	void INC(Register16& dst);
	void INC_SP();
	void INC_Indirect(uint16_t addr);
	void DEC(uint8_t& dst);
	void DEC(Register16& dst);
	void DEC_Indirect(uint16_t addr);
	void DEC_SP();

	void ADD(uint8_t value);
	void ADD(Register16& reg, uint16_t value);
	void ADC(uint8_t value);
	void ADC16(uint16_t value);
	void SUB(uint8_t value);

	void SBC(uint8_t value);
	void SBC16(uint16_t value);

	void AND(uint8_t value);
	void OR(uint8_t value);
	void XOR(uint8_t value);

	void UpdateLogicalOpFlags(uint8_t value);

	void CP(uint8_t value);

	void NOP();
	void HALT();

	void CPL();

	void RL(uint8_t& dst);
	void RL_Indirect(uint16_t addr);
	void RLC(uint8_t& dst);
	void RLC_Indirect(uint16_t addr);
	void RR(uint8_t& dst);
	void RR_Indirect(uint16_t addr);
	void RRC(uint8_t& dst);
	void RRC_Indirect(uint16_t addr);
	void RRA();
	void RRCA();
	void RLCA();
	void RLA();
	void SRL(uint8_t& dst);
	void SRL_Indirect(uint16_t addr);
	void SRA(uint8_t& dst);
	void SRA_Indirect(uint16_t addr);
	void SLA(uint8_t& dst);
	void SLA_Indirect(uint16_t addr);
	void SLL(uint8_t& dst);
	void SLL_Indirect(uint16_t dst);

	template<MemoryOperationType type>
	uint8_t ReadMemory(uint16_t addr);

	template<uint8_t bit>
	void BIT(uint8_t src);

	template<uint8_t bit>
	void BIT_Indirect(Register16& src);

	template<uint8_t bit>
	void RES(uint8_t& dst);

	template<uint8_t bit>
	void RES_Indirect(uint16_t addr);

	template<uint8_t bit>
	void SET(uint8_t& dst);

	template<uint8_t bit>
	void SET_Indirect(uint16_t addr);

	void DAA();

	void JP(uint16_t dstAddr);
	void JP(bool condition, uint16_t dstAddr);
	void JR();
	void JR(bool condition);

	void CALL(uint16_t dstAddr);
	void CALL(bool condition, uint16_t dstAddr);
	void RET();
	void RET(bool condition);
	void RETI();
	void RST(uint8_t value);

	void POP(Register16& reg);
	void PUSH(Register16& reg);
	void POP_AF();

	void SCF();
	void CCF();

	void IM(uint8_t mode);
	void EI();
	void DI();

	template<uint8_t prefix>
	void PREFIX_CB();

	uint8_t GetCbValue(uint8_t dst);
	void SetCbValue(uint8_t& dst, uint8_t val);

	void PREFIX_ED();

	void EXX();
	void ExchangeAf();
	void ExchangeSp(Register16& reg);
	void ExchangeDeHl();
	void DJNZ();
	void OUT(uint8_t src, uint16_t port);
	void OUT_Imm(uint8_t port);
	void IN(uint8_t& dst, uint16_t port);
	uint8_t IN(uint16_t port);
	void IN_Imm(uint8_t port);

	void NEG();

	void RRD();
	void RLD();

	void UpdateBlockRepeatFlags();

	template<bool forInc, bool repeat>
	void BlockLD();
	template<bool forInc, bool repeat>
	void BlockCP();
	template<bool forInc, bool repeat>
	void BlockOUT();
	template<bool forInc, bool repeat>
	void BlockIN();

	void SetInOutFlags(uint8_t value, uint16_t k);
	void UpdateInOutRepeatFlags(uint8_t value);

	void IncrementR();

	void ProcessNmi();
	void ProcessIrq();
	void EndHalt();

public:
	void Init(IZxBus* bus);
	void SetDebugHooks(IZxDebugHooks* hooks) { _hooks = hooks; }
	IZxDebugHooks* GetDebugHooks() { return _hooks; }
	void Reset();

	ZxCpuState& GetState();

	uint64_t GetCycleCount() { return _state.CycleCount; }

	void SetNmiLevel(bool nmiLevel);

	void Exec();

	void Serialize(Serializer& s) override;

	#ifdef DUMMYCPU
private:
	uint32_t _memOpCounter = 0;
	MemoryOperationInfo _memOperations[10] = {};

public:
	void SetDummyState(ZxCpuState& state);
	uint32_t GetOperationCount();
	void LogMemoryOperation(uint32_t addr, uint8_t value, MemoryOperationType type, MemoryType memType);
	MemoryOperationInfo GetOperationInfo(uint32_t index);
	#endif
};
#endif
