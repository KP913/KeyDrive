#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

namespace keydrive {

	struct WindowInfo {
		std::string title;
		std::string windowClass;
		int isElectron;
		int isTerminal;
	};

	class OutputHandler {
	public:
		OutputHandler();
		~OutputHandler();

		bool sendUnicode(char32_t character);
		void forwardEvent(unsigned int code, int value);
		void releaseAllModifiers();
		WindowInfo getActiveWindowInfo() const;
		bool hasWtype() const;
		bool hasXdotool() const;

	private:
		struct SymbolMapping {
			char character;
			const char* xdotoolName;
		};

		// We need to keep BOTH devices:
		// - dev: The libevdev device (for configuration and capability queries)
		// - virtKb: The uinput device (for sending events)
		struct libevdev* dev = nullptr;
		struct libevdev_uinput* virtKb = nullptr;

		static constexpr SymbolMapping symbolMap[] = {
			{',', "comma"},
			{'.', "period"},
			{'-', "minus"},
			{'\'', "apostrophe"},
			{'<', "less"},
			{'>', "greater"},
			{'|', "bar"},
			{'_', "underscore"},
			{'/', "slash"},
			{';', "semicolon"},
			{'[', "bracketleft"},
			{']', "bracketright"},
			{'\\', "backslash"},
			{'`', "grave"},
			{'=', "equal"},
			{'+', "plus"},
			{'*', "asterisk"},
			{'?', "question"},
			{'!', "exclam"},
			{'@', "at"},
			{'#', "numbersign"},
			{'$', "dollar"},
			{'%', "percent"},
			{'^', "asciicircum"},
			{'&', "ampersand"},
			{'(', "parenleft"},
			{')', "parenright"},
			{'{', "braceleft"},
			{'}', "braceright"},
			{':', "colon"},
			{'"', "quotedbl"}
		};

		bool isControlChar(char32_t c) const;
		void sendControlChar(char32_t c);
		bool sendUnicodeWtype(char32_t c);
		bool sendUnicodeXdotool(char32_t c);
		const char* getSymbolName(char c) const;
		void syncEvent();
	};

} // namespace keydrive
