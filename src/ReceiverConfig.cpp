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

	// Stream storage: temp capture of every camera stream plus the optional
	// promotion into the permanent per-stream folder structure.
	if (json.contains("storage")) {
		const auto & s = json["storage"];
		storageTempDir = s.value("temp_dir", storageTempDir);
		storagePermanentDir = s.value("permanent_dir", storagePermanentDir);
		immediatePlayback = s.value("immediate_playback", immediatePlayback);
		permanentStorage = s.value("permanent_storage", permanentStorage);
		minFreeGb = std::max(0.0f, s.value("min_free_gb", minFreeGb));
		minClipSeconds = std::max(0.0f, s.value("min_duration_seconds", minClipSeconds));
	}

	// Automated playback from the permanent storage while no live stream and
	// no immediate replay is running.
	if (json.contains("archive_playback")) {
		const auto & a = json["archive_playback"];
		archiveEnabled = a.value("enabled", archiveEnabled);
		const std::string order = a.value("order", std::string("latest_first"));
		if (order == "oldest_first") {
			archiveOrder = ArchiveOrder::OldestFirst;
		} else if (order == "random") {
			archiveOrder = ArchiveOrder::Random;
		} else {
			if (order != "latest_first") {
				ofLogWarning("ReceiverConfig") << "unknown archive_playback.order '"
					<< order << "' - using latest_first";
			}
			archiveOrder = ArchiveOrder::LatestFirst;
		}
		archivePauseSeconds = std::max(0.0f, a.value("pause_seconds", archivePauseSeconds));
		archivePauseRandomSeconds = std::max(0.0f,
			a.value("pause_random_seconds", archivePauseRandomSeconds));
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

	// ArtNet output: node address, target universes, and the grid-to-channel
	// mapping (validated further in ArtnetSender::setup).
	if (json.contains("artnet")) {
		const auto & a = json["artnet"];
		artnetEnabled = a.value("enabled", artnetEnabled);
		artnetIp = a.value("ip", artnetIp);
		artnetPort = a.value("port", artnetPort);
		if (a.contains("universes") && a["universes"].is_array()
			&& !a["universes"].empty()) {
			artnetUniverses.clear();
			for (const auto & u : a["universes"]) {
				artnetUniverses.push_back(u.get<int>());
			}
		}
		artnetStartChannel = a.value("start_channel", artnetStartChannel);
		artnetColorOrder = a.value("color_order", artnetColorOrder);
		artnetStartCorner = a.value("start_corner", artnetStartCorner);
		artnetSnake = a.value("snake", artnetSnake);
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
