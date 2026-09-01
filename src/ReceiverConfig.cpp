#include "ReceiverConfig.h"

//--------------------------------------------------------------
bool ReceiverConfig::load(const std::string & path) {
	loaded = false;

	ofFile file(path);
	if (!file.exists()) {
		ofLogWarning("ReceiverConfig") << "config file not found: " << path
			<< " - using defaults";
		return false;
	}

	ofJson json;
	try {
		json = ofLoadJson(path);
	} catch (const std::exception & e) {
		ofLogError("ReceiverConfig") << "failed to parse " << path << ": " << e.what();
		return false;
	}
	if (json.is_null() || json.empty()) {
		ofLogError("ReceiverConfig") << "empty or invalid JSON: " << path;
		return false;
	}

	if (json.contains("receiver")) {
		const auto & r = json["receiver"];
		listenPort = r.value("listen_port", listenPort);
		width = r.value("width", width);
		height = r.value("height", height);
		ledWidth = r.value("led_width", ledWidth);
		ledHeight = r.value("led_height", ledHeight);
		// "window_mode" wins; the legacy bool "fullscreen" is honoured as a
		// fallback so old config files keep working.
		std::string mode = r.value("fullscreen", false) ? "fullscreen" : "windowed";
		mode = r.value("window_mode", mode);
		if (mode == "fullscreen") {
			windowMode = WindowMode::Fullscreen;
		} else if (mode == "borderless") {
			windowMode = WindowMode::Borderless;
		} else {
			if (mode != "windowed") {
				ofLogWarning("ReceiverConfig") << "unknown window_mode '" << mode
					<< "' - using windowed";
			}
			windowMode = WindowMode::Windowed;
		}
		display = r.value("display", display);
		vsync = r.value("vsync", vsync);
		scale = r.value("scale", scale);
		applyFade = r.value("apply_fade", applyFade);
		mouthBand = r.value("mouth_band", mouthBand);
		streamTimeoutSeconds = r.value("stream_timeout_seconds", streamTimeoutSeconds);
		fadeInSeconds = r.value("fade_in_seconds", fadeInSeconds);
		fadeOutSeconds = r.value("fade_out_seconds", fadeOutSeconds);
	}

	loaded = true;
	const char * modeName = windowMode == WindowMode::Fullscreen ? "fullscreen"
		: windowMode == WindowMode::Borderless ? "borderless"
											   : "windowed";
	ofLogNotice("ReceiverConfig") << "loaded " << path << " (port=" << listenPort
		<< ", canvas=" << width << "x" << height
		<< ", led=" << ledWidth << "x" << ledHeight
		<< ", window_mode=" << modeName
		<< ", vsync=" << (vsync ? "on" : "off")
		<< ", scale=" << scale << ")";
	return true;
}
