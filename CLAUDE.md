# MesenSpec

Fork of Mesen 2 (GPLv3) adding a ZX Spectrum core, so the Spectrum gets Mesen's debugger, UI and portability.

## Layout
- `Core/ZX/` - Spectrum core. Self-contained for now: not yet registered as a console/CpuType in Mesen, not yet in
  `Core.vcxproj` (the Linux makefile picks it up automatically).
  - `ZxCpu` - Z80 derived from `Core/SMS/SmsCpu`, with Fuse-style timing: every memory cycle and every internal
    cycle carries its bus address so the machine can apply ULA contention; ports are 16-bit.
  - `IZxBus` - everything the CPU sees of the machine (contention, memory, ports, /INT).
  - `ZxMachine` - a complete 48K (CPU, `ZxUla`, `ZxMemoryManager`, `ZxKeyboard`, `ZxBeeper`, `ZxTape`); will be
    wrapped by a `ZxConsole` (IConsole) for Mesen.
  - `ZxUla` - frame timing (`ZxTiming` in ZxTypes.h), contention, floating bus, /INT, and the picture (352x288,
    drawn lazily per 8-pixel chunk up to the current T-state before video RAM/border changes).
  - `ZxSnapshot` - .sna/.z80 load and save (48K). `ZxTape` - .tap via a ROM trap at LD-BYTES (0x0556).
- `ZxTests/` - headless tools, no SDL/.NET needed: `make -f ZxTests/makefile all` (needs `Utilities/*.o` from the
  main makefile: `USE_GCC=true make Utilities/Serializer.o Utilities/miniz.o Utilities/PNGHelper.o Utilities/spng.o`).
  - `make -f ZxTests/makefile test ZXROM=path/to/48.rom` runs the Fuse Z80 suite and `ZxMachineTest`.
  - `ZxRun` - boots a ROM, types keys, loads snapshots/tapes, saves PNG/WAV/snapshots, reads the screen as text
    (`--text`, `--auto-scroll`). See the comment at the top of ZxRun.cpp.
  - ROMs are not in the repo (Amstrad allows distribution, but keep them out) - Fuse's `roms/48.rom` works.

## Conventions
- Timing reference is Fuse (`z80/opcodes_*.dat`); flag behaviour follows post-2018 research where it differs.
- While halted, PC points at the HALT opcode (Fuse/SZX convention); it is stepped past it when an interrupt is taken.
- Debugger/emulator hooks that need `CpuType::Zx` are marked `TODOZX`.
- Code must build with MSVC too (Windows is the main target) - avoid POSIX-only calls like strcasecmp.

## Status
- Fuse suite: 1351 pass + 5 known deviations (pre-2018 expectations for interrupted block instructions, listed in
  FuseZ80Test.cpp). Patrik Rak's z80test 1.2a: all 6 variants pass (full, doc, flags, docflags, ccf, memptr).
- 48K: boots, BASIC works, keyboard, beeper, border, floating bus, contention (memory + I/O), snapshots, TAP loading.
- Next: Mesen integration (ConsoleType/CpuType::Zx, ZxConsole, video filter, audio, input mapping, DummyZxCpu,
  debugger + UI), then 128K/+2/+3, real-time tape (TZX), AY, Kempston.
- Unverified: exact ULA display timing for screen-memory writes (multicolour effects) and the floating bus read
  T-state - check with ULA test programs once the UI runs. OTIR/OTDR MEMPTR on repeat (see TODOZX in ZxCpu).
