#pragma once

#include "ofMain.h"

/// Receiver-side mouth presentation. The wire (and later a recording) only
/// carries the sender's QUANTIZED target edges in light units of the mouth
/// SPAN (the 14-light bar); this class owns everything visual: easing toward
/// the target (transition_seconds), the neutral idle pose shown whenever no
/// live target exists (neutral_width, centered on the span), and rasterization
/// into the physical fixture grid. Keeping these here means the animation can
/// be retuned on the receiver - also for played-back recordings - without
/// touching the sender, and the mouth is ALWAYS visible: it is never multiplied
/// by the presence or link fades.
///
/// The fixture is an 18x5 RGB light grid (origin at the upper-left cell). The
/// mouth occupies a single row: the 4th from the top (row index 3), starting
/// 2 cells in from the left edge, 14 cells wide. Sender coordinates (0..span)
/// are placed onto that span at rasterize time. Empty cells stay off (black);
/// a fully covered mouth cell is opaque RGB white, not a transparent overlay.
/// Edge lights still get fractional brightness via supersampled area-average,
/// which reads as lights handing over.
///
/// The fixture also carries the EYE lights: two blocks on the 2nd row from the
/// top (row index 1), eyeSpan cells each, inset eyeOffset cells from the left
/// and right edges. Their brightness is the INVERSE of the camera-image fade
/// (set per frame via setEyeIntensity): a fully faded-out image lights the
/// eyes fully, a fully visible image turns them off.
class MouthDisplay {
public:
	struct Config {
		glm::ivec2 lights{18, 5};          ///< Physical fixture grid (origin top-left).
		int row = 3;                       ///< Mouth row, 0-based from the top (3 = 4th).
		int offset = 2;                    ///< Mouth column inset from the left edge.
		int span = 14;                     ///< Mouth width in lights; matches the sender.
		int eyeRow = 1;                    ///< Eye row, 0-based from the top (1 = 2nd).
		int eyeOffset = 2;                 ///< Eye block inset from EACH edge.
		int eyeSpan = 3;                   ///< Width of each eye block in lights.
		ofColor color{255, 255, 255, 255}; ///< Fully-on light RGB (alpha ignored).
		int neutralWidth = 6;              ///< Idle width in lights (>= 1), within the span.
		float transitionSeconds = 0.2f;    ///< Ease duration toward a target (~95%).
	};

	/// Applies the config: (re)allocates the FBOs when the grid changed and
	/// clamps/derives the mouth placement and neutral pose. Safe to call again
	/// on a runtime config reload - the eased edges are kept (no visual jump)
	/// unless this is the first call, in which case they snap to neutral so
	/// the mouth is visible immediately, before any packet arrives.
	void setup(const Config & cfg);

	/// Sets the live target edges in mouth-span light units (left inclusive,
	/// right exclusive), clamped into the span. Call every frame while a live
	/// (or played-back) target exists; the easer only moves when the values
	/// actually differ.
	void setTarget(float left, float right);

	/// Reverts the target to the neutral idle pose (neutral_width lights,
	/// centered on the mouth span). Call whenever no data is available: stream
	/// timeout, no packet yet, or a fade-only packet from a sender without a
	/// mouth.
	void setIdle();

	/// Sets the eye-light brightness [0..1] for this frame. Callers pass the
	/// INVERSE of the effective camera-image fade: faded-out image -> eyes
	/// fully lit, fully visible image -> eyes off.
	void setEyeIntensity(float intensity);

	/// Eases the current edges toward the target and re-rasterizes the grid.
	void update(float dt);

	/// Draws the light grid nearest-neighbor upscaled into the given rect,
	/// always at full opacity (the mouth must never disappear).
	void draw(float x, float y, float w, float h) const;

	bool isIdle() const { return idle; }
	const glm::ivec2 & getGridSize() const { return cfg.lights; }
	/// Mouth span in lights (the coordinate space of setTarget / the wire).
	int getMouthSpan() const { return cfg.span; }
	const ofTexture & getTexture() const { return lightsFbo.getTexture(); }
	bool isAllocated() const { return lightsFbo.isAllocated(); }

private:
	/// Neutral pose edges: neutral_width lights centered on the mouth span.
	void computeIdleEdges(float & left, float & right) const;
	/// (Re)allocates the lights + supersample FBOs for the current grid.
	void allocateFbos();
	/// Supersampled draw + mipmap downsample of the current eased edges.
	void renderLights();

	Config cfg;
	bool initialized = false;
	bool idle = true;

	float targetLeft = 0.0f;  ///< Target edges in mouth-span light units.
	float targetRight = 1.0f;
	float currentLeft = 0.0f; ///< Eased edges the rasterizer draws.
	float currentRight = 1.0f;
	float eyeIntensity = 0.0f; ///< Eye-light brightness [0..1], inverse of the fade.

	/// Supersampling factor (same rationale as the sender's Mouth): the bar
	/// is drawn at lights * kSupersample and averaged down so partially
	/// covered lights get fractional brightness.
	static constexpr int kSupersample = 8;

	ofFbo lightsFbo;   ///< lights.w x lights.h - one texel per physical light.
	ofFbo lightsSsFbo; ///< Supersample target (lights * kSupersample).
};
