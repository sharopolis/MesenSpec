#include "pch.h"
#include "ZX/Debugger/ZxPpuTools.h"
#include "Shared/Emulator.h"
#include "Shared/NotificationManager.h"
#include "Shared/Interfaces/INotificationListener.h"

void ZxPpuTools::UpdateViewers(uint32_t prevFrameCycle, uint32_t frameCycle, uint16_t lineLength)
{
	for(auto& updateTiming : _updateTimings) {
		ViewerRefreshConfig& cfg = updateTiming.second;
		uint32_t target = cfg.Scanline * lineLength + cfg.Cycle;
		bool reached = frameCycle >= prevFrameCycle ? (prevFrameCycle < target && target <= frameCycle) : (target > prevFrameCycle || target <= frameCycle);
		if(reached && !_emu->IsDebuggerBlocked()) {
			_emu->GetNotificationManager()->SendNotification(ConsoleNotificationType::ViewerRefresh, (void*)(uint64_t)updateTiming.first);
		}
	}
}
