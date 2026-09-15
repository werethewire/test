// Standalone checks for the parts of the plugin that do not need a GL context.
// Build:  c++ -std=c++14 -I../src test_pose.cpp ../src/OscPose.cpp ../src/PoseTracker.cpp -o test_pose -lpthread
#include "OscPose.h"
#include "PoseTracker.h"
#include "TrackerLauncher.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined( _WIN32 )
	// windows.h defines min/max as macros, which turns any std::max( ... )
	// later in the translation unit into a syntax error.
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment( lib, "Ws2_32.lib" )
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <sys/stat.h>
	#include <unistd.h>
#endif

#if defined( _WIN32 )
using RawSocket = SOCKET;
static const RawSocket kNoSocket = INVALID_SOCKET;
#else
using RawSocket = int;
static const RawSocket kNoSocket = -1;
#endif

/// Holds a UDP port open so the receiver's bind has to fail.
static RawSocket OccupyUdpPort( uint16_t port )
{
#if defined( _WIN32 )
	WSADATA wsa;
	WSAStartup( MAKEWORD( 2, 2 ), &wsa );
#endif
	RawSocket s = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if( s == kNoSocket )
		return kNoSocket;
	sockaddr_in addr;
	std::memset( &addr, 0, sizeof( addr ) );
	addr.sin_family      = AF_INET;
	addr.sin_port        = htons( port );
	addr.sin_addr.s_addr = htonl( INADDR_ANY );
	if( ::bind( s, reinterpret_cast< sockaddr* >( &addr ), sizeof( addr ) ) != 0 )
	{
#if defined( _WIN32 )
		::closesocket( s );
#else
		::close( s );
#endif
		return kNoSocket;
	}
	return s;
}

static void CloseRawSocket( RawSocket s )
{
	if( s == kNoSocket )
		return;
#if defined( _WIN32 )
	::closesocket( s );
#else
	::close( s );
#endif
}

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

static void TestReceiverRebindsAfterAFailedStart()
{
	// A bind can fail when a composition loads: another instance of the plugin
	// is still shutting down, or the OS has not released the socket. The
	// plugin retries on a timer, which is only worth anything if a failed
	// Start leaves the receiver in a state a later Start can recover from.
	uint16_t port    = 0;
	RawSocket blocker = kNoSocket;
	for( uint16_t candidate = 19110; candidate < 19160; ++candidate )
	{
		blocker = OccupyUdpPort( candidate );
		if( blocker != kNoSocket )
		{
			port = candidate;
			break;
		}
	}
	if( port == 0 )
	{
		std::printf( "SKIP rebind test (no bindable port)\n" );
		return;
	}

	mpp::PoseReceiver rx;
	if( rx.Start( port ) )
	{
		// Some platforms let a second socket share the port; there is no
		// failure to recover from there.
		std::printf( "SKIP rebind test (this platform allows a shared bind)\n" );
		rx.Stop();
		CloseRawSocket( blocker );
		return;
	}
	CHECK( !rx.IsListening() );

	CloseRawSocket( blocker );

	// Same receiver, same port, now free: the retry has to take.
	CHECK( rx.Start( port ) );
	CHECK( rx.IsListening() );
	CHECK( rx.Port() == port );

	float lm[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( lm );
	auto packet = MakePosePacket( 4242, lm );
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
		CHECK( got.frames[ 0 ].frameId == 4242 );
	rx.Stop();
}

/// Poll until something arrives or roughly a second passes.
static bool PollFor( mpp::PoseReceiver& rx, mpp::PoseUpdate& got )
{
	for( int i = 0; i < 200; ++i )
	{
		if( rx.PollLatest( got ) )
			return true;
		std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
	}
	return false;
}

static void TestReceiversShareAPort()
{
	// In Arena every clip slot holding the source is its own plugin instance,
	// and one that has played keeps its receiver after the layer moves on to
	// another clip. All of them are listening to the same tracker, so they have
	// to share the port. With one socket each, Windows let every bind succeed
	// (SO_REUSEADDR) and handed each datagram to only one of them: the clip
	// actually on screen went black while a stopped one got the poses.
	uint16_t port = 0;
	for( uint16_t candidate = 19210; candidate < 19260; ++candidate )
	{
		RawSocket probe = OccupyUdpPort( candidate );
		if( probe != kNoSocket )
		{
			CloseRawSocket( probe );
			port = candidate;
			break;
		}
	}
	if( port == 0 )
	{
		std::printf( "SKIP shared port test (no bindable port)\n" );
		return;
	}

	float lm[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( lm );

	{
		mpp::PoseReceiver first;
		mpp::PoseReceiver second;
		CHECK( first.Start( port ) );
		CHECK( second.Start( port ) );
		CHECK( first.IsListening() && second.IsListening() );

		auto packet = MakePosePacket( 7001, lm );
		CHECK( SendUdpLoopback( port, packet.data(), packet.size() ) );
		mpp::PoseUpdate gotFirst, gotSecond;
		CHECK( PollFor( first, gotFirst ) );
		CHECK( PollFor( second, gotSecond ) );
		CHECK( gotFirst.frames[ 0 ].frameId == 7001 );
		CHECK( gotSecond.frames[ 0 ].frameId == 7001 );

		// The instance that goes away must not take the port with it.
		first.Stop();
		CHECK( !first.IsListening() );
		CHECK( second.IsListening() );
		packet = MakePosePacket( 7002, lm );
		CHECK( SendUdpLoopback( port, packet.data(), packet.size() ) );
		CHECK( PollFor( second, gotSecond ) );
		CHECK( gotSecond.frames[ 0 ].frameId == 7002 );

		// Joining after the port is already open still gets everything new.
		mpp::PoseReceiver late;
		CHECK( late.Start( port ) );
		packet = MakePosePacket( 7003, lm );
		CHECK( SendUdpLoopback( port, packet.data(), packet.size() ) );
		mpp::PoseUpdate gotLate;
		CHECK( PollFor( late, gotLate ) );
		CHECK( gotLate.frames[ 0 ].frameId == 7003 );
		CHECK( PollFor( second, gotSecond ) );
		CHECK( gotSecond.frames[ 0 ].frameId == 7003 );
	}

	// Once the last receiver is gone the socket is closed, so the port is free
	// for anybody else again.
	RawSocket after = OccupyUdpPort( port );
	CHECK( after != kNoSocket );
	CloseRawSocket( after );
}

static void TestOtherProcessCannotSplitThePort()
{
	// A socket outside the plugin that asks to share (a second Resolume, or any
	// other OSC app using SO_REUSEADDR) must not be able to quietly take half
	// the datagrams; the plugin's socket has to refuse it.
	uint16_t port = 0;
	mpp::PoseReceiver rx;
	for( uint16_t candidate = 19310; candidate < 19360; ++candidate )
	{
		if( rx.Start( candidate ) )
		{
			port = candidate;
			break;
		}
	}
	if( port == 0 )
	{
		std::printf( "SKIP exclusive port test (no bindable port)\n" );
		return;
	}

#if defined( _WIN32 )
	WSADATA wsa;
	WSAStartup( MAKEWORD( 2, 2 ), &wsa );
#endif
	RawSocket intruder = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	CHECK( intruder != kNoSocket );
	int reuse = 1;
	::setsockopt( intruder, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast< const char* >( &reuse ), sizeof( reuse ) );
	sockaddr_in addr;
	std::memset( &addr, 0, sizeof( addr ) );
	addr.sin_family      = AF_INET;
	addr.sin_port        = htons( port );
	addr.sin_addr.s_addr = htonl( INADDR_ANY );
	const bool intruderBound = ::bind( intruder, reinterpret_cast< sockaddr* >( &addr ), sizeof( addr ) ) == 0;
	CHECK( !intruderBound );
	CloseRawSocket( intruder );
	rx.Stop();
}

static void TestQuoteWindowsArgument()
{
	CHECK( mpp::QuoteWindowsArgument( "plain" ) == "plain" );
	CHECK( mpp::QuoteWindowsArgument( "" ) == "\"\"" );
	// Resolume's default plugin folder has spaces in it.
	CHECK( mpp::QuoteWindowsArgument( "G:\\document\\Resolume Arena\\Extra Effects\\x.py" ) ==
		   "\"G:\\document\\Resolume Arena\\Extra Effects\\x.py\"" );
	// Backslashes only double where a quote follows them.
	CHECK( mpp::QuoteWindowsArgument( "C:\\a dir\\" ) == "\"C:\\a dir\\\\\"" );
	CHECK( mpp::QuoteWindowsArgument( "say \"hi\"" ) == "\"say \\\"hi\\\"\"" );
	CHECK( mpp::QuoteWindowsArgument( "a\\\"b" ) == "\"a\\\\\\\"b\"" );
}

static void TestBuildTrackerArguments()
{
	mpp::TrackerFiles files;
	files.script = "/t/pose_osc.py";
	files.model  = "/t/pose_landmarker_full.task";
	mpp::TrackerSettings settings;
	settings.camera  = 2;
	settings.people  = 3;
	settings.preview = true;

	std::vector< std::string > args = mpp::BuildTrackerArguments( files, settings, 9011 );
	auto valueAfter = [ &args ]( const char* flag ) -> std::string {
		for( size_t i = 0; i + 1 < args.size(); ++i )
		{
			if( args[ i ] == flag )
				return args[ i + 1 ];
		}
		return "<missing>";
	};
	CHECK( !args.empty() && args[ 0 ] == files.script );
	CHECK( valueAfter( "--model" ) == files.model );
	CHECK( valueAfter( "--device" ) == "2" );
	CHECK( valueAfter( "--people" ) == "3" );
	CHECK( valueAfter( "--port" ) == "9011" );
	CHECK( std::find( args.begin(), args.end(), "--preview" ) != args.end() );
#if defined( _WIN32 )
	// Camera indices come from DirectShow and only match that backend.
	CHECK( valueAfter( "--backend" ) == "dshow" );
#endif

	settings.people  = 7;// out of range is clamped, not passed through to argparse
	settings.preview = false;
	args             = mpp::BuildTrackerArguments( files, settings, 9010 );
	CHECK( valueAfter( "--people" ) == "3" );
	CHECK( std::find( args.begin(), args.end(), "--preview" ) == args.end() );
}

static std::string MakeTempDir( const char* tag )
{
#if defined( _WIN32 )
	char base[ MAX_PATH ];
	GetTempPathA( MAX_PATH, base );
	std::string dir = std::string( base ) + "mpp_test_" + tag + "_" + std::to_string( GetCurrentProcessId() );
	CreateDirectoryA( dir.c_str(), nullptr );
#else
	std::string dir = std::string( "/tmp/mpp_test_" ) + tag + "_" + std::to_string( getpid() );
	::mkdir( dir.c_str(), 0755 );
#endif
	return dir;
}

static void TouchFile( const std::string& path )
{
	FILE* f = std::fopen( path.c_str(), "wb" );
	if( f != nullptr )
		std::fclose( f );
}

static void TestFindTrackerFiles()
{
#if defined( _WIN32 )
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	std::string empty    = MakeTempDir( "empty" );
	std::string noModel  = MakeTempDir( "nomodel" );
	std::string complete = MakeTempDir( "complete" );
	TouchFile( noModel + sep + "pose_osc.py" );
	TouchFile( complete + sep + "pose_osc.py" );
	TouchFile( complete + sep + "pose_landmarker_lite.task" );
	TouchFile( complete + sep + "pose_landmarker_full.task" );

	mpp::TrackerFiles files;
	CHECK( mpp::FindTrackerFiles( { empty }, files ) == mpp::TrackerState::NoScript );
	CHECK( mpp::FindTrackerFiles( { empty, noModel }, files ) == mpp::TrackerState::NoModel );

	// A later folder that has everything wins over an earlier half-installed one,
	// and full is preferred to lite.
	files = mpp::TrackerFiles();
	CHECK( mpp::FindTrackerFiles( { noModel, complete }, files ) == mpp::TrackerState::Running );
	CHECK( files.script == complete + sep + "pose_osc.py" );
	CHECK( files.model == complete + sep + "pose_landmarker_full.task" );
}

static void TestEnumerateCameras()
{
	// Nothing to assert about the machine's hardware; this proves the
	// enumeration runs and returns, and shows what the Camera menu will list.
	std::vector< std::string > cameras = mpp::EnumerateCameras();
	std::printf( "cameras: %d\n", int( cameras.size() ) );
	for( size_t i = 0; i < cameras.size(); ++i )
		std::printf( "  %d: %s\n", int( i ), cameras[ i ].c_str() );
}

static void TestLauncherEndToEnd()
{
	// Opt in: this starts a real Python, MediaPipe and a camera, which CI has
	// none of. MPP_E2E_CAMERA is the camera index; MPP_TRACKER_DIR must point
	// at a folder with pose_osc.py and a model.
	const char* cameraEnv = std::getenv( "MPP_E2E_CAMERA" );
	if( cameraEnv == nullptr )
	{
		std::printf( "SKIP launcher end to end (set MPP_E2E_CAMERA)\n" );
		return;
	}

	const uint16_t port = 19410;
	mpp::PoseReceiver rx;
	CHECK( rx.Start( port ) );

	auto launcher = mpp::TrackerLauncher::Acquire( port );
	CHECK( !launcher->HasSettings() );
	CHECK( mpp::TrackerLauncher::Acquire( port ) == launcher );// shared per port

	mpp::TrackerSettings settings;
	settings.camera = std::atoi( cameraEnv );
	launcher->Apply( settings );
	std::printf( "launcher log: %s\n", launcher->LogPath().c_str() );

	auto started = std::chrono::steady_clock::now();
	auto seconds = [ &started ] {
		return std::chrono::duration< double >( std::chrono::steady_clock::now() - started ).count();
	};
	while( launcher->State() != mpp::TrackerState::Running && seconds() < 60.0 )
		std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
	std::printf( "state %s after %.1f s\n", mpp::TrackerStateLabel( launcher->State() ), seconds() );
	CHECK( launcher->State() == mpp::TrackerState::Running );

	// The tracker sends /mp/pose or /mp/clear every camera frame, so something
	// has to arrive whether or not anybody is in front of the camera.
	mpp::PoseUpdate got;
	bool received = false;
	while( !received && seconds() < 90.0 )
	{
		received = rx.PollLatest( got );
		if( !received )
			std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
	}
	std::printf( "first packet after %.1f s, body 0 present=%d\n", seconds(), received ? int( got.frames[ 0 ].present ) : -1 );
	CHECK( received );

	int present = 0, total = 0;
	auto sampleUntil = seconds() + 3.0;
	while( seconds() < sampleUntil )
	{
		if( rx.PollLatest( got ) )
		{
			++total;
			present += got.frames[ 0 ].present ? 1 : 0;
		}
		std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
	}
	std::printf( "3 s sample: %d updates, body present in %d\n", total, present );
	CHECK( total > 10 );

	// Releasing the last reference must take the tracker down with it.
	launcher.reset();
	const uint64_t before = rx.PacketCount();
	std::this_thread::sleep_for( std::chrono::milliseconds( 1500 ) );
	const uint64_t after = rx.PacketCount();
	std::printf( "packets in 1.5 s after release: %d\n", int( after - before ) );
	CHECK( after - before < 5 );
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
	TestReceiverRebindsAfterAFailedStart();
	TestReceiversShareAPort();
	TestOtherProcessCannotSplitThePort();
	TestQuoteWindowsArgument();
	TestBuildTrackerArguments();
	TestFindTrackerFiles();
	TestEnumerateCameras();
	TestLauncherEndToEnd();

	if( failures == 0 )
		std::printf( "all pose tests passed\n" );
	else
		std::printf( "%d check(s) failed\n", failures );
	return failures == 0 ? 0 : 1;
}
