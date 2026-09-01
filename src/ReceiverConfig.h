#pragma once

#include "ofMain.h"

#include <string>

/// Loads the receiver's config.json. Holds the UDP listen port, the expected
/// canvas dimensions (two eyes side-by-side), and window/display options.
class ReceiverConfig {
public:
	/// How the app window is created. The LED controller samples the
	/// upper-left corner of the screen, so content is always anchored there.
	/// - Windowed: normal decorated window (development).
	/// - Borderless: undecorated window pinned at (0,0). NOTE: macOS keeps
	///   the menu bar above normal windows; needs "auto-hide menu bar".
	/// - Fullscreen: true fullscreen, hides menu bar and dock (installation).
	enum class WindowMode { Windowed, Borderless, Fullscreen };

	/// Parses the given config file. Returns false (and logs) if the file is
	/// missing or unparseable; in that case getters return their defaults.
	bool load(const std::string & path);

	bool isLoaded() const { return loaded; }

	int getListenPort() const { return listenPort; }
	int getWidth() const { return width; }
	int getHeight() const { return height; }
	WindowMode getWindowMode() const { return windowMode; }
	/// Monitor index for fullscreen (multi-display setups).
	int getDisplay() const { return display; }
	bool getVsync() const { return vsync; }
	float getScale() const { return scale; }
	/// Initial state of applying the received fade to the rendered image
	/// (toggled at runtime with 'a'). The fade value itself is always shown
	/// in the info bar regardless.
	bool getApplyFade() const { return applyFade; }
	/// Height in pixels of the mouth strip band below the eyes.
	int getMouthBand() const { return mouthBand; }

	/// Seconds without a state packet before the sender counts as gone.
	/// State packets (not video) are the heartbeat: the sender keeps sending
	/// them even while the video stream is gated off at fade 0.
	float getStreamTimeoutSeconds() const { return streamTimeoutSeconds; }
	/// Ramp times of the local link fade that blacks out the frozen frame
	/// when the sender disappears and fades back in when it returns.
	float getFadeInSeconds() const { return fadeInSeconds; }
	float getFadeOutSeconds() const { return fadeOutSeconds; }

private:
	int listenPort = 12345;
	int width = 828;
	int height = 280;
	WindowMode windowMode = WindowMode::Windowed;
	int display = 0;
	bool vsync = true;
	float scale = 1.0f;
	bool applyFade = true;
	int mouthBand = 36;
	float streamTimeoutSeconds = 1.0f;
	float fadeInSeconds = 0.5f;
	float fadeOutSeconds = 2.0f;
	bool loaded = false;
};
