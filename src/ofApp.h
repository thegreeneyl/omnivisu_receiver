#pragma once

#include "ofMain.h"
#include "ofxGui.h"
#include "ReceiverConfig.h"
#include "EyeStreamReceiver.h"
#include "MouthDisplay.h"
#include "ArtnetSender.h"
#include "StreamRecorder.h"
#include "StreamPlayer.h"

class ofApp : public ofBaseApp {
public:
	/// The config is loaded in main() (the window mode must be known before
	/// the window exists) and handed in here.
	explicit ofApp(ReceiverConfig cfg)
		: config(std::move(cfg)) { }

	void setup() override;
	void update() override;
	void draw() override;
	void exit() override;

	void keyPressed(int key) override;

private:
	ReceiverConfig config;
	EyeStreamReceiver receiver;

	// Content is drawn anchored to the window's upper-left corner (the LED
	// controller samples the upper-left corner of the screen). The scale
	// factor only shrinks the layout for development in windowed mode; in
	// fullscreen/borderless it is forced to 1 so the LED pixels are exact.
	float drawScale = 1.0f;

	ofTexture frameTex;
	ofPixels framePixels;
	bool hasFrame = false;
	bool showInfo = true;
	float lastLogTime = 0.0f;

	// Latest mouth/fade state from the "MOUT" datagrams (same UDP port as
	// the video): the eyes' presence fade plus the sender's quantized mouth
	// target edges (light units of its grid).
	bool hasState = false;
	EyeStreamReceiver::MouthState mouthState;
	// Receiver-side mouth presentation: eases toward the live target while
	// the stream is alive, and toward the local neutral pose otherwise
	// (timeout, startup, fade-only packets). Drawn as the 18x5 fixture grid
	// below the eyes, ALWAYS at full opacity - the mouth is exempt from both
	// fades by design, so it never disappears.
	MouthDisplay mouthDisplay;
	bool warnedGridMismatch = false; ///< One-shot sender-span vs local-span warning.
	// ArtNet output of the fixture grid: the rasterized lights (mouth + eye
	// blocks) are read back each frame and sent as one DMX frame to every
	// configured universe.
	ArtnetSender artnet;
	// Whether the received fade darkens the rendered eyes ('a' toggles at
	// runtime; initial value from config). The numeric fade in the info bar
	// stays visible either way so the link can be verified.
	bool applyFade = true;

	// Measured rate of fully-received frames (the actual stream rate), sampled
	// from the receiver's frame counter. Distinct from the render fps.
	std::uint64_t lastRecvCount = 0;
	float lastRecvSampleTime = 0.0f;
	float receivedFps = 0.0f;

	// Stream-liveness tracking. State packets are the heartbeat: the sender
	// keeps sending them even while the video is gated off at fade 0, so
	// their absence (not the video's) means "sender is gone". A video stall
	// while the reported fade is up is treated as gone too (safety net for
	// one-way packet loss).
	std::uint64_t prevStateCount = 0;
	std::uint64_t prevFrameCount = 0;
	float lastStateTime = -1.0f;
	float lastVideoTime = -1.0f;
	bool streamAlive = false;

	// --- recording + playback of camera streams ---
	// A "camera stream" is VIDEO presence (completed frames within the
	// timeout), not the UDP link: the sender heartbeats state packets at
	// fade 0 without any video. Every stream is recorded into one of two
	// temp slot folders (ping-pong, so a new stream can record while the
	// previous clip still plays), optionally replayed immediately when it
	// ends, and optionally promoted into the permanent per-stream archive.
	StreamRecorder recorder;
	StreamPlayer player;

	/// What the LED area is showing / waiting for. Fades are sequential
	/// through black (single linkFade): a source switch first ramps the fade
	/// to 0, then swaps, then ramps back up.
	enum class Mode {
		Idle,        ///< Nothing to show; archive playback disabled.
		ArchiveWait, ///< Idle, waiting out the pause before an archive clip.
		Live,        ///< Live stream on screen (and being recorded).
		LiveEnded,   ///< Live gone: fading out, then waiting for the
		             ///< recorder's finalize result to decide what is next.
		PlayTemp,    ///< Immediate replay of the just-recorded temp clip.
		PlayArchive  ///< Automated playback from the permanent storage.
	};
	Mode mode = Mode::Idle;
	bool videoPresent = false;

	bool recordingActive = false;
	std::uint64_t activeRecordingId = 0;
	int recordSlot = 1; ///< Slot of the current/last recording; next uses the other.
	/// Set when a recording is finalized, cleared when its promote/discard
	/// has been queued - guards against resolving twice or never.
	bool tempNeedsResolve = false;
	StreamRecorder::Result tempResult; ///< Finalize result of the clip in PlayTemp.

	std::string playingDir;                    ///< Folder currently in the player.
	EyeStreamReceiver::MouthState playState;   ///< Latest replayed state.
	bool hasPlayState = false;

	std::string lastArchivePlayed; ///< Skip-repeat anchor for the archive order.
	float archiveNextTime = -1.0f; ///< When the next archive clip may start.

	/// Remote (source-side) fade of whatever source is on screen: the live
	/// packet fade or the replayed timeline fade. Combined with linkFade in
	/// draw(); kept as a member so update() picks the source once.
	float activeRemoteFade = 1.0f;

	// --- color grading of the final LED output ---
	// The doubled eye canvas is rendered into ledFbo at full LED resolution
	// and drawn to screen through the grade shader (same controls and shader
	// as the sender, so grading.json files are interchangeable). The fade
	// overlay is applied AFTER grading: a brightness lift must never turn
	// "faded to black" into gray on the building.
	ofParameterGroup gradingGroup{ "grading" };
	ofParameter<bool> enableGrading{ "enable grading", true };
	ofParameter<float> gradeExposure{ "exposure (stops)", 0.0f, -2.0f, 2.0f };
	ofParameter<float> gradeBrightness{ "brightness", 0.0f, -0.5f, 0.5f };
	ofParameter<float> gradeContrast{ "contrast", 1.0f, 0.0f, 2.0f };
	ofParameter<float> gradeGamma{ "gamma", 1.0f, 0.3f, 3.0f };
	ofParameter<float> gradeSaturation{ "saturation", 1.0f, 0.0f, 2.0f };
	ofxPanel gradingPanel;
	ofFbo ledFbo;
	ofShader gradeShader;
	bool gradeShaderLoaded = false;
	bool gradeShaderUsesRect = false;

	// Window mode chosen at startup; config reloads ('r') can't change it.
	ReceiverConfig::WindowMode activeWindowMode = ReceiverConfig::WindowMode::Windowed;

	bool buildGradeShader(bool useRect);
	void allocateLedFbo();
	/// Assembles the MouthDisplay parameters from the "mouth" config block.
	MouthDisplay::Config mouthDisplayConfig() const;
	/// Assembles the ArtnetSender parameters from the "artnet" config block.
	ArtnetSender::Config artnetConfig() const;
	bool loadGradingParams();
	bool saveGradingParams();
	/// Re-reads config.json for the runtime-safe values (fades, timeout,
	/// apply_fade, scale, LED size, vsync) and reloads grading.json. Window
	/// mode and listen port need a restart and are logged if changed.
	void reloadRuntimeConfig();

	// --- recording/playback helpers ---
	/// Absolute path of temp slot 0 ("a") or 1 ("b").
	std::string tempSlotDir(int slot) const;
	/// Uploads a new pixel buffer into frameTex (allocating on size change).
	void uploadFrame();
	/// Queues promote-or-discard of the last finalized recording (exactly
	/// once, guarded by tempNeedsResolve).
	void resolveFinishedRecording();
	/// Starts the player on dir and switches the mode (frame blanked until
	/// the first replayed frame arrives).
	void startPlayback(const std::string & dir, Mode playMode);
	/// Leaves a playback mode at fade 0: stops the player, resolves a temp
	/// clip, and returns to Live / ArchiveWait / Idle.
	void exitPlayback(float now);
	/// Where to go when there is nothing to show.
	void enterIdle(float now);
	/// Sets archiveNextTime to now + pause +- random deviation.
	void scheduleArchivePlay(float now);
	/// Picks the next archive clip honoring the configured order; empty
	/// string when the archive is empty.
	std::string pickArchiveClip();
	const char * modeName() const;

	// Local link fade [0..1]: ramps toward 1 while the current display source
	// (live stream or playback) should be visible, toward 0 while idle or
	// switching sources, so a frozen last frame fades out gracefully and the
	// next source fades in from black. Combined with the source's own fade by
	// multiplication (effective = link * remote), which resolves every
	// constellation without special cases: a stale remote value gets ramped
	// down by the link fade, and a returning source never snaps in even if
	// its own fade is already at 1.
	float linkFade = 0.0f;
};
