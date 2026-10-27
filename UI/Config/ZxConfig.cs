using CommunityToolkit.Mvvm.ComponentModel;
using Mesen.Interop;
using Mesen.Localization;
using Mesen.ViewModels;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;

namespace Mesen.Config;

public partial class ZxConfig : BaseConfig<ZxConfig>
{
	[ObservableProperty] public partial ConsoleOverrideConfig ConfigOverrides { get; set; } = new();

	[ObservableProperty] public partial ZxControllerConfig Keyboard { get; set; } = new();

	[ObservableProperty] public partial ZxModel Model { get; set; } = ZxModel.Spectrum48K;

	[ObservableProperty][MinMax(0, 100)] public partial UInt32 BeeperVolume { get; set; } = 100;
	[ObservableProperty][MinMax(0, 100)] public partial UInt32 AyVolume { get; set; } = 100;

	public void ApplyConfig()
	{
		Keyboard.Type = ControllerType.ZxKeyboard;

		ConfigApi.SetZxConfig(new InteropZxConfig() {
			Keyboard = Keyboard.ToInterop(),
			Model = Model,
			BeeperVolume = BeeperVolume,
			AyVolume = AyVolume,
		});
	}

	internal void InitializeDefaults(DefaultKeyMappingType defaultMappings)
	{
		//The keyboard has a single fixed layout (no gamepad presets)
		Keyboard.Type = ControllerType.ZxKeyboard;
		Keyboard.Mapping1.SetDefaultKeys(ControllerType.ZxKeyboard);
	}
}

[StructLayout(LayoutKind.Sequential)]
public struct InteropZxConfig
{
	public InteropControllerConfig Keyboard;

	public ZxModel Model;

	public UInt32 BeeperVolume;
	public UInt32 AyVolume;
}

public enum ZxModel : byte
{
	Spectrum48K,
	Spectrum128K,
}

public class ZxControllerConfig : ControllerConfig
{
	public new ZxKeyMapping Mapping1 { get => (ZxKeyMapping)_mapping1; set => _mapping1 = value; }
	public new ZxKeyMapping Mapping2 { get => (ZxKeyMapping)_mapping2; set => _mapping2 = value; }
	public new ZxKeyMapping Mapping3 { get => (ZxKeyMapping)_mapping3; set => _mapping3 = value; }
	public new ZxKeyMapping Mapping4 { get => (ZxKeyMapping)_mapping4; set => _mapping4 = value; }

	public ZxControllerConfig()
	{
		_mapping1 = new ZxKeyMapping();
		_mapping2 = new ZxKeyMapping();
		_mapping3 = new ZxKeyMapping();
		_mapping4 = new ZxKeyMapping();
	}
}

public class ZxKeyMapping : KeyMapping
{
	public UInt16[]? KeyboardButtons { get; set; } = null;

	protected override UInt16[]? GetCustomButtons(ControllerType type)
	{
		return type == ControllerType.ZxKeyboard ? KeyboardButtons : null;
	}

	public override List<CustomKeyMapping> ToCustomKeys(ControllerType type, int mappingIndex)
	{
		if(type != ControllerType.ZxKeyboard) {
			return new();
		}

		if(KeyboardButtons == null) {
			if(mappingIndex == 0) {
				SetDefaultKeys(type, null);
			} else {
				ClearKeys(type);
			}
		}

		//Keep the order of the enum (the keyboard's layout) rather than sorting by name
		return Enum.GetValues<ZxKeyboardButtons>().Select(val => new CustomKeyMapping(ResourceHelper.GetEnumText(val), KeyboardButtons!, (int)val)).ToList();
	}

	public override void ClearKeys(ControllerType type)
	{
		if(type == ControllerType.ZxKeyboard) {
			KeyboardButtons = new UInt16[Enum.GetValues<ZxKeyboardButtons>().Length];
		} else {
			base.ClearKeys(type);
		}
	}

	public override UInt16[]? GetDefaultCustomKeys(ControllerType type, KeyPresetType? preset)
	{
		if(type != ControllerType.ZxKeyboard) {
			return null;
		}

		//Same order as ZxKeyboardButtons (and ZxKeyboardDevice in the core): the 8 half-rows, then the shortcuts
		string[] keys = {
			"Left Shift", "Z", "X", "C", "V",
			"A", "S", "D", "F", "G",
			"Q", "W", "E", "R", "T",
			"1", "2", "3", "4", "5",
			"0", "9", "8", "7", "6",
			"P", "O", "I", "U", "Y",
			"Enter", "L", "K", "J", "H",
			"Space", "Right Shift", "M", "N", "B",
			//Not Esc for BREAK: it pauses the emulator, and remains a shortcut even while the keyboard is connected
			"Backspace", "Left Arrow", "Down Arrow", "Up Arrow", "Right Arrow", "Tab", "End"
		};
		return keys.Select(k => InputApi.GetKeyCode(k)).ToArray();
	}

	public override void SetDefaultKeys(ControllerType type, KeyPresetType? preset = null)
	{
		if(type == ControllerType.ZxKeyboard) {
			KeyboardButtons = GetDefaultCustomKeys(type, preset);
		} else {
			base.SetDefaultKeys(type, preset);
		}
	}
}

//Same order as ZxKeyboardDevice::Buttons in the core
public enum ZxKeyboardButtons
{
	CapsShift, Z, X, C, V,
	A, S, D, F, G,
	Q, W, E, R, T,
	Num1, Num2, Num3, Num4, Num5,
	Num0, Num9, Num8, Num7, Num6,
	P, O, I, U, Y,
	Enter, L, K, J, H,
	Space, SymbolShift, M, N, B,
	Delete, CursorLeft, CursorDown, CursorUp, CursorRight, Edit, Break
}
