#include "MouthDisplay.h"

#include <algorithm>
#include <cmath>

//--------------------------------------------------------------
void MouthDisplay::setup(const Config & c) {
	cfg = c;
	cfg.lights.x = std::max(1, cfg.lights.x);
	cfg.lights.y = std::max(1, cfg.lights.y);
	// Never below 1 light: the mouth must always show something.
	cfg.neutralWidth = std::clamp(cfg.neutralWidth, 1, cfg.lights.x);
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

	ofLogNotice("MouthDisplay") << "setup: lights " << cfg.lights.x << "x"
		<< cfg.lights.y << ", neutral_width " << cfg.neutralWidth
		<< ", transition " << cfg.transitionSeconds << "s";
}

//--------------------------------------------------------------
void MouthDisplay::computeIdleEdges(float & left, float & right) const {
	const int w = std::clamp(cfg.neutralWidth, 1, cfg.lights.x);
	const int l = static_cast<int>(std::lround((cfg.lights.x - w) * 0.5f));
	left = static_cast<float>(l);
	right = static_cast<float>(l + w);
}

//--------------------------------------------------------------
void MouthDisplay::setTarget(float left, float right) {
	const float gridW = static_cast<float>(cfg.lights.x);
	left = ofClamp(left, 0.0f, gridW);
	right = ofClamp(right, left, gridW);
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
	// fades by design.
	ofPushStyle();
	ofEnableAlphaBlending();
	ofSetColor(255);
	lightsFbo.getTexture().draw(x, y, w, h);
	ofPopStyle();
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
		&& static_cast<int>(lightsSsFbo.getWidth()) == ssW) {
		return;
	}

	// GL_TEXTURE_2D is required explicitly: OF's desktop default is a
	// rectangle texture target, which cannot have mipmaps (needed for the
	// area downsample).
	ofFboSettings ss;
	ss.width = ssW;
	ss.height = ssH;
	ss.internalformat = GL_RGBA;
	ss.textureTarget = GL_TEXTURE_2D;
	ss.numSamples = 0;
	ss.useDepth = false;
	ss.useStencil = false;
	lightsSsFbo.allocate(ss);
	lightsSsFbo.begin();
	ofClear(0, 0, 0, 0);
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
	lf.internalformat = GL_RGBA;
	lf.textureTarget = GL_TEXTURE_2D;
	lf.numSamples = 0;
	lf.useDepth = false;
	lf.useStencil = false;
	lightsFbo.allocate(lf);
	// Nearest-neighbor so the on-screen upscale shows hard LED cells.
	lightsFbo.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);

	lightsFbo.begin();
	ofClear(0, 0, 0, 0);
	lightsFbo.end();
}

//--------------------------------------------------------------
void MouthDisplay::renderLights() {
	if (!lightsFbo.isAllocated() || !lightsSsFbo.isAllocated()) {
		return;
	}

	const float ssW = lightsSsFbo.getWidth();
	const float ssH = lightsSsFbo.getHeight();
	// Edges live in light units; one light = kSupersample supersample pixels.
	float leftPx = ofClamp(currentLeft * kSupersample, 0.0f, ssW);
	float rightPx = ofClamp(currentRight * kSupersample, 0.0f, ssW);
	if (rightPx < leftPx) {
		rightPx = leftPx;
	}

	// Supersampled pass. The background is cleared to the mouth COLOR with
	// alpha 0 (not transparent black) so the downsample average only dilutes
	// alpha, never the color: a half-covered light keeps the full RGB and
	// gets half the alpha, which blends linearly with coverage on screen.
	lightsSsFbo.begin();
	ofClear(cfg.color.r, cfg.color.g, cfg.color.b, 0);
	ofPushStyle();
	ofDisableAlphaBlending();
	ofFill();
	ofSetColor(cfg.color);
	ofDrawRectangle(leftPx, 0.0f, rightPx - leftPx, ssH);
	ofPopStyle();
	lightsSsFbo.end();

	// Area-downsample into the light grid via the mipmap chain (see
	// allocateFbos). Blending stays off: this is a resolve, not a composite.
	lightsSsFbo.getTexture().generateMipmap();
	lightsFbo.begin();
	ofClear(0, 0, 0, 0);
	ofPushStyle();
	ofDisableAlphaBlending();
	ofSetColor(255);
	lightsSsFbo.draw(0.0f, 0.0f, lightsFbo.getWidth(), lightsFbo.getHeight());
	ofPopStyle();
	lightsFbo.end();
}
