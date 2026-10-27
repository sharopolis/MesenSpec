# MesenSpec

Fork of Mesen 2 (GPLv3) adding a ZX Spectrum core, so the Spectrum gets Mesen's debugger, UI and portability.

## Layout
- `Core/ZX/` - Spectrum core, registered in Mesen as ConsoleType/CpuType::Zx (files are listed in `Core.vcxproj`;
  the Linux makefile picks them up automatically).
  - `ZxConsole` (IConsole) wraps `ZxMachine` for Mesen: video (`ZxDefaultVideoFilter`), audio (beeper -> SoundMixer),
    input (`ZxControlManager` + `Input/ZxKeyboardDevice`, keys mapped in `ZxConfig.Keyboard`), ROM via
    `FirmwareHelper::LoadZxRom` (48.rom in Mesen's Firmware folder), auto-types LOAD "" for tapes.
  - `ZxCpu` - Z80 derived from `Core/SMS/SmsCpu`, with Fuse-style timing: every memory cycle and every internal
    cycle carries its bus address so the machine can apply ULA contention; ports are 16-bit.
  - `IZxBus` - everything the CPU sees of the machine (contention, memory, ports, /INT).
  - `ZxMachine` - a complete 48K (CPU, `ZxUla`, `ZxMemoryManager`, `ZxKeyboard`, `ZxBeeper`, `ZxTape`), independent
    of the Mesen emulator so the headless tests can use it directly.
  - `ZxUla` - frame timing (`ZxTiming` in ZxTypes.h), contention, floating bus, /INT, and the picture (352x288,
    drawn lazily per 8-pixel chunk up to the current T-state before video RAM/border changes).
  - `ZxSnapshot` - .sna/.z80 load and save (48K). `ZxTape` - .tap via a ROM trap at LD-BYTES (0x0556).
- `ZxTests/` - headless tools, no SDL/.NET needed (built with g++, independently of the main build):
  - `make -f ZxTests/makefile test` runs the Fuse Z80 suite and `ZxMachineTest` (uses ZxTests/roms/48.rom, or ZXROM=path).
  - `ZxRun` - boots a ROM, types keys, loads snapshots/tapes, saves PNG/WAV/snapshots, reads the screen as text
    (`--text`, `--auto-scroll`). See the comment at the top of ZxRun.cpp.
  - ROMs are not committed (ZxTests/roms is git-ignored; Amstrad allows distribution, but keep them out) - Fuse's
    `roms/48.rom` works.

## Building
- `make -j16` (needs `PATH=$HOME/.dotnet:$PATH`; run Mesen with `DOTNET_ROOT=$HOME/.dotnet`).
- Mesen's makefile does NOT track header dependencies: after changing a shared header (enums, settings structs),
  run `make clean` first, or stale objects will use old layouts.
- UI enums (UI/Interop/*.cs, InputConfig.cs) mirror the core's by value - add new values at the same position.
- Portable test copy: a `settings.json` next to the Mesen executable makes it keep config/firmware in that folder.

## Conventions
- Timing reference is Fuse (`z80/opcodes_*.dat`); flag behaviour follows post-2018 research where it differs.
- While halted, PC points at the HALT opcode (Fuse/SZX convention); it is stepped past it when an interrupt is taken.
- Debugger/emulator hooks that need `CpuType::Zx` are marked `TODOZX`.
- Code must build with MSVC too (Windows is the main target) - avoid POSIX-only calls like strcasecmp.

## Status
- Fuse suite: 1351 pass + 5 known deviations (pre-2018 expectations for interrupted block instructions, listed in
  FuseZ80Test.cpp). Patrik Rak's z80test 1.2a: all 6 variants pass (full, doc, flags, docflags, ccf, memptr).
- 48K: boots, BASIC works, keyboard, beeper, border, floating bus, contention (memory + I/O), snapshots, TAP loading.
- Mesen integration stage 1: .sna/.z80/.tap load and run through the real Mesen core and test runner (checked with
  screenshots via MesenCore.so); GUI keyboard/sound/picture still need hands-on testing.
- Stage 2 debugger (done, GUI-tested: debugger, breakpoints, stepping, memory/event/register viewers, trace logger;
  also smoke-tested headless via `Mesen --testRunner script.lua game.tap` - Lua
  stops with `emu.stop(code)`): Core/ZX/Debugger (ZxDebugger, ZxDisUtils, ZxTraceLogger, ZxEventManager, ZxPpuTools,
  DummyZxCpu), IZxDebugHooks (ZxConsole forwards CPU activity to Mesen; the tape trap reports LD-BYTES as an
  instruction first), UI: ZxStatusView, ZxRegisterViewer (ULA/port $FE, keyboard), ZxEventViewerConfig. Tile/sprite/
  palette/tilemap viewers are hidden for Zx (HasPpuViewers).
- Settings tab (Options > ZX Spectrum, ZxConfigView): model, beeper volume, keyboard mapping (Setup opens the custom-key
  list). GUI-tested.
- Next: 128K/+2/+3, real-time tape (TZX), AY, Kempston.
- Windows build not yet verified with MSVC.
- Unverified: exact ULA display timing for screen-memory writes (multicolour effects) and the floating bus read
  T-state - check with ULA test programs once the UI runs. OTIR/OTDR MEMPTR on repeat (see TODOZX in ZxCpu).
