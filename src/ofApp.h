#pragma once

#include "ofMain.h"
#include "ReceiverConfig.h"
#include "EyeStreamReceiver.h"

class ofApp : public ofBaseApp {
public:
	void setup() override;
	void update() override;
	void draw() override;
	void exit() override;

	void keyPressed(int key) override;

private:
	ReceiverConfig config;
	EyeStreamReceiver receiver;

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
};
