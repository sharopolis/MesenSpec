# MesenSpec

Fork of Mesen 2 (GPLv3) adding a ZX Spectrum core, so the Spectrum gets Mesen's debugger, UI and portability.

## Layout
- `Core/ZX/` - Spectrum core. Self-contained for now: not yet registered as a console/CpuType in Mesen.
  - `ZxCpu` - Z80 derived from `Core/SMS/SmsCpu`, with Fuse-style timing: every memory cycle and every internal
    cycle carries its bus address so the machine can apply ULA contention; ports are 16-bit.
  - `IZxBus` - everything the CPU sees of the machine (contention, memory, ports, /INT). The 48K/128K/+3 machines
    and the test harness each implement it.
- `ZxTests/` - headless tests, no SDL/.NET needed. `make -f ZxTests/makefile test` (needs `Utilities/*.o`, built
  by the main makefile, e.g. `USE_GCC=true make -j16 Utilities/Serializer.o Utilities/miniz.o`).
  - `fuse/` - Fuse's Z80 core tests: checks registers, memory and the T-state of every bus/contention event.

## Conventions
- Timing reference is Fuse (`z80/opcodes_*.dat`); flag behaviour follows post-2018 research where it differs.
- While halted, PC points at the HALT opcode (Fuse/SZX convention); it is stepped past it when an interrupt is taken.
- Debugger/emulator hooks that need `CpuType::Zx` are marked `TODOZX`.

## Status
- Fuse suite: 1352/1356. The 4 "failures" (edb2_1, edb3_1, edbb_1, edb9_2) are expected: Fuse expects the
  pre-2018 flags for repeating INIR/OTIR/OTDR/CPDR; ZxCpu implements the newer model (to be confirmed with z80test).
- Next: 48K machine (ULA + contention tables, keyboard, beeper, SNA/Z80 loading), then Mesen integration
  (CpuType/ConsoleType::Zx, DummyZxCpu, debugger, UI), then 128K/+3, tape, AY.
