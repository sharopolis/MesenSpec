#pragma once
#include "pch.h"
#include "ZX/IZxBus.h"
#include "ZX/ZxTypes.h"
#include "Utilities/ISerializable.h"

class ZxUla;
class ZxKeyboard;
class ZxBeeper;

//48K memory map and I/O: 16K ROM at 0x0000, 48K RAM at 0x4000 (0x4000-0x7FFF is shared with the ULA, and
//contended). The ULA answers every even port.
class ZxMemoryManager final : public IZxBus, public ISerializable
{
private:
	ZxUla* _ula = nullptr;
	ZxKeyboard* _keyboard = nullptr;
	ZxBeeper* _beeper = nullptr;

	uint8_t _rom[0x4000] = {};
	uint8_t _ram[0xC000] = {};

	//Last value written to port 0xFE
	uint8_t _portFe = 0;

	//Level of the EAR input (tape signal)
	bool _earInput = false;

	__forceinline bool IsContended(uint16_t addr) { return (addr & 0xC000) == 0x4000; }

public:
	void Init(ZxUla* ula, ZxKeyboard* keyboard, ZxBeeper* beeper);
	void Reset();

	bool LoadRom(const vector<uint8_t>& rom);

	uint8_t* GetRom() { return _rom; }
	uint8_t* GetRam() { return _ram; }
	uint8_t* GetVideoRam() { return _ram; }

	void SetEarInput(bool level) { _earInput = level; }

	//Writes from loaders (snapshots, tape traps) - bypass timing, but keep the picture up to date
	void DirectWrite(uint16_t addr, uint8_t value);

	uint8_t GetContention(uint16_t addr, uint64_t cycle, bool mreq) override;
	uint8_t Read(uint16_t addr, uint64_t cycle, MemoryOperationType type) override;
	void Write(uint16_t addr, uint8_t value, uint64_t cycle) override;
	uint8_t Peek(uint16_t addr) override;

	uint8_t ContendPortPre(uint16_t port, uint64_t cycle) override;
	uint8_t ContendPortPost(uint16_t port, uint64_t cycle) override;
	uint8_t ReadPort(uint16_t port, uint64_t cycle) override;
	void WritePort(uint16_t port, uint8_t value, uint64_t cycle) override;

	bool IsIrqActive(uint64_t cycle) override;

	void Serialize(Serializer& s) override;
};
