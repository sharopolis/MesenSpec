#pragma once
#include "pch.h"
#include "ZX/IZxBus.h"
#include "ZX/ZxTypes.h"
#include "Utilities/ISerializable.h"

class ZxUla;
class ZxKeyboard;
class ZxBeeper;
class ZxAy;

//Memory map and I/O. The 64K address space is made of four 16K slots:
//- 48K: ROM, then 48K of RAM (0x4000-0x7FFF is shared with the ULA, and contended). The RAM array is linear.
//- 128K: ROM 0 (128K editor) or ROM 1 (48K BASIC), RAM bank 5, bank 2 and any bank at 0xC000, selected through port
//  0x7FFD. The RAM array holds the 8 banks in order. Odd banks are contended, the ULA displays bank 5 or 7.
//The ULA answers every even port. The 128K's AY is at 0xFFFD (register select/read) and 0xBFFD (write).
class ZxMemoryManager final : public IZxBus, public ISerializable
{
private:
	static constexpr uint32_t BankSize = 0x4000;

	ZxUla* _ula = nullptr;
	ZxKeyboard* _keyboard = nullptr;
	ZxBeeper* _beeper = nullptr;
	ZxAy* _ay = nullptr;

	ZxModel _model = ZxModel::Spectrum48K;
	uint32_t _romSize = BankSize;
	uint32_t _ramSize = BankSize * 3;

	uint8_t _rom[BankSize * 2] = {};
	uint8_t _ram[BankSize * 8] = {};

	uint8_t* _slots[4] = {};
	bool _slotContended[4] = {};

	//Offset of the displayed screen in _ram
	uint32_t _videoRamOffset = 0;

	//Last value written to port 0xFE
	uint8_t _portFe = 0;

	//Last value written to port 0x7FFD (128K paging: bits 0-2 RAM bank at 0xC000, bit 3 screen in bank 7,
	//bit 4 ROM 1, bit 5 locks paging until the next reset)
	uint8_t _port7ffd = 0;

	//Level of the EAR input (tape signal)
	bool _earInput = false;

	__forceinline bool IsContended(uint16_t addr) { return _slotContended[addr >> 14]; }

	//Updates the memory map from the paging register. Must be called after Render() if the screen changes.
	void UpdateMapping();

public:
	//ay: the 128K's sound chip (nullptr on the 48K)
	void Init(ZxModel model, ZxUla* ula, ZxKeyboard* keyboard, ZxBeeper* beeper, ZxAy* ay);
	void Reset();

	//16K for the 48K, 32K (ROM 0 then ROM 1) for the 128K
	bool LoadRom(const vector<uint8_t>& rom);

	ZxModel GetModel() { return _model; }
	bool HasPaging() { return _model != ZxModel::Spectrum48K; }

	uint8_t* GetRom() { return _rom; }
	uint32_t GetRomSize() { return _romSize; }
	uint8_t* GetRam() { return _ram; }
	uint32_t GetRamSize() { return _ramSize; }
	uint8_t* GetVideoRam() { return _ram + _videoRamOffset; }

	//Where a CPU address is currently mapped: true and the offset in ROM (GetRom()), or false and the offset in RAM
	bool GetMappedOffset(uint16_t addr, uint32_t& offset);
	//CPU address at which a ROM/RAM offset is currently visible, or -1 if it isn't paged in
	int32_t GetCpuAddress(bool rom, uint32_t offset);

	//The 48K BASIC ROM, where the tape routines are, is paged in
	bool IsBasicRomPaged() { return !HasPaging() || (_port7ffd & 0x10); }

	void SetEarInput(bool level) { _earInput = level; }
	bool GetEarInput() { return _earInput; }
	uint8_t GetPortFe() { return _portFe; }
	uint8_t GetPort7ffd() { return _port7ffd; }

	//Sets the paging register without the lock or timing (for snapshots)
	void SetPort7ffd(uint8_t value);

	//Writes from loaders (snapshots, tape traps) - bypass timing, but keep the picture up to date
	void DirectWrite(uint16_t addr, uint8_t value);

	uint8_t GetContention(uint16_t addr, uint64_t cycle, bool mreq) override;
	uint8_t Read(uint16_t addr, uint64_t cycle, MemoryOperationType type) override;
	void Write(uint16_t addr, uint8_t value, uint64_t cycle) override;
	uint8_t Peek(uint16_t addr) override;

	//Side-effect free port read, for the debugger's memory viewer (unattached ports read 0xFF)
	uint8_t PeekPort(uint16_t port);

	uint8_t ContendPortPre(uint16_t port, uint64_t cycle) override;
	uint8_t ContendPortPost(uint16_t port, uint64_t cycle) override;
	uint8_t ReadPort(uint16_t port, uint64_t cycle) override;
	void WritePort(uint16_t port, uint8_t value, uint64_t cycle) override;

	bool IsIrqActive(uint64_t cycle) override;

	void Serialize(Serializer& s) override;
};
