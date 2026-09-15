#pragma once
//
// Turns raw landmark packets into something a particle shader can use:
// filtered positions, per-joint velocity and depth, a length weighted
// emission table across every tracked body, and a presence envelope so
// figures fade instead of popping.
//
// No OpenGL and no FFGL here on purpose -- this is the part worth unit testing.
//
#include "OscPose.h"

namespace mpp
{
/// 1 Euro filter (Casiez et al. 2012). Low lag when the joint moves fast,
/// heavy smoothing when it is nearly still -- the opposite trade-off of a
/// fixed low pass, and the reason hands do not smear here.
class OneEuroFilter
{
public:
	void Configure( float minCutoff, float beta, float dCutoff = 1.0f );
	void Reset();
	/// Returns the filtered value; `derivative` receives the filtered rate of
	/// change in units per second.
	float Filter( float value, float dt, float& derivative );

private:
	static float Alpha( float cutoff, float dt );

	float minCutoff = 1.0f;
	float beta      = 0.02f;
	float dCutoff   = 1.0f;
	float xPrev     = 0.0f;///< previous filtered value
	float xRawPrev  = 0.0f;///< previous input, used for an unbiased derivative
	float dxPrev    = 0.0f;
	bool initialised = false;
};

struct JointState
{
	float x   = 0.0f;///< plugin space, -1..1, +y up
	float y   = 0.0f;
	float z   = 0.0f;///< negative is closer to the camera, roughly x scaled
	float vx  = 0.0f;///< plugin space units per second
	float vy  = 0.0f;
	float vis = 0.0f;///< 0..1 landmark visibility, already eased
};

enum EmitMode : int
{
	EMIT_WHOLE_BODY = 0,
	EMIT_LIMBS      = 1,
	EMIT_TORSO      = 2,
	EMIT_JOINTS     = 3,
	EMIT_HANDS      = 4,///< wrists to fingertips only
	EMIT_HEAD       = 5,///< face and nose-to-ear, no neck
};

/// Everything tracked about a single body.
class BodyTracker
{
public:
	void Configure( float minCutoff, float beta );
	void SetMirror( bool mirror );
	void SetTransform( float scale, float offsetX, float offsetY );
	void SetTimeout( float seconds );
	void Reset();

	/// Advance by `dt` seconds. Pass this body's newest frame, or nullptr when
	/// none arrived for it.
	void Update( const PoseFrame* frame, float dt );

	const JointState* Joints() const { return joints; }
	float Presence() const { return presence; }
	float MotionEnergy() const { return motionEnergy; }

private:
	OneEuroFilter filterX[ NUM_LANDMARKS ];
	OneEuroFilter filterY[ NUM_LANDMARKS ];
	OneEuroFilter filterZ[ NUM_LANDMARKS ];
	JointState joints[ NUM_LANDMARKS ];

	bool mirror   = true;
	float scale   = 1.0f;
	float offsetX = 0.0f;
	float offsetY = 0.0f;
	float timeout = 0.5f;

	float sinceLastFrame = 1e6f;
	float presence       = 0.0f;
	float motionEnergy   = 0.0f;
	bool trackedNow      = false;
};

/// All bodies plus the shared emission table the shader samples.
class PoseTracker
{
public:
	PoseTracker();

	/// 0 = raw and twitchy, 1 = heavily smoothed and laggy. 0.5 is a good stage default.
	void SetSmoothing( float amount01 );
	/// Mirrors x so the performer sees themselves the right way round.
	void SetMirror( bool mirror );
	/// Fits the normalised camera frame into the plugin's output.
	void SetTransform( float scale, float offsetX, float offsetY );
	void SetEmitMode( EmitMode mode );
	/// Seconds without a packet before a body starts fading out.
	void SetTimeout( float seconds );

	/// Advance every body by `dt` seconds. Pass whatever arrived this frame,
	/// or nullptr when the receiver had nothing.
	void Update( const PoseUpdate* update, float dt );
	void Reset();

	const JointState* Joints( int person = 0 ) const;
	/// Normalised cumulative bone lengths over every body, last entry == 1.
	/// All zero when nothing is emittable this frame.
	const float* BoneCdf() const { return boneCdf; }
	static int BoneCdfCount() { return NUM_BONES * MAX_PERSONS; }
	bool HasEmitters() const { return totalWeight > 0.0f; }

	/// 0..1 envelope per body: 1 while tracked, ramps to 0 after timeout.
	float Presence( int person ) const;
	/// The strongest presence across all bodies, which is what gates spawning.
	float Presence() const;
	/// Rough 0..1 measure of how much the bodies are moving. Useful to drive
	/// emission or turbulence without another parameter to ride.
	float MotionEnergy() const;
	/// How many bodies are currently contributing anything.
	int ActiveBodies() const;

private:
	void RebuildEmissionTable();

	BodyTracker bodies[ MAX_PERSONS ];
	float boneCdf[ NUM_BONES * MAX_PERSONS ] = { 0.0f };
	float totalWeight                        = 0.0f;

	float smoothing   = 0.5f;
	EmitMode emitMode = EMIT_WHOLE_BODY;
};

}// namespace mpp
