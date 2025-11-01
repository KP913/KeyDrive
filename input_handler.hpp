#pragma once

#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <optional>  // ADDED: For std::optional
#include <libevdev-1.0/libevdev/libevdev.h>

namespace keydrive {

    // Forward declaration for implementation details
    class InputHandlerImpl;

    /**
     * @brief Event types that can be produced by the input handler
     */
    enum class EventType {
        Press,
        Release,
        Repeat,
        Modifier,
        RawKey
    };

    /**
     * @brief Modifier keys that can be tracked
     */
    enum class Modifier {
        Shift,
        Ctrl,
        Alt,
        Super
    };

    /**
     * @brief Represents a single input event from the keyboard
     */
    struct InputEvent {
        EventType type;
        std::string keyName;      // Key name (e.g., "key_a")
        int keyCode;              // Key code (EV_KEY value)
        bool active;              // For modifiers: true if active, false if released
        std::chrono::system_clock::time_point timestamp;

        // For raw key events
        int value = 0;            // Raw value (1=press, 0=release)
    };

    /**
     * @brief Handles physical keyboard detection and input event processing
     */
    class KeyboardInput {
    public:
        /**
         * @brief Construct a new Keyboard Input object
         *
         * @throws std::runtime_error if no physical keyboard is detected
         */
        KeyboardInput();

        /**
         * @brief Destroy the Keyboard Input object
         *
         * Cleans up resources and releases the keyboard device
         */
        ~KeyboardInput();

        /**
         * @brief Get the next input event (non-blocking with timeout)
         *
         * @param timeout_ms Timeout in milliseconds
         * @return std::optional<InputEvent> Event data or nullopt if no event available
         */
        std::optional<InputEvent> getEvent(int timeout_ms = 100);

        /**
         * @brief Check if a modifier is currently active
         *
         * @param modifier The modifier to check
         * @return true if the modifier is active, false otherwise
         */
        bool isModifierActive(Modifier modifier) const;

        /**
         * @brief Get the current modifier state
         *
         * @return std::unordered_map<Modifier, bool> Modifier states
         */
        std::unordered_map<Modifier, bool> getModifierState() const;

        /**
         * @brief Get the names of all active modifiers
         *
         * @return std::vector<std::string> Active modifier names
         */
        std::vector<std::string> getActiveModifiers() const;

    private:
        std::unique_ptr<InputHandlerImpl> pImpl;  // Pimpl pattern for cleaner interface
    };

} // namespace keydrive
