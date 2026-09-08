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

/// Mirrors what tracker/pose_osc.py puts on the wire.
static std::vector< char > MakePosePacket( int32_t frameId, const float* lm )
{
	std::vector< char > p;
	PushString( p, "/mp/pose" );
	std::string tags = ",i";
	tags.append( mpp::POSE_FLOAT_COUNT, 'f' );
	PushString( p, tags.c_str() );
	PushInt32( p, frameId );
	for( int i = 0; i < mpp::POSE_FLOAT_COUNT; ++i )
		PushFloat( p, lm[ i ] );
	return p;
}

static std::vector< char > MakeClearPacket( int32_t frameId )
{
	std::vector< char > p;
	PushString( p, "/mp/clear" );
	PushString( p, ",i" );
	PushInt32( p, frameId );
	return p;
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
static void FillTPose( float* lm, float shiftX = 0.0f )
{
	for( int i = 0; i < mpp::NUM_LANDMARKS; ++i )
	{
		lm[ i * 4 + 0 ] = 0.5f + shiftX;
		lm[ i * 4 + 1 ] = 0.5f;
		lm[ i * 4 + 2 ] = 0.0f;
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

	mpp::PoseFrame f;
	CHECK( mpp::ParsePosePacket( packet.data(), packet.size(), f ) );
	CHECK( f.present );
	CHECK( f.frameId == 42 );
	CHECK_NEAR( f.lm[ mpp::LM_LEFT_WRIST * 4 + 0 ], 0.18f, 1e-6 );
	CHECK_NEAR( f.lm[ mpp::LM_RIGHT_ANKLE * 4 + 1 ], 0.93f, 1e-6 );
	CHECK_NEAR( f.lm[ mpp::LM_NOSE * 4 + 3 ], 1.0f, 1e-6 );
}

static void TestParseClear()
{
	auto packet = MakeClearPacket( 7 );
	mpp::PoseFrame f;
	f.present = true;
	CHECK( mpp::ParsePosePacket( packet.data(), packet.size(), f ) );
	CHECK( !f.present );
	CHECK( f.frameId == 7 );
}

static void TestParseBundleTakesLast()
{
	float a[ mpp::POSE_FLOAT_COUNT ];
	float b[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( a, 0.0f );
	FillTPose( b, 0.1f );
	auto packet = MakeBundle( { MakePosePacket( 1, a ), MakePosePacket( 2, b ) } );

	mpp::PoseFrame f;
	CHECK( mpp::ParsePosePacket( packet.data(), packet.size(), f ) );
	CHECK( f.frameId == 2 );
	CHECK_NEAR( f.lm[ mpp::LM_NOSE * 4 + 0 ], 0.60f, 1e-6 );
}

static void TestRejectsGarbage()
{
	mpp::PoseFrame f;
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
	mpp::PoseFrame frame;
	frame.present = true;
	std::memcpy( frame.lm, lm, sizeof( lm ) );

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
	mpp::PoseFrame frame;
	frame.present = true;
	std::memcpy( frame.lm, lm, sizeof( lm ) );

	mpp::PoseTracker t;
	t.SetSmoothing( 0.0f );
	// Visibility is eased in, so let it settle before judging the table.
	for( int i = 0; i < 120; ++i )
		t.Update( &frame, 1.0f / 60.0f );

	CHECK( t.HasEmitters() );
	const float* cdf = t.BoneCdf();
	for( int i = 1; i < mpp::NUM_BONES; ++i )
		CHECK( cdf[ i ] >= cdf[ i - 1 ] - 1e-6f );
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
	mpp::PoseFrame hidden = frame;
	for( int i = 0; i < mpp::NUM_LANDMARKS; ++i )
		hidden.lm[ i * 4 + 3 ] = 0.0f;
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
	mpp::PoseFrame frame;
	frame.present = true;
	std::memcpy( frame.lm, lm, sizeof( lm ) );

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
	mpp::PoseFrame clear;
	clear.present = false;
	for( int i = 0; i < 300; ++i )
		c.Update( &clear, 1.0f / 60.0f );
	CHECK( c.Presence() < 0.01f );
}

static void TestMotionEnergy()
{
	float lm[ mpp::POSE_FLOAT_COUNT ];
	FillTPose( lm );
	mpp::PoseFrame still;
	still.present = true;
	std::memcpy( still.lm, lm, sizeof( lm ) );

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
		mpp::PoseFrame f;
		f.present = true;
		std::memcpy( f.lm, moving, sizeof( moving ) );
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

	mpp::PoseFrame got;
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
		CHECK( got.frameId == 99 );
		CHECK( got.present );
		// A second poll with nothing new must report "no fresh frame".
		mpp::PoseFrame again;
		CHECK( !rx.PollLatest( again ) );
	}
	rx.Stop();
	CHECK( !rx.IsListening() );
}

int main()
{
	TestParsePose();
	TestParseClear();
	TestParseBundleTakesLast();
	TestRejectsGarbage();
	TestOneEuroTracksAndSmooths();
	TestTrackerMappingAndMirror();
	TestEmissionTable();
	TestPresenceEnvelope();
	TestMotionEnergy();
	TestReceiverRoundTrip();

	if( failures == 0 )
		std::printf( "all pose tests passed\n" );
	else
		std::printf( "%d check(s) failed\n", failures );
	return failures == 0 ? 0 : 1;
}
