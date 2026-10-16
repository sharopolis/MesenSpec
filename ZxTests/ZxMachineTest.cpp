//Tests for the 48K machine (ULA timing, contention, interrupts, keyboard, snapshots).
//Usage: ZxMachineTest <48.rom>
#include "pch.h"
#include "ZX/ZxMachine.h"
#include "ZxTestUtils.h"

using namespace ZxTestUtils;

static int _passed = 0;
static int _failed = 0;

#define CHECK(cond, ...) \
	do { \
		if(cond) { \
			_passed++; \
		} else { \
			_failed++; \
			printf("FAILED %s:%d: %s - ", __FILE__, __LINE__, #cond); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while(0)

static unique_ptr<ZxMachine> CreateMachine(const vector<uint8_t>& rom)
{
	unique_ptr<ZxMachine> zx(new ZxMachine());
	zx->LoadRom(rom);
	return zx;
}

static void RunFrames(ZxMachine& zx, int count, KeyTyper* typer = nullptr, int startFrame = 0)
{
	for(int i = 0; i < count; i++) {
		if(typer) {
			typer->Update(zx, startFrame + i);
		}
		zx.RunFrame();
	}
}

static void TestBoot(const vector<uint8_t>& rom)
{
	unique_ptr<ZxMachine> zx = CreateMachine(rom);
	RunFrames(*zx, 150);
	CHECK(IsTextOnScreen(*zx, 23, "\x7f 1982 Sinclair Research Ltd"), "copyright message not found:\n%s", GetScreenText(*zx).c_str());

	//FRAMES system variable: incremented by the ROM's interrupt handler, once per frame
	ZxMemoryManager& mm = zx->GetMemoryManager();
	auto frames = [&]() { return mm.Peek(23672) | (mm.Peek(23673) << 8) | (mm.Peek(23674) << 16); };
	uint32_t before = frames();
	RunFrames(*zx, 50);
	CHECK(frames() - before == 50, "FRAMES advanced by %u in 50 frames", frames() - before);
}

static void TestContention(const vector<uint8_t>& rom)
{
	//Documented 48K values: contention starts at T-state 14335 with 6,5,4,3,2,1,0,0, for 128 T-states of each of the 192 lines
	unique_ptr<ZxMachine> zx = CreateMachine(rom);
	ZxUla& ula = zx->GetUla();
	uint64_t f = ula.GetFrameStartCycle();
	struct { uint32_t T; uint8_t Delay; } expected[] = {
		{ 14334, 0 }, { 14335, 6 }, { 14336, 5 }, { 14337, 4 }, { 14338, 3 }, { 14339, 2 }, { 14340, 1 }, { 14341, 0 }, { 14342, 0 },
		{ 14343, 6 }, { 14335 + 120, 6 }, { 14335 + 127, 0 }, { 14335 + 128, 0 }, { 14335 + 223, 0 }, { 14335 + 224, 6 },
		{ 14335 + 191 * 224, 6 }, { 14335 + 191 * 224 + 127, 0 }, { 14335 + 192 * 224, 0 }, { 69887, 0 },
	};
	for(auto& e : expected) {
		uint8_t delay = ula.GetContentionDelay(f + e.T);
		CHECK(delay == e.Delay, "T=%u: delay %d, expected %d", e.T, delay, e.Delay);
	}

	//Contention applies to 0x4000-0x7FFF only
	ZxMemoryManager& mm = zx->GetMemoryManager();
	CHECK(mm.GetContention(0x3FFF, f + 14335, true) == 0, "ROM is not contended");
	CHECK(mm.GetContention(0x4000, f + 14335, true) == 6, "0x4000 is contended");
	CHECK(mm.GetContention(0x7FFF, f + 14335, false) == 6, "0x7FFF is contended (no MREQ cycles too)");
	CHECK(mm.GetContention(0x8000, f + 14335, true) == 0, "0x8000 is not contended");
}

static void TestInstructionTiming(const vector<uint8_t>& rom)
{
	unique_ptr<ZxMachine> zx = CreateMachine(rom);
	ZxCpuState& s = zx->GetCpu().GetState();
	ZxMemoryManager& mm = zx->GetMemoryManager();
	uint64_t f = zx->GetUla().GetFrameStartCycle();

	auto run = [&](uint16_t pc, uint32_t t) {
		s.PC = pc;
		s.IFF1 = false;
		s.Halted = false;
		s.CycleCount = f + t;
		zx->GetCpu().Exec();
		return (uint32_t)(s.CycleCount - f - t);
	};

	//NOP: a single 4 T-state opcode fetch, delayed by contention when fetched from 0x4000-0x7FFF
	mm.DirectWrite(0x8000, 0x00);
	mm.DirectWrite(0x6000, 0x00);
	CHECK(run(0x8000, 14335) == 4, "NOP in uncontended RAM");
	CHECK(run(0x6000, 14335) == 10, "NOP at T=14335 in contended RAM: %u", run(0x6000, 14335));
	CHECK(run(0x6000, 14337) == 8, "NOP at T=14337 in contended RAM: %u", run(0x6000, 14337));
	CHECK(run(0x6000, 14341) == 4, "NOP at T=14341 in contended RAM: %u", run(0x6000, 14341));
	CHECK(run(0x6000, 1000) == 4, "NOP in the top border: %u", run(0x6000, 1000));

	//LD A,(HL) from uncontended code: M1 (4) + read (3), only the read is contended
	mm.DirectWrite(0x8000, 0x7E);
	s.H = 0x40;
	s.L = 0x00;
	CHECK(run(0x8000, 14331) == 7 + 6, "LD A,(HL) with the read at T=14335: %u", run(0x8000, 14331));

	//IN A,(0xFE) with A=0xFF (port 0xFFFE) from uncontended code: the ULA port is contended during the second part of the I/O cycle (N:1 C:3)
	mm.DirectWrite(0x8000, 0xDB);
	mm.DirectWrite(0x8001, 0xFE);
	s.A = 0xFF;
	//Opcode fetch + operand read = 7, then the I/O cycle starts at T=14335 - 7: N:1 at 14335, C:3 at 14336 (delay 5)
	CHECK(run(0x8000, 14335 - 7) == 7 + 1 + 5 + 3, "IN A,(FE) with A=FF at T=14335: %u", run(0x8000, 14335 - 7));
	//Port 0x40FF (contended high byte, odd): C:1 C:1 C:1 C:1, starting at 14335 = 6+1, then 14342: 0+1, 14343: 6+1, 14350: 0+1
	s.A = 0x40;
	mm.DirectWrite(0x8001, 0xFF);
	CHECK(run(0x8000, 14335 - 7) == 7 + 7 + 1 + 7 + 1, "IN A,(40FF) at T=14335: %u", run(0x8000, 14335 - 7));
}

static void TestFloatingBus(const vector<uint8_t>& rom)
{
	//Documented 48K sequence: at T=14338 the ULA reads 0x4000, then 0x5800, 0x4001, 0x5801, then the bus is idle (0xFF) for 4 T-states
	unique_ptr<ZxMachine> zx = CreateMachine(rom);
	ZxMemoryManager& mm = zx->GetMemoryManager();
	ZxUla& ula = zx->GetUla();
	uint64_t f = ula.GetFrameStartCycle();
	mm.DirectWrite(0x4000, 0x11);
	mm.DirectWrite(0x5800, 0x22);
	mm.DirectWrite(0x4001, 0x33);
	mm.DirectWrite(0x5801, 0x44);
	mm.DirectWrite(0x4002, 0x55);
	mm.DirectWrite(0x4100, 0x66); //Second pixel line of the first character row
	uint8_t expected[] = { 0x11, 0x22, 0x33, 0x44, 0xFF, 0xFF, 0xFF, 0xFF, 0x55 };
	for(int i = 0; i < 9; i++) {
		uint8_t value = ula.GetFloatingBusValue(f + 14338 + i);
		CHECK(value == expected[i], "T=%d: %02X, expected %02X", 14338 + i, value, expected[i]);
	}
	CHECK(ula.GetFloatingBusValue(f + 14337) == 0xFF, "before the first fetch");
	CHECK(ula.GetFloatingBusValue(f + 14338 + 224) == 0x66, "second line");
	CHECK(ula.GetFloatingBusValue(f + 14338 + 128) == 0xFF, "right border");
	CHECK(mm.ReadPort(0x40FF, f + 14338) == 0x11, "port 0xFF reads the floating bus");
}

static void TestKeyboard(const vector<uint8_t>& rom)
{
	unique_ptr<ZxMachine> zx = CreateMachine(rom);
	ZxMemoryManager& mm = zx->GetMemoryManager();
	ZxKeyboard& kb = zx->GetKeyboard();
	CHECK((mm.ReadPort(0x00FE, 0) & 0x1F) == 0x1F, "no keys pressed");
	kb.SetKeyState("Q", true);
	CHECK((mm.ReadPort(0xFBFE, 0) & 0x1F) == 0x1E, "Q in row 0xFB");
	CHECK((mm.ReadPort(0xFDFE, 0) & 0x1F) == 0x1F, "Q not in row 0xFD");
	CHECK((mm.ReadPort(0x00FE, 0) & 0x1F) == 0x1E, "all rows selected");
	kb.SetKeyState("SPACE", true);
	CHECK((mm.ReadPort(0x7FFE, 0) & 0x1F) == 0x1E, "SPACE in row 0x7F");
	CHECK((mm.ReadPort(0x7AFE, 0) & 0x1F) == 0x1E, "SPACE and Q with rows 0x7F and 0xFB selected");
}

static void TestSnapshots(const vector<uint8_t>& rom)
{
	//Enter a program, save a snapshot, load it in a new machine and run the program there
	unique_ptr<ZxMachine> zx = CreateMachine(rom);
	KeyTyper typer;
	typer.Type(150, "10b2{SYMBOL+Z}p{SYMBOL+P}hello world{SYMBOL+P}|");
	RunFrames(*zx, 350, &typer);

	for(string ext : { ".sna", ".z80" }) {
		vector<uint8_t> data = zx->SaveSnapshot(ext);
		CHECK(!data.empty(), "%s: save failed", ext.c_str());

		unique_ptr<ZxMachine> copy = CreateMachine(rom);
		RunFrames(*copy, 100);
		string error;
		CHECK(copy->LoadSnapshot(data, ext, error), "%s: %s", ext.c_str(), error.c_str());

		ZxCpuState& a = zx->GetCpu().GetState();
		ZxCpuState& b = copy->GetCpu().GetState();
		bool regsMatch = a.PC == b.PC && a.SP == b.SP && a.A == b.A && a.Flags == b.Flags && a.B == b.B && a.C == b.C && a.D == b.D &&
			a.E == b.E && a.H == b.H && a.L == b.L && a.IXH == b.IXH && a.IXL == b.IXL && a.IYH == b.IYH && a.IYL == b.IYL &&
			a.AltA == b.AltA && a.AltFlags == b.AltFlags && a.AltB == b.AltB && a.AltC == b.AltC && a.AltD == b.AltD && a.AltE == b.AltE &&
			a.AltH == b.AltH && a.AltL == b.AltL && a.I == b.I && a.R == b.R && a.IM == b.IM && a.IFF2 == b.IFF2 &&
			(ext == ".sna" || a.IFF1 == b.IFF1);
		CHECK(regsMatch, "%s: registers differ after loading (PC %04X/%04X SP %04X/%04X)", ext.c_str(), a.PC, b.PC, a.SP, b.SP);

		int ramDiffs = 0;
		for(uint32_t addr = 0x4000; addr < 0x10000; addr++) {
			//.sna stores PC on the stack, below SP
			bool snaStack = ext == ".sna" && (addr == (uint16_t)(a.SP - 1) || addr == (uint16_t)(a.SP - 2));
			if(!snaStack && zx->GetMemoryManager().Peek(addr) != copy->GetMemoryManager().Peek(addr)) {
				ramDiffs++;
			}
		}
		CHECK(ramDiffs == 0, "%s: %d bytes of RAM differ after loading", ext.c_str(), ramDiffs);
		CHECK(copy->GetUla().GetBorderColor() == zx->GetUla().GetBorderColor(), "%s: border color", ext.c_str());

		KeyTyper run;
		run.Type(0, "r|");
		RunFrames(*copy, 100, &run);
		CHECK(IsTextOnScreen(*copy, 0, "hello world"), "%s: program output not found:\n%s", ext.c_str(), GetScreenText(*copy).c_str());
		CHECK(copy->GetUla().GetBorderColor() == 2, "%s: BORDER 2 did not run", ext.c_str());
	}
}

int main(int argc, char** argv)
{
	if(argc < 2) {
		fprintf(stderr, "Usage: %s <48.rom>\n", argv[0]);
		return 2;
	}
	vector<uint8_t> rom;
	if(!ReadFile(argv[1], rom) || rom.size() != 0x4000) {
		fprintf(stderr, "Could not load ROM: %s\n", argv[1]);
		return 2;
	}

	TestBoot(rom);
	TestContention(rom);
	TestInstructionTiming(rom);
	TestFloatingBus(rom);
	TestKeyboard(rom);
	TestSnapshots(rom);

	printf("%d passed, %d failed\n", _passed, _failed);
	return _failed ? 1 : 0;
}
