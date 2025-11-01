#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <yaml-cpp/yaml.h>

namespace keydrive {

	/**
	 * @brief Types of layers supported by the layout manager
	 */
	enum class LayerType {
		Hold,
		Toggle,
		Onetime
	};

	/**
	 * @brief Configuration for a layer key
	 */
	struct LayerKeyConfig {
		std::string targetLayer;
		LayerType type;
	};

	/**
	 * @brief Current state of all layers
	 */
	struct LayerState {
		std::string current{"base"};
		std::unordered_map<std::string, bool> toggles;
		std::string oneTime;
		std::string hold;
		int holdKey{-1};
	};

	/**
	 * @brief Manages keyboard layouts, layers, and character mapping
	 */
	class LayoutManager {
	public:
		/**
		 * @brief Construct a new Layout Manager object
		 *
		 * @param configDir Path to configuration directory
		 */
		explicit LayoutManager(const std::string& configDir = std::string(getenv("HOME")) + "/keydrive-cpp");

		/**
		 * @brief Process a key event and determine what character to output
		 *
		 * @param keyName The key name (e.g., "key_a")
		 * @param keyCode The key code
		 * @param eventType Event type (press, release, repeat)
		 * @return std::optional<char32_t> Character to output, or nullopt if no character
		 */
		std::optional<char32_t> processKeyEvent(
			const std::string& keyName,
			int keyCode,
			const std::string& eventType
		);

		/**
		 * @brief Handle key release events for layer management
		 *
		 * @param keyCode The key code that was released
		 */
		void handleKeyRelease(int keyCode);

		/**
		 * @brief Get the current active layer
		 *
		 * @return std::string Current layer name
		 */
		std::string getCurrentLayer() const;

		/**
		 * @brief Determine if a key should be forwarded instead of remapped
		 *
		 * @param shiftActive Whether Shift is active
		 * @param ctrlActive Whether Ctrl is active
		 * @param altActive Whether Alt is active
		 * @param superActive Whether Super is active
		 * @return true if key should be forwarded (for shortcuts)
		 */
		bool shouldForwardKey(bool shiftActive, bool ctrlActive, bool altActive, bool superActive) const;

		/**
		 * @brief Get the current layer state for debugging
		 *
		 * @return LayerState Current layer state
		 */
		LayerState getLayerState() const;

		/**
		 * @brief Verify layer key configuration matches layout
		 */
		void verifyLayerKeys() const;

	private:
		// Configuration paths
		std::string configDir;
		std::string layoutsDir;
		std::string stateFile;

		// State management
		std::unordered_map<std::string, std::string> state;
		YAML::Node layout;
		std::unordered_map<std::string, size_t> keyPositions;
		LayerState layerState;
		std::unordered_map<std::string, LayerKeyConfig> layerKeys;

		/**
		 * @brief Load persistent state (active layout/layer)
		 */
		void loadState();

		/**
		 * @brief Persist current state
		 */
		void saveState() const;

		/**
		 * @brief Load the current keyboard layout from YAML
		 */
		void loadLayout();

		/**
		 * @brief Determine if a key is a layer key and what layer it activates
		 *
		 * @param keyName The key name (e.g., "key_a")
		 * @param baseChar The character from the base layer
		 * @return std::pair<bool, LayerKeyConfig> Whether it's a layer key and its config
		 */
		std::pair<bool, LayerKeyConfig> getLayerForKey(
			const std::string& keyName,
			const std::string& baseChar
		) const;

		/**
		 * @brief Clean up a character string (remove quotes, whitespace)
		 *
		 * @param charStr The character string to clean
		 * @return std::string Cleaned character string
		 */
		static std::string cleanChar(const std::string& charStr);
	};

} // namespace keydrive
