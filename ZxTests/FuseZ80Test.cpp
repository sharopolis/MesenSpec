//Runs the Fuse Z80 core test suite (z80/tests/tests.in + tests.expected from the Fuse emulator) against ZxCpu.
//The bus below reproduces Fuse's coretest.c: no actual contention, but every memory/port/contention event is
//logged with the T-state it occurs on, so both the results and the exact bus timing of each instruction are checked.
//
//Usage: FuseZ80Test <tests.in> <tests.expected> [-v] [test name prefix...]
#include "pch.h"
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <sstream>
#include <map>
#include "ZX/ZxCpu.h"
#include "ZX/IZxBus.h"

using std::string;
using std::vector;

static string Format(const char* fmt, ...)
{
	char buf[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	return buf;
}

class FuseTestBus final : public IZxBus
{
public:
	uint8_t Memory[0x10000] = {};
	string Log;

	uint8_t GetContention(uint16_t addr, uint64_t cycle, bool mreq) override
	{
		Log += Format("%5d MC %04x\n", (int)cycle, addr);
		return 0;
	}

	uint8_t Read(uint16_t addr, uint64_t cycle, MemoryOperationType type) override
	{
		Log += Format("%5d MR %04x %02x\n", (int)cycle, addr, Memory[addr]);
		return Memory[addr];
	}

	void Write(uint16_t addr, uint8_t value, uint64_t cycle) override
	{
		Log += Format("%5d MW %04x %02x\n", (int)cycle, addr, value);
		Memory[addr] = value;
	}

	uint8_t Peek(uint16_t addr) override { return Memory[addr]; }

	uint8_t ContendPortPre(uint16_t port, uint64_t cycle) override
	{
		if((port & 0xC000) == 0x4000) {
			Log += Format("%5d PC %04x\n", (int)cycle, port);
		}
		return 1;
	}

	uint8_t ContendPortPost(uint16_t port, uint64_t cycle) override
	{
		if(port & 0x01) {
			if((port & 0xC000) == 0x4000) {
				for(int i = 0; i < 3; i++) {
					Log += Format("%5d PC %04x\n", (int)(cycle + i), port);
				}
			}
		} else {
			Log += Format("%5d PC %04x\n", (int)cycle, port);
		}
		return 3;
	}

	uint8_t ReadPort(uint16_t port, uint64_t cycle) override
	{
		uint8_t value = port >> 8;
		Log += Format("%5d PR %04x %02x\n", (int)cycle, port, value);
		return value;
	}

	void WritePort(uint16_t port, uint8_t value, uint64_t cycle) override
	{
		Log += Format("%5d PW %04x %02x\n", (int)cycle, port, value);
	}

	bool IsIrqActive(uint64_t cycle) override { return false; }
};

struct TestCase
{
	string Name;
	uint16_t Regs[13] = {}; //AF BC DE HL AF' BC' DE' HL' IX IY SP PC MEMPTR
	unsigned I = 0, R = 0, Iff1 = 0, Iff2 = 0, Im = 0, Halted = 0, EndCycle = 0;
	vector<std::pair<uint16_t, vector<uint8_t>>> MemoryBlocks;
};

static vector<TestCase> ParseInput(const string& path)
{
	vector<TestCase> tests;
	std::ifstream in(path);
	string line;
	while(std::getline(in, line)) {
		if(line.empty()) {
			continue;
		}
		TestCase t;
		t.Name = line;
		for(int i = 0; i < 13; i++) {
			unsigned v;
			in >> std::hex >> v;
			t.Regs[i] = (uint16_t)v;
		}
		in >> std::hex >> t.I >> t.R >> std::dec >> t.Iff1 >> t.Iff2 >> t.Im >> t.Halted >> t.EndCycle;
		while(true) {
			int addr;
			in >> std::hex >> addr;
			if(addr < 0) {
				break;
			}
			vector<uint8_t> bytes;
			while(true) {
				int b;
				in >> std::hex >> b;
				if(b < 0) {
					break;
				}
				bytes.push_back((uint8_t)b);
			}
			t.MemoryBlocks.push_back({ (uint16_t)addr, bytes });
		}
		std::getline(in, line);
		tests.push_back(t);
	}
	return tests;
}

static std::map<string, string> ParseExpected(const string& path)
{
	std::map<string, string> result;
	std::ifstream in(path);
	std::stringstream ss;
	ss << in.rdbuf();
	string all = ss.str();

	size_t pos = 0;
	while(pos < all.size()) {
		size_t end = all.find("\n\n", pos);
		if(end == string::npos) {
			end = all.size();
		}
		string block = all.substr(pos, end - pos);
		while(!block.empty() && block[0] == '\n') {
			block.erase(0, 1);
		}
		if(!block.empty()) {
			result[block.substr(0, block.find('\n'))] = block + "\n";
		}
		pos = end + 2;
	}
	return result;
}

static string RunTest(const TestCase& t)
{
	FuseTestBus bus;
	for(int i = 0; i < 0x10000; i += 4) {
		bus.Memory[i] = 0xDE;
		bus.Memory[i + 1] = 0xAD;
		bus.Memory[i + 2] = 0xBE;
		bus.Memory[i + 3] = 0xEF;
	}
	for(auto& blk : t.MemoryBlocks) {
		for(size_t i = 0; i < blk.second.size(); i++) {
			bus.Memory[(blk.first + i) & 0xFFFF] = blk.second[i];
		}
	}
	vector<uint8_t> initial(bus.Memory, bus.Memory + 0x10000);

	ZxCpu cpu;
	cpu.Init(&bus);
	ZxCpuState& s = cpu.GetState();
	auto set16 = [](uint8_t& hi, uint8_t& lo, uint16_t v) { hi = v >> 8; lo = (uint8_t)v; };
	set16(s.A, s.Flags, t.Regs[0]);
	set16(s.B, s.C, t.Regs[1]);
	set16(s.D, s.E, t.Regs[2]);
	set16(s.H, s.L, t.Regs[3]);
	set16(s.AltA, s.AltFlags, t.Regs[4]);
	set16(s.AltB, s.AltC, t.Regs[5]);
	set16(s.AltD, s.AltE, t.Regs[6]);
	set16(s.AltH, s.AltL, t.Regs[7]);
	set16(s.IXH, s.IXL, t.Regs[8]);
	set16(s.IYH, s.IYL, t.Regs[9]);
	s.SP = t.Regs[10];
	s.PC = t.Regs[11];
	s.WZ = t.Regs[12];
	s.I = t.I;
	s.R = t.R;
	s.IFF1 = t.Iff1;
	s.IFF2 = t.Iff2;
	s.IM = t.Im;
	s.Halted = t.Halted;

	while(s.CycleCount < t.EndCycle) {
		cpu.Exec();
	}

	string out = t.Name + "\n" + bus.Log;
	auto r16 = [](uint8_t hi, uint8_t lo) { return (unsigned)((hi << 8) | lo); };
	out += Format("%04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x\n",
		r16(s.A, s.Flags), r16(s.B, s.C), r16(s.D, s.E), r16(s.H, s.L),
		r16(s.AltA, s.AltFlags), r16(s.AltB, s.AltC), r16(s.AltD, s.AltE), r16(s.AltH, s.AltL),
		r16(s.IXH, s.IXL), r16(s.IYH, s.IYL), s.SP, s.PC, s.WZ);
	out += Format("%02x %02x %d %d %d %d %d\n", s.I, s.R, s.IFF1, s.IFF2, s.IM, s.Halted, (int)s.CycleCount);

	for(size_t i = 0; i < 0x10000; i++) {
		if(bus.Memory[i] == initial[i]) {
			continue;
		}
		out += Format("%04x ", (unsigned)i);
		while(i < 0x10000 && bus.Memory[i] != initial[i]) {
			out += Format("%02x ", bus.Memory[i++]);
		}
		out += "-1\n";
	}
	return out;
}

//Splits a test's output into events, the two register lines and the memory dump
struct Sections
{
	string Events;
	vector<string> RegFields;
	string Memory;
};

static Sections Split(const string& text)
{
	Sections sec;
	std::istringstream in(text);
	string line;
	std::getline(in, line); //name
	vector<string> lines;
	while(std::getline(in, line)) {
		lines.push_back(line);
	}
	size_t i = 0;
	while(i < lines.size() && !lines[i].empty() && lines[i][0] == ' ') {
		sec.Events += lines[i++] + "\n";
	}
	for(int k = 0; k < 2 && i < lines.size(); k++, i++) {
		std::istringstream f(lines[i]);
		string field;
		while(f >> field) {
			sec.RegFields.push_back(field);
		}
	}
	while(i < lines.size()) {
		sec.Memory += lines[i++] + "\n";
	}
	return sec;
}

static const char* RegNames[] = { "AF", "BC", "DE", "HL", "AF'", "BC'", "DE'", "HL'", "IX", "IY", "SP", "PC", "MEMPTR", "I", "R", "IFF1", "IFF2", "IM", "halted", "tstates" };

int main(int argc, char** argv)
{
	if(argc < 3) {
		fprintf(stderr, "Usage: %s <tests.in> <tests.expected> [-v] [test name prefix...]\n", argv[0]);
		return 2;
	}

	bool verbose = false;
	vector<string> filters;
	for(int i = 3; i < argc; i++) {
		if(string(argv[i]) == "-v") {
			verbose = true;
		} else {
			filters.push_back(argv[i]);
		}
	}

	vector<TestCase> tests = ParseInput(argv[1]);
	std::map<string, string> expected = ParseExpected(argv[2]);

	int passed = 0, failed = 0;
	std::map<string, vector<string>> failuresByKind;

	for(TestCase& t : tests) {
		if(!filters.empty()) {
			bool match = false;
			for(string& f : filters) {
				match |= t.Name.compare(0, f.size(), f) == 0;
			}
			if(!match) {
				continue;
			}
		}

		string actual = RunTest(t);
		auto it = expected.find(t.Name);
		if(it == expected.end()) {
			printf("%s: no expected result\n", t.Name.c_str());
			failed++;
			continue;
		}
		if(actual == it->second) {
			passed++;
			continue;
		}

		failed++;
		Sections a = Split(actual);
		Sections e = Split(it->second);
		vector<string> kinds;
		if(a.Events != e.Events) {
			kinds.push_back("events");
		}
		for(size_t i = 0; i < e.RegFields.size() && i < a.RegFields.size(); i++) {
			if(a.RegFields[i] == e.RegFields[i]) {
				continue;
			}
			if(i == 0) {
				//Distinguish documented flags from the undocumented F3/F5 bits
				unsigned av = std::stoul(a.RegFields[i], nullptr, 16), ev = std::stoul(e.RegFields[i], nullptr, 16);
				unsigned diff = av ^ ev;
				if((diff & 0xFF00) != 0) {
					kinds.push_back("A");
				}
				if(diff & 0xD7) {
					kinds.push_back("F");
				}
				if(diff & 0x28) {
					kinds.push_back("F3/F5");
				}
			} else {
				kinds.push_back(RegNames[i]);
			}
		}
		if(a.Memory != e.Memory) {
			kinds.push_back("memory");
		}

		string kindStr;
		for(string& k : kinds) {
			kindStr += (kindStr.empty() ? "" : ",") + k;
		}
		failuresByKind[kindStr].push_back(t.Name);

		if(verbose) {
			printf("==== %s [%s]\n---- expected\n%s---- actual\n%s\n", t.Name.c_str(), kindStr.c_str(), it->second.c_str(), actual.c_str());
		}
	}

	printf("%d passed, %d failed\n", passed, failed);
	for(auto& kv : failuresByKind) {
		printf("  [%s] %d:", kv.first.c_str(), (int)kv.second.size());
		for(size_t i = 0; i < kv.second.size() && i < 24; i++) {
			printf(" %s", kv.second[i].c_str());
		}
		printf(kv.second.size() > 24 ? " ...\n" : "\n");
	}
	return failed ? 1 : 0;
}
