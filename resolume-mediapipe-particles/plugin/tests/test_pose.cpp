// Standalone checks for the parts of the plugin that do not need a GL context.
// Build:  c++ -std=c++14 -I../src test_pose.cpp ../src/OscPose.cpp ../src/PoseTracker.cpp -o test_pose -lpthread
#include "OscPose.h"
#include "PoseTracker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined( _WIN32 )
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment( lib, "Ws2_32.lib" )
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

/// Fire one datagram at 127.0.0.1:port, so the receiver test covers the real
/// socket path rather than just the parser.
static bool SendUdpLoopback( uint16_t port, const char* data, size_t len )
{
#if defined( _WIN32 )
	WSADATA wsa;
	WSAStartup( MAKEWORD( 2, 2 ), &wsa );
	SOCKET s = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if( s == INVALID_SOCKET )
		return false;
#else
	int s = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if( s < 0 )
		return false;
#endif
	sockaddr_in addr;
	std::memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_port   = htons( port );
	addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	bool ok = ::sendto( s, data, int( len ), 0,
						reinterpret_cast< sockaddr* >( &addr ), sizeof( addr ) ) == int( len );
#if defined( _WIN32 )
	::closesocket( s );
#else
	::close( s );
#endif
	return ok;
}

static int failures = 0;

#define CHECK( cond )                                                       \
	do                                                                      \
	{                                                                       \
		if( !( cond ) )                                                     \
		{                                                                   \
			std::printf( "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond );   \
			++failures;                                                     \
		}                                                                   \
	} while( 0 )

#define CHECK_NEAR( a, b, eps )                                                        \
	do                                                                                 \
	{                                                                                  \
		double va = double( a ), vb = double( b );                                     \
		if( std::fabs( va - vb ) > ( eps ) )                                           \
		{                                                                              \
			std::printf( "FAIL %s:%d  %s (%g) != %s (%g)\n", __FILE__, __LINE__,       \
						 #a, va, #b, vb );                                             \
			++failures;                                                                \
		}                                                                              \
	} while( 0 )

// ---------------------------------------------------------------- OSC writer

static void PushString( std::vector< char >& out, const char* s )
{
	size_t len = std::strlen( s ) + 1;
	out.insert( out.end(), s, s + len );
	while( out.size() % 4 != 0 )
		out.push_back( 0 );
}

static void PushInt32( std::vector< char >& out, int32_t v )
{
	uint32_t u = uint32_t( v );
	out.push_back( char( ( u >> 24 ) & 0xff ) );
	out.push_back( char( ( u >> 16 ) & 0xff ) );
	out.push_back( char( ( u >> 8 ) & 0xff ) );
	out.push_back( char( u & 0xff ) );
}

static void PushFloat( std::vector< char >& out, float f )
{
	uint32_t u;
	std::memcpy( &u, &f, 4 );
	PushInt32( out, int32_t( u ) );
}

/// Mirrors what tracker/pose_osc.py puts on the wire. A negative personId
/// omits the argument entirely, which is what a single person tracker sends.
static std::vector< char > MakePosePacket( int32_t frameId, const float* lm, int personId = -1 )
{
	std::vector< char > p;
	PushString( p, "/mp/pose" );
	std::string tags = personId >= 0 ? ",ii" : ",i";
	tags.append( mpp::POSE_FLOAT_COUNT, 'f' );
	PushString( p, tags.c_str() );
	PushInt32( p, frameId );
	if( personId >= 0 )
		PushInt32( p, personId );
	for( int i = 0; i < mpp::POSE_FLOAT_COUNT; ++i )
		PushFloat( p, lm[ i ] );
	return p;
}

static std::vector< char > MakeClearPacket( int32_t frameId, int personId = -1 )
{
	std::vector< char > p;
	PushString( p, "/mp/clear" );
	PushString( p, personId >= 0 ? ",ii" : ",i" );
	PushInt32( p, frameId );
	if( personId >= 0 )
		PushInt32( p, personId );
	return p;
}

/// Wraps one body's frame into the update the tracker consumes.
static mpp::PoseUpdate SingleBody( const float* lm, bool present = true, int person = 0 )
{
	mpp::PoseUpdate update;
	update.frames[ person ].present = present;
	if( lm != nullptr )
		std::memcpy( update.frames[ person ].lm, lm, sizeof( float ) * mpp::POSE_FLOAT_COUNT );
	update.fresh[ person ] = true;
	return update;
}

static std::vector< char > MakeBundle( const std::vector< std::vector< char > >& elems )
{
	std::vector< char > p;
	p.insert( p.end(), "#bundle", "#bundle" + 8 );
	for( int i = 0; i < 8; ++i )
		p.push_back( i == 7 ? 1 : 0 );// immediate time tag
	for( const auto& e : elems )
	{
		PushInt32( p, int32_t( e.size() ) );
		p.insert( p.end(), e.begin(), e.end() );
	}
	return p;
}

/// A crude T-pose so the bone table has non-zero lengths.
static void FillTPose( float* lm, float shiftX = 0.0f, float depth = 0.0f )
{
	for( int i = 0; i < mpp::NUM_LANDMARKS; ++i )
	{
		lm[ i * 4 + 0 ] = 0.5f + shiftX;
		lm[ i * 4 + 1 ] = 0.5f;
		lm[ i * 4 + 2 ] = depth;
		lm[ i * 4 + 3 ] = 1.0f;
	}
	auto set = [ & ]( int idx, float x, float y ) {
		lm[ idx * 4 + 0 ] = x + shiftX;
		lm[ idx * 4 + 1 ] = y;
	};
	set( mpp::LM_NOSE, 0.50f, 0.15f );
	set( mpp::LM_LEFT_EAR, 0.46f, 0.16f );
	set( mpp::LM_RIGHT_EAR, 0.54f, 0.16f );
	set( mpp::LM_LEFT_SHOULDER, 0.42f, 0.30f );
	set( mpp::LM_RIGHT_SHOULDER, 0.58f, 0.30f );
	set( mpp::LM_LEFT_ELBOW, 0.30f, 0.30f );
	set( mpp::LM_RIGHT_ELBOW, 0.70f, 0.30f );
	set( mpp::LM_LEFT_WRIST, 0.18f, 0.30f );
	set( mpp::LM_RIGHT_WRIST, 0.82f, 0.30f );
	set( mpp::LM_LEFT_HIP, 0.45f, 0.58f );
	set( mpp::LM_RIGHT_HIP, 0.55f, 0.58f );
	set( mpp::LM_LEFT_KNEE, 0.45f, 0.76f );
	set( mpp::LM_RIGHT_KNEE, 0.55f, 0.76f );
	set( mpp::LM_LEFT_ANKLE, 0.45f, 0.93f );
	set( mpp::LM_RIGHT_ANKLE, 0.55f, 0.93f );
	set( mpp::LM_LEFT_FOOT, 0.42f, 0.97f );
	set( mpp::LM_RIGHT_FOOT, 0.58f, 0.97f );
}

// ---------------------------------------------------------------- the tests

static void TestParsePose()
{
	float lm[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( lm );
	auto packet = MakePosePacket( 42, lm );

	mpp::PoseUpdate u;
	CHECK( mpp::ParsePosePacket( packet.data(), packet.size(), u ) );
	CHECK( u.fresh[ 0 ] );
	const mpp::PoseFrame& f = u.frames[ 0 ];
	CHECK( f.present );
	CHECK( f.frameId == 42 );
	CHECK( f.personId == 0 );// omitted personId means body 0
	CHECK_NEAR( f.lm[ mpp::LM_LEFT_WRIST * 4 + 0 ], 0.18f, 1e-6 );
	CHECK_NEAR( f.lm[ mpp::LM_RIGHT_ANKLE * 4 + 1 ], 0.93f, 1e-6 );
	CHECK_NEAR( f.lm[ mpp::LM_NOSE * 4 + 3 ], 1.0f, 1e-6 );
	for( int i = 1; i < mpp::MAX_PERSONS; ++i )
		CHECK( !u.fresh[ i ] );
}

static void TestParsePosePerPerson()
{
	float a[ mpp::POSE_FLOAT_COUNT ];
	float b[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( a, -0.2f );
	FillTPose( b, 0.2f );

	// Two bodies in one bundle, as the multi-person tracker sends them.
	auto packet = MakeBundle( { MakePosePacket( 5, a, 0 ), MakePosePacket( 5, b, 1 ) } );
	mpp::PoseUpdate u;
	CHECK( mpp::ParsePosePacket( packet.data(), packet.size(), u ) );
	CHECK( u.fresh[ 0 ] );
	CHECK( u.fresh[ 1 ] );
	CHECK( u.frames[ 0 ].personId == 0 );
	CHECK( u.frames[ 1 ].personId == 1 );
	CHECK_NEAR( u.frames[ 0 ].lm[ mpp::LM_NOSE * 4 + 0 ], 0.30f, 1e-6 );
	CHECK_NEAR( u.frames[ 1 ].lm[ mpp::LM_NOSE * 4 + 0 ], 0.70f, 1e-6 );

	// A body beyond MAX_PERSONS is dropped, not wrapped onto someone else.
	auto overflow = MakePosePacket( 6, a, mpp::MAX_PERSONS );
	mpp::PoseUpdate spill;
	CHECK( !mpp::ParsePosePacket( overflow.data(), overflow.size(), spill ) );
	CHECK( !spill.AnyFresh() );
}

static void TestParseClear()
{
	// A bare clear retires every body at once.
	auto all = MakeClearPacket( 7 );
	mpp::PoseUpdate u;
	CHECK( mpp::ParsePosePacket( all.data(), all.size(), u ) );
	for( int i = 0; i < mpp::MAX_PERSONS; ++i )
	{
		CHECK( u.fresh[ i ] );
		CHECK( !u.frames[ i ].present );
		CHECK( u.frames[ i ].frameId == 7 );
	}

	// A targeted clear only touches that body.
	mpp::PoseUpdate one;
	auto single = MakeClearPacket( 8, 1 );
	CHECK( mpp::ParsePosePacket( single.data(), single.size(), one ) );
	CHECK( !one.fresh[ 0 ] );
	CHECK( one.fresh[ 1 ] );
	CHECK( !one.frames[ 1 ].present );
}

static void TestParseBundleTakesLast()
{
	float a[ mpp::POSE_FLOAT_COUNT ];
	float b[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( a, 0.0f );
	FillTPose( b, 0.1f );
	// Same body twice in one bundle: the later message wins.
	auto packet = MakeBundle( { MakePosePacket( 1, a ), MakePosePacket( 2, b ) } );

	mpp::PoseUpdate u;
	CHECK( mpp::ParsePosePacket( packet.data(), packet.size(), u ) );
	CHECK( u.frames[ 0 ].frameId == 2 );
	CHECK_NEAR( u.frames[ 0 ].lm[ mpp::LM_NOSE * 4 + 0 ], 0.60f, 1e-6 );
}

static void TestRejectsGarbage()
{
	mpp::PoseUpdate f;
	CHECK( !mpp::ParsePosePacket( nullptr, 0, f ) );

	const char junk[] = "not an osc packet at all";
	CHECK( !mpp::ParsePosePacket( junk, sizeof( junk ), f ) );

	// Unrelated address must be ignored, not mis-parsed.
	std::vector< char > other;
	PushString( other, "/composition/layer1" );
	PushString( other, ",f" );
	PushFloat( other, 0.5f );
	CHECK( !mpp::ParsePosePacket( other.data(), other.size(), f ) );

	// Truncated pose: fewer floats than the protocol promises.
	float lm[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( lm );
	auto good = MakePosePacket( 3, lm );
	CHECK( !mpp::ParsePosePacket( good.data(), good.size() - 8, f ) );

	// A string that never terminates must not run off the buffer.
	std::vector< char > unterminated( 16, 'a' );
	CHECK( !mpp::ParsePosePacket( unterminated.data(), unterminated.size(), f ) );
}

static void TestOneEuroTracksAndSmooths()
{
	mpp::OneEuroFilter filter;
	filter.Configure( 1.0f, 0.02f );

	const float dt = 1.0f / 60.0f;
	float d        = 0.0f;

	// A step input must converge to the new level.
	filter.Filter( 0.0f, dt, d );
	float v = 0.0f;
	for( int i = 0; i < 200; ++i )
		v = filter.Filter( 1.0f, dt, d );
	CHECK_NEAR( v, 1.0f, 0.01 );

	// Noise around a constant must be attenuated.
	mpp::OneEuroFilter noisy;
	noisy.Configure( 1.0f, 0.02f );
	float maxDev = 0.0f;
	for( int i = 0; i < 300; ++i )
	{
		float jitter = ( ( i % 2 ) == 0 ? 0.05f : -0.05f );
		float out    = noisy.Filter( 0.5f + jitter, dt, d );
		if( i > 50 )
			maxDev = std::max( maxDev, std::fabs( out - 0.5f ) );
	}
	CHECK( maxDev < 0.02f );

	// A constant-velocity ramp must report roughly that velocity.
	mpp::OneEuroFilter ramp;
	ramp.Configure( 1.0f, 0.02f );
	float x = 0.0f;
	for( int i = 0; i < 300; ++i )
	{
		x += 1.0f * dt;// 1 unit per second
		ramp.Filter( x, dt, d );
	}
	CHECK_NEAR( d, 1.0f, 0.05 );
}

static void TestTrackerMappingAndMirror()
{
	float lm[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( lm );
	mpp::PoseUpdate frame = SingleBody( lm );

	mpp::PoseTracker t;
	t.SetSmoothing( 0.0f );
	t.SetMirror( false );
	t.SetTransform( 1.0f, 0.0f, 0.0f );
	t.Update( &frame, 1.0f / 60.0f );

	// Camera (0.18, 0.30) -> plugin (-0.64, +0.40): x centred, y flipped.
	CHECK_NEAR( t.Joints()[ mpp::LM_LEFT_WRIST ].x, -0.64f, 1e-5 );
	CHECK_NEAR( t.Joints()[ mpp::LM_LEFT_WRIST ].y, 0.40f, 1e-5 );

	mpp::PoseTracker m;
	m.SetSmoothing( 0.0f );
	m.SetMirror( true );
	m.Update( &frame, 1.0f / 60.0f );
	CHECK_NEAR( m.Joints()[ mpp::LM_LEFT_WRIST ].x, 0.64f, 1e-5 );

	// Zoom and offset apply on top of that mapping.
	mpp::PoseTracker s;
	s.SetSmoothing( 0.0f );
	s.SetMirror( false );
	s.SetTransform( 0.5f, 0.25f, -0.1f );
	s.Update( &frame, 1.0f / 60.0f );
	CHECK_NEAR( s.Joints()[ mpp::LM_LEFT_WRIST ].x, -0.64f * 0.5f + 0.25f, 1e-5 );
	CHECK_NEAR( s.Joints()[ mpp::LM_LEFT_WRIST ].y, 0.40f * 0.5f - 0.1f, 1e-5 );
}

static void TestEmissionTable()
{
	float lm[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( lm );
	mpp::PoseUpdate frame = SingleBody( lm );

	mpp::PoseTracker t;
	t.SetSmoothing( 0.0f );
	// Visibility is eased in, so let it settle before judging the table.
	for( int i = 0; i < 120; ++i )
		t.Update( &frame, 1.0f / 60.0f );

	CHECK( t.HasEmitters() );
	const float* cdf = t.BoneCdf();
	for( int i = 1; i < mpp::PoseTracker::BoneCdfCount(); ++i )
		CHECK( cdf[ i ] >= cdf[ i - 1 ] - 1e-6f );
	CHECK_NEAR( cdf[ mpp::PoseTracker::BoneCdfCount() - 1 ], 1.0f, 1e-6 );
	// Only body 0 is present, so its slice must own the whole table.
	CHECK_NEAR( cdf[ mpp::NUM_BONES - 1 ], 1.0f, 1e-6 );

	// Limb-only mode must leave every torso bone with zero width.
	mpp::PoseTracker limbs;
	limbs.SetSmoothing( 0.0f );
	limbs.SetEmitMode( mpp::EMIT_LIMBS );
	for( int i = 0; i < 120; ++i )
		limbs.Update( &frame, 1.0f / 60.0f );
	const float* lcdf = limbs.BoneCdf();
	for( int i = 0; i < mpp::NUM_BONES; ++i )
	{
		if( mpp::BONES[ i ].group != mpp::GROUP_LIMBS )
		{
			float width = lcdf[ i ] - ( i == 0 ? 0.0f : lcdf[ i - 1 ] );
			CHECK_NEAR( width, 0.0f, 1e-6 );
		}
	}

	// An invisible skeleton must produce no emitters at all.
	mpp::PoseUpdate hidden = frame;
	for( int i = 0; i < mpp::NUM_LANDMARKS; ++i )
		hidden.frames[ 0 ].lm[ i * 4 + 3 ] = 0.0f;
	mpp::PoseTracker none;
	none.SetSmoothing( 0.0f );
	for( int i = 0; i < 120; ++i )
		none.Update( &hidden, 1.0f / 60.0f );
	CHECK( !none.HasEmitters() );
}

static void TestPresenceEnvelope()
{
	float lm[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( lm );
	mpp::PoseUpdate frame = SingleBody( lm );

	mpp::PoseTracker t;
	t.SetTimeout( 0.5f );
	CHECK_NEAR( t.Presence(), 0.0f, 1e-6 );

	for( int i = 0; i < 60; ++i )
		t.Update( &frame, 1.0f / 60.0f );
	CHECK( t.Presence() > 0.95f );

	// Losing the tracker fades out rather than cutting.
	for( int i = 0; i < 20; ++i )
		t.Update( nullptr, 1.0f / 60.0f );
	CHECK( t.Presence() > 0.8f );// still inside the timeout
	for( int i = 0; i < 300; ++i )
		t.Update( nullptr, 1.0f / 60.0f );
	CHECK( t.Presence() < 0.01f );

	// An explicit clear message fades out too.
	mpp::PoseTracker c;
	for( int i = 0; i < 60; ++i )
		c.Update( &frame, 1.0f / 60.0f );
	mpp::PoseUpdate clear = SingleBody( nullptr, false );
	for( int i = 0; i < 300; ++i )
		c.Update( &clear, 1.0f / 60.0f );
	CHECK( c.Presence() < 0.01f );
}

static void TestMotionEnergy()
{
	float lm[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( lm );
	mpp::PoseUpdate still = SingleBody( lm );

	mpp::PoseTracker rest;
	for( int i = 0; i < 180; ++i )
		rest.Update( &still, 1.0f / 60.0f );
	CHECK( rest.MotionEnergy() < 0.02f );

	mpp::PoseTracker busy;
	for( int i = 0; i < 180; ++i )
	{
		float wobble = ( i % 2 ) ? 0.12f : -0.12f;
		float moving[ mpp::POSE_FLOAT_COUNT ];
		FillTPose( moving, wobble );
		mpp::PoseUpdate f = SingleBody( moving );
		busy.Update( &f, 1.0f / 60.0f );
	}
	CHECK( busy.MotionEnergy() > rest.MotionEnergy() );
}

static void TestReceiverRoundTrip()
{
	// Exercises the real socket path on loopback.
	mpp::PoseReceiver rx;
	uint16_t port = 0;
	for( uint16_t candidate = 19010; candidate < 19060; ++candidate )
	{
		if( rx.Start( candidate ) )
		{
			port = candidate;
			break;
		}
	}
	if( port == 0 )
	{
		std::printf( "SKIP receiver round trip (no bindable port)\n" );
		return;
	}

	float lm[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( lm );
	auto packet = MakePosePacket( 99, lm );

	CHECK( SendUdpLoopback( port, packet.data(), packet.size() ) );

	mpp::PoseUpdate got;
	bool received = false;
	for( int i = 0; i < 200 && !received; ++i )
	{
		received = rx.PollLatest( got );
		if( !received )
			std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
	}
	CHECK( received );
	if( received )
	{
		CHECK( got.fresh[ 0 ] );
		CHECK( got.frames[ 0 ].frameId == 99 );
		CHECK( got.frames[ 0 ].present );
		// A second poll with nothing new must report "no fresh frame".
		mpp::PoseUpdate again;
		CHECK( !rx.PollLatest( again ) );
	}
	rx.Stop();
	CHECK( !rx.IsListening() );
}

static void TestDepth()
{
	float lm[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( lm, 0.0f, -0.4f );// -0.4 == leaning toward the camera
	mpp::PoseUpdate frame = SingleBody( lm );

	mpp::PoseTracker t;
	t.SetSmoothing( 0.0f );
	t.SetTransform( 1.0f, 0.0f, 0.0f );
	for( int i = 0; i < 120; ++i )
		t.Update( &frame, 1.0f / 60.0f );

	// z shares x's scale: raw * 2 * zoom, and carries no position offset.
	CHECK_NEAR( t.Joints()[ mpp::LM_NOSE ].z, -0.8f, 0.02 );

	mpp::PoseTracker zoomed;
	zoomed.SetSmoothing( 0.0f );
	zoomed.SetTransform( 0.5f, 0.7f, -0.3f );
	for( int i = 0; i < 120; ++i )
		zoomed.Update( &frame, 1.0f / 60.0f );
	CHECK_NEAR( zoomed.Joints()[ mpp::LM_NOSE ].z, -0.4f, 0.02 );

	// Mirroring flips x but must leave depth alone.
	mpp::PoseTracker mirrored;
	mirrored.SetSmoothing( 0.0f );
	mirrored.SetMirror( true );
	for( int i = 0; i < 120; ++i )
		mirrored.Update( &frame, 1.0f / 60.0f );
	CHECK_NEAR( mirrored.Joints()[ mpp::LM_NOSE ].z, -0.8f, 0.02 );

	// Depth is filtered, so a one frame spike must not come through raw.
	float spike[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( spike, 0.0f, -3.0f );
	mpp::PoseUpdate jolt = SingleBody( spike );
	t.Update( &jolt, 1.0f / 60.0f );
	CHECK( t.Joints()[ mpp::LM_NOSE ].z > -3.0f );
}

static void TestMultipleBodies()
{
	float left[ mpp::POSE_FLOAT_COUNT ];
	float right[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( left, -0.2f );
	FillTPose( right, 0.2f );

	mpp::PoseUpdate both;
	both.frames[ 0 ].present = true;
	std::memcpy( both.frames[ 0 ].lm, left, sizeof( left ) );
	both.fresh[ 0 ] = true;
	both.frames[ 1 ].present = true;
	std::memcpy( both.frames[ 1 ].lm, right, sizeof( right ) );
	both.fresh[ 1 ] = true;

	mpp::PoseTracker t;
	t.SetSmoothing( 0.0f );
	t.SetMirror( false );
	for( int i = 0; i < 180; ++i )
		t.Update( &both, 1.0f / 60.0f );

	CHECK( t.ActiveBodies() == 2 );
	CHECK( t.Presence( 0 ) > 0.95f );
	CHECK( t.Presence( 1 ) > 0.95f );
	CHECK( t.Presence( 2 ) < 0.01f );

	// The two bodies are at different x, and each reads back on its own slot.
	CHECK_NEAR( t.Joints( 0 )[ mpp::LM_NOSE ].x, ( 0.30f * 2.0f - 1.0f ), 1e-4 );
	CHECK_NEAR( t.Joints( 1 )[ mpp::LM_NOSE ].x, ( 0.70f * 2.0f - 1.0f ), 1e-4 );

	// Identical bodies should split the emission budget roughly in half.
	const float* cdf   = t.BoneCdf();
	const float body0  = cdf[ mpp::NUM_BONES - 1 ];
	const float body1  = cdf[ 2 * mpp::NUM_BONES - 1 ] - body0;
	CHECK_NEAR( body0, 0.5f, 0.02 );
	CHECK_NEAR( body1, 0.5f, 0.02 );
	CHECK_NEAR( cdf[ mpp::PoseTracker::BoneCdfCount() - 1 ], 1.0f, 1e-6 );

	// One body leaves: the other must take the whole budget back, and the
	// global presence must stay pinned at 1 the entire time.
	mpp::PoseUpdate onlyFirst;
	onlyFirst.frames[ 0 ]  = both.frames[ 0 ];
	onlyFirst.fresh[ 0 ]   = true;
	onlyFirst.frames[ 1 ].present = false;
	onlyFirst.fresh[ 1 ]   = true;
	for( int i = 0; i < 300; ++i )
	{
		t.Update( &onlyFirst, 1.0f / 60.0f );
		CHECK( t.Presence() > 0.95f );
	}
	CHECK( t.ActiveBodies() == 1 );
	CHECK( t.Presence( 1 ) < 0.01f );
	CHECK_NEAR( t.BoneCdf()[ mpp::NUM_BONES - 1 ], 1.0f, 1e-4 );
}

static void TestReceiverPerPerson()
{
	mpp::PoseReceiver rx;
	uint16_t port = 0;
	for( uint16_t candidate = 19060; candidate < 19110; ++candidate )
	{
		if( rx.Start( candidate ) )
		{
			port = candidate;
			break;
		}
	}
	if( port == 0 )
	{
		std::printf( "SKIP per-person receiver test (no bindable port)\n" );
		return;
	}

	float a[ mpp::POSE_FLOAT_COUNT ];
	float b[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( a, -0.1f );
	FillTPose( b, 0.1f );

	// Two bodies in two separate datagrams must both survive to one poll.
	auto p0 = MakePosePacket( 1, a, 0 );
	auto p1 = MakePosePacket( 1, b, 1 );
	CHECK( SendUdpLoopback( port, p0.data(), p0.size() ) );
	CHECK( SendUdpLoopback( port, p1.data(), p1.size() ) );

	// The two datagrams may land in the same poll or in consecutive ones, so
	// accumulate the way the plugin's tracker does.
	mpp::PoseUpdate merged;
	for( int i = 0; i < 200; ++i )
	{
		mpp::PoseUpdate poll;
		if( rx.PollLatest( poll ) )
		{
			for( int person = 0; person < mpp::MAX_PERSONS; ++person )
			{
				if( !poll.fresh[ person ] )
					continue;
				merged.frames[ person ] = poll.frames[ person ];
				merged.fresh[ person ]  = true;
			}
		}
		if( merged.fresh[ 0 ] && merged.fresh[ 1 ] )
			break;
		std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
	}
	CHECK( merged.fresh[ 0 ] );
	CHECK( merged.fresh[ 1 ] );
	if( merged.fresh[ 0 ] && merged.fresh[ 1 ] )
	{
		CHECK_NEAR( merged.frames[ 0 ].lm[ mpp::LM_NOSE * 4 + 0 ], 0.40f, 1e-6 );
		CHECK_NEAR( merged.frames[ 1 ].lm[ mpp::LM_NOSE * 4 + 0 ], 0.60f, 1e-6 );
	}
	rx.Stop();
}

int main()
{
	TestParsePose();
	TestParsePosePerPerson();
	TestParseClear();
	TestParseBundleTakesLast();
	TestRejectsGarbage();
	TestOneEuroTracksAndSmooths();
	TestTrackerMappingAndMirror();
	TestEmissionTable();
	TestPresenceEnvelope();
	TestMotionEnergy();
	TestDepth();
	TestMultipleBodies();
	TestReceiverRoundTrip();
	TestReceiverPerPerson();

	if( failures == 0 )
		std::printf( "all pose tests passed\n" );
	else
		std::printf( "%d check(s) failed\n", failures );
	return failures == 0 ? 0 : 1;
}
