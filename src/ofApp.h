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

	// Measured rate of fully-received frames (the actual stream rate), sampled
	// from the receiver's frame counter. Distinct from the render fps.
	std::uint64_t lastRecvCount = 0;
	float lastRecvSampleTime = 0.0f;
	float receivedFps = 0.0f;
};
