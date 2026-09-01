#include "MouthDisplay.h"

#include <algorithm>
#include <cmath>

//--------------------------------------------------------------
void MouthDisplay::setup(const Config & c) {
	cfg = c;
	cfg.lights.x = std::max(1, cfg.lights.x);
	cfg.lights.y = std::max(1, cfg.lights.y);
	cfg.row = std::clamp(cfg.row, 0, cfg.lights.y - 1);
	cfg.offset = std::clamp(cfg.offset, 0, cfg.lights.x - 1);
	cfg.span = std::clamp(cfg.span, 1, cfg.lights.x - cfg.offset);
	cfg.eyeRow = std::clamp(cfg.eyeRow, 0, cfg.lights.y - 1);
	cfg.eyeOffset = std::clamp(cfg.eyeOffset, 0, cfg.lights.x - 1);
	// Both eye blocks (inset from each edge) must fit the grid side by side.
	cfg.eyeSpan = std::clamp(cfg.eyeSpan, 1,
		std::max(1, (cfg.lights.x - 2 * cfg.eyeOffset) / 2));
	// Never below 1 light: the mouth must always show something.
	cfg.neutralWidth = std::clamp(cfg.neutralWidth, 1, cfg.span);
	cfg.transitionSeconds = std::max(0.0f, cfg.transitionSeconds);

	allocateFbos();

	if (!initialized) {
		// First setup: snap onto the neutral pose so the mouth is visible
		// immediately, before the first packet (or with no sender at all).
		computeIdleEdges(targetLeft, targetRight);
		currentLeft = targetLeft;
		currentRight = targetRight;
		idle = true;
		initialized = true;
	} else if (idle) {
		// Reload while idle: adopt a possibly changed neutral_width as the
		// new target and let the easer move there (no jump).
		computeIdleEdges(targetLeft, targetRight);
	}

	renderLights();

	ofLogNotice("MouthDisplay") << "setup: fixture " << cfg.lights.x << "x"
		<< cfg.lights.y << ", mouth row " << cfg.row
		<< " offset " << cfg.offset << " span " << cfg.span
		<< ", eyes row " << cfg.eyeRow << " offset " << cfg.eyeOffset
		<< " span " << cfg.eyeSpan
		<< ", neutral_width " << cfg.neutralWidth
		<< ", transition " << cfg.transitionSeconds << "s";
}

//--------------------------------------------------------------
void MouthDisplay::computeIdleEdges(float & left, float & right) const {
	const int w = std::clamp(cfg.neutralWidth, 1, cfg.span);
	const int l = static_cast<int>(std::lround((cfg.span - w) * 0.5f));
	left = static_cast<float>(l);
	right = static_cast<float>(l + w);
}

//--------------------------------------------------------------
void MouthDisplay::setTarget(float left, float right) {
	const float span = static_cast<float>(cfg.span);
	left = ofClamp(left, 0.0f, span);
	right = ofClamp(right, left, span);
	// Degenerate edges (e.g. a zeroed packet) fall back to neutral: the
	// mouth must never collapse to nothing.
	if (right - left < 0.5f) {
		setIdle();
		return;
	}
	targetLeft = left;
	targetRight = right;
	idle = false;
}

//--------------------------------------------------------------
void MouthDisplay::setIdle() {
	computeIdleEdges(targetLeft, targetRight);
	idle = true;
}

//--------------------------------------------------------------
void MouthDisplay::setEyeIntensity(float intensity) {
	eyeIntensity = ofClamp(intensity, 0.0f, 1.0f);
}

//--------------------------------------------------------------
void MouthDisplay::update(float dt) {
	if (!initialized) {
		return;
	}

	// Ease the current edges toward the target. Same curve as the sender's
	// preview: time constant transition_seconds / 3, so a move is ~95% done
	// after transition_seconds.
	if (cfg.transitionSeconds <= 0.0f || dt <= 0.0f) {
		currentLeft = targetLeft;
		currentRight = targetRight;
	} else {
		const float alpha = 1.0f - std::exp(-dt / (cfg.transitionSeconds / 3.0f));
		currentLeft += alpha * (targetLeft - currentLeft);
		currentRight += alpha * (targetRight - currentRight);
	}

	renderLights();
}

//--------------------------------------------------------------
void MouthDisplay::draw(float x, float y, float w, float h) const {
	if (!lightsFbo.isAllocated()) {
		return;
	}
	// Full opacity always: the mouth is exempt from the presence and link
	// fades by design. Cells are already opaque RGB (on = white, off = black).
	ofPushStyle();
	ofDisableAlphaBlending();
	ofSetColor(255);
	lightsFbo.getTexture().draw(x, y, w, h);
	ofPopStyle();
}

//--------------------------------------------------------------
const ofPixels & MouthDisplay::getLightPixels() {
	if (lightsFbo.isAllocated()) {
		lightsFbo.readToPixels(lightsPixels);
	}
	return lightsPixels;
}

//--------------------------------------------------------------
void MouthDisplay::allocateFbos() {
	const int ssW = cfg.lights.x * kSupersample;
	const int ssH = cfg.lights.y * kSupersample;

	// Skip when already allocated at the right size (config reload with an
	// unchanged grid).
	if (lightsFbo.isAllocated()
		&& static_cast<int>(lightsFbo.getWidth()) == cfg.lights.x
		&& static_cast<int>(lightsFbo.getHeight()) == cfg.lights.y
		&& lightsSsFbo.isAllocated()
		&& static_cast<int>(lightsSsFbo.getWidth()) == ssW
		&& static_cast<int>(lightsSsFbo.getHeight()) == ssH) {
		return;
	}

	// GL_TEXTURE_2D is required explicitly: OF's desktop default is a
	// rectangle texture target, which cannot have mipmaps (needed for the
	// area downsample).
	ofFboSettings ss;
	ss.width = ssW;
	ss.height = ssH;
	ss.internalformat = GL_RGB;
	ss.textureTarget = GL_TEXTURE_2D;
	ss.numSamples = 0;
	ss.useDepth = false;
	ss.useStencil = false;
	lightsSsFbo.allocate(ss);
	lightsSsFbo.begin();
	ofClear(0, 0, 0, 255);
	lightsSsFbo.end();
	// Mipmaps give a true area average when downsampling: kSupersample is a
	// power of two, so the lights.x x lights.y mip level is the exact mean of
	// each light's kSupersample^2 block. Generate once here so the texture
	// "has" mipmaps - setTextureMinMagFilter silently ignores mipmap filters
	// on textures without them.
	lightsSsFbo.getTexture().generateMipmap();
	lightsSsFbo.getTexture().setTextureMinMagFilter(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);

	ofFboSettings lf;
	lf.width = cfg.lights.x;
	lf.height = cfg.lights.y;
	lf.internalformat = GL_RGB;
	lf.textureTarget = GL_TEXTURE_2D;
	lf.numSamples = 0;
	lf.useDepth = false;
	lf.useStencil = false;
	lightsFbo.allocate(lf);
	// Nearest-neighbor so the on-screen upscale shows hard LED cells.
	lightsFbo.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);

	lightsFbo.begin();
	ofClear(0, 0, 0, 255);
	lightsFbo.end();
}

//--------------------------------------------------------------
void MouthDisplay::renderLights() {
	if (!lightsFbo.isAllocated() || !lightsSsFbo.isAllocated()) {
		return;
	}

	const float ss = static_cast<float>(kSupersample);
	const float ssW = lightsSsFbo.getWidth();
	const float ssH = lightsSsFbo.getHeight();
	// Edges live in mouth-span units; shift them onto the fixture by offset.
	float leftPx = ofClamp((static_cast<float>(cfg.offset) + currentLeft) * ss, 0.0f, ssW);
	float rightPx = ofClamp((static_cast<float>(cfg.offset) + currentRight) * ss, 0.0f, ssW);
	if (rightPx < leftPx) {
		rightPx = leftPx;
	}
	const float topPx = ofClamp(static_cast<float>(cfg.row) * ss, 0.0f, ssH);
	const float rowH = std::min(ss, ssH - topPx);

	// Opaque RGB raster: off cells are black, a fully covered mouth cell is
	// the configured color at full value (no alpha fade). Partial coverage
	// at the moving edges averages toward black in the downsample, which is
	// the RGB brightness the ArtNet path will eventually send.
	lightsSsFbo.begin();
	ofClear(0, 0, 0, 255);
	ofPushStyle();
	ofDisableAlphaBlending();
	ofFill();
	ofSetColor(cfg.color.r, cfg.color.g, cfg.color.b, 255);
	ofDrawRectangle(leftPx, topPx, rightPx - leftPx, rowH);

	// Eye lights: two blocks on the eye row, inset from each edge, at the
	// intensity fed via setEyeIntensity (the inverse of the camera fade).
	// Scaling the RGB directly keeps the raster opaque like the mouth.
	if (eyeIntensity > 0.0f) {
		ofSetColor(static_cast<int>(cfg.color.r * eyeIntensity),
			static_cast<int>(cfg.color.g * eyeIntensity),
			static_cast<int>(cfg.color.b * eyeIntensity), 255);
		const float eyeTopPx = ofClamp(static_cast<float>(cfg.eyeRow) * ss, 0.0f, ssH);
		const float eyeRowH = std::min(ss, ssH - eyeTopPx);
		const float eyeW = static_cast<float>(cfg.eyeSpan) * ss;
		const float leftEyeX = static_cast<float>(cfg.eyeOffset) * ss;
		const float rightEyeX = ssW - static_cast<float>(cfg.eyeOffset) * ss - eyeW;
		ofDrawRectangle(leftEyeX, eyeTopPx, eyeW, eyeRowH);
		ofDrawRectangle(rightEyeX, eyeTopPx, eyeW, eyeRowH);
	}
	ofPopStyle();
	lightsSsFbo.end();

	// Area-downsample into the light grid via the mipmap chain (see
	// allocateFbos). Blending stays off: this is a resolve, not a composite.
	lightsSsFbo.getTexture().generateMipmap();
	lightsFbo.begin();
	ofClear(0, 0, 0, 255);
	ofPushStyle();
	ofDisableAlphaBlending();
	ofSetColor(255);
	lightsSsFbo.draw(0.0f, 0.0f, lightsFbo.getWidth(), lightsFbo.getHeight());
	ofPopStyle();
	lightsFbo.end();
}
