#include "pch.h"
#include "ZX/ZxTape.h"
#include "ZX/ZxCpu.h"
#include "ZX/ZxMemoryManager.h"

bool ZxTape::Load(const vector<uint8_t>& data, string& error)
{
	//.tap: a sequence of blocks, each preceded by its 16-bit length
	vector<vector<uint8_t>> blocks;
	size_t pos = 0;
	while(pos + 2 <= data.size()) {
		uint16_t length = data[pos] | (data[pos + 1] << 8);
		pos += 2;
		if(pos + length > data.size()) {
			error = "Invalid .tap file (truncated block)";
			return false;
		}
		blocks.emplace_back(data.begin() + pos, data.begin() + pos + length);
		pos += length;
	}

	if(blocks.empty()) {
		error = "Invalid .tap file (no blocks)";
		return false;
	}

	_blocks = std::move(blocks);
	_nextBlock = 0;
	return true;
}

void ZxTape::Eject()
{
	_blocks.clear();
	_nextBlock = 0;
}

bool ZxTape::ProcessLoadTrap(ZxCpu& cpu, ZxMemoryManager& memoryManager)
{
	if(_nextBlock >= _blocks.size()) {
		//End of tape: let the ROM wait for a signal that never comes, like a real tape deck
		return false;
	}

	//LD-BYTES has swapped AF: A' = expected flag byte, carry' set = LOAD (reset = VERIFY). IX = destination, DE = length,
	//and the return address (SA/LD-RET, or the loader's own) is on the stack.
	ZxCpuState& s = cpu.GetState();
	const vector<uint8_t>& block = _blocks[_nextBlock++];
	bool isLoad = (s.AltFlags & ZxCpuFlags::Carry) != 0;
	uint8_t expectedFlag = s.AltA;
	uint16_t addr = (s.IXH << 8) | s.IXL;
	uint16_t length = (s.D << 8) | s.E;

	bool success = false;
	if(!block.empty() && block[0] == expectedFlag) {
		uint8_t parity = block[0];
		uint32_t available = (uint32_t)block.size() - 1;
		uint32_t count = std::min<uint32_t>(length, available);
		bool verifyOk = true;
		for(uint32_t i = 0; i < count; i++) {
			uint8_t value = block[i + 1];
			parity ^= value;
			if(isLoad) {
				memoryManager.DirectWrite(addr, value);
			} else if(memoryManager.Peek(addr) != value) {
				verifyOk = false;
			}
			addr++;
		}
		length -= count;

		//The checksum byte follows the data - the block is only valid if everything XORs to 0
		if(length == 0 && available > count) {
			parity ^= block[count + 1];
			success = parity == 0 && verifyOk;
		}
	}

	s.IXH = addr >> 8;
	s.IXL = (uint8_t)addr;
	s.D = length >> 8;
	s.E = (uint8_t)length;
	if(success) {
		s.Flags |= ZxCpuFlags::Carry;
	} else {
		s.Flags &= ~ZxCpuFlags::Carry;
	}

	//Return like LD-BYTES does - normally to SA/LD-RET, which restores the border, re-enables interrupts and returns to
	//LD-BYTES' caller
	s.PC = memoryManager.Peek(s.SP) | (memoryManager.Peek(s.SP + 1) << 8);
	s.SP += 2;
	return true;
}
