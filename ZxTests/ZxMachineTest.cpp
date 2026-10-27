//Tests for the 48K and 128K machines (ULA timing, contention, paging, interrupts, keyboard, snapshots).
//Usage: ZxMachineTest <48.rom> [<128-0.rom> <128-1.rom> | <128.rom>]
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

static void TestTapeTrap(const vector<uint8_t>& rom)
{
	//A custom loader: its own flag (0x88) in A', its own return address, and a jump into LD-BYTES past its entry point
	//(like Target Renegade's). The trap at LD-START must load the block and return to that address.
	unique_ptr<ZxMachine> zx = CreateMachine(rom);
	RunFrames(*zx, 100);
	vector<uint8_t> tap = { 5, 0, 0x88, 0x11, 0x22, 0x33, (uint8_t)(0x88 ^ 0x11 ^ 0x22 ^ 0x33) };
	string error;
	CHECK(zx->LoadTape(tap, error), "tape: %s", error.c_str());

	ZxMemoryManager& mm = zx->GetMemoryManager();
	const uint8_t code[] = {
		0xDD, 0x21, 0x00, 0x90, //LD IX,0x9000
		0x11, 0x03, 0x00,       //LD DE,3
		0x3E, 0x88,             //LD A,0x88
		0x37,                   //SCF (LOAD)
		0x08,                   //EX AF,AF'
		0x21, 0x00, 0x81,       //LD HL,0x8100
		0xE5,                   //PUSH HL
		0xC3, 0x62, 0x05,       //JP 0x0562
	};
	for(size_t i = 0; i < sizeof(code); i++) {
		mm.DirectWrite(0x8000 + (uint16_t)i, code[i]);
	}
	mm.DirectWrite(0x8100, 0x18); //JR $
	mm.DirectWrite(0x8101, 0xFE);
	ZxCpuState& s = zx->GetCpu().GetState();
	s.PC = 0x8000;
	s.SP = 0xF000;
	RunFrames(*zx, 1);
	CHECK(s.PC == 0x8100, "returned to the loader's address: PC=%04X", s.PC);
	CHECK(mm.Peek(0x9000) == 0x11 && mm.Peek(0x9001) == 0x22 && mm.Peek(0x9002) == 0x33, "block loaded: %02X %02X %02X", mm.Peek(0x9000), mm.Peek(0x9001), mm.Peek(0x9002));
	CHECK(zx->GetTape().GetNextBlock() == 1, "tape advanced");
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

static unique_ptr<ZxMachine> Create128(const vector<uint8_t>& rom)
{
	unique_ptr<ZxMachine> zx(new ZxMachine(ZxModel::Spectrum128K));
	zx->LoadRom(rom);
	return zx;
}

static void Test128Boot(const vector<uint8_t>& rom)
{
	unique_ptr<ZxMachine> zx = Create128(rom);
	RunFrames(*zx, 150);
	string text = GetScreenText(*zx);
	CHECK(text.find("Tape Loader") != string::npos && text.find("48 BASIC") != string::npos, "128K menu not found:\n%s", text.c_str());
	CHECK(IsTextOnScreen(*zx, 23, "\x7f 1986 Sinclair Research Ltd"), "128K copyright message not found:\n%s", text.c_str());
}

static void Test128Paging(const vector<uint8_t>& rom)
{
	unique_ptr<ZxMachine> zx = Create128(rom);
	ZxMemoryManager& mm = zx->GetMemoryManager();
	uint8_t* ram = mm.GetRam();
	uint64_t f = zx->GetUla().GetFrameStartCycle();
	auto out = [&](uint16_t port, uint8_t value) { mm.WritePort(port, value, f + 1000); };

	//Each bank can be paged at 0xC000 - bank 5 is also at 0x4000 and bank 2 at 0x8000
	for(int bank = 0; bank < 8; bank++) {
		out(0x7FFD, bank);
		mm.DirectWrite(0xC000, 0x10 + bank);
	}
	for(int bank = 0; bank < 8; bank++) {
		CHECK(ram[bank * 0x4000] == 0x10 + bank, "bank %d: %02X", bank, ram[bank * 0x4000]);
	}
	CHECK(mm.Peek(0x4000) == 0x15, "bank 5 at 0x4000: %02X", mm.Peek(0x4000));
	CHECK(mm.Peek(0x8000) == 0x12, "bank 2 at 0x8000: %02X", mm.Peek(0x8000));

	//ROM 0 (128K editor) or ROM 1 (48K BASIC)
	out(0x7FFD, 0x00);
	CHECK(mm.Peek(0x0001) == rom[0x0001] && !mm.IsBasicRomPaged(), "ROM 0 paged in");
	out(0x7FFD, 0x10);
	CHECK(mm.Peek(0x0001) == rom[0x4001] && mm.IsBasicRomPaged(), "ROM 1 paged in");

	//Screen in bank 5 or 7
	out(0x7FFD, 0x08);
	CHECK(mm.GetVideoRam() == ram + 7 * 0x4000, "shadow screen (bank 7)");
	out(0x7FFD, 0x00);
	CHECK(mm.GetVideoRam() == ram + 5 * 0x4000, "normal screen (bank 5)");

	//Decoded from A15 = 0 and A1 = 0 only
	out(0x7FFF, 0x01);
	CHECK(mm.GetPort7ffd() == 0x00, "0x7FFF is not the paging port");
	out(0xFFFD, 0x01);
	CHECK(mm.GetPort7ffd() == 0x00, "0xFFFD (AY) is not the paging port");
	out(0x3FFD, 0x01);
	CHECK(mm.GetPort7ffd() == 0x01, "0x3FFD is decoded as the paging port");

	//Bit 5 locks paging until the next reset
	out(0x7FFD, 0x23);
	out(0x7FFD, 0x04);
	CHECK(mm.GetPort7ffd() == 0x23 && mm.Peek(0xC000) == 0x13, "paging locked: %02X", mm.GetPort7ffd());
	zx->Reset();
	CHECK(mm.GetPort7ffd() == 0x00, "reset unlocks paging");

	//Odd banks are contended, also at 0xC000 (and for I/O with a high byte in 0xC000-0xFFFF)
	uint32_t c = zx->GetUla().GetTiming().ContentionStart;
	out(0x7FFD, 0x01);
	CHECK(mm.GetContention(0xC000, f + c, true) == 6, "bank 1 at 0xC000 is contended");
	CHECK(mm.ContendPortPre(0xC0FF, f + c) == 7, "I/O with the high byte in contended bank 1");
	out(0x7FFD, 0x02);
	CHECK(mm.GetContention(0xC000, f + c, true) == 0, "bank 2 at 0xC000 is not contended");
	CHECK(mm.GetContention(0x4000, f + c, true) == 6, "bank 5 at 0x4000 is contended");
	CHECK(mm.GetContention(0x8000, f + c, true) == 0, "bank 2 at 0x8000 is not contended");
}

static void Test128Ula(const vector<uint8_t>& rom)
{
	//Documented 128K values: 228 T-states per line, 311 lines, contention from 14361, floating bus first bitmap read at 14364
	unique_ptr<ZxMachine> zx = Create128(rom);
	ZxUla& ula = zx->GetUla();
	ZxMemoryManager& mm = zx->GetMemoryManager();
	uint64_t f = ula.GetFrameStartCycle();
	CHECK(ula.GetTiming().FrameLength == 70908 && ula.GetScanlineCount() == 311, "128K frame: %u T-states", ula.GetTiming().FrameLength);

	struct { uint32_t T; uint8_t Delay; } expected[] = {
		{ 14360, 0 }, { 14361, 6 }, { 14362, 5 }, { 14367, 0 }, { 14369, 6 }, { 14361 + 127, 0 }, { 14361 + 228, 6 },
		{ 14361 + 191 * 228, 6 }, { 14361 + 192 * 228, 0 },
	};
	for(auto& e : expected) {
		uint8_t delay = ula.GetContentionDelay(f + e.T);
		CHECK(delay == e.Delay, "128K T=%u: delay %d, expected %d", e.T, delay, e.Delay);
	}

	mm.DirectWrite(0x4000, 0xA5);
	mm.DirectWrite(0x5800, 0x5A);
	CHECK(mm.ReadPort(0x40FF, f + 14363) == 0xFF, "floating bus before the paper");
	CHECK(mm.ReadPort(0x40FF, f + 14364) == 0xA5, "floating bus bitmap at 14364: %02X", mm.ReadPort(0x40FF, f + 14364));
	CHECK(mm.ReadPort(0x40FF, f + 14365) == 0x5A, "floating bus attribute at 14365: %02X", mm.ReadPort(0x40FF, f + 14365));

	RunFrames(*zx, 1);
	CHECK(ula.GetFrameStartCycle() == f + 70908, "frame length");

	//The picture comes from the shadow screen when bit 3 is set: pixel (0,0) of bank 7 in red ink
	uint8_t* bank7 = mm.GetRam() + 7 * 0x4000;
	bank7[0] = 0x80;
	bank7[0x1800] = 0x02;
	mm.SetPort7ffd(0x08);
	ula.EndFrame();
	uint16_t* fb = ula.GetFrameBuffer();
	uint32_t paper = ZxConstants::BorderHeight * ZxConstants::ScreenWidth + ZxConstants::BorderWidth;
	CHECK(fb[paper] == 2 && fb[paper + 1] == 0, "shadow screen drawn: %d %d", fb[paper], fb[paper + 1]);
}

static void TestAy(const vector<uint8_t>& rom128, const vector<uint8_t>& rom48)
{
	unique_ptr<ZxMachine> zx = Create128(rom128);
	ZxMemoryManager& mm = zx->GetMemoryManager();
	uint64_t f = zx->GetUla().GetFrameStartCycle();
	auto setReg = [&](uint8_t reg, uint8_t value) {
		mm.WritePort(0xFFFD, reg, f);
		mm.WritePort(0xBFFD, value, f);
	};
	auto getReg = [&](uint8_t reg) {
		mm.WritePort(0xFFFD, reg, f);
		return mm.ReadPort(0xFFFD, f);
	};

	//Registers read back with their unused bits cleared
	setReg(0, 0xFF);
	setReg(1, 0xFF);
	setReg(6, 0xFF);
	setReg(8, 0xFF);
	setReg(13, 0xFF);
	CHECK(getReg(0) == 0xFF && getReg(1) == 0x0F && getReg(6) == 0x1F && getReg(8) == 0x1F && getReg(13) == 0x0F,
		"register masks: %02X %02X %02X %02X %02X", getReg(0), getReg(1), getReg(6), getReg(8), getReg(13));

	//Port decoding: writes to 0xBFFD with A14 set select instead, and 0xFFFD is decoded on A15/A14/A1 only
	setReg(2, 0x12);
	mm.WritePort(0xC0FD, 2, f);
	CHECK(mm.ReadPort(0xC0FD, f) == 0x12, "0xC0FD reads the AY");
	mm.WritePort(0xFFFF, 0x34, f);
	CHECK(getReg(2) == 0x12, "0xFFFF (A1 set) is not the AY");

	//Register 14 (I/O port A) reads 0xFF while in input mode
	setReg(7, 0x00);
	setReg(14, 0x55);
	CHECK(getReg(14) == 0xFF, "port A in input mode: %02X", getReg(14));
	setReg(7, 0x40);
	CHECK(getReg(14) == 0x55, "port A in output mode: %02X", getReg(14));

	//Tone frequency: AY clock / (16 * period) - measured over 1 second of channel A alone
	ZxAy& ay = zx->GetAy();
	ay.Reset();
	ay.Init(zx->GetUla().GetTiming().CpuClockRate);
	setReg(0, 100);
	setReg(1, 0);
	setReg(7, 0x3E);
	setReg(8, 0x0F);
	uint32_t frameLength = zx->GetUla().GetTiming().FrameLength;
	int crossings = 0;
	int16_t prev = 0;
	uint64_t frameEnd = 0;
	for(int i = 0; i < 55; i++) {
		frameEnd += frameLength;
		ay.EndFrame(frameEnd, frameLength);
		if(i < 5) {
			//Let the output filter settle
			continue;
		}
		for(int16_t sample : ay.GetSamples()) {
			//The output is high-pass filtered (centred on 0)
			if(prev < 0 && sample >= 0) {
				crossings++;
			}
			prev = sample;
		}
	}
	double expected = (zx->GetUla().GetTiming().CpuClockRate / 2.0) / (16 * 100) * (50.0 * frameLength / zx->GetUla().GetTiming().CpuClockRate);
	CHECK(std::abs(crossings - expected) <= 2, "tone period 100: %d cycles in 50 frames, expected %.1f", crossings, expected);

	//Envelope shapes: volume after a few ramps (period 1 = 32 T-states per step, 512 per ramp)
	struct { uint8_t Shape; uint8_t AfterRamp; uint8_t MidSecondRamp; } shapes[] = {
		{ 0x00, 0, 0 }, { 0x04, 0, 0 },   //\___ and /___: back to 0
		{ 0x08, 15, 7 }, { 0x0C, 0, 8 },  //\\\\ and ////: the ramp starts over
		{ 0x09, 0, 0 }, { 0x0B, 15, 15 }, //\___ and \¯¯¯
		{ 0x0D, 15, 15 }, { 0x0F, 0, 0 }, ///¯¯¯ and /___
		{ 0x0A, 0, 8 }, { 0x0E, 15, 7 },  //\/\/ and /\/\: the direction flips
	};
	for(auto& e : shapes) {
		uint64_t start = frameEnd;
		ay.Update(start);
		mm.WritePort(0xFFFD, 11, start);
		mm.WritePort(0xBFFD, 1, start);
		mm.WritePort(0xFFFD, 12, start);
		mm.WritePort(0xBFFD, 0, start);
		mm.WritePort(0xFFFD, 13, start);
		mm.WritePort(0xBFFD, e.Shape, start);
		//16 steps of 2 ticks of 16 T-states per ramp
		ay.Update(start + 512 + 16);
		uint8_t afterRamp = ay.GetState().EnvelopeVolume;
		ay.Update(start + 512 + 256 + 16);
		uint8_t midSecondRamp = ay.GetState().EnvelopeVolume;
		CHECK(afterRamp == e.AfterRamp && midSecondRamp == e.MidSecondRamp, "envelope shape %X: %d then %d, expected %d then %d",
			e.Shape, afterRamp, midSecondRamp, e.AfterRamp, e.MidSecondRamp);
		frameEnd = start + 2000;
	}

	//No AY on the 48K: its ports are unattached
	unique_ptr<ZxMachine> zx48 = CreateMachine(rom48);
	CHECK(!zx48->HasAy() && zx48->GetMemoryManager().ReadPort(0xFFFD, 0) == 0xFF, "48K has no AY");
}

int main(int argc, char** argv)
{
	if(argc < 2) {
		fprintf(stderr, "Usage: %s <48.rom> [<128-0.rom> <128-1.rom> | <128.rom>]\n", argv[0]);
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
	TestTapeTrap(rom);

	//128K tests, if its ROM is given (one 32K file or the two 16K halves)
	vector<uint8_t> rom128;
	for(int i = 2; i < argc; i++) {
		vector<uint8_t> data;
		if(!ReadFile(argv[i], data)) {
			fprintf(stderr, "Could not load ROM: %s\n", argv[i]);
			return 2;
		}
		rom128.insert(rom128.end(), data.begin(), data.end());
	}
	if(rom128.size() == 0x8000) {
		Test128Boot(rom128);
		Test128Paging(rom128);
		Test128Ula(rom128);
		TestAy(rom128, rom);
	} else if(argc > 2) {
		fprintf(stderr, "The 128K ROM must be 32K in total\n");
		return 2;
	} else {
		printf("No 128K ROM given - skipping the 128K tests\n");
	}

	printf("%d passed, %d failed\n", _passed, _failed);
	return _failed ? 1 : 0;
}
