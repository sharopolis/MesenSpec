#pragma once
#include "pch.h"
#include "Debugger/DebugTypes.h"
#include "Debugger/BaseEventManager.h"
#include "Utilities/SimpleLock.h"
#include "ZX/ZxTypes.h"

enum class DebugEventType;
struct DebugEventInfo;
class ZxConsole;
class Debugger;

struct ZxEventViewerConfig : public BaseEventViewerConfig
{
	EventViewerCategoryCfg Irq;
	EventViewerCategoryCfg Nmi;
	EventViewerCategoryCfg MarkedBreakpoints;

	//Port 0xFE (any even port): border/beeper writes, keyboard/EAR reads
	EventViewerCategoryCfg UlaPortWrite;
	EventViewerCategoryCfg UlaPortRead;

	EventViewerCategoryCfg OtherPortWrite;
	EventViewerCategoryCfg OtherPortRead;

	//Writes to screen memory (0x4000-0x5AFF)
	EventViewerCategoryCfg ScreenWrite;

	bool ShowPreviousFrameEvents;
};

//Events are shown at the T-state they happen: one row per frame line (224 T-states), 2 pixels per T-state.
//The timeline is shifted so the picture appears unbroken: each row starts with the left border, which the
//ULA displays at the end of the previous line's T-states.
class ZxEventManager final : public BaseEventManager
{
private:
	ZxEventViewerConfig _config = {};

	ZxConsole* _console;
	Debugger* _debugger;

	ZxTiming _timing = {};
	uint16_t _scanlineCount = 312;
	uint16_t _ppuBuffer[ZxConstants::PixelCount] = {};

	uint32_t GetDisplayWidth() { return _timing.LineLength * 2; }

protected:
	bool ShowPreviousFrameEvents() override;
	void ConvertScanlineCycleToRowColumn(int32_t& x, int32_t& y) override;
	void DrawScreen(uint32_t* buffer) override;

public:
	ZxEventManager(Debugger* debugger, ZxConsole* console);

	void AddEvent(DebugEventType type, MemoryOperationInfo& operation, int32_t breakpointId = -1) override;
	void AddEvent(DebugEventType type) override;

	//Screen memory writes are only recorded when they are shown (they are very frequent)
	bool IsScreenWriteEnabled() { return _config.ScreenWrite.Visible; }

	EventViewerCategoryCfg GetEventConfig(DebugEventInfo& evt) override;

	uint32_t TakeEventSnapshot(bool forAutoRefresh) override;
	DebugEventInfo GetEvent(uint16_t y, uint16_t x) override;

	FrameInfo GetDisplayBufferSize() override;
	void SetConfiguration(BaseEventViewerConfig& config) override;
};
