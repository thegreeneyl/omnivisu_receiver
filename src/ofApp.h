#pragma once

#include "ofMain.h"
#include "ReceiverConfig.h"
#include "EyeStreamReceiver.h"

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
	// the video). The lights texture is the sender's LED grid (e.g. 14x1),
	// drawn nearest-neighbor as a strip below the eyes.
	bool hasState = false;
	float stateFade = 0.0f;
	ofPixels stateLights;
	ofTexture lightsTex;
	// Whether the received fade darkens the rendered eyes + mouth ('a'
	// toggles at runtime; initial value from config). The numeric fade in
	// the info bar stays visible either way so the link can be verified.
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

	// Local link fade [0..1]: ramps toward 1 while the stream is alive,
	// toward 0 when it disappears, so a frozen last frame fades out
	// gracefully and a returning stream fades back in. Combined with the
	// received fade by multiplication (effective = link * remote), which
	// resolves every constellation without special cases: a stale remote
	// value gets ramped down by the link fade, and a returning sender never
	// snaps in even if its own fade is already at 1.
	float linkFade = 0.0f;
};
