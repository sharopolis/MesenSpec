#include "pch.h"
#include "ZX/ZxMemoryManager.h"
#include "ZX/ZxUla.h"
#include "ZX/ZxKeyboard.h"
#include "ZX/ZxBeeper.h"
#include "ZX/ZxAy.h"
#include "Utilities/Serializer.h"

void ZxMemoryManager::Init(ZxModel model, ZxUla* ula, ZxKeyboard* keyboard, ZxBeeper* beeper, ZxAy* ay)
{
	_ay = ay;
	_model = model;
	_ula = ula;
	_keyboard = keyboard;
	_beeper = beeper;
	_romSize = HasPaging() ? BankSize * 2 : BankSize;
	_ramSize = HasPaging() ? BankSize * 8 : BankSize * 3;
	Reset();
}

void ZxMemoryManager::Reset()
{
	memset(_ram, 0, sizeof(_ram));
	_portFe = 0;
	_port7ffd = 0;
	_earInput = false;
	UpdateMapping();
}

void ZxMemoryManager::UpdateMapping()
{
	if(!HasPaging()) {
		_slots[0] = _rom;
		_slots[1] = _ram;
		_slots[2] = _ram + BankSize;
		_slots[3] = _ram + BankSize * 2;
		_slotContended[0] = false;
		_slotContended[1] = true;
		_slotContended[2] = false;
		_slotContended[3] = false;
		_videoRamOffset = 0;
	} else {
		uint8_t bank = _port7ffd & 0x07;
		_slots[0] = _rom + ((_port7ffd & 0x10) ? BankSize : 0);
		_slots[1] = _ram + BankSize * 5;
		_slots[2] = _ram + BankSize * 2;
		_slots[3] = _ram + BankSize * bank;
		_slotContended[0] = false;
		_slotContended[1] = true;
		_slotContended[2] = false;
		_slotContended[3] = (bank & 0x01) != 0;
		_videoRamOffset = BankSize * ((_port7ffd & 0x08) ? 7 : 5);
	}

	if(_ula) {
		_ula->SetVideoRam(GetVideoRam());
	}
}

bool ZxMemoryManager::LoadRom(const vector<uint8_t>& rom)
{
	if(rom.size() != _romSize) {
		return false;
	}
	memcpy(_rom, rom.data(), _romSize);
	return true;
}

bool ZxMemoryManager::GetMappedOffset(uint16_t addr, uint32_t& offset)
{
	uint8_t* ptr = _slots[addr >> 14] + (addr & 0x3FFF);
	if(addr < 0x4000) {
		offset = (uint32_t)(ptr - _rom);
		return true;
	}
	offset = (uint32_t)(ptr - _ram);
	return false;
}

int32_t ZxMemoryManager::GetCpuAddress(bool rom, uint32_t offset)
{
	for(int slot = rom ? 0 : 1; slot < (rom ? 1 : 4); slot++) {
		uint32_t start = (uint32_t)(_slots[slot] - (rom ? _rom : _ram));
		if(offset >= start && offset < start + BankSize) {
			return (slot << 14) | (offset - start);
		}
	}
	return -1;
}

void ZxMemoryManager::SetPort7ffd(uint8_t value)
{
	if(HasPaging()) {
		_port7ffd = value;
		UpdateMapping();
	}
}

void ZxMemoryManager::DirectWrite(uint16_t addr, uint8_t value)
{
	if(addr >= 0x4000) {
		_slots[addr >> 14][addr & 0x3FFF] = value;
	}
}

uint8_t ZxMemoryManager::GetContention(uint16_t addr, uint64_t cycle, bool mreq)
{
	return IsContended(addr) ? _ula->GetContentionDelay(cycle) : 0;
}

uint8_t ZxMemoryManager::Read(uint16_t addr, uint64_t cycle, MemoryOperationType type)
{
	return _slots[addr >> 14][addr & 0x3FFF];
}

void ZxMemoryManager::Write(uint16_t addr, uint8_t value, uint64_t cycle)
{
	if(addr < 0x4000) {
		return;
	}
	uint8_t* ptr = _slots[addr >> 14] + (addr & 0x3FFF);
	if((uint32_t)(ptr - _ram) - _videoRamOffset < 0x1B00) {
		//Screen memory - draw what the beam has already displayed before changing it
		_ula->Render(cycle);
	}
	*ptr = value;
}

uint8_t ZxMemoryManager::Peek(uint16_t addr)
{
	return _slots[addr >> 14][addr & 0x3FFF];
}

uint8_t ZxMemoryManager::PeekPort(uint16_t port)
{
	if(!(port & 0x01)) {
		bool ear = _earInput || (_portFe & 0x10);
		return 0xA0 | (ear ? 0x40 : 0) | _keyboard->Read(port >> 8);
	}
	if(_ay && (port & 0xC002) == 0xC000) {
		return _ay->ReadRegister();
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

	if(_ay && (port & 0xC002) == 0xC000) {
		//AY register (0xFFFD, decoded from A15, A14 and A1)
		return _ay->ReadRegister();
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

	if(_ay && (port & 0x8002) == 0x8000) {
		//AY: 0xFFFD selects a register, 0xBFFD writes it (decoded from A15, A14 and A1)
		if(port & 0x4000) {
			_ay->SelectRegister(value);
		} else {
			_ay->WriteRegister(cycle, value);
		}
	}

	if(HasPaging() && !(port & 0x8002) && !(_port7ffd & 0x20)) {
		//128K paging: decoded from A1 and A15 only (0x7FFD is the documented address)
		if((value ^ _port7ffd) & 0x08) {
			//Screen bank change - draw what the beam has already displayed first
			_ula->Render(cycle);
		}
		_port7ffd = value;
		UpdateMapping();
	}
}

bool ZxMemoryManager::IsIrqActive(uint64_t cycle)
{
	return _ula->IsIrqActive(cycle);
}

void ZxMemoryManager::Serialize(Serializer& s)
{
	SVArray(_ram, _ramSize);
	SV(_portFe);
	SV(_earInput);
	SV(_port7ffd);
	if(!s.IsSaving()) {
		UpdateMapping();
	}
}
