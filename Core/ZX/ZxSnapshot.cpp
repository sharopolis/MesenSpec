#include "pch.h"
#include "ZX/ZxSnapshot.h"
#include "ZX/ZxMachine.h"

static constexpr uint32_t BankSize = 0x4000;
static constexpr size_t SnaHeaderSize = 27;
static constexpr size_t Sna48Size = SnaHeaderSize + 0xC000;
//128K .sna: the 48K .sna layout (with the bank paged at 0xC000), then PC, port 0x7FFD, the TR-DOS flag and the 5 other
//banks (6 if the paged bank is 2 or 5, which then appears twice)
static constexpr size_t Sna128Size = Sna48Size + 4 + 5 * BankSize;
static constexpr size_t Sna128SizeDuplicateBank = Sna128Size + BankSize;

bool ZxSnapshot::IsSnapshot(const string& extension)
{
	return extension == ".sna" || extension == ".z80";
}

bool ZxSnapshot::GetModel(const vector<uint8_t>& data, const string& extension, ZxModel& model, string& error)
{
	if(extension == ".sna") {
		if(data.size() == Sna48Size) {
			model = ZxModel::Spectrum48K;
			return true;
		} else if(data.size() == Sna128Size || data.size() == Sna128SizeDuplicateBank) {
			model = ZxModel::Spectrum128K;
			return true;
		}
		error = "Invalid .sna file";
		return false;
	}

	if(extension == ".z80") {
		if(data.size() < 30) {
			error = "Invalid .z80 file";
			return false;
		}
		if(data[6] || data[7] || data.size() < 35) {
			//Version 1 (PC in the main header): always 48K
			model = ZxModel::Spectrum48K;
			return true;
		}

		uint16_t extraLength = data[30] | (data[31] << 8);
		bool isVersion3 = extraLength >= 54;
		uint8_t hwMode = data[34];
		//Version 2: 0-1 = 48K (+ Interface 1), 3-4 = 128K (+ Interface 1). Version 3: 0, 1 and 3 (M.G.T.) = 48K,
		//4-6 = 128K (+ Interface 1/M.G.T.), 12 = +2 (the "modify hardware" flag also turns 128K into +2 and 48K into 16K -
		//both run here as the closest model)
		if(hwMode == 0 || hwMode == 1 || (isVersion3 && hwMode == 3)) {
			model = ZxModel::Spectrum48K;
			return true;
		} else if((!isVersion3 && (hwMode == 3 || hwMode == 4)) || (isVersion3 && (hwMode == 4 || hwMode == 5 || hwMode == 6 || hwMode == 12))) {
			model = ZxModel::Spectrum128K;
			return true;
		} else if(isVersion3 && (hwMode == 7 || hwMode == 8 || hwMode == 13)) {
			error = "+3/+2A .z80 snapshots are not supported yet";
		} else {
			error = "This .z80 snapshot is for an unsupported machine (hardware mode " + std::to_string(hwMode) + ")";
		}
		return false;
	}

	error = "Unsupported snapshot format";
	return false;
}

bool ZxSnapshot::Load(const vector<uint8_t>& data, const string& extension, ZxMachine& machine, string& error)
{
	ZxModel model;
	if(!GetModel(data, extension, model, error)) {
		return false;
	}
	if(model != machine.GetModel()) {
		error = model == ZxModel::Spectrum128K ? "This snapshot needs a 128K Spectrum" : "This snapshot needs a 48K Spectrum";
		return false;
	}

	if(extension == ".sna") {
		return LoadSna(data, machine, error);
	} else {
		return LoadZ80(data, machine, error);
	}
}

static void ReadSnaHeader(const uint8_t* h, ZxCpuState& s)
{
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
}

static void WriteSnaHeader(uint8_t* h, ZxCpuState& s, uint16_t sp, uint8_t border)
{
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
	h[26] = border;
}

bool ZxSnapshot::LoadSna(const vector<uint8_t>& data, ZxMachine& machine, string& error)
{
	ZxMemoryManager& memoryManager = machine.GetMemoryManager();
	ZxCpuState& s = machine.GetCpu().GetState();
	const uint8_t* h = data.data();
	ReadSnaHeader(h, s);

	if(machine.GetModel() == ZxModel::Spectrum48K) {
		//27-byte header followed by the 48K of RAM. PC is on the stack (the snapshot was taken inside an interrupt handler).
		for(uint32_t i = 0; i < 0xC000; i++) {
			memoryManager.DirectWrite(0x4000 + i, h[SnaHeaderSize + i]);
		}

		//RETN
		s.PC = memoryManager.Peek(s.SP) | (memoryManager.Peek(s.SP + 1) << 8);
		s.SP += 2;
	} else {
		const uint8_t* extra = h + Sna48Size;
		uint8_t port7ffd = extra[2];
		uint8_t pagedBank = port7ffd & 0x07;
		bool duplicateBank = pagedBank == 2 || pagedBank == 5;
		if(data.size() != (duplicateBank ? Sna128SizeDuplicateBank : Sna128Size)) {
			error = "Invalid .sna file (size doesn't match the paged bank)";
			return false;
		}

		uint8_t* ram = memoryManager.GetRam();
		const uint8_t* first48k = h + SnaHeaderSize;
		memcpy(ram + 5 * BankSize, first48k, BankSize);
		memcpy(ram + 2 * BankSize, first48k + BankSize, BankSize);
		memcpy(ram + pagedBank * BankSize, first48k + 2 * BankSize, BankSize);

		const uint8_t* src = extra + 4;
		for(uint8_t bank = 0; bank < 8; bank++) {
			if(bank != 2 && bank != 5 && bank != pagedBank) {
				memcpy(ram + bank * BankSize, src, BankSize);
				src += BankSize;
			}
		}

		memoryManager.SetPort7ffd(port7ffd);
		s.PC = extra[0] | (extra[1] << 8);
	}

	s.Halted = false;
	s.FlagsChanged = 0;
	s.WZ = s.PC;
	machine.GetUla().SetBorderColor(machine.GetCpu().GetCycleCount(), h[26] & 0x07);
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

bool ZxSnapshot::LoadZ80(const vector<uint8_t>& data, ZxMachine& machine, string& error)
{
	ZxMemoryManager& memoryManager = machine.GetMemoryManager();
	bool is128k = machine.GetModel() == ZxModel::Spectrum128K;
	const uint8_t* h = data.data();
	uint8_t flags1 = h[12] == 0xFF ? 1 : h[12];
	uint16_t pc = h[6] | (h[7] << 8);

	//The RAM banks in order (48K: 0x4000-0xFFFF)
	vector<uint8_t> ram(is128k ? 8 * BankSize : 3 * BankSize);
	uint8_t port7ffd = 0;
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
		//Version 2/3: extended header, then 16K pages, each with a 3-byte header. 48K pages: 8 = 0x4000, 4 = 0x8000,
		//5 = 0xC000. 128K pages: 3-10 = banks 0-7.
		uint16_t extraLength = h[30] | (h[31] << 8);
		size_t pos = 32 + extraLength;
		if(data.size() < pos || extraLength < 23) {
			error = "Invalid .z80 file";
			return false;
		}
		pc = h[32] | (h[33] << 8);
		port7ffd = h[35];

		uint8_t pageCount = is128k ? 8 : 3;
		vector<bool> loaded(pageCount);
		while(pos + 3 <= data.size()) {
			uint16_t length = h[pos] | (h[pos + 1] << 8);
			uint8_t page = h[pos + 2];
			pos += 3;

			bool compressed = length != 0xFFFF;
			size_t srcLength = compressed ? length : BankSize;
			if(pos + srcLength > data.size()) {
				error = "Invalid .z80 file (truncated page)";
				return false;
			}

			int index = -1;
			if(is128k) {
				if(page >= 3 && page <= 10) {
					index = page - 3;
				}
			} else {
				switch(page) {
					case 8: index = 0; break;
					case 4: index = 1; break;
					case 5: index = 2; break;
				}
			}
			if(index >= 0) {
				uint8_t* dst = ram.data() + index * BankSize;
				if(compressed) {
					if(!DecompressZ80(h + pos, srcLength, dst, BankSize, false)) {
						error = "Invalid .z80 file (bad page data)";
						return false;
					}
				} else {
					memcpy(dst, h + pos, BankSize);
				}
				loaded[index] = true;
			}
			pos += srcLength;
		}

		for(bool pageLoaded : loaded) {
			if(!pageLoaded) {
				error = "Invalid .z80 file (missing memory pages)";
				return false;
			}
		}
	}

	if(is128k) {
		memcpy(memoryManager.GetRam(), ram.data(), ram.size());
		memoryManager.SetPort7ffd(port7ffd);

		//AY: last register selected through 0xFFFD, then the 16 registers
		ZxAy& ay = machine.GetAy();
		uint64_t cycle = machine.GetCpu().GetCycleCount();
		for(uint8_t i = 0; i < 16; i++) {
			ay.SelectRegister(i);
			ay.WriteRegister(cycle, h[39 + i]);
		}
		ay.SelectRegister(h[38]);
	} else {
		for(uint32_t i = 0; i < 0xC000; i++) {
			memoryManager.DirectWrite(0x4000 + i, ram[i]);
		}
	}

	ZxCpuState& s = machine.GetCpu().GetState();
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
	machine.GetUla().SetBorderColor(machine.GetCpu().GetCycleCount(), (flags1 >> 1) & 0x07);
	return true;
}

vector<uint8_t> ZxSnapshot::Save(const string& extension, ZxMachine& machine)
{
	if(extension == ".sna") {
		return SaveSna(machine);
	} else if(extension == ".z80") {
		return SaveZ80(machine);
	}
	return {};
}

vector<uint8_t> ZxSnapshot::SaveSna(ZxMachine& machine)
{
	ZxMemoryManager& memoryManager = machine.GetMemoryManager();
	ZxCpuState& s = machine.GetCpu().GetState();
	uint8_t border = machine.GetUla().GetBorderColor();

	if(machine.GetModel() == ZxModel::Spectrum128K) {
		//PC and the paging register have their own fields
		uint8_t port7ffd = memoryManager.GetPort7ffd();
		uint8_t pagedBank = port7ffd & 0x07;
		uint8_t* ram = memoryManager.GetRam();

		vector<uint8_t> data(SnaHeaderSize);
		WriteSnaHeader(data.data(), s, s.SP, border);
		for(uint8_t bank : { (uint8_t)5, (uint8_t)2, pagedBank }) {
			data.insert(data.end(), ram + bank * BankSize, ram + (bank + 1) * BankSize);
		}
		data.insert(data.end(), { (uint8_t)s.PC, (uint8_t)(s.PC >> 8), port7ffd, 0 });
		for(uint8_t bank = 0; bank < 8; bank++) {
			if(bank != 2 && bank != 5 && bank != pagedBank) {
				data.insert(data.end(), ram + bank * BankSize, ram + (bank + 1) * BankSize);
			}
		}
		return data;
	}

	vector<uint8_t> data(Sna48Size);
	for(uint32_t i = 0; i < 0xC000; i++) {
		data[SnaHeaderSize + i] = memoryManager.Peek(0x4000 + i);
	}

	//The format has no PC field: push it, the loader pops it (RETN)
	uint16_t sp = s.SP - 2;
	auto pokeRam = [&](uint16_t addr, uint8_t value) {
		if(addr >= 0x4000) {
			data[SnaHeaderSize + addr - 0x4000] = value;
		}
	};
	pokeRam(sp, (uint8_t)s.PC);
	pokeRam(sp + 1, s.PC >> 8);

	WriteSnaHeader(data.data(), s, sp, border);
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

vector<uint8_t> ZxSnapshot::SaveZ80(ZxMachine& machine)
{
	//Version 3 (54-byte extended header), compressed pages
	ZxMemoryManager& memoryManager = machine.GetMemoryManager();
	ZxCpuState& s = machine.GetCpu().GetState();
	bool is128k = machine.GetModel() == ZxModel::Spectrum128K;

	vector<uint8_t> data(32 + 54, 0);
	uint8_t* h = data.data();
	h[0] = s.A; h[1] = s.Flags;
	h[2] = s.C; h[3] = s.B;
	h[4] = s.L; h[5] = s.H;
	//h[6..7] = 0 (PC is in the extended header)
	h[8] = (uint8_t)s.SP; h[9] = s.SP >> 8;
	h[10] = s.I;
	h[11] = s.R & 0x7F;
	h[12] = (s.R >> 7) | (machine.GetUla().GetBorderColor() << 1);
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

	//Pages: {page number, data}
	vector<std::pair<uint8_t, const uint8_t*>> pages;
	uint8_t ram48k[0xC000];
	if(is128k) {
		h[34] = 4; //128K
		h[35] = memoryManager.GetPort7ffd();
		h[37] = 0x04; //AY in use
		ZxAyState ay = machine.GetAy().GetState();
		h[38] = ay.SelectedRegister;
		memcpy(h + 39, ay.Registers, 16);
		for(uint8_t bank = 0; bank < 8; bank++) {
			pages.push_back({ (uint8_t)(bank + 3), memoryManager.GetRam() + bank * BankSize });
		}
	} else {
		h[34] = 0; //48K
		for(uint32_t i = 0; i < 0xC000; i++) {
			ram48k[i] = memoryManager.Peek(0x4000 + i);
		}
		pages.push_back({ 8, ram48k });
		pages.push_back({ 4, ram48k + BankSize });
		pages.push_back({ 5, ram48k + 2 * BankSize });
	}

	for(auto& page : pages) {
		vector<uint8_t> compressed;
		CompressZ80(page.second, BankSize, compressed);
		bool useCompressed = compressed.size() < BankSize;
		uint16_t length = useCompressed ? (uint16_t)compressed.size() : 0xFFFF;
		data.push_back((uint8_t)length);
		data.push_back(length >> 8);
		data.push_back(page.first);
		if(useCompressed) {
			data.insert(data.end(), compressed.begin(), compressed.end());
		} else {
			data.insert(data.end(), page.second, page.second + BankSize);
		}
	}
	return data;
}
