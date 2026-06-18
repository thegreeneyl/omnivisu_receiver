#pragma once

#include "ofMain.h"

#include <string>

/// Loads the receiver's config.json. Holds the UDP listen port, the expected
/// canvas dimensions (two eyes side-by-side), and window/display options.
class ReceiverConfig {
public:
	/// Parses the given config file. Returns false (and logs) if the file is
	/// missing or unparseable; in that case getters return their defaults.
	bool load(const std::string & path);

	bool isLoaded() const { return loaded; }

	int getListenPort() const { return listenPort; }
	int getWidth() const { return width; }
	int getHeight() const { return height; }
	bool getFullscreen() const { return fullscreen; }
	bool getVsync() const { return vsync; }
	float getScale() const { return scale; }

private:
	int listenPort = 12345;
	int width = 828;
	int height = 280;
	bool fullscreen = false;
	bool vsync = true;
	float scale = 1.0f;
	bool loaded = false;
};
