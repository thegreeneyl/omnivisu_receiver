#include "ofApp.h"

#include <algorithm>
#include <sstream>

//--------------------------------------------------------------
void ofApp::setup() {
	ofSetWindowTitle("omnivisu receiver");
	ofBackground(0);

	config.load("config.json");

	ofSetVerticalSync(config.getVsync());
	if (config.getFullscreen()) {
		ofSetFullscreen(true);
	} else {
		// Extra band below the eyes for the mouth strip so it never covers
		// the video canvas.
		const int w = static_cast<int>(config.getWidth() * config.getScale());
		const int h = static_cast<int>(config.getHeight() * config.getScale())
			+ config.getMouthBand();
		ofSetWindowShape(w, h);
	}

	applyFade = config.getApplyFade();

	// Pre-allocate the canvas texture to the configured dimensions; loadData
	// re-allocates if an incoming frame ever differs.
	frameTex.allocate(config.getWidth(), config.getHeight(), GL_RGB);

	if (!receiver.setup(config.getListenPort())) {
		ofLogError("omnivisu_receiver") << "failed to start stream receiver";
	}
}

//--------------------------------------------------------------
void ofApp::update() {
	if (receiver.getLatestFrame(framePixels) && framePixels.isAllocated()) {
		if (!frameTex.isAllocated()
			|| frameTex.getWidth() != framePixels.getWidth()
			|| frameTex.getHeight() != framePixels.getHeight()) {
			frameTex.allocate(framePixels);
		}
		frameTex.loadData(framePixels);
		hasFrame = true;
	}

	// Mouth/fade state: always take the latest (the fade must track live even
	// when the light grid is unchanged). If the sender vanishes we simply keep
	// the last state, mirroring the last-video-frame freeze.
	if (receiver.getLatestState(stateFade, stateLights)) {
		hasState = true;
		if (stateLights.isAllocated() && stateLights.getWidth() > 0) {
			if (!lightsTex.isAllocated()
				|| lightsTex.getWidth() != stateLights.getWidth()
				|| lightsTex.getHeight() != stateLights.getHeight()) {
				lightsTex.allocate(stateLights);
				// Hard LED cells, not a smear: nearest-neighbor upscale like
				// the sender's debug strip.
				lightsTex.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
			}
			lightsTex.loadData(stateLights);
		} else if (lightsTex.isAllocated()) {
			lightsTex.clear();
		}
	}

	const float now = ofGetElapsedTimef();

	// Sample the received-frame rate over a short window (the real stream rate,
	// independent of how fast we render).
	const std::uint64_t count = receiver.getFrameCount();
	if (lastRecvSampleTime <= 0.0f) {
		lastRecvSampleTime = now;
		lastRecvCount = count;
	} else if (now - lastRecvSampleTime >= 0.5f) {
		receivedFps = static_cast<float>(count - lastRecvCount) / (now - lastRecvSampleTime);
		lastRecvCount = count;
		lastRecvSampleTime = now;
	}

	// Liveness: timestamp packet arrivals via the receiver's counters. State
	// packets are the heartbeat (they keep flowing while the video is gated
	// off at fade 0); a video stall while the reported fade is up counts as
	// dead too, so a one-way packet-loss anomaly can't freeze the image.
	const std::uint64_t stateCountNow = receiver.getStateCount();
	if (stateCountNow != prevStateCount) {
		prevStateCount = stateCountNow;
		lastStateTime = now;
	}
	if (count != prevFrameCount) {
		prevFrameCount = count;
		lastVideoTime = now;
	}

	const float timeout = std::max(0.1f, config.getStreamTimeoutSeconds());
	bool alive = lastStateTime >= 0.0f && (now - lastStateTime) < timeout;
	if (alive && hasState && stateFade > 0.05f
		&& (lastVideoTime < 0.0f || now - lastVideoTime > timeout)) {
		alive = false;
	}
	if (alive != streamAlive) {
		streamAlive = alive;
		ofLogNotice("omnivisu_receiver")
			<< "stream " << (streamAlive ? "alive" : "lost")
			<< " (fading " << (streamAlive ? "in" : "out") << ")";
	}

	// Ramp the local link fade toward the liveness target. dt is clamped so a
	// long hitch (window drag, debugger pause) can't jump the fade.
	const float dt = std::min(0.1f, static_cast<float>(ofGetLastFrameTime()));
	if (streamAlive) {
		const float fadeIn = config.getFadeInSeconds();
		linkFade = (fadeIn > 1e-4f) ? std::min(1.0f, linkFade + dt / fadeIn) : 1.0f;
	} else {
		const float fadeOut = config.getFadeOutSeconds();
		linkFade = (fadeOut > 1e-4f) ? std::max(0.0f, linkFade - dt / fadeOut) : 0.0f;
	}

	if (now - lastLogTime >= 2.0f) {
		ofLogNotice("omnivisu_receiver") << "render_fps=" << ofToString(ofGetFrameRate(), 1)
			<< " recv_fps=" << ofToString(receivedFps, 1)
			<< " frames=" << receiver.getFrameCount()
			<< " dropped=" << receiver.getDroppedCount()
			<< " states=" << receiver.getStateCount()
			<< " fade=" << ofToString(stateFade, 2)
			<< (applyFade ? "" : " (not applied)")
			<< " link=" << ofToString(linkFade, 2)
			<< (streamAlive ? "" : " (stream lost)");
		lastLogTime = now;
	}
}

//--------------------------------------------------------------
void ofApp::draw() {
	ofClear(0, 255);

	const float winW = static_cast<float>(ofGetWidth());
	const float winH = static_cast<float>(ofGetHeight());
	// Bottom band reserved for the mouth strip; the eyes fill the rest. In
	// fullscreen the band is simply pinned to the bottom edge.
	const float bandH = std::min(static_cast<float>(config.getMouthBand()), winH);
	const float eyesH = winH - bandH;

	if (hasFrame && frameTex.isAllocated()) {
		ofSetColor(255);
		// Fill the eyes region; the window is sized to the canvas plus the
		// mouth band, so this preserves the side-by-side eye layout.
		frameTex.draw(0, 0, winW, eyesH);
	}

	// Mouth strip below the eyes: the sender's LED grid upscaled
	// nearest-neighbor with cell ticks, matching the sender's debug strip.
	if (bandH > 0.0f && lightsTex.isAllocated() && lightsTex.getWidth() > 0) {
		const float pad = 4.0f;
		const float stripX = pad;
		const float stripY = eyesH + pad;
		const float stripW = winW - 2.0f * pad;
		const float stripH = std::max(1.0f, bandH - 2.0f * pad);
		const int cells = static_cast<int>(lightsTex.getWidth());

		ofPushStyle();
		ofEnableAlphaBlending();
		ofFill();
		ofSetColor(0, 0, 0, 200); // dark backdrop so dim lights stay readable
		ofDrawRectangle(stripX - 2.0f, stripY - 2.0f, stripW + 4.0f, stripH + 4.0f);
		ofSetColor(255);
		lightsTex.draw(stripX, stripY, stripW, stripH);
		ofNoFill();
		ofSetColor(110, 110, 110, 200);
		const float cellPx = stripW / static_cast<float>(cells);
		for (int c = 1; c < cells; ++c) {
			const float x = stripX + c * cellPx;
			ofDrawLine(x, stripY, x, stripY + stripH);
		}
		ofDrawRectangle(stripX, stripY, stripW, stripH);
		ofPopStyle();
	}

	// Draw-time fade over eyes + mouth: the received presence fade multiplied
	// by the local link fade. 'a' only disables the *received* part (to
	// inspect the raw stream); the link fade always applies so a dead sender
	// never leaves a frozen frame on screen. The info bar below is drawn
	// after this overlay so it always stays readable.
	const float remoteFade = (applyFade && hasState) ? stateFade : 1.0f;
	const float effectiveFade = remoteFade * linkFade;
	if (effectiveFade < 1.0f) {
		ofPushStyle();
		ofEnableAlphaBlending();
		ofFill();
		ofSetColor(0, 0, 0, static_cast<int>((1.0f - effectiveFade) * 255.0f));
		ofDrawRectangle(0.0f, 0.0f, winW, winH);
		ofPopStyle();
	}

	// Waiting notice: before the first frame ever, or once a lost stream has
	// fully faded to black (the frozen frame stays underneath so a returning
	// sender fades in over it until fresh frames replace it).
	if (!hasFrame || (!streamAlive && linkFade <= 0.0f)) {
		ofSetColor(180);
		ofDrawBitmapString("omnivisu receiver - waiting for stream on UDP port "
			+ ofToString(config.getListenPort()), 20, 30);
	}

	if (showInfo) {
		std::ostringstream msg;
		msg << "recv " << ofToString(receivedFps, 1) << " fps"
			<< " | render " << ofToString(ofGetFrameRate(), 1)
			<< " | frames " << receiver.getFrameCount()
			<< " | dropped " << receiver.getDroppedCount()
			<< " | fade " << (hasState ? ofToString(stateFade, 2) : std::string("--"))
			<< " (" << (applyFade ? "applied" : "off") << ")"
			<< " | link " << ofToString(linkFade, 2)
			<< (streamAlive ? "" : " (lost)");

		const float textX = 20.0f;
		const float textY = winH - 20.0f;
		ofSetColor(0, 255, 0);
		ofDrawBitmapString(msg.str(), textX, textY);

		// Fade meter right of the text (bitmap glyphs are 8 px wide) so the
		// incoming value is visible at a glance even with apply off.
		const float meterX = textX + msg.str().size() * 8.0f + 16.0f;
		const float meterW = 80.0f;
		const float meterH = 10.0f;
		const float meterY = textY - meterH + 1.0f;
		ofPushStyle();
		ofNoFill();
		ofSetColor(0, 255, 0);
		ofDrawRectangle(meterX, meterY, meterW, meterH);
		if (hasState) {
			ofFill();
			ofDrawRectangle(meterX, meterY, meterW * stateFade, meterH);
		}
		ofPopStyle();
	}
}

//--------------------------------------------------------------
void ofApp::exit() {
	receiver.close();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
	if (key == 'i' || key == 'I') {
		showInfo = !showInfo;
	} else if (key == 'f' || key == 'F') {
		ofToggleFullscreen();
	} else if (key == 'a' || key == 'A') {
		// Toggle applying the received fade to the rendered image. The fade
		// value keeps arriving and stays visible in the info bar either way.
		applyFade = !applyFade;
		ofLogNotice("omnivisu_receiver") << "fade rendering "
			<< (applyFade ? "on" : "off");
	}
}
