#include "ReceiverConfig.h"

#include <algorithm>

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

	// Mouth presentation: the receiver owns fixture placement, easing, and
	// the neutral idle pose (the wire only carries target edges in the
	// mouth-span), so these are receiver-side knobs.
	if (json.contains("mouth")) {
		const auto & m = json["mouth"];
		if (m.contains("lights")) {
			const auto & l = m["lights"];
			mouthLightsW = std::max(1, l.value("w", mouthLightsW));
			mouthLightsH = std::max(1, l.value("h", mouthLightsH));
		}
		mouthRow = m.value("row", mouthRow);
		mouthOffset = m.value("offset", mouthOffset);
		mouthSpan = m.value("span", mouthSpan);
		if (m.contains("color")) {
			const auto & c = m["color"];
			mouthColor.r = static_cast<int>(ofClamp(c.value("r", static_cast<int>(mouthColor.r)), 0, 255));
			mouthColor.g = static_cast<int>(ofClamp(c.value("g", static_cast<int>(mouthColor.g)), 0, 255));
			mouthColor.b = static_cast<int>(ofClamp(c.value("b", static_cast<int>(mouthColor.b)), 0, 255));
			mouthColor.a = static_cast<int>(ofClamp(c.value("a", static_cast<int>(mouthColor.a)), 0, 255));
		}
		mouthNeutralWidth = std::max(1, m.value("neutral_width", mouthNeutralWidth));
		mouthTransitionSeconds = std::max(0.0f,
			m.value("transition_seconds", mouthTransitionSeconds));
	}

	// Eye lights on the same fixture: placement of the two blocks whose
	// brightness runs inverse to the camera-image fade.
	if (json.contains("eyes")) {
		const auto & e = json["eyes"];
		eyeRow = e.value("row", eyeRow);
		eyeOffset = e.value("offset", eyeOffset);
		eyeSpan = e.value("span", eyeSpan);
	}

	mouthRow = std::clamp(mouthRow, 0, mouthLightsH - 1);
	mouthOffset = std::clamp(mouthOffset, 0, mouthLightsW - 1);
	mouthSpan = std::clamp(mouthSpan, 1, mouthLightsW - mouthOffset);
	mouthNeutralWidth = std::clamp(mouthNeutralWidth, 1, mouthSpan);
	eyeRow = std::clamp(eyeRow, 0, mouthLightsH - 1);
	eyeOffset = std::clamp(eyeOffset, 0, mouthLightsW - 1);
	eyeSpan = std::clamp(eyeSpan, 1,
		std::max(1, (mouthLightsW - 2 * eyeOffset) / 2));

	loaded = true;
	const char * modeName = windowMode == WindowMode::Fullscreen ? "fullscreen"
		: windowMode == WindowMode::Borderless ? "borderless"
											   : "windowed";
	ofLogNotice("ReceiverConfig") << "loaded " << path << " (port=" << listenPort
		<< ", canvas=" << width << "x" << height
		<< ", led=" << ledWidth << "x" << ledHeight
		<< ", window_mode=" << modeName
		<< ", vsync=" << (vsync ? "on" : "off")
		<< ", scale=" << scale
		<< ", mouth=" << mouthLightsW << "x" << mouthLightsH
		<< " row=" << mouthRow << " offset=" << mouthOffset
		<< " span=" << mouthSpan
		<< ", eyes row=" << eyeRow << " offset=" << eyeOffset
		<< " span=" << eyeSpan << ")";
	return true;
}
