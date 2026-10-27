using System;
using System.Runtime.InteropServices;

namespace Mesen.Interop;

//Same layout as ZxCpuState in Core/ZX/ZxTypes.h
public struct ZxCpuState : BaseState
{
	public UInt64 CycleCount;
	public UInt16 PC;
	public UInt16 SP;

	public byte A;
	public byte Flags;

	public byte B;
	public byte C;
	public byte D;
	public byte E;

	public byte H;
	public byte L;

	public byte IXL;
	public byte IXH;

	public byte IYL;
	public byte IYH;

	public byte I;
	public byte R;

	public byte AltA;
	public byte AltFlags;
	public byte AltB;
	public byte AltC;
	public byte AltD;
	public byte AltE;
	public byte AltH;
	public byte AltL;

	[MarshalAs(UnmanagedType.I1)] public bool NmiLevel;
	[MarshalAs(UnmanagedType.I1)] public bool NmiPending;
	[MarshalAs(UnmanagedType.I1)] public bool Halted;

	[MarshalAs(UnmanagedType.I1)] public bool IFF1;
	[MarshalAs(UnmanagedType.I1)] public bool IFF2;
	public byte IM;

	public byte FlagsChanged;
	public UInt16 WZ;

	[MarshalAs(UnmanagedType.I1)] public bool Iff2Read;
}

[Flags]
public enum ZxCpuFlags : byte
{
	Carry = 0x01,
	AddSub = 0x02,
	Parity = 0x04,
	F3 = 0x08,
	HalfCarry = 0x10,
	F5 = 0x20,
	Zero = 0x40,
	Sign = 0x80,
}

//Same layout as ZxUlaState in Core/ZX/ZxTypes.h
public struct ZxUlaState : BaseState
{
	public UInt32 FrameCount;
	public UInt32 FrameCycle;
	public UInt16 Scanline;
	public UInt16 Cycle;
	public byte BorderColor;
}

//Same layout as ZxIoState in Core/ZX/ZxTypes.h
public struct ZxIoState : BaseState
{
	public byte PortFe;
	[MarshalAs(UnmanagedType.I1)] public bool EarInput;
	[MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)] public byte[] KeyboardRows;
	public byte Port7ffd;
	[MarshalAs(UnmanagedType.I1)] public bool HasPaging;
}

//Same layout as ZxAyState in Core/ZX/ZxTypes.h
public struct ZxAyState : BaseState
{
	[MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)] public byte[] Registers;
	public byte SelectedRegister;
	public byte EnvelopeVolume;
	[MarshalAs(UnmanagedType.I1)] public bool Present;
}

public struct ZxState : BaseState
{
	public ZxCpuState Cpu;
	public ZxUlaState Ula;
	public ZxIoState Io;
	public ZxAyState Ay;
}
