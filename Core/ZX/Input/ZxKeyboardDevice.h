#pragma once
#include "pch.h"
#include "Shared/BaseControlDevice.h"
#include "ZX/ZxKeyboard.h"

//The Spectrum keyboard as a Mesen input device: each of the 40 keys (and a few PC-style shortcuts for key
//combinations) is mapped to a host key in the input settings (KeyMapping::CustomKeys).
class ZxKeyboardDevice : public BaseControlDevice
{
public:
	//Buttons 0-39: row * 5 + key, in the order of ZxKeyboard::KeyNames. The rest are shortcuts.
	enum Shortcuts
	{
		Delete = 40, //CAPS SHIFT + 0
		CursorLeft, //CAPS SHIFT + 5
		CursorDown, //CAPS SHIFT + 6
		CursorUp, //CAPS SHIFT + 7
		CursorRight, //CAPS SHIFT + 8
		Edit, //CAPS SHIFT + 1
		Break, //CAPS SHIFT + SPACE
		ButtonCount
	};

protected:
	string GetKeyNames() override
	{
		//One character per button, used by the input display and movies
		return "^ZXCVASDFGQWERT1234509876POIUY_LKJH $MNBdlvurex";
	}

	void InternalSetStateFromInput() override
	{
		for(KeyMapping& keyMapping : _keyMappings) {
			for(int i = 0; i < ButtonCount; i++) {
				SetPressedState(i, keyMapping.CustomKeys[i]);
			}
		}
	}

public:
	ZxKeyboardDevice(Emulator* emu, KeyMappingSet keyMappings) : BaseControlDevice(emu, ControllerType::ZxKeyboard, 0, keyMappings)
	{
	}

	//The machine reads the keyboard through ZxKeyboard - copy this device's state into it once per frame
	void ApplyToMatrix(ZxKeyboard& keyboard)
	{
		for(uint8_t i = 0; i < 40; i++) {
			keyboard.SetKeyState(i / 5, i % 5, IsPressed(i));
		}

		//Shortcuts press CAPS SHIFT plus another key (on top of whatever is pressed directly)
		static constexpr struct { uint8_t Button; uint8_t Row; uint8_t Key; } shortcuts[] = {
			{ Delete, 4, 0 }, //0
			{ CursorLeft, 3, 4 }, //5
			{ CursorDown, 4, 4 }, //6
			{ CursorUp, 4, 3 }, //7
			{ CursorRight, 4, 2 }, //8
			{ Edit, 3, 0 }, //1
			{ Break, 7, 0 }, //SPACE
		};
		for(auto& s : shortcuts) {
			if(IsPressed(s.Button)) {
				keyboard.SetKeyState(0, 0, true);
				keyboard.SetKeyState(s.Row, s.Key, true);
			}
		}
	}

	uint8_t ReadRam(uint16_t addr) override { return 0; }
	void WriteRam(uint16_t addr, uint8_t value) override {}
};
