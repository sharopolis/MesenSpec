using Mesen.Debugger.ViewModels;
using Mesen.Interop;
using System.Collections.Generic;
using static Mesen.Debugger.ViewModels.RegEntry;

namespace Mesen.Debugger.RegisterViewer;

public class ZxRegisterViewer
{
	//Same order as ZxKeyboard::KeyNames in the core (rows selected by A8-A15, keys on D0-D4)
	private static readonly string[][] _keyNames = new string[][] {
		new[] { "CAPS SHIFT", "Z", "X", "C", "V" },
		new[] { "A", "S", "D", "F", "G" },
		new[] { "Q", "W", "E", "R", "T" },
		new[] { "1", "2", "3", "4", "5" },
		new[] { "0", "9", "8", "7", "6" },
		new[] { "P", "O", "I", "U", "Y" },
		new[] { "ENTER", "L", "K", "J", "H" },
		new[] { "SPACE", "SYMBOL SHIFT", "M", "N", "B" },
	};

	private static readonly string[] _colorNames = { "Black", "Blue", "Red", "Magenta", "Green", "Cyan", "Yellow", "White" };

	public static List<RegisterViewerTab> GetTabs(ref ZxState zxState)
	{
		return new List<RegisterViewerTab>() {
			GetUlaTab(ref zxState),
			GetKeyboardTab(ref zxState),
		};
	}

	private static RegisterViewerTab GetUlaTab(ref ZxState zx)
	{
		ZxUlaState ula = zx.Ula;
		ZxIoState io = zx.Io;

		List<RegEntry> entries = new List<RegEntry>() {
			new RegEntry("", "State"),
			new RegEntry("", "Cycle (H)", ula.Cycle),
			new RegEntry("", "Scanline (V)", ula.Scanline),
			new RegEntry("", "T-state in frame", ula.FrameCycle),
			new RegEntry("", "Frame Number", ula.FrameCount),

			new RegEntry("", "Port $FE (write)"),
			new RegEntry("$FE", "Last value written", io.PortFe, Format.X8),
			new RegEntry("$FE.0-2", "Border color", _colorNames[io.PortFe & 0x07], io.PortFe & 0x07),
			new RegEntry("$FE.3", "MIC output", (io.PortFe & 0x08) != 0),
			new RegEntry("$FE.4", "EAR output (speaker)", (io.PortFe & 0x10) != 0),

			new RegEntry("", "Port $FE (read)"),
			new RegEntry("$FE.6", "EAR input (tape)", io.EarInput),
		};

		if(io.HasPaging) {
			int p = io.Port7ffd;
			entries.AddRange(new List<RegEntry>() {
				new RegEntry("", "Port $7FFD (128K paging)"),
				new RegEntry("$7FFD", "Last value written", io.Port7ffd, Format.X8),
				new RegEntry("$7FFD.0-2", "RAM bank at $C000", "Bank " + (p & 0x07), p & 0x07),
				new RegEntry("$7FFD.3", "Screen", (p & 0x08) != 0 ? "Bank 7 (shadow)" : "Bank 5 (normal)", (p >> 3) & 0x01),
				new RegEntry("$7FFD.4", "ROM", (p & 0x10) != 0 ? "ROM 1 (48K BASIC)" : "ROM 0 (128K editor)", (p >> 4) & 0x01),
				new RegEntry("$7FFD.5", "Paging locked", (p & 0x20) != 0),
			});
		}

		return new RegisterViewerTab("ULA", entries, CpuType.Zx, MemoryType.ZxPort);
	}

	private static RegisterViewerTab GetKeyboardTab(ref ZxState zx)
	{
		List<RegEntry> entries = new List<RegEntry>();
		byte[] rows = zx.Io.KeyboardRows ?? new byte[8];
		for(int row = 0; row < 8; row++) {
			int port = ((~(1 << row)) & 0xFF) << 8 | 0xFE;
			entries.Add(new RegEntry($"${port:X4}", "Half-row " + _keyNames[row][0] + "..." + _keyNames[row][4], rows[row], Format.X8));
			for(int key = 0; key < 5; key++) {
				entries.Add(new RegEntry($"${port:X4}.{key}", _keyNames[row][key] + " pressed", (rows[row] & (1 << key)) == 0));
			}
		}

		return new RegisterViewerTab("Keyboard", entries, CpuType.Zx, MemoryType.ZxPort);
	}
}
