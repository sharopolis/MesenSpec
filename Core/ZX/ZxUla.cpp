#include "pch.h"
#include "ZX/ZxUla.h"
#include "Utilities/Serializer.h"

void ZxUla::Init(ZxModel model, uint8_t* videoRam)
{
	_timing = ZxTiming::Get(model);
	_videoRam = videoRam;
	Reset();
}

void ZxUla::Reset()
{
	_frameStartCycle = 0;
	_frameCount = 0;
	_borderColor = 7;
	_nextChunk = 0;
}

uint32_t ZxUla::GetChunkCycle(uint32_t chunk)
{
	//Paper pixel (0,0) is displayed at FirstPaperLine * LineLength, every 8-pixel chunk takes 4 T-states, and the
	//left border of a line is displayed at the end of the previous line's T-states
	uint32_t row = chunk / ChunksPerLine;
	uint32_t column = chunk % ChunksPerLine;
	uint32_t frameLine = row + _timing.FirstPaperLine - ZxConstants::BorderHeight;
	return frameLine * _timing.LineLength + column * 4 - ZxConstants::BorderWidth / 2;
}

void ZxUla::DrawChunk(uint32_t chunk)
{
	uint32_t row = chunk / ChunksPerLine;
	uint32_t column = chunk % ChunksPerLine;
	uint16_t* out = _frameBuffer + row * ZxConstants::ScreenWidth + column * 8;

	int32_t y = (int32_t)row - (int32_t)ZxConstants::BorderHeight;
	int32_t x = (int32_t)column - (int32_t)(ZxConstants::BorderWidth / 8);
	if(y < 0 || y >= 192 || x < 0 || x >= 32) {
		for(int i = 0; i < 8; i++) {
			out[i] = _borderColor;
		}
		return;
	}

	uint8_t bitmap = _videoRam[((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | x];
	uint8_t attr = _videoRam[0x1800 + (y >> 3) * 32 + x];

	uint8_t bright = (attr & 0x40) ? 8 : 0;
	uint8_t ink = (attr & 0x07) | bright;
	uint8_t paper = ((attr >> 3) & 0x07) | bright;
	if((attr & 0x80) && (_frameCount & 0x10)) {
		std::swap(ink, paper);
	}

	for(int i = 0; i < 8; i++) {
		out[i] = (bitmap & (0x80 >> i)) ? ink : paper;
	}
}

void ZxUla::Render(uint64_t cycle)
{
	uint32_t t = GetFrameCycle(cycle);
	if((uint32_t)(cycle - _frameStartCycle) >= _timing.FrameLength) {
		//Already in the next frame, which hasn't been started yet - the rest of this frame is drawn by EndFrame
		return;
	}
	while(_nextChunk < ChunkCount && GetChunkCycle(_nextChunk) < t) {
		DrawChunk(_nextChunk++);
	}
}

void ZxUla::SetBorderColor(uint64_t cycle, uint8_t color)
{
	Render(cycle);
	_borderColor = color & 0x07;
}

uint8_t ZxUla::GetFloatingBusValue(uint64_t cycle)
{
	//During each 8 T-state block of a paper line, the ULA reads bitmap, attribute, bitmap+1, attribute+1 and
	//then leaves the bus idle for 4 T-states
	uint32_t t = GetFrameCycle(cycle);
	uint32_t start = _timing.ContentionStart + 3;
	if(t < start) {
		return 0xFF;
	}
	t -= start;
	uint32_t y = t / _timing.LineLength;
	uint32_t lineCycle = t % _timing.LineLength;
	if(y >= 192 || lineCycle >= 128) {
		return 0xFF;
	}

	uint32_t x = (lineCycle / 8) * 2;
	uint16_t bitmapAddr = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
	uint16_t attrAddr = 0x1800 + (y >> 3) * 32;
	switch(lineCycle & 0x07) {
		case 0: return _videoRam[bitmapAddr + x];
		case 1: return _videoRam[attrAddr + x];
		case 2: return _videoRam[bitmapAddr + x + 1];
		case 3: return _videoRam[attrAddr + x + 1];
		default: return 0xFF;
	}
}

void ZxUla::EndFrame()
{
	while(_nextChunk < ChunkCount) {
		DrawChunk(_nextChunk++);
	}
	_nextChunk = 0;
	_frameStartCycle += _timing.FrameLength;
	_frameCount++;
}

void ZxUla::Serialize(Serializer& s)
{
	SV(_frameStartCycle);
	SV(_frameCount);
	SV(_borderColor);
	SV(_nextChunk);
	SVArray(_frameBuffer, ZxConstants::PixelCount);
}
