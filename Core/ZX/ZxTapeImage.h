#pragma once
#include "pch.h"

//One pulse of the tape signal: the EAR input stays at Level for Length T-states
struct ZxTapePulse
{
	uint32_t Length;
	bool Level;
};

enum class ZxTapeBlockType : uint8_t
{
	//Pilot tone, 2 sync pulses, data bits and a pause (TZX 0x10/0x11/0x14 - .tap blocks are standard speed ones)
	Data,
	//A sequence of identical pulses (TZX 0x12)
	Tone,
	//Pulses of any length (TZX 0x13)
	Pulses,
	//Sampled signal, one bit per sample (TZX 0x15)
	DirectRecording,
	//Silence (TZX 0x20)
	Pause,
	//Stops the tape (TZX 0x20 with no length)
	Stop,
	//Stops the tape on the 48K only (TZX 0x2A)
	Stop48K,
	//Forces the signal level (TZX 0x2B)
	SetLevel,
};

//A block of a tape image, with its timings in 3.5MHz T-states (the TZX convention)
struct ZxTapeBlock
{
	ZxTapeBlockType Type = ZxTapeBlockType::Data;

	//Data: PilotLength/PilotCount = pilot tone (no pilot if PilotCount is 0), Sync1/Sync2 = sync pulses (skipped if 0),
	//ZeroLength/OneLength = each bit is 2 pulses of this length, UsedBits = bits used in the last byte
	//Tone: PilotLength x PilotCount
	uint16_t PilotLength = 0;
	uint16_t PilotCount = 0;
	uint16_t Sync1 = 0;
	uint16_t Sync2 = 0;
	uint16_t ZeroLength = 0;
	uint16_t OneLength = 0;
	uint8_t UsedBits = 8;

	//Silence after the block (Data/DirectRecording/Pause)
	uint32_t PauseMs = 0;

	//Data: the bytes (flag byte and checksum included). DirectRecording: the samples, MSB first.
	vector<uint8_t> Data;
	//Pulses: the pulse lengths
	vector<uint16_t> PulseLengths;
	//DirectRecording: T-states per sample
	uint16_t SampleLength = 0;
	//SetLevel: the new level
	bool Level = false;

	//The ROM's LD-BYTES can read it (standard timings) - such blocks can be loaded instantly by the ROM trap
	bool IsStandardSpeed() const;
};

//A tape image (.tap or .tzx) as the sequence of blocks to play. TZX loops, calls and jumps are unrolled when the file
//is loaded; information blocks (texts, archive info, hardware type...) are skipped.
class ZxTapeImage
{
private:
	vector<ZxTapeBlock> _blocks;

	bool LoadTap(const vector<uint8_t>& data, string& error);
	bool LoadTzx(const vector<uint8_t>& data, string& error);

public:
	static bool IsTape(const string& extension) { return extension == ".tap" || extension == ".tzx"; }

	//Fails if the file isn't a valid tape image (unknown TZX blocks are skipped if their length is known)
	bool Load(const vector<uint8_t>& data, string& error);

	const vector<ZxTapeBlock>& GetBlocks() const { return _blocks; }

	//Generates a block's signal. level: the signal level before the block, updated to the level after it.
	//clockRate: T-states per second of the machine (the image's timings are scaled from 3.5MHz).
	static void GetPulses(const ZxTapeBlock& block, bool& level, uint32_t clockRate, vector<ZxTapePulse>& pulses);
};
