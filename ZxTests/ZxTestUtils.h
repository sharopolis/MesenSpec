#pragma once
//Helpers shared by the headless Spectrum tools: file loading, typing on the keyboard, reading the screen as text
#include "pch.h"
#include <fstream>
#include "ZX/ZxMachine.h"

namespace ZxTestUtils
{
	inline bool ReadFile(const string& path, vector<uint8_t>& data)
	{
		std::ifstream f(path, std::ios::binary);
		if(!f) {
			return false;
		}
		data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		return true;
	}

	//Types text on the keyboard, one keystroke every 8 frames (held for 4).
	//Letters, digits and spaces are typed as-is, '\n' or '|' is ENTER, {NAME} presses a key by name (see
	//ZxKeyboard::KeyNames, e.g. {CAPS}, {SYMBOL}) and {NAME+NAME} presses keys together (e.g. {SYMBOL+P} = ")
	class KeyTyper
	{
	private:
		struct ScheduledKey
		{
			int Frame;
			vector<string> Keys;
			bool Press;
		};
		vector<ScheduledKey> _schedule;

	public:
		void Type(int startFrame, const string& text)
		{
			int frame = startFrame;
			for(size_t i = 0; i < text.size(); i++) {
				char c = text[i];
				vector<string> keys;
				if(c == '{') {
					size_t end = text.find('}', i);
					string names = text.substr(i + 1, end - i - 1);
					size_t pos = 0;
					while(pos <= names.size()) {
						size_t plus = names.find('+', pos);
						if(plus == string::npos) {
							plus = names.size();
						}
						keys.push_back(names.substr(pos, plus - pos));
						pos = plus + 1;
					}
					i = end;
				} else if(c == '\n' || c == '|') {
					keys.push_back("ENTER");
				} else if(c == ' ') {
					keys.push_back("SPACE");
				} else {
					keys.push_back(string(1, (char)toupper(c)));
				}
				_schedule.push_back({ frame, keys, true });
				_schedule.push_back({ frame + 4, keys, false });
				frame += 8;
			}
		}

		//Call before running each frame. Returns false (and reports it) if a key name is unknown.
		bool Update(ZxMachine& zx, int frame)
		{
			for(ScheduledKey& k : _schedule) {
				if(k.Frame != frame) {
					continue;
				}
				for(string& name : k.Keys) {
					if(!zx.GetKeyboard().SetKeyState(name, k.Press)) {
						fprintf(stderr, "Unknown key: %s\n", name.c_str());
						return false;
					}
				}
			}
			return true;
		}
	};

	//True if the character cell holds the given character drawn with the ROM font
	inline bool IsCharAt(ZxMachine& zx, uint8_t row, uint8_t col, int ch)
	{
		ZxMemoryManager& mm = zx.GetMemoryManager();
		for(int line = 0; line < 8; line++) {
			uint16_t screenAddr = 0x4000 + ((row & 0x18) << 8) + ((row & 0x07) << 5) + (line << 8) + col;
			if(mm.Peek(screenAddr) != mm.Peek(0x3D00 + (ch - 32) * 8 + line)) {
				return false;
			}
		}
		return true;
	}

	//True if text is printed at the start of the given character row
	inline bool IsTextOnScreen(ZxMachine& zx, uint8_t row, const string& text)
	{
		for(size_t i = 0; i < text.size(); i++) {
			if(!IsCharAt(zx, row, (uint8_t)i, (uint8_t)text[i])) {
				return false;
			}
		}
		return true;
	}

	//Screen contents as text, recognizing characters drawn with the ROM font ('.' for anything else, 0x7F is the (C) sign)
	inline string GetScreenText(ZxMachine& zx)
	{
		string text;
		for(uint8_t row = 0; row < 24; row++) {
			string line;
			for(uint8_t col = 0; col < 32; col++) {
				char found = '.';
				for(int ch = 32; ch < 128; ch++) {
					if(IsCharAt(zx, row, col, ch)) {
						found = (char)ch;
						break;
					}
				}
				line += found;
			}
			line.erase(line.find_last_not_of(' ') + 1);
			text += line + "\n";
		}
		return text;
	}
}
