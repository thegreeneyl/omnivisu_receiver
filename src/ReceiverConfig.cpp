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
		fullscreen = r.value("fullscreen", fullscreen);
		vsync = r.value("vsync", vsync);
		scale = r.value("scale", scale);
	}

	loaded = true;
	ofLogNotice("ReceiverConfig") << "loaded " << path << " (port=" << listenPort
		<< ", canvas=" << width << "x" << height
		<< ", fullscreen=" << (fullscreen ? "on" : "off")
		<< ", vsync=" << (vsync ? "on" : "off")
		<< ", scale=" << scale << ")";
	return true;
}
