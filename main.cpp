#include "input_handler.hpp"
#include "output_handler.hpp"
#include "layout_manager.hpp"
#include <iostream>
#include <thread>
#include <csignal>
#include <atomic>
#include <chrono>
#include <vector>
#include <optional>

namespace keydrive {

    // Global flag for clean shutdown
    std::atomic<bool> running{true};

    // Signal handler for Ctrl+C
    void signalHandler(int signal) {
        if (signal == SIGINT) {
            std::cout << "\n👋 Shutting down..." << std::endl;
            running = false;
        }
    }

} // namespace keydrive

int main() {
    // Set up signal handling for Ctrl+C
    std::signal(SIGINT, keydrive::signalHandler);
    keydrive::KeyboardInput keyboard;
    keydrive::OutputHandler output;
    keydrive::LayoutManager layoutManager;

    try {

        std::cout << "\n🎹 Keyboard Remapper Active" << std::endl;
        std::cout << "================================" << std::endl;
        std::cout << "💡 TIPS:" << std::endl;
        std::cout << "  - Press Ctrl+Alt+Esc to force exit if Super key gets stuck" << std::endl;
        std::cout << "  - Check debug output for 'WARNING: Key appears stuck'" << std::endl;
        std::cout << "================================" << std::endl;

        std::cout << "Keydrive" << std::to_string(keydrive::running) << std::endl;
        while (keydrive::running) {
            //std::cout << "Start loop" << std::endl;
            // Get next input event
            if (auto event = keyboard.getEvent(100)) {
                // --- CORE EVENT PROCESSING LOGIC ---

                // 1. ALWAYS handle Release events first for layer management
                //    This is crucial for Hold layers.
                if (event->type == keydrive::EventType::Release) {
                    layoutManager.handleKeyRelease(event->keyCode);
                    // Do NOT 'continue' here. The corresponding RawKey release event
                    // also needs to be forwarded to the system. Let it fall through.
                    // Or, handle forwarding here if needed, but RawKey should cover it.
                }

                // 2. ALWAYS forward RawKey events to the system.
                //    This ensures modifiers and all keys are seen by the system for shortcuts etc.
                //    InputHandlerImpl generates these for *every* physical key event.
                if (event->type == keydrive::EventType::RawKey) {
                    // Debug: std::cout << "Forwarding RawKey: " << event->keyName << " (" << event->keyCode << ") value=" << event->value << std::endl;
                    output.forwardEvent(event->keyCode, event->value);
                    // RawKey events are purely for system forwarding.
                    // Do not process them for layout character output here.
                    // Their purpose is fulfilled by the forwardEvent call.
                    continue; // Done with RawKey event processing for main logic.
                }

                // 3. Get the current modifier state (tracked by InputHandlerImpl)
                auto modifierState = keyboard.getModifierState();
                bool shiftActive = modifierState[keydrive::Modifier::Shift];
                bool ctrlActive = modifierState[keydrive::Modifier::Ctrl];
                bool altActive = modifierState[keydrive::Modifier::Alt];
                bool superActive = modifierState[keydrive::Modifier::Super];

                // 4. Determine if we should bypass layout remapping for system shortcuts
                //    Bypass if Ctrl, Alt, or Super is active.
                //    Note: Shift alone does NOT trigger bypass.
                //    Exception: If the key itself is defined as a layer key in the layout,
                //    it should still be processed by the layout manager.
                bool bypassRemapping = ctrlActive || altActive || superActive;

                // Debugging output
                // std::cout << "DEBUG: Key=" << event->keyName << " Type=" << static_cast<int>(event->type)
                //           << " Modifiers: S=" << shiftActive << " C=" << ctrlActive << " A=" << altActive << " M=" << superActive
                //           << " Bypass=" << bypassRemapping << std::endl;

                // 5. Process key events that might generate characters or trigger layers
                //    This includes Press and Repeat events.
                //    Crucially, this also includes Modifier events IF they are layer keys in the layout.
                if (event->type == keydrive::EventType::Press || event->type == keydrive::EventType::Repeat) {
                    // Ask the layout manager to process the key event.
                    // It will:
                    // - Check if it's a layer key (defined in layout's base layer) and activate/deactivate layers.
                    // - Determine the correct character based on the active layer.
                    // - Return the character or nullopt.
                    std::optional<char32_t> maybeCharacter = layoutManager.processKeyEvent(
                        event->keyName,
                        event->keyCode,
                        (event->type == keydrive::EventType::Press) ? "press" : "repeat"
                    );

                    // Check if Ctrl/Alt/Super is active (bypass condition)
                    if (bypassRemapping) {
                        // System shortcut is active (e.g., Ctrl+C).
                        // The RawKey event ensures the system sees the key press.
                        // We explicitly skip sending the character via our layout *unless*
                        // the key is a layout-defined layer key (which layoutManager.processKeyEvent handles).
                        // If maybeCharacter has a value, it means the layout *wants* to send a character
                        // even with modifiers (e.g., a custom layout mapping Ctrl+D to 'Δ').
                        // If it's nullopt, it was likely a layer key or unmapped.
                        //if (maybeCharacter.has_value()) {
                            // Layout explicitly defined a character for this key+modifier combo.
                            // This is an intentional remapping that overrides the system shortcut.
                            // Example: Layout maps Ctrl+Shift+K to 'ಠ' -> maybeCharacter holds 'ಠ'.
                            // Send the character.
                            //std::cout << "INFO: Layout override for system shortcut combo. Sending character." << std::endl;
                            //if (!output.sendUnicode(maybeCharacter.value())) {
                                //std::cerr << "❌ Failed to send character U+" << std::hex << static_cast<int>(maybeCharacter.value()) << std::dec << std::endl;
                            //}
                        //} else {
                            // It was likely a layer key or unmapped. System handles the shortcut.
                            // Character sending is intentionally skipped.
                            //std::cout << "INFO: Bypassing layout for system shortcut. Key: " << event->keyName << std::endl;
                        //}
                        // In either sub-case, we've decided how to handle the key press with modifiers.
                        std::cout << "INFO: Bypassing layout for system shortcut. Key: " << event->keyName << std::endl;
                        continue; // Move to the next event.
                    }

                    // If we reach here, either:
                    // 1. No Ctrl/Alt/Super modifiers are active (bypassRemapping is false).
                    // 2. Ctrl/Alt/Super is active, but we are NOT bypassing (layout override).
                    // In both cases, if the layout produced a character, we should send it.

                    // If the layout manager provided a character, send it.
                    if (maybeCharacter.has_value()) {
                        // This covers:
                        // - Normal key presses (e.g., 'a' -> 'α')
                        // - Layer-affected key presses (e.g., Hold 'Sym' + 'k' -> '★')
                        // - Shift acting as a key (e.g., if layout maps physical Shift to '⇑', and it's pressed alone)
                        // - Layout override for modifier combos (handled in the if-block above)
                        if (!output.sendUnicode(maybeCharacter.value())) {
                            std::cerr << "❌ Failed to send character U+" << std::hex << static_cast<int>(maybeCharacter.value()) << std::dec << std::endl;
                        }
                        continue; // Character sent, move to next event.
                    }

                    // If we reach here:
                    // - maybeCharacter is nullopt.
                    // - This usually means the key press was consumed by the layout manager
                    //   for layer activation/deactivation (e.g., pressing the 'Sym' layer key).
                    // - Or, the key is unmapped in the current layer.
                    // - The system already received the key event via the RawKey event.
                    // No character needs to be sent by us.
                    // std::cout << "INFO: Key press consumed by layout (likely layer key) or unmapped: " << event->keyName << std::endl;
                    continue; // Done processing this event.
                }

                // 5. Handle any other event types if necessary (though Press/Repeat/RawKey/Release should cover most)
                //    EventType::Modifier events generated by InputHandlerImpl for physical modifiers
                //    should have been handled by the RawKey forwarding and the Press/Repeat logic above.
                //    If an EventType::Modifier sneaks through here, it might be unexpected.
                //    Let's log it to be safe.
                std::cout << "INFO: Unhandled event type: " << static_cast<int>(event->type) << " for key: " << event->keyName << std::endl;

                // --- END CORE EVENT PROCESSING LOGIC ---
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "\n❌ CRITICAL ERROR: " << e.what() << std::endl;
        std::cerr << "Attempting safe shutdown..." << std::endl;
    }

    // Safety cleanup: Release all modifiers
    std::cout << "🧹 Releasing all modifiers..." << std::endl;
    output.releaseAllModifiers();

    std::cout << "✅ Shutdown complete" << std::endl;
    return 0;
}
