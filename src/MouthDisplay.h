#pragma once

#include "ofMain.h"

/// Receiver-side mouth presentation. The wire (and later a recording) only
/// carries the sender's QUANTIZED target edges in light units; this class owns
/// everything visual: easing toward the target (transition_seconds), the
/// neutral idle pose shown whenever no live target exists (neutral_width,
/// centered), and the rasterization into the discrete light grid. Keeping
/// these here means the animation can be retuned on the receiver - also for
/// played-back recordings - without touching the sender, and the mouth is
/// ALWAYS visible: it is never multiplied by the presence or link fades.
///
/// Rasterization mirrors the sender's Mouth::renderLights(): the bar is drawn
/// into a supersampled FBO and area-downsampled (mipmap) into the lights.w x
/// lights.h grid, so edge lights get fractional brightness and transitions
/// read as lights handing over.
class MouthDisplay {
public:
	struct Config {
		glm::ivec2 lights{14, 1};          ///< Grid size; must match the sender.
		ofColor color{255, 255, 255, 180}; ///< Fill color with alpha.
		int neutralWidth = 6;              ///< Idle width in lights (>= 1).
		float transitionSeconds = 0.2f;    ///< Ease duration toward a target (~95%).
	};

	/// Applies the config: (re)allocates the FBOs when the grid changed and
	/// clamps/derives the neutral pose. Safe to call again on a runtime
	/// config reload - the eased edges are kept (no visual jump) unless this
	/// is the first call, in which case they snap to neutral so the mouth is
	/// visible immediately, before any packet arrives.
	void setup(const Config & cfg);

	/// Sets the live target edges in light units (left inclusive, right
	/// exclusive), clamped into the grid. Call every frame while a live
	/// (or played-back) target exists; the easer only moves when the values
	/// actually differ.
	void setTarget(float left, float right);

	/// Reverts the target to the neutral idle pose (neutral_width lights,
	/// centered). Call whenever no data is available: stream timeout, no
	/// packet yet, or a fade-only packet from a sender without a mouth.
	void setIdle();

	/// Eases the current edges toward the target and re-rasterizes the grid.
	void update(float dt);

	/// Draws the light grid nearest-neighbor upscaled into the given rect,
	/// always at full opacity (the mouth must never disappear).
	void draw(float x, float y, float w, float h) const;

	bool isIdle() const { return idle; }
	const glm::ivec2 & getGridSize() const { return cfg.lights; }
	const ofTexture & getTexture() const { return lightsFbo.getTexture(); }
	bool isAllocated() const { return lightsFbo.isAllocated(); }

private:
	/// Neutral pose edges: neutral_width lights centered on the grid.
	void computeIdleEdges(float & left, float & right) const;
	/// (Re)allocates the lights + supersample FBOs for the current grid.
	void allocateFbos();
	/// Supersampled draw + mipmap downsample of the current eased edges.
	void renderLights();

	Config cfg;
	bool initialized = false;
	bool idle = true;

	float targetLeft = 0.0f;  ///< Target edges in light units.
	float targetRight = 1.0f;
	float currentLeft = 0.0f; ///< Eased edges the rasterizer draws.
	float currentRight = 1.0f;

	/// Supersampling factor (same rationale as the sender's Mouth): the bar
	/// is drawn at lights * kSupersample and averaged down so partially
	/// covered lights get fractional brightness.
	static constexpr int kSupersample = 8;

	ofFbo lightsFbo;   ///< lights.w x lights.h - one texel per physical light.
	ofFbo lightsSsFbo; ///< Supersample target (lights * kSupersample).
};
