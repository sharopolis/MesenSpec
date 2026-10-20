#pragma once
#include "pch.h"
#include "ZX/ZxMachine.h"
#include "Shared/Video/BaseVideoFilter.h"
#include "Shared/EmuSettings.h"
#include "Shared/Emulator.h"

//Converts the ULA's palette indexes (0-7 normal, 8-15 bright) to ARGB
class ZxDefaultVideoFilter : public BaseVideoFilter
{
private:
	uint32_t _palette[16] = {};
	VideoConfig _videoConfig = {};

	void InitLookupTable()
	{
		VideoConfig config = _emu->GetSettings()->GetVideoConfig();
		InitConversionMatrix(config.Hue, config.Saturation);

		for(int i = 0; i < 16; i++) {
			uint32_t argb = ZxMachine::GetPaletteColor(i);
			uint8_t r = (argb >> 16) & 0xFF;
			uint8_t g = (argb >> 8) & 0xFF;
			uint8_t b = argb & 0xFF;
			if(config.Hue != 0 || config.Saturation != 0 || config.Brightness != 0 || config.Contrast != 0) {
				ApplyColorOptions(r, g, b, config.Brightness, config.Contrast);
			}
			_palette[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
		}

		_videoConfig = config;
	}

protected:
	void OnBeforeApplyFilter() override
	{
		VideoConfig& config = _emu->GetSettings()->GetVideoConfig();
		if(_videoConfig.Hue != config.Hue || _videoConfig.Saturation != config.Saturation || _videoConfig.Contrast != config.Contrast || _videoConfig.Brightness != config.Brightness) {
			InitLookupTable();
		}
	}

public:
	ZxDefaultVideoFilter(Emulator* emu) : BaseVideoFilter(emu)
	{
		InitLookupTable();
	}

	void ApplyFilter(uint16_t* ppuOutputBuffer) override
	{
		uint32_t* out = GetOutputBuffer();
		uint32_t pixelCount = _baseFrameInfo.Width * _baseFrameInfo.Height;
		for(uint32_t i = 0; i < pixelCount; i++) {
			out[i] = _palette[ppuOutputBuffer[i] & 0x0F];
		}
	}
};
