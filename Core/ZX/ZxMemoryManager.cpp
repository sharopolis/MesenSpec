#include "pch.h"
#include "ZX/ZxMemoryManager.h"
#include "ZX/ZxUla.h"
#include "ZX/ZxKeyboard.h"
#include "ZX/ZxBeeper.h"
#include "Utilities/Serializer.h"

void ZxMemoryManager::Init(ZxUla* ula, ZxKeyboard* keyboard, ZxBeeper* beeper)
{
	_ula = ula;
	_keyboard = keyboard;
	_beeper = beeper;
	Reset();
}

void ZxMemoryManager::Reset()
{
	memset(_ram, 0, sizeof(_ram));
	_portFe = 0;
	_earInput = false;
}

bool ZxMemoryManager::LoadRom(const vector<uint8_t>& rom)
{
	if(rom.size() != sizeof(_rom)) {
		return false;
	}
	memcpy(_rom, rom.data(), sizeof(_rom));
	return true;
}

void ZxMemoryManager::DirectWrite(uint16_t addr, uint8_t value)
{
	if(addr >= 0x4000) {
		_ram[addr - 0x4000] = value;
	}
}

uint8_t ZxMemoryManager::GetContention(uint16_t addr, uint64_t cycle, bool mreq)
{
	return IsContended(addr) ? _ula->GetContentionDelay(cycle) : 0;
}

uint8_t ZxMemoryManager::Read(uint16_t addr, uint64_t cycle, MemoryOperationType type)
{
	return addr < 0x4000 ? _rom[addr] : _ram[addr - 0x4000];
}

void ZxMemoryManager::Write(uint16_t addr, uint8_t value, uint64_t cycle)
{
	if(addr < 0x4000) {
		return;
	}
	if(addr < 0x5B00) {
		//Screen memory - draw what the beam has already displayed before changing it
		_ula->Render(cycle);
	}
	_ram[addr - 0x4000] = value;
}

uint8_t ZxMemoryManager::Peek(uint16_t addr)
{
	return addr < 0x4000 ? _rom[addr] : _ram[addr - 0x4000];
}

uint8_t ZxMemoryManager::PeekPort(uint16_t port)
{
	if(!(port & 0x01)) {
		bool ear = _earInput || (_portFe & 0x10);
		return 0xA0 | (ear ? 0x40 : 0) | _keyboard->Read(port >> 8);
	}
	return 0xFF;
}

uint8_t ZxMemoryManager::ContendPortPre(uint16_t port, uint64_t cycle)
{
	//The high byte of the port is on the address bus - it is contended like a memory address would be
	return (IsContended(port) ? _ula->GetContentionDelay(cycle) : 0) + 1;
}

uint8_t ZxMemoryManager::ContendPortPost(uint16_t port, uint64_t cycle)
{
	if(!(port & 0x01)) {
		//ULA port: contended for the rest of the I/O cycle
		return _ula->GetContentionDelay(cycle) + 3;
	}

	if(IsContended(port)) {
		//Not a ULA port, but the address looks contended: contended on each of the remaining T-states
		uint64_t start = cycle;
		for(int i = 0; i < 3; i++) {
			cycle += _ula->GetContentionDelay(cycle) + 1;
		}
		return (uint8_t)(cycle - start);
	}

	return 3;
}

uint8_t ZxMemoryManager::ReadPort(uint16_t port, uint64_t cycle)
{
	if(!(port & 0x01)) {
		//Bits 0-4: keyboard, bit 6: EAR input, bits 5 and 7 are always set.
		//On an Issue 3 board with no tape signal, bit 6 follows the EAR output bit
		return PeekPort(port);
	}

	return _ula->GetFloatingBusValue(cycle);
}

void ZxMemoryManager::WritePort(uint16_t port, uint8_t value, uint64_t cycle)
{
	if(!(port & 0x01)) {
		_portFe = value;
		_ula->SetBorderColor(cycle, value & 0x07);
		_beeper->WritePort(cycle, value);
	}
}

bool ZxMemoryManager::IsIrqActive(uint64_t cycle)
{
	return _ula->IsIrqActive(cycle);
}

void ZxMemoryManager::Serialize(Serializer& s)
{
	SVArray(_ram, sizeof(_ram));
	SV(_portFe);
	SV(_earInput);
}
