#pragma once

#include "ofMain.h"

#include <string>
#include <vector>

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
	/// LED area: the upper-left region of the screen sampled by the LED
	/// controller. Only the (doubled) eye canvas may render inside it; all
	/// text/UI goes below.
	int getLedWidth() const { return ledWidth; }
	int getLedHeight() const { return ledHeight; }
	WindowMode getWindowMode() const { return windowMode; }
	/// Monitor index for fullscreen (multi-display setups).
	int getDisplay() const { return display; }
	bool getVsync() const { return vsync; }
	float getScale() const { return scale; }
	/// Initial state of applying the received fade to the rendered image
	/// (toggled at runtime with 'a'). The fade value itself is always shown
	/// in the info bar regardless.
	bool getApplyFade() const { return applyFade; }
	/// Height in pixels of the mouth-grid band below the eyes.
	int getMouthBand() const { return mouthBand; }

	/// Seconds without a state packet before the sender counts as gone.
	/// State packets (not video) are the heartbeat: the sender keeps sending
	/// them even while the video stream is gated off at fade 0.
	float getStreamTimeoutSeconds() const { return streamTimeoutSeconds; }
	/// Ramp times of the local link fade that blacks out the frozen frame
	/// when the sender disappears and fades back in when it returns.
	float getFadeInSeconds() const { return fadeInSeconds; }
	float getFadeOutSeconds() const { return fadeOutSeconds; }

	// --- mouth presentation (config "mouth" block) ---
	// The wire only carries target edges in the mouth-span (typically 14x1);
	// these parameters shape how the receiver places that span on the
	// physical fixture grid, animates, and idles - reloadable at runtime ('r').
	/// Physical RGB fixture grid the mouth is rasterized into (origin top-left).
	int getMouthLightsW() const { return mouthLightsW; }
	int getMouthLightsH() const { return mouthLightsH; }
	/// Mouth row on the fixture, 0-based from the top (3 = 4th line).
	int getMouthRow() const { return mouthRow; }
	/// Mouth column inset from the left edge of the fixture.
	int getMouthOffset() const { return mouthOffset; }
	/// Mouth width in lights; this is the sender's coordinate space. The
	/// packet's lightsW/H is checked against (span, 1) and logged on mismatch.
	int getMouthSpan() const { return mouthSpan; }
	/// Eye-light row on the fixture, 0-based from the top (1 = 2nd line).
	int getEyeRow() const { return eyeRow; }
	/// Eye block inset from EACH edge of the fixture.
	int getEyeOffset() const { return eyeOffset; }
	/// Width of each eye block in lights.
	int getEyeSpan() const { return eyeSpan; }
	ofColor getMouthColor() const { return mouthColor; }
	/// Width (in lights) of the neutral idle pose, shown centered on the
	/// mouth span whenever no live target exists (timeout, startup, fade-only
	/// packets).
	int getMouthNeutralWidth() const { return mouthNeutralWidth; }
	/// Ease duration toward a new target (~95% done after this time).
	float getMouthTransitionSeconds() const { return mouthTransitionSeconds; }

	// --- ArtNet output (config "artnet" block) ---
	// The fixture grid is one universe's DMX frame, sent identically to every
	// listed universe (the building repeats the lights on two sides).
	bool getArtnetEnabled() const { return artnetEnabled; }
	std::string getArtnetIp() const { return artnetIp; }
	int getArtnetPort() const { return artnetPort; }
	const std::vector<int> & getArtnetUniverses() const { return artnetUniverses; }
	/// 1-based DMX address of the first light's first channel.
	int getArtnetStartChannel() const { return artnetStartChannel; }
	/// Channel order per light, chars R/G/B/W (e.g. "RGB", "GBR", "RGBW").
	std::string getArtnetColorOrder() const { return artnetColorOrder; }
	/// Traversal start: top_left/top_right/bottom_left/bottom_right.
	std::string getArtnetStartCorner() const { return artnetStartCorner; }
	/// Serpentine rows (true) or every row in the same direction (false).
	bool getArtnetSnake() const { return artnetSnake; }

private:
	int listenPort = 12345;
	int width = 828;
	int height = 280;
	int ledWidth = 2688;
	int ledHeight = 504;
	WindowMode windowMode = WindowMode::Windowed;
	int display = 0;
	bool vsync = true;
	float scale = 1.0f;
	bool applyFade = true;
	int mouthBand = 96;
	float streamTimeoutSeconds = 1.0f;
	float fadeInSeconds = 0.5f;
	float fadeOutSeconds = 2.0f;
	int mouthLightsW = 18;
	int mouthLightsH = 5;
	int mouthRow = 3;
	int mouthOffset = 2;
	int mouthSpan = 14;
	int eyeRow = 1;
	int eyeOffset = 2;
	int eyeSpan = 3;
	ofColor mouthColor{255, 255, 255, 255};
	int mouthNeutralWidth = 6;
	float mouthTransitionSeconds = 0.2f;
	bool artnetEnabled = false;
	std::string artnetIp = "255.255.255.255";
	int artnetPort = 6454;
	std::vector<int> artnetUniverses{0, 1};
	int artnetStartChannel = 1;
	std::string artnetColorOrder = "RGB";
	std::string artnetStartCorner = "top_left";
	bool artnetSnake = true;
	bool loaded = false;
};
