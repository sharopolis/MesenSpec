#pragma once
#include "pch.h"
#include "Debugger/PpuTools.h"

class Debugger;
class Emulator;

//The ULA has no tiles, sprites or palette RAM, so the PPU viewers are disabled for the Spectrum. This only provides
//the viewer refresh timing used by the other tools (register viewer, memory viewer, etc.)
class ZxPpuTools final : public PpuTools
{
public:
	ZxPpuTools(Debugger* debugger, Emulator* emu) : PpuTools(debugger, emu) {}

	//Notifies the viewers whose refresh position (scanline/cycle) was reached between two frame T-states.
	//The ULA has no cycle loop of its own, so this is called between instructions, not on every T-state.
	void UpdateViewers(uint32_t prevFrameCycle, uint32_t frameCycle, uint16_t lineLength);

	DebugTilemapInfo GetTilemap(GetTilemapOptions options, BaseState& state, BaseState& ppuToolsState, uint8_t* vram, uint32_t* palette, uint32_t* outBuffer) override { return {}; }
	FrameInfo GetTilemapSize(GetTilemapOptions options, BaseState& state) override { return {}; }
	DebugTilemapTileInfo GetTilemapTileInfo(uint32_t x, uint32_t y, uint8_t* vram, GetTilemapOptions options, BaseState& baseState, BaseState& ppuToolsState) override { return {}; }

	DebugSpritePreviewInfo GetSpritePreviewInfo(GetSpritePreviewOptions options, BaseState& state, BaseState& ppuToolsState) override { return {}; }
	void GetSpriteList(GetSpritePreviewOptions options, BaseState& baseState, BaseState& ppuToolsState, uint8_t* vram, uint8_t* oamRam, uint32_t* palette, DebugSpriteInfo outBuffer[], uint32_t* spritePreviews, uint32_t* screenPreview) override {}

	DebugPaletteInfo GetPaletteInfo(GetPaletteInfoOptions options) override { return {}; }
	void SetPaletteColor(int32_t colorIndex, uint32_t color) override {}
};
