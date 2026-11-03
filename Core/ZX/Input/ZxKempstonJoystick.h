#pragma once
#include "pch.h"
#include "Shared/BaseControlDevice.h"

//Kempston joystick interface: read through port 0x1F (decoded on A5-A7 = 0), one bit per direction/fire, active high.
//Fire is on both the A and B buttons of the mapping (either face button of a gamepad).
class ZxKempstonJoystick : public BaseControlDevice
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
	//Same order as the bits of port 0x1F
	enum Buttons
	{
		Right = 0,
		Left,
		Down,
		Up,
		Fire
	};

	ZxKempstonJoystick(Emulator* emu, uint8_t port, KeyMappingSet keyMappings) : BaseControlDevice(emu, ControllerType::ZxKempston, port, keyMappings)
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

	//Not on Mesen's bus - the machine reads the joystick through GetPortValue
	uint8_t ReadRam(uint16_t addr) override { return 0; }
	void WriteRam(uint16_t addr, uint8_t value) override {}

	//Value read from port 0x1F
	uint8_t GetPortValue()
	{
		uint8_t value = 0;
		for(int i = 0; i <= Buttons::Fire; i++) {
			if(IsPressed(i)) {
				value |= 1 << i;
			}
		}
		return value;
	}
};
