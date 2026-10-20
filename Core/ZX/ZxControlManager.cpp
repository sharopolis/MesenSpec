#include "pch.h"
#include "ZX/ZxControlManager.h"
#include "ZX/ZxKeyboard.h"
#include "ZX/Input/ZxKeyboardDevice.h"
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
		case ControllerType::ZxKeyboard: device.reset(new ZxKeyboardDevice(_emu, _emu->GetSettings()->GetZxConfig().Keyboard.Keys)); break;
	}
	return device;
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
	_prevConfig = cfg;
}

void ZxControlManager::ApplyToMatrix(ZxKeyboard& keyboard)
{
	shared_ptr<ZxKeyboardDevice> device = GetControlDevice<ZxKeyboardDevice>();
	if(device) {
		device->ApplyToMatrix(keyboard);
	}
}
