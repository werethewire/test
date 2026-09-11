#include "OscPose.h"

#include <cstring>

#if defined( _WIN32 )
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment( lib, "Ws2_32.lib" )
	using socket_t                     = SOCKET;
	static const socket_t kInvalidSock = INVALID_SOCKET;
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <sys/time.h>
	#include <unistd.h>
	using socket_t                     = int;
	static const socket_t kInvalidSock = -1;
#endif

namespace mpp
{
namespace
{
/// Cursor over an OSC packet that refuses to read past the end.
struct Reader
{
	const unsigned char* p;
	const unsigned char* end;

	size_t Remaining() const { return size_t( end - p ); }

	bool ReadInt32( int32_t& out )
	{
		if( Remaining() < 4 )
			return false;
		out = int32_t( ( uint32_t( p[ 0 ] ) << 24 ) | ( uint32_t( p[ 1 ] ) << 16 ) |
					   ( uint32_t( p[ 2 ] ) << 8 ) | uint32_t( p[ 3 ] ) );
		p += 4;
		return true;
	}

	bool ReadFloat32( float& out )
	{
		int32_t bits;
		if( !ReadInt32( bits ) )
			return false;
		uint32_t u = uint32_t( bits );
		std::memcpy( &out, &u, sizeof( float ) );
		return true;
	}

	/// OSC strings are NUL terminated and padded to a 4 byte boundary.
	bool ReadString( const char*& out )
	{
		const unsigned char* start = p;
		while( p < end && *p != 0 )
			++p;
		if( p >= end )
			return false;// unterminated
		out          = reinterpret_cast< const char* >( start );
		size_t bytes = size_t( p - start ) + 1;
		size_t pad   = ( 4 - ( bytes & 3 ) ) & 3;
		if( Remaining() < 1 + pad )
			return false;
		p += 1 + pad;
		return true;
	}
};

bool ParseMessage( Reader r, PoseUpdate& out );

bool ParseElement( Reader r, PoseUpdate& out )
{
	if( r.Remaining() >= 8 && std::memcmp( r.p, "#bundle", 8 ) == 0 )
	{
		r.p += 8;// address
		if( r.Remaining() < 8 )
			return false;
		r.p += 8;// time tag, ignored: we always want the freshest pose

		bool any = false;
		while( r.Remaining() >= 4 )
		{
			int32_t size;
			if( !r.ReadInt32( size ) )
				break;
			if( size < 0 || size_t( size ) > r.Remaining() )
				break;
			Reader sub{ r.p, r.p + size };
			any |= ParseElement( sub, out );
			r.p += size;
		}
		return any;
	}
	return ParseMessage( r, out );
}

bool ParseMessage( Reader r, PoseUpdate& out )
{
	const char* address = nullptr;
	if( !r.ReadString( address ) )
		return false;

	const bool isPose  = std::strcmp( address, "/mp/pose" ) == 0;
	const bool isClear = std::strcmp( address, "/mp/clear" ) == 0;
	if( !isPose && !isClear )
		return false;

	const char* tags = nullptr;
	if( !r.ReadString( tags ) || tags[ 0 ] != ',' )
		return false;
	++tags;// skip the comma

	PoseFrame parsed;
	parsed.present = isPose;

	int floatsSeen = 0;
	int intsSeen   = 0;
	bool gotPerson = false;
	for( const char* t = tags; *t != 0; ++t )
	{
		if( *t == 'i' )
		{
			int32_t v;
			if( !r.ReadInt32( v ) )
				return false;
			// First int is the frame counter, second (optional) is the body.
			// A single person tracker omits the second one.
			if( intsSeen == 0 )
				parsed.frameId = v;
			else if( intsSeen == 1 )
			{
				parsed.personId = v;
				gotPerson       = true;
			}
			++intsSeen;
		}
		else if( *t == 'f' )
		{
			float v;
			if( !r.ReadFloat32( v ) )
				return false;
			if( floatsSeen < POSE_FLOAT_COUNT )
				parsed.lm[ floatsSeen ] = v;
			++floatsSeen;
		}
		else
		{
			// Any other type means this is not a message we understand; bail
			// rather than mis-associating the remaining arguments.
			return false;
		}
	}

	if( isPose && floatsSeen < POSE_FLOAT_COUNT )
		return false;// truncated pose, keep the previous one

	if( isClear && !gotPerson )
	{
		// A bare clear means the frame is empty: retire every body.
		for( int i = 0; i < MAX_PERSONS; ++i )
		{
			out.frames[ i ]          = parsed;
			out.frames[ i ].personId = i;
			out.fresh[ i ]           = true;
		}
		return true;
	}

	// Bodies beyond what the plugin can draw are dropped here rather than
	// wrapping onto somebody else's slot.
	if( parsed.personId < 0 || parsed.personId >= MAX_PERSONS )
		return false;

	out.frames[ parsed.personId ] = parsed;
	out.fresh[ parsed.personId ]  = true;
	return true;
}
}// namespace

bool ParsePosePacket( const char* data, size_t len, PoseUpdate& out )
{
	if( data == nullptr || len < 4 )
		return false;
	Reader r{ reinterpret_cast< const unsigned char* >( data ),
			  reinterpret_cast< const unsigned char* >( data ) + len };
	return ParseElement( r, out );
}

PoseReceiver::~PoseReceiver()
{
	Stop();
}

bool PoseReceiver::Start( uint16_t port )
{
	if( running.load() && boundPort == port )
		return listening.load();
	Stop();

#if defined( _WIN32 )
	// Refcounted, and Resolume has almost certainly done this already.
	WSADATA wsa;
	WSAStartup( MAKEWORD( 2, 2 ), &wsa );
#endif

	socket_t s = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if( s == kInvalidSock )
		return false;

	int reuse = 1;
	::setsockopt( s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast< const char* >( &reuse ), sizeof( reuse ) );

	// Wake up periodically so Stop() does not have to wait for a datagram.
#if defined( _WIN32 )
	DWORD timeoutMs = 200;
	::setsockopt( s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast< const char* >( &timeoutMs ), sizeof( timeoutMs ) );
#else
	struct timeval tv;
	tv.tv_sec  = 0;
	tv.tv_usec = 200 * 1000;
	::setsockopt( s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof( tv ) );
#endif

	sockaddr_in addr;
	std::memset( &addr, 0, sizeof( addr ) );
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl( INADDR_ANY );
	addr.sin_port        = htons( port );
	if( ::bind( s, reinterpret_cast< sockaddr* >( &addr ), sizeof( addr ) ) != 0 )
	{
#if defined( _WIN32 )
		::closesocket( s );
#else
		::close( s );
#endif
		return false;
	}

	sock      = intptr_t( s );
	boundPort = port;
	packetCount.store( 0 );
	running.store( true );
	listening.store( true );
	worker = std::thread( &PoseReceiver::ReceiveLoop, this );
	return true;
}

void PoseReceiver::CloseSocket()
{
	if( sock == intptr_t( kInvalidSock ) || sock == -1 )
		return;
#if defined( _WIN32 )
	::closesocket( socket_t( sock ) );
#else
	::close( int( sock ) );
#endif
	sock = -1;
}

void PoseReceiver::Stop()
{
	running.store( false );
	listening.store( false );
	if( worker.joinable() )
		worker.join();
	CloseSocket();
	boundPort = 0;

	std::lock_guard< std::mutex > lock( frameMutex );
	pending = PoseUpdate();
}

void PoseReceiver::ReceiveLoop()
{
	// 33 landmarks * 4 floats is ~550 bytes; 4 KiB covers any bundling.
	char buffer[ 4096 ];
	while( running.load( std::memory_order_relaxed ) )
	{
#if defined( _WIN32 )
		int received = ::recv( socket_t( sock ), buffer, int( sizeof( buffer ) ), 0 );
#else
		ssize_t received = ::recv( int( sock ), buffer, sizeof( buffer ), 0 );
#endif
		if( received <= 0 )
			continue;// timeout or transient error

		PoseUpdate update;
		if( !ParsePosePacket( buffer, size_t( received ), update ) )
			continue;

		packetCount.fetch_add( 1, std::memory_order_relaxed );
		std::lock_guard< std::mutex > lock( frameMutex );
		for( int i = 0; i < MAX_PERSONS; ++i )
		{
			if( !update.fresh[ i ] )
				continue;
			pending.frames[ i ] = update.frames[ i ];
			pending.fresh[ i ]  = true;
		}
	}
}

bool PoseReceiver::PollLatest( PoseUpdate& out )
{
	std::lock_guard< std::mutex > lock( frameMutex );
	if( !pending.AnyFresh() )
		return false;
	out = pending;
	for( int i = 0; i < MAX_PERSONS; ++i )
		pending.fresh[ i ] = false;
	return true;
}

}// namespace mpp
