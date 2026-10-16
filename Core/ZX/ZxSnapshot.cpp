#include "pch.h"
#include "ZX/ZxSnapshot.h"
#include "ZX/ZxCpu.h"
#include "ZX/ZxMemoryManager.h"
#include "ZX/ZxUla.h"

bool ZxSnapshot::IsSnapshot(const string& extension)
{
	return extension == ".sna" || extension == ".z80";
}

bool ZxSnapshot::Load(const vector<uint8_t>& data, const string& extension, ZxCpu& cpu, ZxMemoryManager& memoryManager, ZxUla& ula, string& error)
{
	if(extension == ".sna") {
		return LoadSna(data, cpu, memoryManager, ula, error);
	} else if(extension == ".z80") {
		return LoadZ80(data, cpu, memoryManager, ula, error);
	}
	error = "Unsupported snapshot format";
	return false;
}

bool ZxSnapshot::LoadSna(const vector<uint8_t>& data, ZxCpu& cpu, ZxMemoryManager& memoryManager, ZxUla& ula, string& error)
{
	//27-byte header followed by the 48K of RAM. PC is on the stack (the snapshot was taken inside an interrupt handler).
	if(data.size() != 27 + 0xC000) {
		error = data.size() > 27 + 0xC000 ? "128K .sna snapshots are not supported yet" : "Invalid .sna file";
		return false;
	}

	const uint8_t* h = data.data();
	for(uint32_t i = 0; i < 0xC000; i++) {
		memoryManager.DirectWrite(0x4000 + i, h[27 + i]);
	}

	ZxCpuState& s = cpu.GetState();
	s.I = h[0];
	s.AltL = h[1]; s.AltH = h[2];
	s.AltE = h[3]; s.AltD = h[4];
	s.AltC = h[5]; s.AltB = h[6];
	s.AltFlags = h[7]; s.AltA = h[8];
	s.L = h[9]; s.H = h[10];
	s.E = h[11]; s.D = h[12];
	s.C = h[13]; s.B = h[14];
	s.IYL = h[15]; s.IYH = h[16];
	s.IXL = h[17]; s.IXH = h[18];
	s.IFF2 = (h[19] & 0x04) != 0;
	s.IFF1 = s.IFF2;
	s.R = h[20];
	s.Flags = h[21]; s.A = h[22];
	s.SP = h[23] | (h[24] << 8);
	s.IM = h[25] & 0x03;

	//RETN
	s.PC = memoryManager.Peek(s.SP) | (memoryManager.Peek(s.SP + 1) << 8);
	s.SP += 2;

	s.Halted = false;
	s.FlagsChanged = 0;
	s.WZ = s.PC;
	ula.SetBorderColor(cpu.GetCycleCount(), h[26] & 0x07);
	return true;
}

bool ZxSnapshot::DecompressZ80(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize, bool hasEndMarker)
{
	//ED ED nn bb = nn repetitions of bb. Version 1 files end with 00 ED ED 00.
	size_t in = 0;
	size_t out = 0;
	while(in < srcSize && out < dstSize) {
		if(hasEndMarker && in + 3 < srcSize && src[in] == 0x00 && src[in + 1] == 0xED && src[in + 2] == 0xED && src[in + 3] == 0x00) {
			break;
		}
		if(in + 3 < srcSize && src[in] == 0xED && src[in + 1] == 0xED) {
			uint8_t count = src[in + 2];
			uint8_t value = src[in + 3];
			for(int i = 0; i < count && out < dstSize; i++) {
				dst[out++] = value;
			}
			in += 4;
		} else {
			dst[out++] = src[in++];
		}
	}
	return out == dstSize;
}

bool ZxSnapshot::LoadZ80(const vector<uint8_t>& data, ZxCpu& cpu, ZxMemoryManager& memoryManager, ZxUla& ula, string& error)
{
	if(data.size() < 30) {
		error = "Invalid .z80 file";
		return false;
	}

	const uint8_t* h = data.data();
	uint8_t flags1 = h[12] == 0xFF ? 1 : h[12];
	uint16_t pc = h[6] | (h[7] << 8);

	vector<uint8_t> ram(0xC000);
	if(pc != 0) {
		//Version 1: 48K only, data follows the 30-byte header
		if(flags1 & 0x20) {
			if(!DecompressZ80(h + 30, data.size() - 30, ram.data(), ram.size(), true)) {
				error = "Invalid .z80 file (compressed data is too short)";
				return false;
			}
		} else {
			if(data.size() < 30 + 0xC000) {
				error = "Invalid .z80 file";
				return false;
			}
			memcpy(ram.data(), h + 30, 0xC000);
		}
	} else {
		//Version 2/3: extended header, then 16K pages, each with a 3-byte header
		if(data.size() < 34) {
			error = "Invalid .z80 file";
			return false;
		}
		uint16_t extraLength = h[30] | (h[31] << 8);
		size_t pos = 32 + extraLength;
		if(data.size() < pos) {
			error = "Invalid .z80 file";
			return false;
		}
		pc = h[32] | (h[33] << 8);
		uint8_t hwMode = h[34];
		bool isVersion3 = extraLength >= 54;
		bool is48k = hwMode == 0 || hwMode == 1 || (isVersion3 && hwMode == 3);
		if(!is48k) {
			error = "Only 48K .z80 snapshots are supported for now";
			return false;
		}

		bool loaded[3] = {};
		while(pos + 3 <= data.size()) {
			uint16_t length = h[pos] | (h[pos + 1] << 8);
			uint8_t page = h[pos + 2];
			pos += 3;

			bool compressed = length != 0xFFFF;
			size_t srcLength = compressed ? length : 0x4000;
			if(pos + srcLength > data.size()) {
				error = "Invalid .z80 file (truncated page)";
				return false;
			}

			int slot = -1;
			switch(page) {
				case 8: slot = 0; break; //0x4000
				case 4: slot = 1; break; //0x8000
				case 5: slot = 2; break; //0xC000
			}
			if(slot >= 0) {
				uint8_t* dst = ram.data() + slot * 0x4000;
				if(compressed) {
					if(!DecompressZ80(h + pos, srcLength, dst, 0x4000, false)) {
						error = "Invalid .z80 file (bad page data)";
						return false;
					}
				} else {
					memcpy(dst, h + pos, 0x4000);
				}
				loaded[slot] = true;
			}
			pos += srcLength;
		}

		if(!loaded[0] || !loaded[1] || !loaded[2]) {
			error = "Invalid .z80 file (missing memory pages)";
			return false;
		}
	}

	for(uint32_t i = 0; i < 0xC000; i++) {
		memoryManager.DirectWrite(0x4000 + i, ram[i]);
	}

	ZxCpuState& s = cpu.GetState();
	s.A = h[0]; s.Flags = h[1];
	s.C = h[2]; s.B = h[3];
	s.L = h[4]; s.H = h[5];
	s.PC = pc;
	s.SP = h[8] | (h[9] << 8);
	s.I = h[10];
	s.R = (h[11] & 0x7F) | ((flags1 & 0x01) << 7);
	s.E = h[13]; s.D = h[14];
	s.AltC = h[15]; s.AltB = h[16];
	s.AltE = h[17]; s.AltD = h[18];
	s.AltL = h[19]; s.AltH = h[20];
	s.AltA = h[21]; s.AltFlags = h[22];
	s.IYL = h[23]; s.IYH = h[24];
	s.IXL = h[25]; s.IXH = h[26];
	s.IFF1 = h[27] != 0;
	s.IFF2 = h[28] != 0;
	s.IM = h[29] & 0x03;

	s.Halted = false;
	s.FlagsChanged = 0;
	s.WZ = s.PC;
	ula.SetBorderColor(cpu.GetCycleCount(), (flags1 >> 1) & 0x07);
	return true;
}

vector<uint8_t> ZxSnapshot::Save(const string& extension, ZxCpu& cpu, ZxMemoryManager& memoryManager, ZxUla& ula)
{
	if(extension == ".sna") {
		return SaveSna(cpu, memoryManager, ula);
	} else if(extension == ".z80") {
		return SaveZ80(cpu, memoryManager, ula);
	}
	return {};
}

vector<uint8_t> ZxSnapshot::SaveSna(ZxCpu& cpu, ZxMemoryManager& memoryManager, ZxUla& ula)
{
	ZxCpuState& s = cpu.GetState();
	vector<uint8_t> data(27 + 0xC000);
	for(uint32_t i = 0; i < 0xC000; i++) {
		data[27 + i] = memoryManager.Peek(0x4000 + i);
	}

	//The format has no PC field: push it, the loader pops it (RETN)
	uint16_t sp = s.SP - 2;
	auto pokeRam = [&](uint16_t addr, uint8_t value) {
		if(addr >= 0x4000) {
			data[27 + addr - 0x4000] = value;
		}
	};
	pokeRam(sp, (uint8_t)s.PC);
	pokeRam(sp + 1, s.PC >> 8);

	uint8_t* h = data.data();
	h[0] = s.I;
	h[1] = s.AltL; h[2] = s.AltH;
	h[3] = s.AltE; h[4] = s.AltD;
	h[5] = s.AltC; h[6] = s.AltB;
	h[7] = s.AltFlags; h[8] = s.AltA;
	h[9] = s.L; h[10] = s.H;
	h[11] = s.E; h[12] = s.D;
	h[13] = s.C; h[14] = s.B;
	h[15] = s.IYL; h[16] = s.IYH;
	h[17] = s.IXL; h[18] = s.IXH;
	h[19] = s.IFF2 ? 0x04 : 0;
	h[20] = s.R;
	h[21] = s.Flags; h[22] = s.A;
	h[23] = (uint8_t)sp; h[24] = sp >> 8;
	h[25] = s.IM;
	h[26] = ula.GetBorderColor();
	return data;
}

void ZxSnapshot::CompressZ80(const uint8_t* src, size_t size, vector<uint8_t>& out)
{
	//Runs of 5+ identical bytes (or 2+ 0xED bytes) become ED ED count value. A byte that follows a single ED is
	//always stored as-is, otherwise "ED ED" would be ambiguous.
	size_t i = 0;
	while(i < size) {
		uint8_t value = src[i];
		size_t run = 1;
		while(i + run < size && src[i + run] == value && run < 255) {
			run++;
		}

		if(run >= 5 || (value == 0xED && run >= 2)) {
			out.insert(out.end(), { 0xED, 0xED, (uint8_t)run, value });
			i += run;
		} else if(value == 0xED) {
			out.push_back(0xED);
			i++;
			if(i < size) {
				out.push_back(src[i++]);
			}
		} else {
			out.push_back(value);
			i++;
		}
	}
}

vector<uint8_t> ZxSnapshot::SaveZ80(ZxCpu& cpu, ZxMemoryManager& memoryManager, ZxUla& ula)
{
	//Version 3 (54-byte extended header), 48K, compressed pages
	ZxCpuState& s = cpu.GetState();
	vector<uint8_t> data(32 + 54, 0);
	uint8_t* h = data.data();
	h[0] = s.A; h[1] = s.Flags;
	h[2] = s.C; h[3] = s.B;
	h[4] = s.L; h[5] = s.H;
	//h[6..7] = 0 (PC is in the extended header)
	h[8] = (uint8_t)s.SP; h[9] = s.SP >> 8;
	h[10] = s.I;
	h[11] = s.R & 0x7F;
	h[12] = (s.R >> 7) | (ula.GetBorderColor() << 1);
	h[13] = s.E; h[14] = s.D;
	h[15] = s.AltC; h[16] = s.AltB;
	h[17] = s.AltE; h[18] = s.AltD;
	h[19] = s.AltL; h[20] = s.AltH;
	h[21] = s.AltA; h[22] = s.AltFlags;
	h[23] = s.IYL; h[24] = s.IYH;
	h[25] = s.IXL; h[26] = s.IXH;
	h[27] = s.IFF1 ? 1 : 0;
	h[28] = s.IFF2 ? 1 : 0;
	h[29] = s.IM & 0x03;
	h[30] = 54; h[31] = 0;
	h[32] = (uint8_t)s.PC; h[33] = s.PC >> 8;
	h[34] = 0; //48K

	//Page 8 = 0x4000, 4 = 0x8000, 5 = 0xC000
	const std::pair<uint8_t, uint16_t> pages[3] = { { 8, 0x4000 }, { 4, 0x8000 }, { 5, 0xC000 } };
	for(auto& page : pages) {
		uint8_t buffer[0x4000];
		for(uint32_t i = 0; i < 0x4000; i++) {
			buffer[i] = memoryManager.Peek(page.second + i);
		}
		vector<uint8_t> compressed;
		CompressZ80(buffer, sizeof(buffer), compressed);
		bool useCompressed = compressed.size() < 0x4000;
		uint16_t length = useCompressed ? (uint16_t)compressed.size() : 0xFFFF;
		data.push_back((uint8_t)length);
		data.push_back(length >> 8);
		data.push_back(page.first);
		if(useCompressed) {
			data.insert(data.end(), compressed.begin(), compressed.end());
		} else {
			data.insert(data.end(), buffer, buffer + sizeof(buffer));
		}
	}
	return data;
}
