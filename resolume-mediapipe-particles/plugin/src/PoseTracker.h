#pragma once
//
// Turns raw landmark packets into something a particle shader can use:
// filtered positions, per-joint velocity, a length weighted emission table and
// a presence envelope so the figure fades instead of popping.
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
};

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
	/// Seconds without a packet before the figure starts fading out.
	void SetTimeout( float seconds );

	/// Advance by `dt` seconds. Pass the newest frame or nullptr when none arrived.
	void Update( const PoseFrame* frame, float dt );
	void Reset();

	const JointState* Joints() const { return joints; }
	/// Normalised cumulative bone lengths, NUM_BONES entries, last one == 1.
	/// All zero when nothing is emittable this frame.
	const float* BoneCdf() const { return boneCdf; }
	bool HasEmitters() const { return totalWeight > 0.0f; }

	/// 0..1 envelope: 1 while a person is tracked, ramps to 0 after timeout.
	float Presence() const { return presence; }
	/// Rough 0..1 measure of how much the body is moving. Useful to drive
	/// emission or turbulence without another parameter to ride.
	float MotionEnergy() const { return motionEnergy; }

private:
	void RebuildEmissionTable();

	OneEuroFilter filterX[ NUM_LANDMARKS ];
	OneEuroFilter filterY[ NUM_LANDMARKS ];
	JointState joints[ NUM_LANDMARKS ];
	float boneCdf[ NUM_BONES ] = { 0.0f };
	float totalWeight          = 0.0f;

	float smoothing    = 0.5f;
	bool mirror        = true;
	float scale        = 1.0f;
	float offsetX      = 0.0f;
	float offsetY      = 0.0f;
	EmitMode emitMode  = EMIT_WHOLE_BODY;
	float timeout      = 0.5f;

	float sinceLastFrame = 1e6f;
	float presence       = 0.0f;
	float motionEnergy   = 0.0f;
	bool trackedNow      = false;
};

}// namespace mpp
