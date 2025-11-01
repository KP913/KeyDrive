#include "output_handler.hpp"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <csignal>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <iomanip>

namespace keydrive {

	namespace {

		std::string utf32ToUtf8(char32_t c) {
			std::string result;
			if (c <= 0x7F) {
				result.push_back(static_cast<char>(c));
			} else if (c <= 0x7FF) {
				result.push_back(static_cast<char>(0xC0 | ((c >> 6) & 0x1F)));
				result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
			} else if (c <= 0xFFFF) {
				result.push_back(static_cast<char>(0xE0 | ((c >> 12) & 0x0F)));
				result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
				result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
			} else if (c <= 0x10FFFF) {
				result.push_back(static_cast<char>(0xF0 | ((c >> 18) & 0x07)));
				result.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
				result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
				result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
			}
			return result;
		}

		std::pair<int, std::string> executeCommand(const std::string& command, int timeoutMs = 200) {
			int pipefd[2];
			if (pipe(pipefd) == -1) {
				return {-1, "Failed to create pipe"};
			}

			pid_t pid = fork();
			if (pid == -1) {
				close(pipefd[0]);
				close(pipefd[1]);
				return {-1, "Failed to fork"};
			}

			if (pid == 0) {
				close(pipefd[0]);
				dup2(pipefd[1], STDOUT_FILENO);
				dup2(pipefd[1], STDERR_FILENO);
				close(pipefd[1]);

				sigset_t mask;
				sigemptyset(&mask);
				sigaddset(&mask, SIGTERM);
				sigprocmask(SIG_UNBLOCK, &mask, nullptr);

				std::vector<char*> args;
				std::istringstream iss(command);
				std::string token;
				while (iss >> token) {
					args.push_back(strdup(token.c_str()));
				}
				args.push_back(nullptr);

				execvp(args[0], args.data());
				_exit(EXIT_FAILURE);
			} else {
				close(pipefd[1]);

				auto start = std::chrono::steady_clock::now();
				std::string output;
				char buffer[128];
				ssize_t bytesRead;

				while ((bytesRead = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
					buffer[bytesRead] = '\0';
					output += buffer;

					auto now = std::chrono::steady_clock::now();
					auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
					if (elapsed.count() > timeoutMs) {
						kill(pid, SIGTERM);
						waitpid(pid, nullptr, 0);
						close(pipefd[0]);
						return {-1, "Command timed out"};
					}
				}

				close(pipefd[0]);

				int status;
				waitpid(pid, &status, 0);

				if (WIFEXITED(status)) {
					return {WEXITSTATUS(status), output};
				} else {
					return {-1, "Command terminated abnormally"};
				}
			}
		}

		bool fileExists(const std::string& path) {
			struct stat buffer;
			return stat(path.c_str(), &buffer) == 0;
		}

	} // anonymous namespace

	// Define the symbol map
	constexpr OutputHandler::SymbolMapping OutputHandler::symbolMap[];

	OutputHandler::OutputHandler() {
		// 1. Create the libevdev device (configuration object)
		dev = libevdev_new();
		if (!dev) {
			throw std::runtime_error("Failed to create libevdev device");
		}

		libevdev_set_name(dev, "Keyforge Virtual Keyboard");

		// 2. Configure the device with the events we want to support
		for (unsigned int i = 0; i < KEY_MAX; ++i) {
			libevdev_enable_event_code(dev, EV_KEY, i, nullptr);
		}

		// Enable MSC_SCAN for Unicode input
		libevdev_enable_event_code(dev, EV_MSC, MSC_SCAN, nullptr);

		// 3. Create the uinput device from the configured libevdev device
		if (libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &virtKb) < 0) {
			libevdev_free(dev);
			throw std::runtime_error("Failed to create uinput device");
		}

		std::cout << "✅ Output handler initialized" << std::endl;

		// 4. Verify MSC_SCAN works using the ORIGINAL dev object
		if (libevdev_has_event_code(dev, EV_MSC, MSC_SCAN)) {
			try {
				const char32_t testChar = U'✓';
				std::string utf8 = utf32ToUtf8(testChar);

				bool testSucceeded = true;
				for (char c : utf8) {
					if (libevdev_uinput_write_event(virtKb, EV_MSC, MSC_SCAN,
						static_cast<unsigned char>(c)) < 0) {
						testSucceeded = false;
					break;
						}
				}

				if (testSucceeded) {
					syncEvent();

					std::cout << "✅ Verified MSC_SCAN support (sent ✓ as";
					for (unsigned char b : utf8) {
						std::cout << " " << std::hex << std::setw(2) << std::setfill('0')
						<< static_cast<int>(b);
					}
					std::cout << ")" << std::endl;
				} else {
					std::cout << "⚠ MSC_SCAN is present but failed to send test character" << std::endl;
				}
			} catch (const std::exception& e) {
				std::cout << "⚠ MSC_SCAN verification failed: " << e.what() << std::endl;
			}
		} else {
			std::cout << "⚠ MSC_SCAN not supported by this system" << std::endl;
		}
	}

	OutputHandler::~OutputHandler() {
		// 5. Clean up in the correct order
		if (virtKb) {
			libevdev_uinput_destroy(virtKb);
		}
		if (dev) {
			libevdev_free(dev);
		}
		std::cout << "🧹 Output handler cleaned up" << std::endl;
	}

	void OutputHandler::syncEvent() {
		libevdev_uinput_write_event(virtKb, EV_SYN, SYN_REPORT, 0);
	}

	bool OutputHandler::sendUnicode(char32_t character) {
		WindowInfo windowInfo = getActiveWindowInfo();
		//bool isTerminal = windowInfo.isTerminal;
		int isElectron = windowInfo.isElectron;
		//std::cout << "electron: "<<isElectron<<"\n";
		std::cout << "Hell yeah! Send in the " << utf32ToUtf8(character) << "\n";
		if (isControlChar(character)) {
			sendControlChar(character);
			return true;
		}

		if (isElectron && hasXdotool()) {
			return sendUnicodeXdotool(character);
		} else {//printf("wtype\n");
			//if (hasWtype()) {
				return sendUnicodeWtype(character);
			//}
		}

		std::cerr << "❌ Failed to send character: '";
		if (character >= 32 && character < 127) {
			std::cerr << static_cast<char>(character);
		} else {
			std::cerr << "U+" << std::hex << static_cast<int>(character);
		}
		std::cerr << "' (U+" << std::hex << static_cast<int>(character) << ")" << std::endl;

		return false;
	}

	void OutputHandler::forwardEvent(unsigned int code, int value) {
		if (virtKb) {
			std::cout << "Forwarding " << code << "!!!!\n";
			libevdev_uinput_write_event(virtKb, EV_KEY, code, value);
			syncEvent();

			std::this_thread::sleep_for(std::chrono::microseconds(100));
		}
	}

	void OutputHandler::releaseAllModifiers() {
		const unsigned int modifiers[] = {
			KEY_LEFTCTRL, KEY_RIGHTCTRL,
			KEY_LEFTSHIFT, KEY_RIGHTSHIFT,
			KEY_LEFTALT, KEY_RIGHTALT,
			KEY_LEFTMETA, KEY_RIGHTMETA
		};

		for (unsigned int mod : modifiers) {
			libevdev_uinput_write_event(virtKb, EV_KEY, mod, 0);
		}
		syncEvent();
	}

	WindowInfo OutputHandler::getActiveWindowInfo() const {
		WindowInfo info;
		info.isElectron = 0;
		try {
			auto [exitCode, output] = executeCommand("hyprctl activewindow -j", 200);

			if (exitCode == 0 && !output.empty()) {
				auto json = nlohmann::json::parse(output, nullptr, false);
				if (!json.is_discarded()) {
					info.title = json.value("title", "");
					info.windowClass = json.value("class", "");
					std::transform(info.windowClass.begin(), info.windowClass.end(),
								   info.windowClass.begin(), ::tolower);

					static const std::vector<std::string> electronApps = {
						"code", "discord", "slack", "vscodium", "codium", "godot"
					};

					for (const auto& app : electronApps) {
						if (info.windowClass.find(app) != std::string::npos) {
							info.isElectron = 1;
							break;
						}
					}

					static const std::vector<std::string> terminalApps = {
						"terminal", "alacritty", "kitty", "foot", "konsole", "org.kde.konsole"
					};

					for (const auto& term : terminalApps) {
						if (info.windowClass.find(term) != std::string::npos) {
							info.isTerminal = true;
							break;
						}
					}

					return info;
				}
			}
		} catch (const std::exception& e) {
			std::cerr << "⚠ Window detection failed: " << e.what() << std::endl;
		}

		info.isElectron = 0;
		info.isTerminal = (std::getenv("TERM") != nullptr);

		return info;
	}

	bool OutputHandler::hasWtype() const {
		return fileExists("/usr/bin/wtype") || fileExists("/usr/local/bin/wtype");
	}

	bool OutputHandler::hasXdotool() const {
		return fileExists("/usr/bin/xdotool") || fileExists("/usr/local/bin/xdotool");
	}

	bool OutputHandler::isControlChar(char32_t c) const {
		return (c == U'\n' || c == U' ' || c == U'\b' || c == U'\t' || c == U'\x1b');
	}

	void OutputHandler::sendControlChar(char32_t c) {
		unsigned int key = 0;

		switch (c) {
			case U'\n': key = KEY_ENTER; break;
			case U' ': key = KEY_SPACE; break;
			case U'\b': key = KEY_BACKSPACE; break;
			case U'\t': key = KEY_TAB; break;
			case U'\x1b': key = KEY_ESC; break;
			default: return;
		}

		libevdev_uinput_write_event(virtKb, EV_KEY, key, 1);
		libevdev_uinput_write_event(virtKb, EV_KEY, key, 0);
		syncEvent();

		std::cout << "→ CONTROL: " << std::string(1, static_cast<char>(c)) << std::endl;
	}

	bool OutputHandler::sendUnicodeWtype(char32_t c) {
		std::string charStr = utf32ToUtf8(c);
		auto [exitCode2, output] = executeCommand("wtype -- " + charStr);
		if (exitCode2 == 0) {
			std::cout << "→ WTYPE: " << std::hex << charStr << std::endl;
			return true;
		}
		printf("wtype failed\n");
		return false;

		/*std::string charStr;
		if (c < 128) {
			charStr = std::string(1, static_cast<char>(c));
		} else {
			charStr = utf32ToUtf8(c);
		}

		auto [exitCode, output] = executeCommand("wtype -- '" + charStr + "'", 200);

		if (exitCode == 0) {
			std::cout << "→ WTYPE (UNICODE): '";
			if (c < 128) {
				std::cout << static_cast<char>(c);
			} else {
				std::cout << "U+" << std::hex << static_cast<int>(c);
			}
			std::cout << "' (U+" << std::hex << static_cast<int>(c) << ")" << std::endl;
			return true;
		}

		std::cerr << "⚠ WTYPE (UNICODE) failed: ";
		if (c < 128) {
			std::cerr << static_cast<char>(c);
		} else {
			std::cerr << "U+" << std::hex << static_cast<int>(c);
		}
		std::cerr << " → " << output << std::endl;

		return false;*/
	}

	bool OutputHandler::sendUnicodeXdotool(char32_t c) {
		executeCommand("setxkbmap");

		std::string charStr = utf32ToUtf8(c);
		std::cout << "xdotool type --clearmodifiers " << charStr << "\n";
		auto [exitCode2, output] = executeCommand("xdotool type --clearmodifiers " + charStr);
		if (exitCode2 == 0) {
			std::cout << "→ XDOTOOL: " << std::hex << charStr << std::endl;
			return true;
		}
		printf("xdotool failed\n");
		return false;
		/*std::string charStr;
		if (c < 128) {
			charStr = std::string(1, static_cast<char>(c));
		} else {
			charStr = utf32ToUtf8(c);
		}

		auto [exitCode, output] = executeCommand("setxkbmap");

		if (c < 128) {
			char ch = static_cast<char>(c);
			if (const char* symbolName = getSymbolName(ch)) {
				auto [exitCode, output] = executeCommand("xdotool key --clearmodifiers " + std::string(symbolName), 200);
				if (exitCode == 0) {
					std::cout << "→ XDOTOOL (SYMBOL): '" << ch << "' (U+" << std::hex << static_cast<int>(c) << ")" << std::endl;
					return true;
				}
			}
		}

		auto [exitCode, output] = executeCommand("xdotool type --clearmodifiers '" + charStr + "'", 200);

		if (exitCode == 0) {
			std::cout << "→ XDOTOOL (UNICODE): '";
			if (c < 128) {
				std::cout << static_cast<char>(c);
			} else {
				std::cout << "U+" << std::hex << static_cast<int>(c);
			}
			std::cout << "' (U+" << std::hex << static_cast<int>(c) << ")" << std::endl;
			return true;
		}

		std::cerr << "⚠ XDOTOOL (UNICODE) failed: ";
		if (c < 128) {
			std::cerr << static_cast<char>(c);
		} else {
			std::cerr << "U+" << std::hex << static_cast<int>(c);
		}
		std::cerr << " → " << output << std::endl;

		return false;*/
	}

	const char* OutputHandler::getSymbolName(char c) const {
		for (const auto& mapping : symbolMap) {
			if (mapping.character == c) {
				return mapping.xdotoolName;
			}
		}
		return nullptr;
	}

} // namespace keydrive
