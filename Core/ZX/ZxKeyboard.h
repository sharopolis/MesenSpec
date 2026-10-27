#pragma once
#include "pch.h"
#include "Utilities/StringUtilities.h"

//8x5 key matrix. Reading port 0xFE selects half-rows with the high byte of the port address (a 0 bit selects
//the row), and returns the keys of all selected rows ANDed together (a 0 bit = pressed).
class ZxKeyboard
{
private:
	uint8_t _rows[8] = { 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F };

public:
	//Row order matches the port address bits A8-A15, key order matches data bits D0-D4
	static constexpr const char* KeyNames[8][5] = {
		{ "CAPS", "Z", "X", "C", "V" },
		{ "A", "S", "D", "F", "G" },
		{ "Q", "W", "E", "R", "T" },
		{ "1", "2", "3", "4", "5" },
		{ "0", "9", "8", "7", "6" },
		{ "P", "O", "I", "U", "Y" },
		{ "ENTER", "L", "K", "J", "H" },
		{ "SPACE", "SYMBOL", "M", "N", "B" },
	};

	uint8_t GetRow(int row) { return _rows[row]; }

	void SetKeyState(uint8_t row, uint8_t key, bool pressed)
	{
		if(pressed) {
			_rows[row] &= ~(1 << key);
		} else {
			_rows[row] |= (1 << key);
		}
	}

	//Name as in KeyNames, case-insensitive. Returns false if the name is unknown.
	bool SetKeyState(const string& name, bool pressed)
	{
		string upperName = StringUtilities::ToUpper(name);
		for(uint8_t row = 0; row < 8; row++) {
			for(uint8_t key = 0; key < 5; key++) {
				if(upperName == KeyNames[row][key]) {
					SetKeyState(row, key, pressed);
					return true;
				}
			}
		}
		return false;
	}

	void ReleaseAll()
	{
		for(uint8_t& row : _rows) {
			row = 0x1F;
		}
	}

	uint8_t Read(uint8_t addrHigh)
	{
		uint8_t result = 0x1F;
		for(int i = 0; i < 8; i++) {
			if(!(addrHigh & (1 << i))) {
				result &= _rows[i];
			}
		}
		return result;
	}
};
