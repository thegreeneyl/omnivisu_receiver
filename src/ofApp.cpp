#include "ofApp.h"

#include "ofAppGLFWWindow.h"

#include <algorithm>
#include <sstream>

//--------------------------------------------------------------
void ofApp::setup() {
	ofSetWindowTitle("omnivisu receiver");
	ofBackground(0);

	// Window mode/size/position were applied at creation in main(). Here we
	// only derive the draw scale and installation niceties.
	ofSetVerticalSync(config.getVsync());
	const auto mode = config.getWindowMode();
	activeWindowMode = mode;
	if (mode == ReceiverConfig::WindowMode::Windowed) {
		drawScale = std::max(0.05f, config.getScale());
	} else {
		drawScale = 1.0f;
		ofHideCursor();
		if (mode == ReceiverConfig::WindowMode::Borderless) {
			// GLFW may have nudged the undecorated window; re-pin the origin.
			ofSetWindowPosition(0, 0);
		}
	}

	// The LED controller samples physical pixels; on a Retina/scaled display
	// one OF unit is more than one pixel and the corner region would come out
	// wrong. Run the output display at a non-scaled resolution.
	if (auto * glfw = dynamic_cast<ofAppGLFWWindow *>(ofGetWindowPtr())) {
		const int pixelScale = glfw->getPixelScreenCoordScale();
		if (pixelScale != 1) {
			ofLogWarning("omnivisu_receiver")
				<< "display pixel scale is " << pixelScale
				<< "x (Retina/scaled mode): the LED corner region will not be "
				<< "pixel-exact - switch the display to a non-scaled resolution";
		}
	}

	applyFade = config.getApplyFade();

	// Pre-allocate the canvas texture to the configured dimensions; loadData
	// re-allocates if an incoming frame ever differs.
	frameTex.allocate(config.getWidth(), config.getHeight(), GL_RGB);

	// Grading: FBO at full LED resolution, controls panel, persisted values.
	allocateLedFbo();
	gradingGroup.add(enableGrading);
	gradingGroup.add(gradeExposure);
	gradingGroup.add(gradeBrightness);
	gradingGroup.add(gradeContrast);
	gradingGroup.add(gradeGamma);
	gradingGroup.add(gradeSaturation);
	gradingPanel.setup(gradingGroup);
	loadGradingParams();

	if (!receiver.setup(config.getListenPort())) {
		ofLogError("omnivisu_receiver") << "failed to start stream receiver";
	}
}

//--------------------------------------------------------------
void ofApp::allocateLedFbo() {
	ofFboSettings fboSettings;
	fboSettings.width = config.getLedWidth();
	fboSettings.height = config.getLedHeight();
	fboSettings.internalformat = GL_RGB;
	fboSettings.numSamples = 0;
	fboSettings.useDepth = false;
	fboSettings.useStencil = false;
	ledFbo.allocate(fboSettings);
	ledFbo.begin();
	ofClear(0, 255);
	ledFbo.end();
}

//--------------------------------------------------------------
bool ofApp::buildGradeShader(bool useRect) {
	// Same shader as the sender (EyeCameraStream::buildGradeShader) so the
	// receiver's grade matches what the operator knows from there.
	std::string defines;
	if (useRect) {
		defines = "#define USE_RECT 1\n";
	}

	const std::string vert =
		"#version 150\n"
		+ defines +
		"uniform mat4 modelViewProjectionMatrix;\n"
		"uniform mat4 textureMatrix;\n"
		"in vec4 position;\n"
		"in vec2 texcoord;\n"
		"out vec2 vTexCoord;\n"
		"void main() {\n"
		"    vTexCoord = (textureMatrix * vec4(texcoord, 0.0, 1.0)).xy;\n"
		"    gl_Position = modelViewProjectionMatrix * position;\n"
		"}\n";

	const std::string frag =
		"#version 150\n"
		+ defines +
		"#ifdef USE_RECT\n"
		"uniform sampler2DRect tex0;\n"
		"#else\n"
		"uniform sampler2D tex0;\n"
		"#endif\n"
		"uniform float uExposure;\n"
		"uniform float uBrightness;\n"
		"uniform float uContrast;\n"
		"uniform float uGamma;\n"
		"uniform float uSaturation;\n"
		"in vec2 vTexCoord;\n"
		"out vec4 outColor;\n"
		"void main() {\n"
		"    vec3 c = texture(tex0, vTexCoord).rgb;\n"
		"    c *= exp2(uExposure);\n"
		"    c += vec3(uBrightness);\n"
		"    c = (c - vec3(0.5)) * uContrast + vec3(0.5);\n"
		"    c = pow(max(c, vec3(0.0)), vec3(1.0 / uGamma));\n"
		"    float luma = dot(c, vec3(0.299, 0.587, 0.114));\n"
		"    c = mix(vec3(luma), c, uSaturation);\n"
		"    c = clamp(c, vec3(0.0), vec3(1.0));\n"
		"    outColor = vec4(c, 1.0);\n"
		"}\n";

	gradeShader.unload();
	bool ok = true;
	ok = ok && gradeShader.setupShaderFromSource(GL_VERTEX_SHADER, vert);
	ok = ok && gradeShader.setupShaderFromSource(GL_FRAGMENT_SHADER, frag);
	ok = ok && gradeShader.bindDefaults();
	ok = ok && gradeShader.linkProgram();

	if (ok) {
		ofLogNotice("omnivisu_receiver") << "grade shader built (sampler="
			<< (useRect ? "sampler2DRect" : "sampler2D") << ")";
	} else {
		ofLogWarning("omnivisu_receiver")
			<< "grade shader failed to build; falling back to direct draw";
	}
	gradeShaderUsesRect = useRect;
	return ok;
}

//--------------------------------------------------------------
bool ofApp::loadGradingParams() {
	const auto path = ofToDataPath("grading.json", true);
	if (!ofFile::doesFileExist(path)) {
		ofLogWarning("omnivisu_receiver") << "grading file not found at " << path
			<< " - using defaults";
		return false;
	}
	try {
		const ofJson json = ofLoadJson(path);
		ofDeserialize(json, gradingGroup);
	} catch (const std::exception & e) {
		ofLogError("omnivisu_receiver") << "failed to load grading.json: " << e.what();
		return false;
	}
	ofLogNotice("omnivisu_receiver") << "grading loaded from " << path;
	return true;
}

//--------------------------------------------------------------
bool ofApp::saveGradingParams() {
	const auto path = ofToDataPath("grading.json", true);
	ofJson json;
	ofSerialize(json, gradingGroup);
	if (!ofSavePrettyJson(path, json)) {
		ofLogError("omnivisu_receiver") << "failed to save " << path;
		return false;
	}
	ofLogNotice("omnivisu_receiver") << "grading saved to " << path;
	return true;
}

//--------------------------------------------------------------
void ofApp::reloadRuntimeConfig() {
	const auto prevMode = config.getWindowMode();
	const int prevPort = config.getListenPort();

	config.load("config.json");

	if (activeWindowMode == ReceiverConfig::WindowMode::Windowed) {
		drawScale = std::max(0.05f, config.getScale());
	}
	ofSetVerticalSync(config.getVsync());
	applyFade = config.getApplyFade();
	if (ledFbo.getWidth() != config.getLedWidth()
		|| ledFbo.getHeight() != config.getLedHeight()) {
		allocateLedFbo();
	}
	if (config.getWindowMode() != prevMode) {
		ofLogNotice("omnivisu_receiver")
			<< "window_mode changed in config.json - restart to apply";
	}
	if (config.getListenPort() != prevPort) {
		ofLogNotice("omnivisu_receiver")
			<< "listen_port changed in config.json - restart to apply";
	}

	loadGradingParams();
	ofLogNotice("omnivisu_receiver") << "config + grading reloaded";
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

	// Two zones, both anchored to the window's upper-left corner:
	//
	// LED area (0,0 .. ledW,ledH): sampled by the LED controller. Only the
	// eye canvas renders here - twice side by side (left, right, left,
	// right) because the LED wraps the front and back of the building. Black
	// whenever there is no live stream.
	//
	// UI area (below ledH): mouth strip, status text, notices. Monochrome
	// white on black, laid out on fixed rows so nothing ever overlaps.
	const float ledW = config.getLedWidth() * drawScale;
	const float ledH = config.getLedHeight() * drawScale;

	// Effective fade: received presence fade times the local link fade. 'a'
	// only disables the *received* part (to inspect the raw stream); the link
	// fade always applies so a dead sender never leaves a frozen frame.
	const float remoteFade = (applyFade && hasState) ? stateFade : 1.0f;
	const float effectiveFade = remoteFade * linkFade;

	// Compose the LED content at full LED resolution in the FBO, then draw it
	// to screen through the grade shader so the grade applies to the final
	// rendered result. The fade overlay comes after grading: a brightness
	// lift must never turn "faded to black" into gray on the building.
	ledFbo.begin();
	ofClear(0, 255);
	if (hasFrame && frameTex.isAllocated()) {
		ofSetColor(255);
		const float fboHalfW = ledFbo.getWidth() * 0.5f;
		const float fboH = ledFbo.getHeight();
		frameTex.draw(0, 0, fboHalfW, fboH);
		frameTex.draw(fboHalfW, 0, fboHalfW, fboH);
	}
	ledFbo.end();

	// (Re)build the shader lazily for the FBO's actual sampler target.
	const bool needsRect = ledFbo.getTexture().getTextureData().textureTarget
		== GL_TEXTURE_RECTANGLE_ARB;
	if (!gradeShaderLoaded || gradeShaderUsesRect != needsRect) {
		gradeShaderLoaded = buildGradeShader(needsRect);
	}

	ofSetColor(255);
	if (enableGrading && gradeShaderLoaded) {
		gradeShader.begin();
		gradeShader.setUniformTexture("tex0", ledFbo.getTexture(), 0);
		gradeShader.setUniform1f("uExposure", gradeExposure.get());
		gradeShader.setUniform1f("uBrightness", gradeBrightness.get());
		gradeShader.setUniform1f("uContrast", gradeContrast.get());
		gradeShader.setUniform1f("uGamma", std::max(0.01f, gradeGamma.get()));
		gradeShader.setUniform1f("uSaturation", gradeSaturation.get());
		ledFbo.getTexture().draw(0, 0, ledW, ledH);
		gradeShader.end();
	} else {
		ledFbo.getTexture().draw(0, 0, ledW, ledH);
	}

	if (effectiveFade < 1.0f) {
		ofPushStyle();
		ofEnableAlphaBlending();
		ofFill();
		ofSetColor(0, 0, 0, static_cast<int>((1.0f - effectiveFade) * 255.0f));
		ofDrawRectangle(0.0f, 0.0f, ledW, ledH);
		ofPopStyle();
	}

	// ---- UI area ----
	const float pad = 8.0f;
	const float bandH = static_cast<float>(config.getMouthBand());
	const float stripY = ledH + pad;
	const float textY = stripY + bandH + 24.0f; // status row baseline
	const float noticeY = textY + 20.0f;        // notices row baseline

	// Mouth strip: the sender's LED grid upscaled nearest-neighbor with cell
	// ticks. The lights are dimmed by the effective fade so the strip shows
	// what the building's mouth actually does; the frame stays visible.
	if (bandH > 0.0f && lightsTex.isAllocated() && lightsTex.getWidth() > 0) {
		const float stripX = pad;
		const float stripW = ledW - 2.0f * pad;
		const float stripH = std::max(1.0f, bandH - 2.0f * pad);
		const int cells = static_cast<int>(lightsTex.getWidth());

		ofPushStyle();
		ofEnableAlphaBlending();
		ofSetColor(static_cast<int>(255.0f * effectiveFade));
		lightsTex.draw(stripX, stripY, stripW, stripH);
		ofNoFill();
		ofSetColor(90);
		const float cellPx = stripW / static_cast<float>(cells);
		for (int c = 1; c < cells; ++c) {
			const float x = stripX + c * cellPx;
			ofDrawLine(x, stripY, x, stripY + stripH);
		}
		ofSetColor(255);
		ofDrawRectangle(stripX, stripY, stripW, stripH);
		ofPopStyle();
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

		const float textX = pad;
		ofSetColor(255);
		ofDrawBitmapString(msg.str(), textX, textY);

		// Fade meter right of the text (bitmap glyphs are 8 px wide) showing
		// the effective fade at a glance even with apply off.
		const float meterX = textX + msg.str().size() * 8.0f + 16.0f;
		const float meterW = 80.0f;
		const float meterH = 10.0f;
		const float meterY = textY - meterH + 1.0f;
		ofPushStyle();
		ofNoFill();
		ofSetColor(255);
		ofDrawRectangle(meterX, meterY, meterW, meterH);
		ofFill();
		ofDrawRectangle(meterX, meterY, meterW * effectiveFade, meterH);
		ofPopStyle();
	}

	// Waiting notice (own row, below the status text so nothing overlaps):
	// before the first frame ever, or once a lost stream has fully faded to
	// black. The LED area itself stays black either way.
	if (!hasFrame || (!streamAlive && linkFade <= 0.0f)) {
		ofSetColor(255);
		ofDrawBitmapString("waiting for stream on UDP port "
			+ ofToString(config.getListenPort()), pad, noticeY);
	}

	// Grading panel: right side of the UI area, below the mouth strip. The
	// position is clamped every frame so it can never sit inside the LED
	// area regardless of window size or scale.
	const float panelX = std::max(pad, ledW - gradingPanel.getWidth() - pad);
	const float panelY = std::max(ledH + pad, stripY + bandH + pad);
	gradingPanel.setPosition(panelX, panelY);
	gradingPanel.draw();
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
	} else if (key == 's' || key == 'S') {
		saveGradingParams();
	} else if (key == 'r' || key == 'R') {
		reloadRuntimeConfig();
	}
}
