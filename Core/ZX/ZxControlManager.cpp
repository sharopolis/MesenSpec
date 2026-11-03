#include "pch.h"
#include "ZX/ZxControlManager.h"
#include "ZX/ZxKeyboard.h"
#include "ZX/Input/ZxKeyboardDevice.h"
#include "ZX/Input/ZxJoystick.h"
#include "ZX/ZxMemoryManager.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"

ZxControlManager::ZxControlManager(Emulator* emu) : BaseControlManager(emu, CpuType::Zx)
{
}

shared_ptr<BaseControlDevice> ZxControlManager::CreateControllerDevice(ControllerType type, uint8_t port)
{
	shared_ptr<BaseControlDevice> device;
	switch(type) {
		default:
		case ControllerType::None: break;
		case ControllerType::ZxKeyboard: {
			ZxConfig& cfg = _emu->GetSettings()->GetZxConfig();
			device.reset(new ZxKeyboardDevice(_emu, cfg.Keyboard.Keys, GetJoystickKeys(cfg)));
			break;
		}
		case ControllerType::ZxKempston:
		case ControllerType::ZxSinclair1:
		case ControllerType::ZxSinclair2:
		case ControllerType::ZxCursor:
			device.reset(new ZxJoystick(_emu, type, port, _emu->GetSettings()->GetZxConfig().Joystick.Keys));
			break;
	}
	return device;
}

vector<uint16_t> ZxControlManager::GetJoystickKeys(ZxConfig& cfg)
{
	//The host keys the joystick uses (ZxJoystick reads the directions, A/B and TurboA/TurboB)
	vector<uint16_t> keys;
	if(cfg.Joystick.Type != ControllerType::None) {
		for(KeyMapping& m : cfg.Joystick.Keys.GetKeyMappingArray()) {
			for(uint16_t key : { m.Up, m.Down, m.Left, m.Right, m.A, m.B, m.TurboA, m.TurboB }) {
				if(key) {
					keys.push_back(key);
				}
			}
		}
	}
	return keys;
}

void ZxControlManager::UpdateControlDevices()
{
	ZxConfig& cfg = _emu->GetSettings()->GetZxConfig();
	if(_emu->GetSettings()->IsEqual(_prevConfig, cfg) && _controlDevices.size() > 0) {
		//Do nothing if configuration is unchanged
		return;
	}

	auto lock = _deviceLock.AcquireSafe();
	ClearDevices();

	//The keyboard is built in, it is always connected
	shared_ptr<BaseControlDevice> device = CreateControllerDevice(ControllerType::ZxKeyboard, 0);
	if(device) {
		RegisterControlDevice(device);
	}

	//Joystick interface (port 1)
	device = CreateControllerDevice(cfg.Joystick.Type, 1);
	if(device) {
		RegisterControlDevice(device);
	}
	_prevConfig = cfg;
}

void ZxControlManager::ApplyToMatrix(ZxKeyboard& keyboard)
{
	shared_ptr<ZxKeyboardDevice> device = GetControlDevice<ZxKeyboardDevice>();
	if(device) {
		device->ApplyToMatrix(keyboard);
	}
}

void ZxControlManager::ApplyToJoystick(ZxMemoryManager& memoryManager, ZxKeyboard& keyboard)
{
	shared_ptr<ZxJoystick> joystick = GetControlDevice<ZxJoystick>();
	bool kempston = joystick && joystick->IsKempston();
	memoryManager.SetKempston(kempston, kempston ? joystick->GetKempstonValue() : 0);
	if(joystick && !kempston) {
		joystick->ApplyToMatrix(keyboard);
	}
}
