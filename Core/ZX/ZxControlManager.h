#pragma once
#include "pch.h"
#include "Shared/SettingTypes.h"
#include "Shared/BaseControlManager.h"

class ZxKeyboard;
class ZxMemoryManager;

class ZxControlManager : public BaseControlManager
{
private:
	ZxConfig _prevConfig = {};

	vector<uint16_t> GetJoystickKeys(ZxConfig& cfg);

public:
	ZxControlManager(Emulator* emu);

	shared_ptr<BaseControlDevice> CreateControllerDevice(ControllerType type, uint8_t port) override;
	void UpdateControlDevices() override;

	//The Spectrum's keyboard is always connected - this disables Mesen's keyboard shortcuts (except pause) while running
	bool IsKeyboardConnected() override { return true; }

	//Copies the state of the keyboard device (host keys) into the machine's key matrix
	void ApplyToMatrix(ZxKeyboard& keyboard);
	//Connects/disconnects the Kempston interface and copies the joystick's state into it, or presses the keys of a
	//Sinclair/Cursor joystick (call after ApplyToMatrix)
	void ApplyToJoystick(ZxMemoryManager& memoryManager, ZxKeyboard& keyboard);
};
