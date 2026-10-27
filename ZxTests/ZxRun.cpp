//Headless Spectrum runner: boots the ROM (optionally loads a snapshot or tape), runs a number of frames, can type on
//the keyboard, and saves the final picture as a PNG, the audio as a WAV file, and/or the state as a snapshot.
//
//Usage: ZxRun --rom 48.rom [options]
//  --rom FILE          can be given twice (e.g. 128-0.rom and 128-1.rom) - the files are concatenated, and a 32K ROM
//                      selects the 128K model
//  --load FILE         load a .sna/.z80 snapshot (after letting the ROM initialize for 100 frames)
//  --tape FILE         insert a .tap file (loaded instantly when the ROM's LOAD routine runs - each block loaded and
//                      the end of the tape are logged)
//  --frames N          number of frames to run (default 150)
//  --type FRAME TEXT   type TEXT starting at FRAME (see ZxTestUtils::KeyTyper for the syntax), can be repeated
//  --auto-scroll       answer the ROM's "scroll?" prompt (useful for programs that print long reports)
//  --text              print the screen as text (recognized with the ROM font), and each page before it is scrolled
//  --png FILE          save the last frame
//  --wav FILE          save the audio (44.1kHz mono)
//  --save FILE         save a .sna/.z80 snapshot at the end
#include "pch.h"
#include <fstream>
#include <algorithm>
#include "ZX/ZxMachine.h"
#include "Utilities/PNGHelper.h"
#include "ZxTestUtils.h"

using namespace ZxTestUtils;

static void WriteWav(const string& path, const vector<int16_t>& samples)
{
	std::ofstream f(path, std::ios::binary);
	auto write32 = [&](uint32_t v) { f.write((char*)&v, 4); };
	auto write16 = [&](uint16_t v) { f.write((char*)&v, 2); };
	uint32_t dataSize = (uint32_t)samples.size() * 2;
	f.write("RIFF", 4); write32(36 + dataSize); f.write("WAVE", 4);
	f.write("fmt ", 4); write32(16); write16(1); write16(1); write32(44100); write32(44100 * 2); write16(2); write16(16);
	f.write("data", 4); write32(dataSize);
	f.write((char*)samples.data(), dataSize);
}

static string GetExtension(const string& path)
{
	size_t dot = path.find_last_of('.');
	string ext = dot == string::npos ? "" : path.substr(dot);
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	return ext;
}

int main(int argc, char** argv)
{
	vector<string> romPaths;
	string loadPath, tapePath, pngPath, wavPath, savePath;
	int frames = 150;
	bool autoScroll = false;
	bool printText = false;
	KeyTyper typer;

	for(int i = 1; i < argc; i++) {
		string arg = argv[i];
		if(arg == "--rom" && i + 1 < argc) {
			romPaths.push_back(argv[++i]);
		} else if(arg == "--load" && i + 1 < argc) {
			loadPath = argv[++i];
		} else if(arg == "--tape" && i + 1 < argc) {
			tapePath = argv[++i];
		} else if(arg == "--frames" && i + 1 < argc) {
			frames = std::stoi(argv[++i]);
		} else if(arg == "--text") {
			printText = true;
		} else if(arg == "--auto-scroll") {
			autoScroll = true;
		} else if(arg == "--png" && i + 1 < argc) {
			pngPath = argv[++i];
		} else if(arg == "--wav" && i + 1 < argc) {
			wavPath = argv[++i];
		} else if(arg == "--save" && i + 1 < argc) {
			savePath = argv[++i];
		} else if(arg == "--type" && i + 2 < argc) {
			int frame = std::stoi(argv[++i]);
			typer.Type(frame, argv[++i]);
		} else {
			fprintf(stderr, "Unknown or incomplete argument: %s\n", arg.c_str());
			return 2;
		}
	}

	if(romPaths.empty()) {
		fprintf(stderr, "Usage: %s --rom 48.rom [--load file.sna|file.z80] [--tape file.tap] [--frames N] [--type FRAME TEXT]... [--auto-scroll] [--text] [--png out.png] [--wav out.wav] [--save out.sna|out.z80]\n", argv[0]);
		return 2;
	}

	vector<uint8_t> rom;
	for(string& romPath : romPaths) {
		vector<uint8_t> data;
		if(!ReadFile(romPath, data)) {
			fprintf(stderr, "Could not read ROM: %s\n", romPath.c_str());
			return 1;
		}
		rom.insert(rom.end(), data.begin(), data.end());
	}

	unique_ptr<ZxMachine> zx(new ZxMachine(rom.size() == 0x8000 ? ZxModel::Spectrum128K : ZxModel::Spectrum48K));
	if(!zx->LoadRom(rom)) {
		fprintf(stderr, "Invalid ROM size (%d bytes, expected 16K for the 48K or 32K for the 128K)\n", (int)rom.size());
		return 1;
	}

	if(!loadPath.empty()) {
		//Let the ROM initialize the machine first, the snapshot then replaces the RAM and CPU state
		for(int i = 0; i < 100; i++) {
			zx->RunFrame();
		}
		vector<uint8_t> data;
		string error;
		if(!ReadFile(loadPath, data) || !zx->LoadSnapshot(data, GetExtension(loadPath), error)) {
			fprintf(stderr, "Could not load %s: %s\n", loadPath.c_str(), error.c_str());
			return 1;
		}
	}

	if(!tapePath.empty()) {
		vector<uint8_t> data;
		string error;
		if(!ReadFile(tapePath, data) || !zx->LoadTape(data, error)) {
			fprintf(stderr, "Could not load %s: %s\n", tapePath.c_str(), error.c_str());
			return 1;
		}
	}

	vector<int16_t> audio;
	int scrollKeyRelease = -1;
	int scrollCount = 0;
	for(int frame = 0; frame < frames; frame++) {
		if(autoScroll) {
			if(scrollKeyRelease < 0 && IsTextOnScreen(*zx, 23, "scroll?")) {
				if(printText) {
					printf("%s", GetScreenText(*zx).c_str());
				}
				zx->GetKeyboard().SetKeyState("ENTER", true);
				scrollKeyRelease = frame + 4;
				scrollCount++;
			} else if(frame == scrollKeyRelease) {
				zx->GetKeyboard().SetKeyState("ENTER", false);
			} else if(scrollKeyRelease >= 0 && frame > scrollKeyRelease + 4) {
				scrollKeyRelease = -1;
			}
		}

		if(!typer.Update(*zx, frame)) {
			return 2;
		}
		size_t tapeBlock = zx->GetTape().GetNextBlock();
		zx->RunFrame();
		if(zx->GetTape().GetNextBlock() != tapeBlock) {
			fprintf(stderr, "Frame %d: tape at block %d/%d\n", frame, (int)zx->GetTape().GetNextBlock(), (int)zx->GetTape().GetBlockCount());
		}
		if(zx->GetTape().TakeEndReached()) {
			fprintf(stderr, "Frame %d: end of tape (PC=%04X)\n", frame, zx->GetCpu().GetState().PC);
		}
		const vector<int16_t>& samples = zx->GetBeeper().GetSamples();
		if(zx->HasAy()) {
			//Beeper + AY - both produce the same number of samples per frame
			const vector<int16_t>& aySamples = zx->GetAy().GetSamples();
			for(size_t i = 0; i < samples.size(); i++) {
				int32_t mixed = samples[i] + (i < aySamples.size() ? aySamples[i] : 0);
				audio.push_back((int16_t)std::clamp<int32_t>(mixed, INT16_MIN, INT16_MAX));
			}
		} else {
			audio.insert(audio.end(), samples.begin(), samples.end());
		}
	}

	ZxCpuState& s = zx->GetCpu().GetState();
	if(printText) {
		printf("%s", GetScreenText(*zx).c_str());
	}
	if(autoScroll) {
		printf("Answered scroll? %d times\n", scrollCount);
	}
	printf("Ran %d frames, %llu T-states. PC=%04X SP=%04X IM=%d IFF1=%d\n", frames, (unsigned long long)s.CycleCount, s.PC, s.SP, s.IM, s.IFF1);

	if(!pngPath.empty()) {
		vector<uint32_t> rgb(ZxConstants::PixelCount);
		uint16_t* fb = zx->GetFrameBuffer();
		for(uint32_t i = 0; i < ZxConstants::PixelCount; i++) {
			rgb[i] = ZxMachine::GetPaletteColor(fb[i]);
		}
		if(!PNGHelper::WritePNG(pngPath, rgb.data(), ZxConstants::ScreenWidth, ZxConstants::ScreenHeight, 24)) {
			fprintf(stderr, "Could not write %s\n", pngPath.c_str());
			return 1;
		}
	}
	if(!wavPath.empty()) {
		WriteWav(wavPath, audio);
	}
	if(!savePath.empty()) {
		vector<uint8_t> data = zx->SaveSnapshot(GetExtension(savePath));
		std::ofstream f(savePath, std::ios::binary);
		if(data.empty() || !f.write((char*)data.data(), data.size())) {
			fprintf(stderr, "Could not save %s\n", savePath.c_str());
			return 1;
		}
	}
	return 0;
}
