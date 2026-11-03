#include "pch.h"
#include "ZX/ZxTapeImage.h"

//Standard (ROM) timings, in 3.5MHz T-states
static constexpr uint16_t StdPilotLength = 2168;
static constexpr uint16_t StdHeaderPilotCount = 8063;
static constexpr uint16_t StdDataPilotCount = 3223;
static constexpr uint16_t StdSync1 = 667;
static constexpr uint16_t StdSync2 = 735;
static constexpr uint16_t StdZeroLength = 855;
static constexpr uint16_t StdOneLength = 1710;

static bool IsNear(uint16_t value, uint16_t expected)
{
	return std::abs((int)value - (int)expected) <= expected / 20;
}

bool ZxTapeBlock::IsStandardSpeed() const
{
	return Type == ZxTapeBlockType::Data && PilotCount > 0 && UsedBits == 8 && !Data.empty() &&
		IsNear(PilotLength, StdPilotLength) && IsNear(Sync1, StdSync1) && IsNear(Sync2, StdSync2) &&
		IsNear(ZeroLength, StdZeroLength) && IsNear(OneLength, StdOneLength);
}

static ZxTapeBlock GetStandardBlock(vector<uint8_t> data, uint32_t pauseMs)
{
	ZxTapeBlock block;
	block.Type = ZxTapeBlockType::Data;
	block.PilotLength = StdPilotLength;
	//Headers (flag < 0x80) have a longer pilot tone
	block.PilotCount = (!data.empty() && data[0] < 0x80) ? StdHeaderPilotCount : StdDataPilotCount;
	block.Sync1 = StdSync1;
	block.Sync2 = StdSync2;
	block.ZeroLength = StdZeroLength;
	block.OneLength = StdOneLength;
	block.UsedBits = 8;
	block.PauseMs = pauseMs;
	block.Data = std::move(data);
	return block;
}

bool ZxTapeImage::Load(const vector<uint8_t>& data, string& error)
{
	static const char signature[] = "ZXTape!\x1A";
	if(data.size() >= 10 && memcmp(data.data(), signature, 8) == 0) {
		return LoadTzx(data, error);
	}
	return LoadTap(data, error);
}

bool ZxTapeImage::LoadTap(const vector<uint8_t>& data, string& error)
{
	//.tap: a sequence of blocks, each preceded by its 16-bit length
	vector<ZxTapeBlock> blocks;
	size_t pos = 0;
	while(pos + 2 <= data.size()) {
		uint16_t length = data[pos] | (data[pos + 1] << 8);
		pos += 2;
		if(pos + length > data.size()) {
			error = "Invalid .tap file (truncated block)";
			return false;
		}
		//1 second of silence after each block, like the ROM's SAVE
		blocks.push_back(GetStandardBlock(vector<uint8_t>(data.begin() + pos, data.begin() + pos + length), 1000));
		pos += length;
	}

	if(blocks.empty()) {
		error = "Invalid .tap file (no blocks)";
		return false;
	}

	_blocks = std::move(blocks);
	return true;
}

bool ZxTapeImage::LoadTzx(const vector<uint8_t>& data, string& error)
{
	//First pass: the file's blocks, with the control blocks (loops, jumps, calls) kept to be run in the second pass
	enum class EntryType { Block, LoopStart, LoopEnd, Jump, Call, Return, Info };
	struct Entry
	{
		EntryType Type;
		size_t BlockIndex = 0;
		int32_t Value = 0;
		vector<int16_t> Offsets;
	};

	vector<ZxTapeBlock> fileBlocks;
	vector<Entry> entries;

	size_t pos = 10;
	bool truncated = false;
	auto has = [&](size_t count) {
		if(pos + count > data.size()) {
			truncated = true;
			return false;
		}
		return true;
	};
	auto read8 = [&](size_t offset) -> uint32_t { return data[pos + offset]; };
	auto read16 = [&](size_t offset) -> uint32_t { return data[pos + offset] | (data[pos + offset + 1] << 8); };
	auto read24 = [&](size_t offset) -> uint32_t { return read16(offset) | (data[pos + offset + 2] << 16); };
	auto read32 = [&](size_t offset) -> uint32_t { return read24(offset) | (data[pos + offset + 3] << 24); };
	auto addBlock = [&](ZxTapeBlock&& block) {
		entries.push_back({ EntryType::Block, fileBlocks.size() });
		fileBlocks.push_back(std::move(block));
	};
	auto getData = [&](size_t offset, size_t length) {
		return vector<uint8_t>(data.begin() + pos + offset, data.begin() + pos + offset + length);
	};

	while(pos < data.size() && !truncated) {
		uint8_t id = data[pos++];
		size_t length = 0;
		switch(id) {
			case 0x10: {
				//Standard speed data
				if(!has(4) || !has(4 + read16(2))) { break; }
				length = 4 + read16(2);
				addBlock(GetStandardBlock(getData(4, read16(2)), read16(0)));
				break;
			}

			case 0x11: {
				//Turbo speed data
				if(!has(18) || !has(18 + read24(15))) { break; }
				length = 18 + read24(15);
				ZxTapeBlock block;
				block.PilotLength = read16(0);
				block.Sync1 = read16(2);
				block.Sync2 = read16(4);
				block.ZeroLength = read16(6);
				block.OneLength = read16(8);
				block.PilotCount = read16(10);
				block.UsedBits = read8(12);
				block.PauseMs = read16(13);
				block.Data = getData(18, read24(15));
				addBlock(std::move(block));
				break;
			}

			case 0x12: {
				//Pure tone
				if(!has(4)) { break; }
				length = 4;
				ZxTapeBlock block;
				block.Type = ZxTapeBlockType::Tone;
				block.PilotLength = read16(0);
				block.PilotCount = read16(2);
				addBlock(std::move(block));
				break;
			}

			case 0x13: {
				//Pulse sequence
				if(!has(1) || !has(1 + read8(0) * 2)) { break; }
				length = 1 + read8(0) * 2;
				ZxTapeBlock block;
				block.Type = ZxTapeBlockType::Pulses;
				for(uint32_t i = 0; i < read8(0); i++) {
					block.PulseLengths.push_back(read16(1 + i * 2));
				}
				addBlock(std::move(block));
				break;
			}

			case 0x14: {
				//Pure data (no pilot/sync)
				if(!has(10) || !has(10 + read24(7))) { break; }
				length = 10 + read24(7);
				ZxTapeBlock block;
				block.ZeroLength = read16(0);
				block.OneLength = read16(2);
				block.UsedBits = read8(4);
				block.PauseMs = read16(5);
				block.Data = getData(10, read24(7));
				addBlock(std::move(block));
				break;
			}

			case 0x15: {
				//Direct recording
				if(!has(8) || !has(8 + read24(5))) { break; }
				length = 8 + read24(5);
				ZxTapeBlock block;
				block.Type = ZxTapeBlockType::DirectRecording;
				block.SampleLength = read16(0);
				block.PauseMs = read16(2);
				block.UsedBits = read8(4);
				block.Data = getData(8, read24(5));
				addBlock(std::move(block));
				break;
			}

			case 0x20: {
				//Pause, or "stop the tape" if 0
				if(!has(2)) { break; }
				length = 2;
				ZxTapeBlock block;
				block.Type = read16(0) ? ZxTapeBlockType::Pause : ZxTapeBlockType::Stop;
				block.PauseMs = read16(0);
				addBlock(std::move(block));
				break;
			}

			case 0x21: if(has(1)) { length = 1 + read8(0); entries.push_back({ EntryType::Info }); } break; //Group start
			case 0x22: entries.push_back({ EntryType::Info }); break; //Group end

			case 0x23:
				//Jump (relative to this block)
				if(has(2)) {
					length = 2;
					entries.push_back({ EntryType::Jump, 0, (int16_t)read16(0) });
				}
				break;

			case 0x24:
				//Loop start
				if(has(2)) {
					length = 2;
					entries.push_back({ EntryType::LoopStart, 0, (int32_t)read16(0) });
				}
				break;

			case 0x25: entries.push_back({ EntryType::LoopEnd }); break;

			case 0x26: {
				//Call sequence (offsets relative to this block)
				if(!has(2) || !has(2 + read16(0) * 2)) { break; }
				length = 2 + read16(0) * 2;
				Entry entry = { EntryType::Call };
				for(uint32_t i = 0; i < read16(0); i++) {
					entry.Offsets.push_back((int16_t)read16(2 + i * 2));
				}
				entries.push_back(std::move(entry));
				break;
			}

			case 0x27: entries.push_back({ EntryType::Return }); break;

			case 0x28: if(has(2)) { length = 2 + read16(0); entries.push_back({ EntryType::Info }); } break; //Select block (ignored - the tape plays on)

			case 0x2A: {
				//Stop the tape if in 48K mode
				if(!has(4)) { break; }
				length = 4 + read32(0);
				ZxTapeBlock block;
				block.Type = ZxTapeBlockType::Stop48K;
				addBlock(std::move(block));
				break;
			}

			case 0x2B: {
				//Set signal level
				if(!has(5)) { break; }
				length = 4 + read32(0);
				ZxTapeBlock block;
				block.Type = ZxTapeBlockType::SetLevel;
				block.Level = read8(4) != 0;
				addBlock(std::move(block));
				break;
			}

			//Information blocks
			case 0x30: if(has(1)) { length = 1 + read8(0); entries.push_back({ EntryType::Info }); } break; //Text description
			case 0x31: if(has(2)) { length = 2 + read8(1); entries.push_back({ EntryType::Info }); } break; //Message
			case 0x32: if(has(2)) { length = 2 + read16(0); entries.push_back({ EntryType::Info }); } break; //Archive info
			case 0x33: if(has(1)) { length = 1 + read8(0) * 3; entries.push_back({ EntryType::Info }); } break; //Hardware type
			case 0x34: length = 8; entries.push_back({ EntryType::Info }); break; //Emulation info (deprecated)
			case 0x35: if(has(20)) { length = 20 + read32(16); entries.push_back({ EntryType::Info }); } break; //Custom info
			case 0x40: if(has(4)) { length = 4 + read24(1); entries.push_back({ EntryType::Info }); } break; //Snapshot (deprecated)
			case 0x5A: length = 9; entries.push_back({ EntryType::Info }); break; //Glue (concatenated files)

			default:
				//TODOZX: 0x18 (CSW recording) and 0x19 (generalized data) aren't supported - they are skipped, like any
				//other block whose length follows its ID (the TZX rule for unknown blocks)
				if(has(4)) {
					length = 4 + read32(0);
					entries.push_back({ EntryType::Info });
				}
				break;
		}

		if(!truncated && !has(length)) {
			break;
		}
		pos += length;
	}

	if(truncated) {
		error = "Invalid .tzx file (truncated block)";
		return false;
	}

	//Second pass: run the control blocks to get the sequence of blocks to play
	struct Loop { size_t Start; int32_t Remaining; };
	struct Call { size_t CallEntry; size_t Next; };
	vector<Loop> loops;
	vector<Call> calls;
	vector<ZxTapeBlock> blocks;
	size_t i = 0;
	//Guards against endless jumps/loops in broken files
	uint32_t steps = 0;
	while(i < entries.size() && steps++ < 1000000) {
		Entry& entry = entries[i];
		switch(entry.Type) {
			case EntryType::Block:
				blocks.push_back(fileBlocks[entry.BlockIndex]);
				i++;
				break;

			case EntryType::LoopStart:
				loops.push_back({ i + 1, std::max(entry.Value, 1) });
				i++;
				break;

			case EntryType::LoopEnd:
				if(!loops.empty() && --loops.back().Remaining > 0) {
					i = loops.back().Start;
				} else {
					if(!loops.empty()) {
						loops.pop_back();
					}
					i++;
				}
				break;

			case EntryType::Jump:
				i = entry.Value ? (size_t)((int64_t)i + entry.Value) : i + 1;
				break;

			case EntryType::Call:
				if(entry.Offsets.empty()) {
					i++;
				} else {
					calls.push_back({ i, 1 });
					i = (size_t)((int64_t)i + entry.Offsets[0]);
				}
				break;

			case EntryType::Return:
				if(calls.empty()) {
					i++;
				} else {
					Call& call = calls.back();
					Entry& callEntry = entries[call.CallEntry];
					if(call.Next < callEntry.Offsets.size()) {
						i = (size_t)((int64_t)call.CallEntry + callEntry.Offsets[call.Next++]);
					} else {
						i = call.CallEntry + 1;
						calls.pop_back();
					}
				}
				break;

			case EntryType::Info:
				i++;
				break;
		}
	}

	if(blocks.empty()) {
		error = "Invalid .tzx file (no blocks)";
		return false;
	}

	_blocks = std::move(blocks);
	return true;
}

void ZxTapeImage::GetPulses(const ZxTapeBlock& block, bool& level, uint32_t clockRate, vector<ZxTapePulse>& pulses)
{
	pulses.clear();
	auto scale = [=](uint32_t length) { return (uint32_t)((uint64_t)length * clockRate / 3500000); };

	//Each pulse starts with an edge
	auto addEdge = [&](uint32_t length) {
		level = !level;
		pulses.push_back({ scale(length), level });
	};

	auto addPause = [&](uint32_t pauseMs) {
		if(pauseMs == 0) {
			return;
		}
		//An edge ends the last pulse. The level is then held for 1ms and goes low for the rest of the pause.
		uint32_t length = pauseMs * 3500;
		level = !level;
		if(level) {
			pulses.push_back({ scale(3500), true });
			length -= 3500;
			level = false;
		}
		if(length > 0) {
			pulses.push_back({ scale(length), false });
		}
	};

	switch(block.Type) {
		case ZxTapeBlockType::Data: {
			for(uint32_t i = 0; i < block.PilotCount; i++) {
				addEdge(block.PilotLength);
			}
			if(block.Sync1) {
				addEdge(block.Sync1);
			}
			if(block.Sync2) {
				addEdge(block.Sync2);
			}
			for(size_t i = 0; i < block.Data.size(); i++) {
				uint8_t bitCount = (i == block.Data.size() - 1 && block.UsedBits >= 1 && block.UsedBits <= 8) ? block.UsedBits : 8;
				for(uint8_t bit = 0; bit < bitCount; bit++) {
					uint16_t length = (block.Data[i] & (0x80 >> bit)) ? block.OneLength : block.ZeroLength;
					addEdge(length);
					addEdge(length);
				}
			}
			addPause(block.PauseMs);
			break;
		}

		case ZxTapeBlockType::Tone:
			for(uint32_t i = 0; i < block.PilotCount; i++) {
				addEdge(block.PilotLength);
			}
			break;

		case ZxTapeBlockType::Pulses:
			for(uint16_t length : block.PulseLengths) {
				addEdge(length);
			}
			break;

		case ZxTapeBlockType::DirectRecording: {
			//One sample per bit - consecutive samples at the same level make a single pulse
			size_t first = pulses.size();
			for(size_t i = 0; i < block.Data.size(); i++) {
				uint8_t bitCount = (i == block.Data.size() - 1 && block.UsedBits >= 1 && block.UsedBits <= 8) ? block.UsedBits : 8;
				for(uint8_t bit = 0; bit < bitCount; bit++) {
					bool sample = (block.Data[i] & (0x80 >> bit)) != 0;
					if(pulses.size() > first && pulses.back().Level == sample) {
						pulses.back().Length += scale(block.SampleLength);
					} else {
						pulses.push_back({ scale(block.SampleLength), sample });
					}
					level = sample;
				}
			}
			addPause(block.PauseMs);
			break;
		}

		case ZxTapeBlockType::Pause:
			addPause(block.PauseMs);
			break;

		case ZxTapeBlockType::SetLevel:
			level = block.Level;
			break;

		case ZxTapeBlockType::Stop:
		case ZxTapeBlockType::Stop48K:
			break;
	}
}
