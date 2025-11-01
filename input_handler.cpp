#include "input_handler.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <thread>           // ADDED: For std::thread
#include <mutex>            // ADDED: For std::mutex
#include <condition_variable> // ADDED: For std::condition_variable
#include <chrono>           // ADDED: For time-related functionality
#include <vector>           // ADDED: For std::vector
#include <unordered_map>    // ADDED: For std::unordered_map
#include <optional>         // ADDED: For std::optional

namespace keydrive {

    namespace {

        // Physical key codes we consider essential for a keyboard
        constexpr int PHYSICAL_KEYS[] = {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E,
            KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
            KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
            KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
            KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y,
            KEY_Z, KEY_1, KEY_2, KEY_3, KEY_4,
            KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
            KEY_0, KEY_LEFTCTRL, KEY_RIGHTCTRL,
            KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTALT,
            KEY_RIGHTALT, KEY_TAB, KEY_ESC, KEY_BACKSPACE
        };

        // Modifier key mapping
        const std::unordered_map<unsigned int, Modifier> MODIFIER_MAP = {
            {KEY_LEFTSHIFT, Modifier::Shift},
            {KEY_RIGHTSHIFT, Modifier::Shift},
            {KEY_LEFTCTRL, Modifier::Ctrl},
            {KEY_RIGHTCTRL, Modifier::Ctrl},
            {KEY_LEFTALT, Modifier::Alt},
            {KEY_RIGHTALT, Modifier::Alt},
            {KEY_LEFTMETA, Modifier::Super},
            {KEY_RIGHTMETA, Modifier::Super}
        };

        // Emergency exit key combination
        const std::vector<unsigned int> EMERGENCY_EXIT = {
            KEY_LEFTCTRL, KEY_LEFTALT, KEY_ESC
        };

        // Convert key code to name
        std::string keyCodeToName(unsigned int code) {
            const char* name = libevdev_event_code_get_name(EV_KEY, code);
            if (name) {
                std::string result = name;
                std::transform(result.begin(), result.end(), result.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                return result;
            }
            return "key_" + std::to_string(code);
        }

    } // anonymous namespace

    // Implementation details hidden from public interface
    class InputHandlerImpl {
    public:
        InputHandlerImpl() {
            physKb = findPhysicalKeyboard();
            setupInputThread();
            std::cout << "✅ Input handler initialized with " << deviceName << std::endl;
        }

        ~InputHandlerImpl() {
            stopThread = true;
            if (inputThread.joinable()) {
                inputThread.join();
            }

            if (physKb) {
                libevdev_free(physKb);
                close(deviceFd);
            }
        }

        std::optional<InputEvent> getEvent(int timeout_ms) {
            auto start = std::chrono::steady_clock::now();
            std::unique_lock<std::mutex> lock(queueMutex);
            //printf("a\n");
            while (eventQueue.empty()) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);

                if (elapsed.count() >= timeout_ms) {
                    //printf("timeout\n");
                    return std::nullopt;
                }

                cv.wait_for(lock, std::chrono::milliseconds(timeout_ms - elapsed.count()));
                if (stopThread) {
                    //printf("stop\n");
                    return std::nullopt;
                }
            }
            //printf("success\n");
            InputEvent event = std::move(eventQueue.front());
            eventQueue.pop();
            return event;
        }

        bool isModifierActive(Modifier modifier) const {
            return modifiers.at(modifier);
        }

        std::unordered_map<Modifier, bool> getModifierState() const {
            return modifiers;
        }

        std::vector<std::string> getActiveModifiers() const {
            std::vector<std::string> active;
            for (const auto& [modifier, activeState] : modifiers) {
                if (activeState) {
                    switch (modifier) {
                        case Modifier::Shift: active.push_back("shift"); break;
                        case Modifier::Ctrl: active.push_back("ctrl"); break;
                        case Modifier::Alt: active.push_back("alt"); break;
                        case Modifier::Super: active.push_back("super"); break;
                    }
                }
            }
            return active;
        }

    private:
        struct KeyRepeatState {
            int activeKeyCode = -1;
            int count = 0;
            std::chrono::system_clock::time_point lastTime;
            double initialDelay = 0.5;   // 500ms before repeats start
            double repeatDelay = 0.06;   // 50ms between repeats
        };

        void setupInputThread() {
            inputThread = std::thread([this] {
                inputLoop();
            });
        }

        void inputLoop() {
            //printf("inputLoop\n");
            while (!stopThread) {
                processAvailableEvents();
                checkKeyRepeat();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        void processAvailableEvents() {
            //printf("processAvailableEvents\n");
            input_event ev;
            int rc;

            // CORRECT: Use NONBLOCK flag for non-blocking reads
            rc = libevdev_next_event(physKb, LIBEVDEV_READ_FLAG_NORMAL, &ev);
            //if (ev.type != 0) std::cout << ev.type <<"\n";
            while ((rc = libevdev_next_event(physKb, LIBEVDEV_READ_FLAG_NORMAL, &ev)) == 0) {
                //std::cout << ev.type <<"\n";
                if (ev.type == EV_KEY) {
                    processInputEvent(ev);
                }
                // Optional: Handle other event types
                else {
                    //std::cout << "DEBUG: Non-key event - type: " << ev.type
                    //<< ", code: " << ev.code
                    //<< ", value: " << ev.value << std::endl;
                }
            }
            //std::cout << "Error:" <<rc << "\n";
            // Handle errors (except EAGAIN which is normal for non-blocking mode)
            if (rc < 0 && rc != -EAGAIN) {
                std::cerr << "⚠ Input error: " << std::strerror(-rc) << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        void processInputEvent(const input_event& ev) {
            std::cout << "DEBUG: Processing key event - code: " << ev.code
                << ", value: " << ev.value << std::endl;
            // Track key state for safety checks
            if (ev.value == 1) {  // Key down
                keyState[ev.code] = std::chrono::system_clock::now();
            } else if (ev.value == 0) {  // Key up
                keyState.erase(ev.code);

                // CRITICAL FIX: Immediately stop repeat when key is released
                if (keyRepeat.activeKeyCode == ev.code) {
                    keyRepeat.activeKeyCode = -1;
                    keyRepeat.count = 0;
                }
            }

            // Check for emergency exit sequence
            checkEmergencyExit(ev);

            // Track modifier state
            auto it = MODIFIER_MAP.find(ev.code);
            if (it != MODIFIER_MAP.end()) {
                modifiers[it->second] = (ev.value > 0);

                // Forward modifier event for internal tracking
                enqueueEvent({
                    EventType::Modifier,
                    keyCodeToName(ev.code),
                             ev.code,
                             ev.value > 0,
                             std::chrono::system_clock::now()
                });

                // Also forward as raw key event for system processing
                enqueueEvent({
                    EventType::RawKey,
                    keyCodeToName(ev.code),
                             ev.code,
                             false,  // active flag not used for raw keys
                             std::chrono::system_clock::now(),
                             ev.value
                });
                return;
            }

            // Handle key press/release
            std::string keyName = keyCodeToName(ev.code);

            if (ev.value == 1) {  // Key down
                // Start key repeat tracking
                keyRepeat.activeKeyCode = ev.code;
                keyRepeat.count = 0;
                keyRepeat.lastTime = std::chrono::system_clock::now();

                // Send key press event
                enqueueEvent({
                    EventType::Press,
                    keyName,
                    ev.code,
                    true,
                    std::chrono::system_clock::now()
                });
            } else if (ev.value == 0) {  // Key up
                // Stop key repeat - THIS IS CRITICAL
                if (keyRepeat.activeKeyCode == ev.code) {
                    keyRepeat.activeKeyCode = -1;
                    keyRepeat.count = 0;
                }

                // Send key release event
                enqueueEvent({
                    EventType::Release,
                    keyName,
                    ev.code,
                    false,
                    std::chrono::system_clock::now()
                });
            }

            // Safety check: Report stuck keys
            checkStuckKeys();
        }

        void checkKeyRepeat() {
            if (keyRepeat.activeKeyCode == -1) {
                return;
            }

            auto currentTime = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration<double>(currentTime - keyRepeat.lastTime).count();

            // Handle initial delay
            if (keyRepeat.count == 0) {
                if (elapsed >= keyRepeat.initialDelay) {
                    triggerKeyRepeat();
                }
            }
            // Handle repeat delay
            else {
                if (elapsed >= keyRepeat.repeatDelay) {
                    triggerKeyRepeat();
                }
            }
        }

        void triggerKeyRepeat() {
            std::string keyName = keyCodeToName(keyRepeat.activeKeyCode);

            // Create repeat event
            enqueueEvent({
                EventType::Repeat,
                keyName,
                keyRepeat.activeKeyCode,
                true,
                std::chrono::system_clock::now()
            });

            // Update state
            keyRepeat.count++;
            keyRepeat.lastTime = std::chrono::system_clock::now();

            // Special handling for backspace/delete
            if (keyRepeat.activeKeyCode == KEY_BACKSPACE ||
                keyRepeat.activeKeyCode == KEY_DELETE) {
                // Gradual acceleration (5ms faster each repeat, down to 10ms)
                keyRepeat.repeatDelay = std::max(0.01, 0.05 - (keyRepeat.count * 0.005));
                }
        }

        void checkEmergencyExit(const input_event& ev) {
            auto currentTime = std::chrono::steady_clock::now();

            // Reset sequence if timeout exceeded
            auto elapsed = std::chrono::duration<double>(currentTime - exitTimeout).count();
            if (elapsed > 1.0) {
                exitSequence.clear();
            }

            // Only process key down events
            if (ev.value != 1) {
                return;
            }

            // Add to sequence if it's part of emergency exit
            if (std::find(EMERGENCY_EXIT.begin(), EMERGENCY_EXIT.end(), ev.code) != EMERGENCY_EXIT.end()) {
                // Don't add duplicates
                if (exitSequence.empty() || exitSequence.back() != ev.code) {
                    exitSequence.push_back(ev.code);
                }

                exitTimeout = currentTime;

                // Check if full sequence was pressed
                if (exitSequence == EMERGENCY_EXIT) {
                    std::cerr << "\n🚨 EMERGENCY EXIT TRIGGERED: Ctrl+Alt+Esc" << std::endl;
                    std::cerr << "Shutting down safely..." << std::endl;
                    _exit(0);  // Force immediate exit
                }
            }
        }

        void checkStuckKeys() {
            auto currentTime = std::chrono::system_clock::now();
            const double stuckThreshold = 5.0;  // seconds

            for (auto it = keyState.begin(); it != keyState.end();) {
                auto elapsed = std::chrono::duration<double>(currentTime - it->second).count();
                if (elapsed > stuckThreshold) {
                    std::string keyName = keyCodeToName(it->first);
                    std::cerr << "⚠️ WARNING: Key appears stuck: " << keyName << std::endl;
                    std::cerr << "  Press Ctrl+Alt+Esc to force exit if needed" << std::endl;
                    it = keyState.erase(it);
                } else {
                    ++it;
                }
            }
        }

        void enqueueEvent(const InputEvent& event) {
            std::lock_guard<std::mutex> lock(queueMutex);
            eventQueue.push(event);
            cv.notify_one();
        }

        struct KeyboardCandidate {
            int score;
            int endpoint;
            std::string name;
            int fd;
            libevdev* dev;
        };

        libevdev* findPhysicalKeyboard() {
            std::vector<KeyboardCandidate> candidates;

            DIR* dir = opendir("/dev/input");
            if (!dir) {
                throw std::runtime_error("Failed to open /dev/input directory");
            }

            struct dirent* ent;
            while ((ent = readdir(dir)) != nullptr) {
                if (strncmp(ent->d_name, "event", 5) != 0) {
                    continue;
                }

                std::string path = "/dev/input/" + std::string(ent->d_name);
                int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
                if (fd < 0) {
                    continue;
                }

                libevdev* dev = nullptr;
                int rc = libevdev_new_from_fd(fd, &dev);
                if (rc < 0 || !dev) {
                    close(fd);
                    continue;
                }

                // 1. Must have key capability
                if (!libevdev_has_event_type(dev, EV_KEY)) {
                    libevdev_free(dev);
                    close(fd);
                    continue;
                }

                // 2. Must have LED capability (indicates physical keyboard)
                if (!libevdev_has_event_type(dev, EV_LED)) {
                    libevdev_free(dev);
                    close(fd);
                    continue;
                }

                // 3. Must NOT have mouse capabilities
                if (libevdev_has_event_type(dev, EV_REL) ||
                    libevdev_has_event_type(dev, EV_ABS)) {
                    libevdev_free(dev);
                close(fd);
                continue;
                    }

                    // 4. Must have minimum physical keys
                    int physicalCount = 0;
                    for (int key : PHYSICAL_KEYS) {
                        if (libevdev_has_event_code(dev, EV_KEY, key)) {
                            physicalCount++;
                        }
                    }
                    if (physicalCount < 30) {
                        libevdev_free(dev);
                        close(fd);
                        continue;
                    }

                    // 5. Extract USB endpoint number
                    int endpoint = -1;
                    const char* phys = libevdev_get_phys(dev);
                    if (phys) {
                        if (sscanf(phys, "input%d", &endpoint) != 1) {
                            endpoint = -1;
                        }
                    }

                    // 6. Calculate priority score
                    int score = 0;
                    if (endpoint == 0) {
                        score += 100;
                    } else if (endpoint != -1) {
                        score += 50 - endpoint;
                    }

                    const char* name = libevdev_get_name(dev);
                    if (name && std::strstr(name, "keyboard")) {
                        score += 10;
                    }

                    candidates.push_back({score, endpoint, name ? name : "Unknown", fd, dev});
            }
            closedir(dir);

            // Sort candidates
            std::sort(candidates.begin(), candidates.end(),
                      [](const auto& a, const auto& b) {
                          if (a.score != b.score) return a.score > b.score;
                          if (a.endpoint != b.endpoint) {
                              return (a.endpoint == -1) ? false :
                              (b.endpoint == -1) ? true : a.endpoint < b.endpoint;
                          }
                          return a.name < b.name;
                      });

            if (candidates.empty()) {
                throw std::runtime_error("No physical keyboard detected");
            }

            std::cout << "🔍 Found candidate keyboards:" << std::endl;
            for (const auto& candidate : candidates) {
                std::cout << "  Score: " << candidate.score
                << " | Endpoint: " << (candidate.endpoint == -1 ? "N/A" : std::to_string(candidate.endpoint))
                << " | Name: " << candidate.name << std::endl;
            }

            // Select the best candidate
            auto& best = candidates.front();
            deviceName = best.name;
            deviceFd = best.fd;
            libevdev* dev = best.dev;

            // Take exclusive control
            int grabResult = libevdev_grab(dev, LIBEVDEV_GRAB);
            if (grabResult < 0) {
                std::cerr << "⚠ Failed to grab keyboard: " << std::strerror(-grabResult)
                << " (errno: " << -grabResult << ")" << std::endl;
                libevdev_free(dev);
                close(deviceFd);
                throw std::runtime_error("Failed to grab keyboard device");
            }
            std::cout << "✅ Successfully grabbed keyboard: " << deviceName << std::endl;

            return dev;
        }

        // Member variables
        libevdev* physKb = nullptr;
        int deviceFd = -1;
        std::string deviceName;

        std::thread inputThread;
        std::atomic<bool> stopThread{false};

        // Key state tracking
        std::unordered_map<Modifier, bool> modifiers = {
            {Modifier::Shift, false},
            {Modifier::Ctrl, false},
            {Modifier::Alt, false},
            {Modifier::Super, false}
        };

        // Key repeat state
        KeyRepeatState keyRepeat;

        // Safety mechanism: Track key state to detect stuck keys
        std::unordered_map<unsigned int, std::chrono::system_clock::time_point> keyState;

        // Safety mechanism: Emergency exit key combination
        std::vector<unsigned int> exitSequence;
        std::chrono::steady_clock::time_point exitTimeout;

        // Event queue
        std::queue<InputEvent> eventQueue;
        std::mutex queueMutex;
        std::condition_variable cv;
    };

    // Public class implementation
    KeyboardInput::KeyboardInput()
    : pImpl(std::make_unique<InputHandlerImpl>()) {}

    KeyboardInput::~KeyboardInput() = default;

    std::optional<InputEvent> KeyboardInput::getEvent(int timeout_ms) {
        return pImpl->getEvent(timeout_ms);
    }

    bool KeyboardInput::isModifierActive(Modifier modifier) const {
        return pImpl->isModifierActive(modifier);
    }

    std::unordered_map<Modifier, bool> KeyboardInput::getModifierState() const {
        return pImpl->getModifierState();
    }

    std::vector<std::string> KeyboardInput::getActiveModifiers() const {
        return pImpl->getActiveModifiers();
    }

} // namespace keydrive
