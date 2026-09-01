#include "ofMain.h"
#include "ofApp.h"
#include "ReceiverConfig.h"

//========================================================================
int main() {
	// The window mode must be known before the window exists, so the config
	// is loaded here (data-path resolution works pre-window) and handed to
	// ofApp instead of being loaded twice.
	ReceiverConfig config;
	config.load("config.json");

	ofGLFWWindowSettings settings;
	settings.setGLVersion(3, 3);

	// Windowed size: the LED area (scaled for small dev screens) plus the UI
	// band below it (mouth fixture grid, info text and grading panel; matches
	// ofApp::draw layout).
	const int w = static_cast<int>(config.getLedWidth() * config.getScale());
	const int h = static_cast<int>(config.getLedHeight() * config.getScale())
		+ config.getMouthBand() + 200;

	switch (config.getWindowMode()) {
	case ReceiverConfig::WindowMode::Fullscreen:
		settings.monitor = config.getDisplay();
		settings.windowMode = OF_FULLSCREEN;
		break;
	case ReceiverConfig::WindowMode::Borderless:
		// Undecorated window pinned to the screen origin so the LED area sits
		// exactly in the upper-left corner. macOS still draws the menu bar
		// above normal windows: enable "auto-hide menu bar" on the machine,
		// or use fullscreen mode instead.
		settings.decorated = false;
		settings.setPosition(glm::vec2(0, 0));
		settings.setSize(w, h);
		settings.windowMode = OF_WINDOW;
		break;
	case ReceiverConfig::WindowMode::Windowed:
	default:
		settings.setSize(w, h);
		settings.windowMode = OF_WINDOW;
		break;
	}

	auto window = ofCreateWindow(settings);

	ofRunApp(window, std::make_shared<ofApp>(std::move(config)));
	ofRunMainLoop();
}
