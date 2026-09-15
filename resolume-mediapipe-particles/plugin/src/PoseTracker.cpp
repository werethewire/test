#include "PoseTracker.h"

#include <algorithm>
#include <cmath>

namespace mpp
{
namespace
{
const float kPi = 3.14159265358979323846f;

float Clamp01( float v )
{
	return v < 0.0f ? 0.0f : ( v > 1.0f ? 1.0f : v );
}

float Mix( float a, float b, float t )
{
	return a + ( b - a ) * t;
}

/// Frame rate independent exponential approach: `rate` is the fraction of the
/// remaining distance covered in one second.
float Approach( float current, float target, float rate, float dt )
{
	if( dt <= 0.0f )
		return current;
	float k = 1.0f - std::exp( -rate * dt );
	return current + ( target - current ) * k;
}

float GroupWeight( int group, EmitMode mode )
{
	const bool detail = group == GROUP_FACE || group == GROUP_HANDS;
	switch( mode )
	{
	case EMIT_LIMBS:
		return group == GROUP_LIMBS ? 1.0f : 0.0f;
	case EMIT_TORSO:
		return group == GROUP_TORSO ? 1.0f : 0.0f;
	case EMIT_JOINTS:
		// Joint mode still needs a non-empty table: the shader snaps the
		// sampled point to the nearer bone end.
		return detail ? 0.0f : 1.0f;
	case EMIT_HANDS:
		return group == GROUP_HANDS ? 1.0f : 0.0f;
	case EMIT_HEAD:
		return group == GROUP_HEAD || group == GROUP_FACE ? 1.0f : 0.0f;
	case EMIT_WHOLE_BODY:
	default:
		// The head bones are short; without a boost they get almost nothing.
		// The face and hand detail stays out, so the body modes look exactly
		// as they did before those bones existed.
		if( detail )
			return 0.0f;
		return group == GROUP_HEAD || group == GROUP_NECK ? 1.6f : 1.0f;
	}
}
}// namespace

// -------------------------------------------------------------- 1 Euro

float OneEuroFilter::Alpha( float cutoff, float dt )
{
	float tau = 1.0f / ( 2.0f * kPi * cutoff );
	return 1.0f / ( 1.0f + tau / dt );
}

void OneEuroFilter::Configure( float minCutoff_, float beta_, float dCutoff_ )
{
	minCutoff = std::max( minCutoff_, 0.001f );
	beta      = std::max( beta_, 0.0f );
	dCutoff   = std::max( dCutoff_, 0.001f );
}

void OneEuroFilter::Reset()
{
	initialised = false;
	xPrev       = 0.0f;
	xRawPrev    = 0.0f;
	dxPrev      = 0.0f;
}

float OneEuroFilter::Filter( float value, float dt, float& derivative )
{
	if( dt <= 0.0f )
		dt = 1.0f / 60.0f;

	if( !initialised )
	{
		initialised = true;
		xPrev       = value;
		xRawPrev    = value;
		dxPrev      = 0.0f;
		derivative  = 0.0f;
		return value;
	}

	// Derived from the raw input rather than the filtered one: using the
	// filtered value biases the estimate high by the filter's own lag, which
	// matters here because particles inherit this velocity.
	float dx    = ( value - xRawPrev ) / dt;
	xRawPrev    = value;
	float aD    = Alpha( dCutoff, dt );
	float dxHat = Mix( dxPrev, dx, aD );
	dxPrev      = dxHat;

	float cutoff = minCutoff + beta * std::fabs( dxHat );
	float a      = Alpha( cutoff, dt );
	float xHat   = Mix( xPrev, value, a );
	xPrev        = xHat;

	derivative = dxHat;
	return xHat;
}

// ---------------------------------------------------------- BodyTracker

void BodyTracker::Configure( float minCutoff, float beta )
{
	for( int i = 0; i < NUM_LANDMARKS; ++i )
	{
		filterX[ i ].Configure( minCutoff, beta );
		filterY[ i ].Configure( minCutoff, beta );
		// Depth is much noisier than x/y, so it always gets the heavy setting.
		filterZ[ i ].Configure( std::min( minCutoff, 1.5f ), beta * 0.5f );
	}
}

void BodyTracker::SetMirror( bool m )
{
	mirror = m;
}

void BodyTracker::SetTransform( float s, float ox, float oy )
{
	scale   = s;
	offsetX = ox;
	offsetY = oy;
}

void BodyTracker::SetTimeout( float seconds )
{
	timeout = std::max( seconds, 0.0f );
}

void BodyTracker::Reset()
{
	for( int i = 0; i < NUM_LANDMARKS; ++i )
	{
		filterX[ i ].Reset();
		filterY[ i ].Reset();
		filterZ[ i ].Reset();
		joints[ i ] = JointState();
	}
	presence       = 0.0f;
	motionEnergy   = 0.0f;
	sinceLastFrame = 1e6f;
	trackedNow     = false;
}

void BodyTracker::Update( const PoseFrame* frame, float dt )
{
	dt = std::max( dt, 1.0f / 1000.0f );

	if( frame != nullptr )
	{
		sinceLastFrame = 0.0f;
		trackedNow     = frame->present;
	}
	else
	{
		sinceLastFrame += dt;
	}

	if( frame != nullptr && frame->present )
	{
		float speedSum = 0.0f;
		for( int i = 0; i < NUM_LANDMARKS; ++i )
		{
			const float* src = frame->lm + i * FLOATS_PER_LANDMARK;
			float rawX       = src[ 0 ];
			float rawY       = src[ 1 ];
			float rawZ       = src[ 2 ];
			float vis        = Clamp01( src[ 3 ] );

			// Camera image space (origin top-left, y down) -> plugin space
			// (origin centre, y up), then the user's fit transform.
			float nx = mirror ? ( 1.0f - rawX ) : rawX;
			float px = ( nx * 2.0f - 1.0f ) * scale + offsetX;
			float py = ( 1.0f - rawY * 2.0f ) * scale + offsetY;
			// z shares x's scale in MediaPipe, so the same zoom applies. It
			// carries no offset: it is a distance, not a screen position.
			float pz = rawZ * 2.0f * scale;

			float dvx = 0.0f, dvy = 0.0f, dvz = 0.0f;
			joints[ i ].x  = filterX[ i ].Filter( px, dt, dvx );
			joints[ i ].y  = filterY[ i ].Filter( py, dt, dvy );
			joints[ i ].z  = filterZ[ i ].Filter( pz, dt, dvz );
			joints[ i ].vx = dvx;
			joints[ i ].vy = dvy;
			// Visibility is eased so a flickering landmark does not strobe the
			// particles it feeds.
			joints[ i ].vis = Approach( joints[ i ].vis, vis, 12.0f, dt );

			speedSum += std::sqrt( dvx * dvx + dvy * dvy ) * joints[ i ].vis;
		}

		// ~2 plugin-space units/second across the tracked joints reads as
		// "moving hard"; that is the normalisation point.
		float raw    = Clamp01( speedSum / ( float( NUM_LANDMARKS ) * 2.0f ) );
		motionEnergy = Approach( motionEnergy, raw, 8.0f, dt );
	}
	else
	{
		motionEnergy = Approach( motionEnergy, 0.0f, 4.0f, dt );
		// Let visibility bleed away too, so a body that walked out stops
		// contributing bones (and stops costing attraction lookups) instead of
		// staying frozen at its last pose.
		for( int i = 0; i < NUM_LANDMARKS; ++i )
			joints[ i ].vis = Approach( joints[ i ].vis, 0.0f, 3.0f, dt );
	}

	bool alive = trackedNow && sinceLastFrame < timeout;
	// Fade in quickly, out slowly: a dropped frame should not blink the body.
	presence   = Approach( presence, alive ? 1.0f : 0.0f, alive ? 14.0f : 3.0f, dt );
	if( presence < 0.0005f )
		presence = 0.0f;
}

// ---------------------------------------------------------- PoseTracker

PoseTracker::PoseTracker()
{
	SetSmoothing( smoothing );
}

void PoseTracker::SetSmoothing( float amount01 )
{
	smoothing = Clamp01( amount01 );
	// Low min-cutoff => more smoothing. beta scales down with it so a heavily
	// smoothed setting still snaps on fast motion instead of turning to syrup.
	float minCutoff = Mix( 6.0f, 0.4f, smoothing );
	float beta      = Mix( 0.02f, 0.006f, smoothing );
	for( BodyTracker& body : bodies )
		body.Configure( minCutoff, beta );
}

void PoseTracker::SetMirror( bool mirror )
{
	for( BodyTracker& body : bodies )
		body.SetMirror( mirror );
}

void PoseTracker::SetTransform( float scale, float offsetX, float offsetY )
{
	for( BodyTracker& body : bodies )
		body.SetTransform( scale, offsetX, offsetY );
}

void PoseTracker::SetEmitMode( EmitMode mode )
{
	emitMode = mode;
}

void PoseTracker::SetTimeout( float seconds )
{
	for( BodyTracker& body : bodies )
		body.SetTimeout( seconds );
}

void PoseTracker::Reset()
{
	for( BodyTracker& body : bodies )
		body.Reset();
	totalWeight = 0.0f;
	for( int i = 0; i < BoneCdfCount(); ++i )
		boneCdf[ i ] = 0.0f;
}

void PoseTracker::Update( const PoseUpdate* update, float dt )
{
	for( int person = 0; person < MAX_PERSONS; ++person )
	{
		const PoseFrame* frame = nullptr;
		if( update != nullptr && update->fresh[ person ] )
			frame = &update->frames[ person ];
		bodies[ person ].Update( frame, dt );
	}
	RebuildEmissionTable();
}

const JointState* PoseTracker::Joints( int person ) const
{
	if( person < 0 || person >= MAX_PERSONS )
		person = 0;
	return bodies[ person ].Joints();
}

float PoseTracker::Presence( int person ) const
{
	if( person < 0 || person >= MAX_PERSONS )
		return 0.0f;
	return bodies[ person ].Presence();
}

float PoseTracker::Presence() const
{
	float best = 0.0f;
	for( const BodyTracker& body : bodies )
		best = std::max( best, body.Presence() );
	return best;
}

float PoseTracker::MotionEnergy() const
{
	// The busiest body wins: two people should not read as "calm" just because
	// one of them is standing still.
	float best = 0.0f;
	for( const BodyTracker& body : bodies )
		best = std::max( best, body.MotionEnergy() );
	return best;
}

int PoseTracker::ActiveBodies() const
{
	int count = 0;
	for( const BodyTracker& body : bodies )
	{
		if( body.Presence() > 0.01f )
			++count;
	}
	return count;
}

void PoseTracker::RebuildEmissionTable()
{
	// One table spanning every body, so the particle budget is shared by bone
	// length: two people each get roughly half the particles, and a body that
	// is fading out hands its share back to the others.
	float weights[ NUM_BONES * MAX_PERSONS ];
	float running = 0.0f;

	for( int person = 0; person < MAX_PERSONS; ++person )
	{
		const JointState* joints = bodies[ person ].Joints();
		const float bodyWeight   = bodies[ person ].Presence();

		for( int i = 0; i < NUM_BONES; ++i )
		{
			const int slot      = person * NUM_BONES + i;
			const Bone& bone    = BONES[ i ];
			const JointState& a = joints[ bone.a ];
			const JointState& b = joints[ bone.b ];

			float groupW = GroupWeight( bone.group, emitMode );
			// A bone is only worth emitting from when both ends are actually seen.
			float visW = std::min( a.vis, b.vis );
			visW       = visW < 0.35f ? 0.0f : ( visW - 0.35f ) / 0.65f;

			float dx  = b.x - a.x;
			float dy  = b.y - a.y;
			float len = std::sqrt( dx * dx + dy * dy );

			weights[ slot ] = len * groupW * visW * bodyWeight;
			running += weights[ slot ];
		}
	}

	totalWeight = running;
	if( running <= 0.0f )
	{
		for( int i = 0; i < BoneCdfCount(); ++i )
			boneCdf[ i ] = 0.0f;
		return;
	}

	float acc = 0.0f;
	for( int i = 0; i < BoneCdfCount(); ++i )
	{
		acc += weights[ i ];
		boneCdf[ i ] = acc / running;
	}
	boneCdf[ BoneCdfCount() - 1 ] = 1.0f;// guard against fp drift at the top end
}

}// namespace mpp
