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

	// Mouth presentation: starts on its neutral pose, so the fixture grid is
	// visible immediately - before any packet and with no sender at all.
	mouthDisplay.setup(mouthDisplayConfig());

	artnet.setup(artnetConfig());

	// Recording/playback workers. The temp slots are wiped at startup (a
	// leftover from a crashed run is not resumable) and the receiver gets its
	// recording sink before the UDP thread starts.
	recorder.start();
	recorder.discard(tempSlotDir(0));
	recorder.discard(tempSlotDir(1));
	player.start();
	receiver.setRecorder(&recorder);

	if (!receiver.setup(config.getListenPort())) {
		ofLogError("omnivisu_receiver") << "failed to start stream receiver";
	}
}

//--------------------------------------------------------------
std::string ofApp::tempSlotDir(int slot) const {
	const std::string base = ofToDataPath(config.getStorageTempDir(), true);
	return base + (slot == 0 ? "/a" : "/b");
}

//--------------------------------------------------------------
MouthDisplay::Config ofApp::mouthDisplayConfig() const {
	MouthDisplay::Config c;
	c.lights = {config.getMouthLightsW(), config.getMouthLightsH()};
	c.row = config.getMouthRow();
	c.offset = config.getMouthOffset();
	c.span = config.getMouthSpan();
	c.eyeRow = config.getEyeRow();
	c.eyeOffset = config.getEyeOffset();
	c.eyeSpan = config.getEyeSpan();
	c.color = config.getMouthColor();
	c.neutralWidth = config.getMouthNeutralWidth();
	c.transitionSeconds = config.getMouthTransitionSeconds();
	return c;
}

//--------------------------------------------------------------
ArtnetSender::Config ofApp::artnetConfig() const {
	ArtnetSender::Config c;
	c.enabled = config.getArtnetEnabled();
	c.ip = config.getArtnetIp();
	c.port = config.getArtnetPort();
	c.universes = config.getArtnetUniverses();
	c.startChannel = config.getArtnetStartChannel();
	c.colorOrder = config.getArtnetColorOrder();
	c.startCorner = config.getArtnetStartCorner();
	c.snake = config.getArtnetSnake();
	return c;
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

	// Mouth presentation parameters (neutral_width, transition_seconds,
	// color, fixture grid, row/offset/span) are runtime-tunable; the eased
	// edges are kept so a reload never makes the mouth jump.
	mouthDisplay.setup(mouthDisplayConfig());
	warnedGridMismatch = false;

	// ArtNet is fully runtime-tunable: the socket is recreated on reload.
	artnet.setup(artnetConfig());

	loadGradingParams();
	ofLogNotice("omnivisu_receiver") << "config + grading reloaded";
}

//--------------------------------------------------------------
void ofApp::uploadFrame() {
	if (!frameTex.isAllocated()
		|| frameTex.getWidth() != framePixels.getWidth()
		|| frameTex.getHeight() != framePixels.getHeight()) {
		frameTex.allocate(framePixels);
	}
	frameTex.loadData(framePixels);
	hasFrame = true;
}

//--------------------------------------------------------------
void ofApp::update() {
	// Live frames are ALWAYS consumed (the receiver hands them out once) so a
	// source switch never finds a stale one; they are only uploaded to the
	// texture further down when live content is actually on screen.
	const bool liveFrameNew = receiver.getLatestFrame(framePixels)
		&& framePixels.isAllocated();

	// Mouth/fade state: always take the latest (the fade must track live even
	// when the target is unchanged). If the sender vanishes we keep the last
	// state; the mouth drive below then falls back onto the neutral pose.
	if (receiver.getLatestState(mouthState)) {
		hasState = true;
		// The packet's grid size is the sender's mouth SPAN (typically 14x1),
		// not the physical 18x5 fixture. Edges are in those span units; a
		// mismatch distorts the pose, so it is worth one loud log line.
		if (mouthState.hasTarget && !warnedGridMismatch
			&& (mouthState.lightsW != mouthDisplay.getMouthSpan()
				|| mouthState.lightsH != 1)) {
			warnedGridMismatch = true;
			ofLogWarning("omnivisu_receiver")
				<< "sender mouth span " << mouthState.lightsW << "x" << mouthState.lightsH
				<< " differs from local mouth.span "
				<< mouthDisplay.getMouthSpan() << "x1"
				<< " - fix config.json so the spans match";
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
	if (alive && hasState && mouthState.fade > 0.05f
		&& (lastVideoTime < 0.0f || now - lastVideoTime > timeout)) {
		alive = false;
	}
	if (alive != streamAlive) {
		streamAlive = alive;
		ofLogNotice("omnivisu_receiver")
			<< "stream " << (streamAlive ? "alive" : "lost");
	}

	// A camera STREAM is video presence: completed frames within the timeout.
	// The heartbeat above only says "sender process is up"; video starting and
	// stopping is what bounds a recording and what wins over any playback.
	videoPresent = lastVideoTime >= 0.0f && (now - lastVideoTime) < timeout;

	// --- recording control (independent of what is on screen) ---
	// Every video presence is captured; the ping-pong slot keeps the folder a
	// still-running playback reads from untouched.
	if (videoPresent && !recordingActive) {
		recordSlot = 1 - recordSlot;
		activeRecordingId = recorder.startRecording(tempSlotDir(recordSlot));
		recordingActive = true;
		ofLogNotice("omnivisu_receiver") << "camera stream started - recording to slot "
			<< (recordSlot == 0 ? "a" : "b");
	} else if (!videoPresent && recordingActive) {
		recorder.finalizeRecording();
		recordingActive = false;
		tempNeedsResolve = true;
		ofLogNotice("omnivisu_receiver") << "camera stream ended - finalizing recording";
	}

	// --- display-source state machine ---
	// Live always wins; switches are sequential through black (fade out the
	// old source, swap at fade 0, fade the new one in).
	switch (mode) {
	case Mode::Idle:
		if (videoPresent) {
			mode = Mode::Live;
		} else if (config.getArchivePlaybackEnabled()) {
			enterIdle(now); // moves to ArchiveWait with a scheduled start
		}
		break;

	case Mode::ArchiveWait:
		if (videoPresent) {
			mode = Mode::Live;
		} else if (!config.getArchivePlaybackEnabled()) {
			mode = Mode::Idle;
		} else if (archiveNextTime >= 0.0f && now >= archiveNextTime) {
			const std::string clip = pickArchiveClip();
			if (!clip.empty()) {
				startPlayback(clip, Mode::PlayArchive);
			} else {
				scheduleArchivePlay(now); // archive empty: retry after a pause
			}
		}
		break;

	case Mode::Live:
		if (!videoPresent) {
			mode = Mode::LiveEnded; // finalize is already queued above
		}
		break;

	case Mode::LiveEnded: {
		if (videoPresent) {
			// The person came back before we settled: the previous clip will
			// never be played, so resolve it (promote/discard) right away.
			resolveFinishedRecording();
			mode = Mode::Live;
			break;
		}
		if (linkFade > 0.0f) {
			break; // still fading out the frozen last live frame
		}
		StreamRecorder::Result res;
		if (!recorder.getResult(activeRecordingId, res)) {
			break; // writer still flushing the tail; stay black meanwhile
		}
		tempResult = res;
		if (res.frameCount > 0 && config.getImmediatePlayback()) {
			startPlayback(res.dir, Mode::PlayTemp);
		} else {
			resolveFinishedRecording();
			enterIdle(now);
		}
		break;
	}

	case Mode::PlayTemp:
	case Mode::PlayArchive: {
		const auto st = player.getStatus();
		const bool wantExit = videoPresent
			|| st == StreamPlayer::Status::Finished
			|| st == StreamPlayer::Status::Idle;
		if (wantExit && linkFade <= 0.0f) {
			exitPlayback(now);
		}
		break;
	}
	}

	// --- source plumbing: frames + state of whatever is on screen ---
	const bool playbackMode = (mode == Mode::PlayTemp || mode == Mode::PlayArchive);
	if (playbackMode) {
		if (player.getLatestFrame(framePixels) && framePixels.isAllocated()) {
			uploadFrame();
		}
		if (player.getLatestState(playState)) {
			hasPlayState = true;
		}
	} else if (liveFrameNew) {
		uploadFrame();
	}

	// Fade target: 1 while the current source should be visible, 0 while
	// idle, waiting, or fading out toward a source switch (videoPresent
	// during playback = live wants the screen).
	float fadeTarget = 0.0f;
	if (mode == Mode::Live) {
		fadeTarget = 1.0f;
	} else if (playbackMode) {
		fadeTarget = (!videoPresent && player.getStatus() == StreamPlayer::Status::Playing)
			? 1.0f : 0.0f;
	}

	// Ramp the local link fade toward the target. dt is clamped so a long
	// hitch (window drag, debugger pause) can't jump the fade.
	const float dt = std::min(0.1f, static_cast<float>(ofGetLastFrameTime()));
	if (fadeTarget > linkFade) {
		const float fadeIn = config.getFadeInSeconds();
		linkFade = (fadeIn > 1e-4f) ? std::min(fadeTarget, linkFade + dt / fadeIn) : fadeTarget;
	} else if (fadeTarget < linkFade) {
		const float fadeOut = config.getFadeOutSeconds();
		linkFade = (fadeOut > 1e-4f) ? std::max(fadeTarget, linkFade - dt / fadeOut) : fadeTarget;
	}

	// Drive the mouth from the active source: replayed timeline states during
	// playback, the live target while the stream is alive, and the local
	// neutral pose otherwise. The mouth itself is never faded - it eases
	// between poses instead.
	if (playbackMode) {
		if (player.getStatus() == StreamPlayer::Status::Playing
			&& hasPlayState && playState.hasTarget) {
			mouthDisplay.setTarget(playState.targetLeft, playState.targetRight);
		} else {
			mouthDisplay.setIdle();
		}
		activeRemoteFade = (applyFade && hasPlayState) ? playState.fade : 1.0f;
	} else {
		if (streamAlive && hasState && mouthState.hasTarget) {
			mouthDisplay.setTarget(mouthState.targetLeft, mouthState.targetRight);
		} else {
			mouthDisplay.setIdle();
		}
		activeRemoteFade = (applyFade && hasState) ? mouthState.fade : 1.0f;
	}
	// Eye lights run INVERSE to the effective camera-image fade (same value
	// draw() darkens the eyes with): image faded to black -> eye lights fully
	// on; image fully visible -> eye lights off.
	mouthDisplay.setEyeIntensity(1.0f - activeRemoteFade * linkFade);
	mouthDisplay.update(dt);

	// Ship the freshly rasterized fixture grid to the lights. Same frame to
	// every configured universe (one universe per building side).
	if (artnet.isEnabled()) {
		artnet.send(mouthDisplay.getLightPixels());
	}

	if (now - lastLogTime >= 2.0f) {
		ofLogNotice("omnivisu_receiver") << "render_fps=" << ofToString(ofGetFrameRate(), 1)
			<< " recv_fps=" << ofToString(receivedFps, 1)
			<< " frames=" << receiver.getFrameCount()
			<< " dropped=" << receiver.getDroppedCount()
			<< " states=" << receiver.getStateCount()
			<< " fade=" << ofToString(activeRemoteFade, 2)
			<< (applyFade ? "" : " (not applied)")
			<< " link=" << ofToString(linkFade, 2)
			<< " mode=" << modeName()
			<< (recordingActive ? (recordSlot == 0 ? " rec=a" : " rec=b") : "")
			<< " mouth=" << (mouthDisplay.isIdle() ? "idle" : "live")
			<< " artnet=" << (artnet.isEnabled()
				? ofToString(artnet.getPacketCount()) : std::string("off"))
			<< (recorder.getDroppedItems() > 0
				? " rec_dropped=" + ofToString(recorder.getDroppedItems()) : std::string())
			<< (streamAlive ? "" : " (stream lost)");
		lastLogTime = now;
	}
}

//--------------------------------------------------------------
void ofApp::resolveFinishedRecording() {
	if (!tempNeedsResolve) {
		return;
	}
	tempNeedsResolve = false;
	// The recorder's ordered queue guarantees this acts on the recording
	// finalized before it - even if a new one has already started behind it.
	recorder.resolveLastRecording(config.getPermanentStorage(),
		ofToDataPath(config.getStoragePermanentDir(), true),
		static_cast<double>(config.getMinFreeGb()),
		static_cast<double>(config.getMinClipSeconds()));
}

//--------------------------------------------------------------
void ofApp::startPlayback(const std::string & dir, Mode playMode) {
	player.startPlayback(dir);
	playingDir = dir;
	hasPlayState = false;
	hasFrame = false; // stay black until the first replayed frame arrives
	mode = playMode;
	ofLogNotice("omnivisu_receiver")
		<< (playMode == Mode::PlayTemp ? "immediate replay: " : "archive playback: ")
		<< dir;
}

//--------------------------------------------------------------
void ofApp::exitPlayback(float now) {
	player.stopPlayback();
	if (mode == Mode::PlayTemp) {
		// The replayed clip is done (or was interrupted by a new stream):
		// promote or discard it now, before its slot can be reused. Remember
		// it as "just played" so latest-first doesn't immediately repeat it
		// from the archive.
		if (config.getPermanentStorage() && !tempResult.timestampName.empty()) {
			lastArchivePlayed = ofToDataPath(config.getStoragePermanentDir(), true)
				+ "/" + tempResult.timestampName;
		}
		resolveFinishedRecording();
	} else {
		lastArchivePlayed = playingDir;
	}
	playingDir.clear();
	hasPlayState = false;
	hasFrame = false;
	if (videoPresent) {
		mode = Mode::Live;
	} else {
		enterIdle(now);
	}
}

//--------------------------------------------------------------
void ofApp::enterIdle(float now) {
	if (config.getArchivePlaybackEnabled()) {
		mode = Mode::ArchiveWait;
		scheduleArchivePlay(now);
	} else {
		mode = Mode::Idle;
	}
}

//--------------------------------------------------------------
void ofApp::scheduleArchivePlay(float now) {
	const float base = config.getArchivePauseSeconds();
	const float dev = config.getArchivePauseRandomSeconds();
	archiveNextTime = now + std::max(0.0f, base + ofRandom(-dev, dev));
}

//--------------------------------------------------------------
std::string ofApp::pickArchiveClip() {
	ofDirectory dir(ofToDataPath(config.getStoragePermanentDir(), true));
	if (!dir.exists()) {
		return "";
	}
	dir.listDir();
	std::vector<std::string> clips;
	for (const auto & f : dir.getFiles()) {
		if (f.isDirectory()) {
			clips.push_back(f.getAbsolutePath());
		}
	}
	if (clips.empty()) {
		return "";
	}
	// Folder names are "%Y-%m-%d_%H-%M-%S", so a lexical sort is chronological.
	std::sort(clips.begin(), clips.end());

	using Order = ReceiverConfig::ArchiveOrder;
	const Order order = config.getArchiveOrder();
	if (order == Order::Random) {
		if (clips.size() == 1) {
			return clips.front();
		}
		std::string pick;
		do {
			const int idx = std::min(static_cast<int>(clips.size()) - 1,
				static_cast<int>(ofRandom(static_cast<float>(clips.size()))));
			pick = clips[static_cast<std::size_t>(idx)];
		} while (pick == lastArchivePlayed);
		return pick;
	}

	if (order == Order::LatestFirst) {
		std::reverse(clips.begin(), clips.end());
	}
	if (lastArchivePlayed.empty()) {
		return clips.front();
	}
	const auto it = std::find(clips.begin(), clips.end(), lastArchivePlayed);
	if (it == clips.end()) {
		return clips.front();
	}
	const std::size_t next = (static_cast<std::size_t>(std::distance(clips.begin(), it)) + 1)
		% clips.size();
	return clips[next];
}

//--------------------------------------------------------------
const char * ofApp::modeName() const {
	switch (mode) {
	case Mode::Idle: return "idle";
	case Mode::ArchiveWait: return "archive-wait";
	case Mode::Live: return "live";
	case Mode::LiveEnded: return "live-ended";
	case Mode::PlayTemp: return "replay";
	case Mode::PlayArchive: return "archive";
	}
	return "?";
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
	// UI area (below ledH): mouth fixture grid, status text, notices.
	// Monochrome white on black, laid out on fixed rows so nothing overlaps.
	const float ledW = config.getLedWidth() * drawScale;
	const float ledH = config.getLedHeight() * drawScale;

	// Effective fade: the active source's fade (live packet fade or replayed
	// timeline fade, picked in update()) times the local link fade. 'a' only
	// disables the *source* part (to inspect the raw stream); the link fade
	// always applies so a dead source never leaves a frozen frame. The fade
	// darkens the EYES only - the mouth grid below stays fully visible and
	// eases to its neutral pose instead.
	const float effectiveFade = activeRemoteFade * linkFade;

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

	// Mouth fixture grid: the receiver-side 18x5 RGB lights (eased live
	// target or the neutral idle pose, occupying the 4th row) upscaled
	// nearest-neighbor with cell ticks. NEVER dimmed by the fades: the mouth
	// must always be present on the building; with no data it shows the
	// neutral pose instead of disappearing.
	if (bandH > 0.0f && mouthDisplay.isAllocated()) {
		const float stripX = pad;
		const float stripW = ledW - 2.0f * pad;
		const float stripH = std::max(1.0f, bandH - 2.0f * pad);
		const int cols = mouthDisplay.getGridSize().x;
		const int rows = mouthDisplay.getGridSize().y;

		mouthDisplay.draw(stripX, stripY, stripW, stripH);
		ofPushStyle();
		ofEnableAlphaBlending();
		ofNoFill();
		ofSetColor(90);
		const float cellW = stripW / static_cast<float>(cols);
		const float cellH = stripH / static_cast<float>(rows);
		for (int c = 1; c < cols; ++c) {
			const float x = stripX + c * cellW;
			ofDrawLine(x, stripY, x, stripY + stripH);
		}
		for (int r = 1; r < rows; ++r) {
			const float y = stripY + r * cellH;
			ofDrawLine(stripX, y, stripX + stripW, y);
		}
		ofSetColor(255);
		ofDrawRectangle(stripX, stripY, stripW, stripH);
		ofPopStyle();
	}

	if (showInfo) {
		std::ostringstream msg;
		msg << "main " << ofToString(ofGetFrameRate(), 1) << " fps"
			<< " | recv " << ofToString(receivedFps, 1) << " fps"
			<< " | frames " << receiver.getFrameCount()
			<< " | dropped " << receiver.getDroppedCount()
			<< " | fade " << ofToString(activeRemoteFade, 2)
			<< " (" << (applyFade ? "applied" : "off") << ")"
			<< " | link " << ofToString(linkFade, 2)
			<< " | " << modeName()
			<< (recordingActive ? (recordSlot == 0 ? " (rec a)" : " (rec b)") : "")
			<< " | mouth " << (mouthDisplay.isIdle() ? "idle" : "live")
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
	// only while genuinely idle - never over a running or starting playback.
	// The LED area itself stays black either way.
	if (mode == Mode::ArchiveWait) {
		ofSetColor(255);
		ofDrawBitmapString("waiting for stream on UDP port "
			+ ofToString(config.getListenPort())
			+ " | next archive clip in "
			+ ofToString(std::max(0.0f, archiveNextTime - ofGetElapsedTimef()), 0) + "s",
			pad, noticeY);
	} else if (mode == Mode::Idle && (!hasFrame || linkFade <= 0.0f)) {
		ofSetColor(255);
		ofDrawBitmapString("waiting for stream on UDP port "
			+ ofToString(config.getListenPort()), pad, noticeY);
	}

	// Grading panel: right side of the UI area, below the mouth grid. The
	// position is clamped every frame so it can never sit inside the LED
	// area regardless of window size or scale.
	const float panelX = std::max(pad, ledW - gradingPanel.getWidth() - pad);
	const float panelY = std::max(ledH + pad, stripY + bandH + pad);
	gradingPanel.setPosition(panelX, panelY);
	gradingPanel.draw();
}

//--------------------------------------------------------------
void ofApp::exit() {
	// Stop the producer first, then flush the writer: finalize a recording
	// that is still open and promote/discard the last finalized one before
	// the recorder drains its queue and joins.
	receiver.close();
	player.close();
	if (recordingActive) {
		recorder.finalizeRecording();
		recordingActive = false;
		tempNeedsResolve = true;
	}
	resolveFinishedRecording();
	recorder.close();
	artnet.close();
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
