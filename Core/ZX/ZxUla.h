#pragma once
#include "pch.h"
#include "ZX/ZxTypes.h"
#include "Utilities/ISerializable.h"

//ULA: frame timing, memory contention, the floating bus and the picture.
//The picture is drawn lazily: before anything that can change it (a write to screen memory, a border change)
//and at the end of the frame, every 8-pixel chunk the beam has already passed is drawn with the current state.
class ZxUla final : public ISerializable
{
private:
	static constexpr uint32_t ChunksPerLine = ZxConstants::ScreenWidth / 8;
	static constexpr uint32_t ChunkCount = ChunksPerLine * ZxConstants::ScreenHeight;

	ZxTiming _timing = {};
	uint8_t* _videoRam = nullptr;

	uint8_t _contentionTable[8] = { 6, 5, 4, 3, 2, 1, 0, 0 };

	uint64_t _frameStartCycle = 0;
	uint32_t _frameCount = 0;
	uint8_t _borderColor = 7;

	uint32_t _nextChunk = 0;
	uint16_t _frameBuffer[ZxConstants::PixelCount] = {};

	__forceinline uint32_t GetChunkCycle(uint32_t chunk);
	void DrawChunk(uint32_t chunk);

public:
	void Init(ZxModel model, uint8_t* videoRam);
	void Reset();

	//Frame-relative T-state of an absolute cycle count. An instruction can end a few T-states into the next
	//frame before the frame is closed - those cycles are already relative to the next frame.
	__forceinline uint32_t GetFrameCycle(uint64_t cycle)
	{
		uint32_t frameCycle = (uint32_t)(cycle - _frameStartCycle);
		return frameCycle >= _timing.FrameLength ? frameCycle - _timing.FrameLength : frameCycle;
	}

	//Extra T-states the CPU is held for when it accesses contended memory at the given cycle
	__forceinline uint8_t GetContentionDelay(uint64_t cycle)
	{
		uint32_t t = GetFrameCycle(cycle);
		if(t < _timing.ContentionStart) {
			return 0;
		}
		t -= _timing.ContentionStart;
		uint32_t line = t / _timing.LineLength;
		uint32_t lineCycle = t % _timing.LineLength;
		if(line >= 192 || lineCycle >= 128) {
			return 0;
		}
		return _contentionTable[lineCycle & 0x07];
	}

	bool IsIrqActive(uint64_t cycle) { return GetFrameCycle(cycle) < _timing.IrqLength; }

	//Value read from an unattached port: whatever the ULA is fetching from video RAM at that time, or 0xFF
	uint8_t GetFloatingBusValue(uint64_t cycle);

	//Draw the picture up to the given cycle - must be called before video RAM or the border is changed
	void Render(uint64_t cycle);

	void SetBorderColor(uint64_t cycle, uint8_t color);
	uint8_t GetBorderColor() { return _borderColor; }

	//Called by the machine when the frame ends (after the last instruction that started in it)
	void EndFrame();

	uint64_t GetFrameStartCycle() { return _frameStartCycle; }
	uint32_t GetFrameCount() { return _frameCount; }
	const ZxTiming& GetTiming() { return _timing; }

	//Palette indexes: 0-7 normal, 8-15 bright
	uint16_t* GetFrameBuffer() { return _frameBuffer; }

	void Serialize(Serializer& s) override;
};
