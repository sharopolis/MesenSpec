#include "pch.h"
#include "Debugger/ExpressionEvaluator.h"
#include "ZX/Debugger/ZxDebugger.h"
#include "ZX/ZxTypes.h"

unordered_map<string, int64_t>& ExpressionEvaluator::GetZxTokens()
{
	static unordered_map<string, int64_t> supportedTokens = {
		{ "a", EvalValues::RegA },
		{ "b", EvalValues::RegB },
		{ "c", EvalValues::RegC },
		{ "d", EvalValues::RegD },
		{ "e", EvalValues::RegE },
		{ "f", EvalValues::RegF },
		{ "h", EvalValues::RegH },
		{ "l", EvalValues::RegL },
		{ "af", EvalValues::RegAF },
		{ "bc", EvalValues::RegBC },
		{ "de", EvalValues::RegDE },
		{ "hl", EvalValues::RegHL },
		{ "a'", EvalValues::RegAltA },
		{ "b'", EvalValues::RegAltB },
		{ "c'", EvalValues::RegAltC },
		{ "d'", EvalValues::RegAltD },
		{ "e'", EvalValues::RegAltE },
		{ "f'", EvalValues::RegAltF },
		{ "h'", EvalValues::RegAltH },
		{ "l'", EvalValues::RegAltL },
		{ "af'", EvalValues::RegAltAF },
		{ "bc'", EvalValues::RegAltBC },
		{ "de'", EvalValues::RegAltDE },
		{ "hl'", EvalValues::RegAltHL },
		{ "i", EvalValues::RegI },
		{ "r", EvalValues::RegR },
		{ "ix", EvalValues::RegIX },
		{ "iy", EvalValues::RegIY },
		{ "sp", EvalValues::RegSP },
		{ "pc", EvalValues::RegPC },

		{ "border", EvalValues::ZxBorderColor },
		{ "halted", EvalValues::ZxHalted },

		{ "frame", EvalValues::PpuFrameCount },
		{ "cycle", EvalValues::PpuCycle },
		{ "scanline", EvalValues::PpuScanline },
		//T-state within the frame (scanline * 224 + cycle on the 48K)
		{ "framecycle", EvalValues::ZxFrameCycle },
	};

	return supportedTokens;
}

int64_t ExpressionEvaluator::GetZxTokenValue(int64_t token, EvalResultType& resultType)
{
	auto ula = [this]() -> ZxUlaState {
		ZxUlaState ula;
		((ZxDebugger*)_cpuDebugger)->GetPpuState(ula);
		return ula;
	};

	ZxCpuState& s = (ZxCpuState&)((ZxDebugger*)_cpuDebugger)->GetState();
	switch(token) {
		case EvalValues::RegA: return s.A;
		case EvalValues::RegB: return s.B;
		case EvalValues::RegC: return s.C;
		case EvalValues::RegD: return s.D;
		case EvalValues::RegE: return s.E;
		case EvalValues::RegF: return s.Flags;
		case EvalValues::RegH: return s.H;
		case EvalValues::RegL: return s.L;
		case EvalValues::RegAF: return (s.A << 8) | s.Flags;
		case EvalValues::RegBC: return (s.B << 8) | s.C;
		case EvalValues::RegDE: return (s.D << 8) | s.E;
		case EvalValues::RegHL: return (s.H << 8) | s.L;
		case EvalValues::RegAltA: return s.AltA;
		case EvalValues::RegAltB: return s.AltB;
		case EvalValues::RegAltC: return s.AltC;
		case EvalValues::RegAltD: return s.AltD;
		case EvalValues::RegAltE: return s.AltE;
		case EvalValues::RegAltF: return s.AltFlags;
		case EvalValues::RegAltH: return s.AltH;
		case EvalValues::RegAltL: return s.AltL;
		case EvalValues::RegAltAF: return (s.AltA << 8) | s.AltFlags;
		case EvalValues::RegAltBC: return (s.AltB << 8) | s.AltC;
		case EvalValues::RegAltDE: return (s.AltD << 8) | s.AltE;
		case EvalValues::RegAltHL: return (s.AltH << 8) | s.AltL;
		case EvalValues::RegIX: return (s.IXH << 8) | s.IXL;
		case EvalValues::RegIY: return (s.IYH << 8) | s.IYL;
		case EvalValues::RegI: return s.I;
		case EvalValues::RegR: return s.R;
		case EvalValues::RegSP: return s.SP;
		case EvalValues::RegPC: return s.PC;

		case EvalValues::ZxHalted: return ReturnBool(s.Halted, resultType);
		case EvalValues::ZxBorderColor: return ula().BorderColor;

		case EvalValues::PpuFrameCount: return ula().FrameCount;
		case EvalValues::PpuCycle: return ula().Cycle;
		case EvalValues::PpuScanline: return ula().Scanline;
		case EvalValues::ZxFrameCycle: return ula().FrameCycle;

		default: return 0;
	}
}
