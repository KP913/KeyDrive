#include "layout_manager.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <system_error>

namespace keydrive {

	namespace {

		// Default state values
		constexpr const char* DEFAULT_LAYOUT = "default";
		constexpr const char* DEFAULT_LAYER = "base";

		// Helper to convert string to lowercase
		std::string toLower(const std::string& str) {
			std::string result = str;
			std::transform(result.begin(), result.end(), result.begin(),
						   [](unsigned char c) { return std::tolower(c); });
			return result;
		}

		// Helper to check if a string starts with a prefix
		bool startsWith(const std::string& str, const std::string& prefix) {
			return str.size() >= prefix.size() &&
			str.compare(0, prefix.size(), prefix) == 0;
		}

		// Convert YAML node to string, handling different types
		std::string yamlNodeToString(const YAML::Node& node) {
			if (node.IsScalar()) {
				return node.as<std::string>();
			}
			if (node.IsDefined()) {
				std::ostringstream ss;
				ss << node;
				return ss.str();
			}
			return "";
		}

		// Convert YAML node to vector of strings
		std::vector<std::string> yamlNodeToStringVector(const YAML::Node& node) {
			std::vector<std::string> result;
			if (node.IsSequence()) {
				for (const auto& item : node) {
					result.push_back(yamlNodeToString(item));
				}
			} else if (node.IsScalar()) {
				result.push_back(yamlNodeToString(node));
			}
			return result;
		}

		// Parse layer type from string
		LayerType parseLayerType(const std::string& typeStr) {
			std::string lowerType = toLower(typeStr);
			if (lowerType == "hold") {
				return LayerType::Hold;
			} else if (lowerType == "toggle") {
				return LayerType::Toggle;
			} else if (lowerType == "onetime") {
				return LayerType::Onetime;
			}
			return LayerType::Hold;  // Default to hold
		}

		// Convert layer type to string for debugging
		std::string layerTypeToString(LayerType type) {
			switch (type) {
				case LayerType::Hold: return "hold";
				case LayerType::Toggle: return "toggle";
				case LayerType::Onetime: return "onetime";
			}
			return "unknown";
		}

		// Convert a string to char32_t (Unicode code point)
		std::optional<char32_t> stringToChar32(const std::string& str) {
			if (str.empty()) {
				return std::nullopt;
			}

			// Handle special control characters
			if (str == "\\n") return U'\n';
			if (str == "\\t") return U'\t';
			if (str == "\\b") return U'\b';
			if (str == "\\x1b") return U'\x1b';
			if (str == " ") return U' ';

			// UTF-8 decoding logic
			unsigned char firstByte = static_cast<unsigned char>(str[0]);

			// 1-byte sequence (ASCII)
			if ((firstByte & 0x80) == 0x00) {
				return static_cast<char32_t>(firstByte);
			}
			// 2-byte sequence
			else if ((firstByte & 0xE0) == 0xC0 && str.size() >= 2) {
				unsigned char secondByte = static_cast<unsigned char>(str[1]);
				if ((secondByte & 0xC0) == 0x80) {
					char32_t codePoint = ((firstByte & 0x1F) << 6) | (secondByte & 0x3F);
					return codePoint;
				}
			}
			// 3-byte sequence
			else if ((firstByte & 0xF0) == 0xE0 && str.size() >= 3) {
				unsigned char secondByte = static_cast<unsigned char>(str[1]);
				unsigned char thirdByte = static_cast<unsigned char>(str[2]);
				if (((secondByte & 0xC0) == 0x80) && ((thirdByte & 0xC0) == 0x80)) {
					char32_t codePoint = ((firstByte & 0x0F) << 12) |
					((secondByte & 0x3F) << 6) |
					(thirdByte & 0x3F);
					return codePoint;
				}
			}
			// 4-byte sequence
			else if ((firstByte & 0xF8) == 0xF0 && str.size() >= 4) {
				unsigned char secondByte = static_cast<unsigned char>(str[1]);
				unsigned char thirdByte = static_cast<unsigned char>(str[2]);
				unsigned char fourthByte = static_cast<unsigned char>(str[3]);
				if (((secondByte & 0xC0) == 0x80) &&
					((thirdByte & 0xC0) == 0x80) &&
					((fourthByte & 0xC0) == 0x80)) {
					char32_t codePoint = ((firstByte & 0x07) << 18) |
					((secondByte & 0x3F) << 12) |
					((thirdByte & 0x3F) << 6) |
					(fourthByte & 0x3F);
				return codePoint;
					}
			}

			// Invalid UTF-8 sequence
			return std::nullopt;
		}

	} // anonymous namespace

	LayoutManager::LayoutManager(const std::string& configDir)
	: configDir(configDir),
	layoutsDir(configDir + "/layouts"),
	stateFile(configDir + "/state.yaml") {

		// Create config directory if it doesn't exist
		std::error_code ec;
		std::filesystem::create_directories(configDir, ec);
		std::filesystem::create_directories(layoutsDir, ec);

		// Load state and layout
		loadState();
		loadLayout();

		std::cout << "✅ Layout manager initialized with " << state["layout"] << " layout" << std::endl;
	}

	void LayoutManager::loadState() {
		// Default state
		state["layout"] = DEFAULT_LAYOUT;
		state["layer"] = DEFAULT_LAYER;

		// Try to load from file
		std::ifstream stateStream(stateFile);
		if (stateStream) {
			try {
				YAML::Node stateNode = YAML::Load(stateStream);
				if (stateNode["layout"]) {
					state["layout"] = stateNode["layout"].as<std::string>();
				}
				if (stateNode["layer"]) {
					state["layer"] = stateNode["layer"].as<std::string>();
				}
			} catch (const YAML::Exception& e) {
				std::cerr << "⚠ State file corrupted: " << e.what() << std::endl;
			}
		}
	}

	void LayoutManager::saveState() const {
		YAML::Node stateNode;
		stateNode["layout"] = state.at("layout");
		stateNode["layer"] = state.at("layer");

		// Save toggle states
		for (const auto& [layer, active] : layerState.toggles) {
			stateNode["toggle_" + layer] = active;
		}

		std::ofstream stateStream(stateFile);
		if (stateStream) {
			stateStream << stateNode;
			std::cout << "💾 State saved: layout=" << state.at("layout")
			<< ", layer=" << state.at("layer") << std::endl;
		} else {
			std::cerr << "⚠ Failed to save state to " << stateFile << std::endl;
		}
	}

	void LayoutManager::loadLayout() {
		std::string layoutPath = layoutsDir + "/" + state["layout"] + ".kbd";

		// Check if layout file exists
		if (!std::filesystem::exists(layoutPath)) {
			throw std::runtime_error("Layout file not found: " + layoutPath);
		}

		// Load YAML content
		std::ifstream layoutStream(layoutPath);
		if (!layoutStream) {
			throw std::runtime_error("Failed to open layout file: " + layoutPath);
		}

		// Parse YAML
		try {
			layout = YAML::Load(layoutStream);
		} catch (const YAML::Exception& e) {
			throw std::runtime_error("Failed to parse layout file: " + std::string(e.what()));
		}

		// Create key position mapping
		keyPositions.clear();
		if (layout["source"] && layout["source"].IsSequence()) {
			for (size_t i = 0; i < layout["source"].size(); ++i) {
				std::string key = yamlNodeToString(layout["source"][i]);
				keyPositions[key] = i;
			}
		} else {
			throw std::runtime_error("Invalid layout format: missing or invalid 'source' array");
		}

		// Validate all layers
		size_t sourceLength = keyPositions.size();
		if (layout["layers"] && layout["layers"].IsMap()) {
			// USE NON-CONST ITERATOR SINCE WE NEED TO MODIFY LAYERS
			for (YAML::iterator it = layout["layers"].begin(); it != layout["layers"].end(); ++it) {
				const std::string& layerName = it->first.as<std::string>();
				YAML::Node& layer = it->second;  // NON-CONST REFERENCE

				if (!layer.IsSequence()) {
					throw std::runtime_error("Layer '" + layerName + "' is not a sequence");
				}

				if (layer.size() != sourceLength) {
					if (layer.size() < sourceLength) {
						// Extend with empty strings
						while (layer.size() < sourceLength) {
							layer.push_back("");
						}
					} else {
						// Truncate - create a new node since we can't resize directly
						YAML::Node newLayer(YAML::NodeType::Sequence);
						for (size_t i = 0; i < sourceLength; ++i) {
							newLayer.push_back(layer[i]);
						}
						layout["layers"][layerName] = newLayer;
					}
				}
			}
		} else {
			throw std::runtime_error("Invalid layout format: missing or invalid 'layers' map");
		}

		// Parse layer keys configuration
		layerKeys.clear();
		if (layout["layer_keys"] && layout["layer_keys"].IsMap()) {
			for (YAML::const_iterator it = layout["layer_keys"].begin(); it != layout["layer_keys"].end(); ++it) {
				const std::string& layerName = it->first.as<std::string>();
				if (layerName == "base") {
					continue;
				}

				YAML::Node config = it->second;
				if (!config.IsMap()) {
					std::cerr << "⚠ Invalid layer key config for " << layerName << std::endl;
					continue;
				}

				// Get key specification
				YAML::Node keyNode = config["key"];
				std::vector<std::string> keyList = yamlNodeToStringVector(keyNode);

				// Get layer type
				LayerType layerType = LayerType::Hold;
				if (config["type"]) {
					layerType = parseLayerType(config["type"].as<std::string>());
				}

				// Register each key
				for (const std::string& key : keyList) {
					layerKeys[key] = {layerName, layerType};
				}
			}
		}

		// Initialize layer state
		layerState = {
			state["layer"],
			{},
			"",
			"",
			-1
		};

		// Initialize toggle states
		if (layout["layer_keys"] && layout["layer_keys"].IsMap()) {
			for (YAML::const_iterator it = layout["layer_keys"].begin(); it != layout["layer_keys"].end(); ++it) {
				const std::string& layerName = it->first.as<std::string>();
				YAML::Node config = it->second;

				if (config["type"] && parseLayerType(config["type"].as<std::string>()) == LayerType::Toggle) {
					// Load saved toggle state
					bool savedState = false;
					std::string stateKey = "toggle_" + layerName;
					if (state.find(stateKey) != state.end()) {
						savedState = (state[stateKey] == "true");
					}
					layerState.toggles[layerName] = savedState;
				}
			}
		}

		// Verify layer keys
		verifyLayerKeys();

		std::cout << "✅ Loaded layout with " << keyPositions.size()
		<< " keys and " << layerKeys.size() << " layer keys" << std::endl;
	}

	std::optional<char32_t> LayoutManager::processKeyEvent(
		const std::string& keyName,
		int keyCode,
		const std::string& eventType
	) {
		// Skip non-press events for character output
		if (eventType != "press" && eventType != "repeat") {
			return std::nullopt;
		}

		// Find key position
		auto posIt = keyPositions.find(keyName);
		if (posIt == keyPositions.end()) {
			// Key not in layout - return special value to indicate forwarding
			return std::nullopt;  // Use nullopt instead of sentinel value
		}
		size_t pos = posIt->second;

		// Get character for base layer (we need this to check for layer keys)
		if (!layout["layers"] || !layout["layers"]["base"] ||
			pos >= layout["layers"]["base"].size()) {
			std::cerr << "⚠ Position " << pos << " out of bounds for base layer" << std::endl;
			return std::nullopt;
			}

		std::string baseChar = yamlNodeToString(layout["layers"]["base"][pos]);

		// Check if this is a layer key
		auto [isLayerKey, layerConfig] = getLayerForKey(keyName, baseChar);
		if (isLayerKey) {
			const std::string& layerName = layerConfig.targetLayer;
			LayerType layerType = layerConfig.type;

			// Handle different layer types
			if (layerType == LayerType::Hold) {
				layerState.hold = layerName;
				layerState.holdKey = keyCode;
				std::cout << "→ LAYER: HOLD '" << layerName << "' activated" << std::endl;
				return std::nullopt;  // Consume the event - don't output a character
			}
			else if (layerType == LayerType::Toggle) {
				// Check if this toggle layer is CURRENTLY ACTIVE
				std::string currentLayer = getCurrentLayer();
				bool is_active = (currentLayer == layerName);

				if (is_active) {
					// If it's active, deactivate it (return to base)
					layerState.toggles[layerName] = false;
					std::cout << "→ LAYER: TOGGLE '" << layerName
					<< "' deactivated (returning to base)" << std::endl;
				} else {
					// If it's not active, activate it
					layerState.toggles[layerName] = true;
					std::cout << "→ LAYER: TOGGLE '" << layerName << "' activated" << std::endl;
				}

				// Save toggle state
				state["toggle_" + layerName] = layerState.toggles[layerName] ? "true" : "false";
				saveState();

				return std::nullopt;  // Consume the event - don't output a character
			}
			else if (layerType == LayerType::Onetime) {
				layerState.oneTime = layerName;
				std::cout << "→ LAYER: ONETIME '" << layerName << "' activated (one use)" << std::endl;
				return std::nullopt;  // Consume the event - don't output a character
			}
		}

		// Determine current active layer
		std::string currentLayer = getCurrentLayer();

		// Get character for this position in current layer
		if (!layout["layers"] || !layout["layers"][currentLayer]) {
			std::cerr << "⚠ Layer not found: " << currentLayer << std::endl;
			return std::nullopt;
		}

		YAML::Node layerNode = layout["layers"][currentLayer];
		if (pos >= layerNode.size()) {
			std::cerr << "⚠ Position " << pos << " out of bounds for layer '" << currentLayer << "'" << std::endl;
			return std::nullopt;
		}

		std::string charStr = yamlNodeToString(layerNode[pos]);

		// One-time layers are consumed after one use
		if (!layerState.oneTime.empty()) {
			std::cout << "→ LAYER: ONETIME '" << layerState.oneTime << "' consumed" << std::endl;
			layerState.oneTime.clear();
		}
		std::cout << "INCOMING: " << charStr << "\n";
		// Convert to Unicode character
		auto character = stringToChar32(charStr);

		// Debug output
		if (character) {
			std::cout << "→ CHARACTER: '";
			if (*character < 127 && *character >= 32) {
				std::cout << static_cast<char>(*character);
			} else {
				std::cout << "U+" << std::hex << static_cast<int>(*character);
			}
			std::cout << "' from layer '" << currentLayer << "'" << std::endl;
		} else if (!charStr.empty()) {
			std::cout << "→ CHARACTER: '" << charStr << "' from layer '" << currentLayer << "'" << std::endl;
		} else {
			std::cout << "→ NO CHARACTER (empty position)" << std::endl;
		}

		return character;
	}

	void LayoutManager::handleKeyRelease(int keyCode) {
		// Handle hold layer deactivation
		if (layerState.holdKey == keyCode) {
			layerState.hold.clear();
			layerState.holdKey = -1;
			std::cout << "→ LAYER: HOLD deactivated" << std::endl;
		}
	}

	std::string LayoutManager::getCurrentLayer() const {
		// One-time layers have highest priority
		if (!layerState.oneTime.empty()) {
			return layerState.oneTime;
		}

		// Hold layers next
		if (!layerState.hold.empty()) {
			return layerState.hold;
		}

		// Toggle layers
		for (const auto& [layer, active] : layerState.toggles) {
			if (active) {
				return layer;
			}
		}

		// Base layer is default
		return "base";
	}

	bool LayoutManager::shouldForwardKey(bool shiftActive, bool ctrlActive, bool altActive, bool superActive) const {
		// Forward if any non-Shift modifier is active
		return (ctrlActive || altActive || superActive);
	}

	LayerState LayoutManager::getLayerState() const {
		return layerState;
	}

	std::pair<bool, LayerKeyConfig> LayoutManager::getLayerForKey(
		const std::string& keyName,
		const std::string& baseChar
	) const {
		// Clean up the character value
		std::string clean_char = cleanChar(baseChar);

		// Check if this is a layer key (starts with 'ly')
		if (!clean_char.empty() && startsWith(clean_char, "ly")) {
			auto it = layerKeys.find(clean_char);
			if (it != layerKeys.end()) {
				return {true, it->second};
			}
		}

		return {false, LayerKeyConfig{}};
	}

	std::string LayoutManager::cleanChar(const std::string& charStr) {
		// Remove any quotes and whitespace
		std::string result = charStr;
		result.erase(std::remove(result.begin(), result.end(), '"'), result.end());
		result.erase(std::remove(result.begin(), result.end(), '\''), result.end());
		result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
		return result;
	}

	void LayoutManager::verifyLayerKeys() const {
		std::cout << "\n🔍 LAYER KEY VERIFICATION:" << std::endl;

		// Check base layer for layer keys
		if (!layout["layers"] || !layout["layers"]["base"]) {
			std::cerr << "⚠ Base layer not found in layout" << std::endl;
			return;
		}

		YAML::Node baseLayer = layout["layers"]["base"];
		for (size_t i = 0; i < baseLayer.size(); ++i) {
			std::string charStr = yamlNodeToString(baseLayer[i]);
			std::string clean_char = cleanChar(charStr);

			if (!clean_char.empty() && startsWith(clean_char, "ly")) {
				auto it = layerKeys.find(clean_char);
				if (it != layerKeys.end()) {
					const auto& config = it->second;
					std::cout << "  ✅ Position " << i << ": '" << charStr << "' → '" << clean_char
					<< "' → " << layerTypeToString(config.type)
					<< " layer '" << config.targetLayer << "'" << std::endl;
				} else {
					std::cout << "  ❌ Position " << i << ": '" << charStr << "' → '" << clean_char
					<< "' → NOT CONFIGURED IN layer_keys" << std::endl;
				}
			}
		}

		// Check layer_keys configuration
		std::cout << "\n  Configured layer keys:" << std::endl;
		for (const auto& [keyId, config] : layerKeys) {
			// Find where this layer key appears in the layout
			std::vector<size_t> positions;
			for (size_t i = 0; i < baseLayer.size(); ++i) {
				std::string charStr = yamlNodeToString(baseLayer[i]);
				std::string clean_char = cleanChar(charStr);
				if (clean_char == keyId) {
					positions.push_back(i);
				}
			}

			std::string positionStr;
			if (positions.empty()) {
				positionStr = "NOT IN LAYOUT";
			} else {
				for (size_t i = 0; i < positions.size(); ++i) {
					if (i > 0) positionStr += ", ";
					positionStr += std::to_string(positions[i]);
				}
			}

			std::cout << "  - '" << keyId << "': " << layerTypeToString(config.type)
			<< " layer '" << config.targetLayer
			<< "' (positions: " << positionStr << ")" << std::endl;
		}
	}

} // namespace keydrive
