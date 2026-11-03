#pragma once
#include "pch.h"
#include "Shared/BaseControlDevice.h"
#include "ZX/ZxKeyboard.h"

//A joystick, through one of the common interfaces (ControllerType::ZxKempston/ZxSinclair1/ZxSinclair2/ZxCursor):
//- Kempston: read through port 0x1F (decoded on A5-A7 = 0), one bit per direction/fire, active high
//- Sinclair 1/2 (Interface 2) and Cursor (Protek, AGF...): they press keys - 6-0, 1-5 and 5/6/7/8/0
//Fire is on both the A and B buttons of the mapping (either face button of a gamepad).
class ZxJoystick : public BaseControlDevice
{
private:
	uint32_t _turboSpeed = 0;

protected:
	string GetKeyNames() override
	{
		return "RLDUF";
	}

	void InternalSetStateFromInput() override
	{
		bool turboOn = IsTurboOn(_turboSpeed);
		for(KeyMapping& keyMapping : _keyMappings) {
			SetPressedState(Buttons::Right, keyMapping.Right);
			SetPressedState(Buttons::Left, keyMapping.Left);
			SetPressedState(Buttons::Down, keyMapping.Down);
			SetPressedState(Buttons::Up, keyMapping.Up);
			SetPressedState(Buttons::Fire, keyMapping.A);
			SetPressedState(Buttons::Fire, keyMapping.B);
			if(turboOn) {
				SetPressedState(Buttons::Fire, keyMapping.TurboA);
				SetPressedState(Buttons::Fire, keyMapping.TurboB);
			}
		}
	}

public:
	//Same order as the bits of the Kempston port (0x1F)
	enum Buttons
	{
		Right = 0,
		Left,
		Down,
		Up,
		Fire
	};

	ZxJoystick(Emulator* emu, ControllerType type, uint8_t port, KeyMappingSet keyMappings) : BaseControlDevice(emu, type, port, keyMappings)
	{
		_turboSpeed = keyMappings.TurboSpeed;
	}

	//Button names for Lua's emu.getInput/setInput
	vector<DeviceButtonName> GetKeyNameAssociations() override
	{
		return {
			{ "right", Buttons::Right },
			{ "left", Buttons::Left },
			{ "down", Buttons::Down },
			{ "up", Buttons::Up },
			{ "fire", Buttons::Fire },
		};
	}

	//Not on Mesen's bus - the machine reads the joystick through GetKempstonValue or the keyboard
	uint8_t ReadRam(uint16_t addr) override { return 0; }
	void WriteRam(uint16_t addr, uint8_t value) override {}

	bool IsKempston() { return _type == ControllerType::ZxKempston; }

	//Kempston: value read from port 0x1F
	uint8_t GetKempstonValue()
	{
		uint8_t value = 0;
		for(int i = 0; i <= Buttons::Fire; i++) {
			if(IsPressed(i)) {
				value |= 1 << i;
			}
		}
		return value;
	}

	//Sinclair/Cursor: presses the keys of the pressed buttons (on top of the keyboard's state)
	void ApplyToMatrix(ZxKeyboard& keyboard)
	{
		//Keys as {row, key} in the matrix, in the order of Buttons (right, left, down, up, fire).
		//Row 3 = 1 2 3 4 5, row 4 = 0 9 8 7 6.
		struct Key { uint8_t Row; uint8_t Key; };
		static constexpr Key sinclair1[5] = { { 4, 3 }, { 4, 4 }, { 4, 2 }, { 4, 1 }, { 4, 0 } }; //7 6 8 9 0
		static constexpr Key sinclair2[5] = { { 3, 1 }, { 3, 0 }, { 3, 2 }, { 3, 3 }, { 3, 4 } }; //2 1 3 4 5
		static constexpr Key cursor[5] = { { 4, 2 }, { 3, 4 }, { 4, 4 }, { 4, 3 }, { 4, 0 } }; //8 5 6 7 0

		const Key* keys = nullptr;
		switch(_type) {
			case ControllerType::ZxSinclair1: keys = sinclair1; break;
			case ControllerType::ZxSinclair2: keys = sinclair2; break;
			case ControllerType::ZxCursor: keys = cursor; break;
			default: return;
		}
		for(int i = 0; i <= Buttons::Fire; i++) {
			if(IsPressed(i)) {
				keyboard.SetKeyState(keys[i].Row, keys[i].Key, true);
			}
		}
	}
};
