#pragma once
//
// Minimal OSC-over-UDP receiver for MediaPipe pose data.
//
// Deliberately dependency free: FFGL plugins are loaded into Resolume's
// process, so every extra shared library is a deployment problem. This handles
// exactly the subset of OSC 1.0 the tracker emits (int32, float32, bundles).
//
#include "PoseProtocol.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <thread>

namespace mpp
{
/// One camera frame worth of landmarks, straight off the wire (unfiltered).
struct PoseFrame
{
	int32_t frameId = 0;
	bool present    = false;///< false when the tracker reported /mp/clear
	float lm[ POSE_FLOAT_COUNT ] = { 0.0f };
};

/// Parses a single UDP datagram. Returns true when it contained a pose or a
/// clear message, in which case `out` holds the last one found in the packet.
/// Exposed (rather than hidden in the receive loop) so it can be unit tested.
bool ParsePosePacket( const char* data, size_t len, PoseFrame& out );

/// Background UDP listener. Keeps only the most recent frame -- if the render
/// thread is slower than the camera, dropping intermediate poses is correct.
class PoseReceiver
{
public:
	PoseReceiver()  = default;
	~PoseReceiver();

	PoseReceiver( const PoseReceiver& )            = delete;
	PoseReceiver& operator=( const PoseReceiver& ) = delete;

	/// Binds to `port` on the loopback-reachable wildcard address and starts
	/// the listener thread. Re-binds when called with a different port.
	bool Start( uint16_t port );
	void Stop();

	bool IsListening() const { return listening.load( std::memory_order_relaxed ); }
	uint16_t Port() const { return boundPort; }

	/// Copies the newest frame out. Returns false when nothing arrived since
	/// the previous call, so callers can keep extrapolating instead of
	/// snapping back to a stale pose.
	bool PollLatest( PoseFrame& out );

	/// Total datagrams accepted since Start(), for the status readout.
	uint64_t PacketCount() const { return packetCount.load( std::memory_order_relaxed ); }

private:
	void ReceiveLoop();
	void CloseSocket();

	std::thread worker;
	std::mutex frameMutex;
	PoseFrame latest;
	bool hasFresh = false;

	std::atomic< bool > running{ false };
	std::atomic< bool > listening{ false };
	std::atomic< uint64_t > packetCount{ 0 };
	uint16_t boundPort = 0;
	intptr_t sock      = -1;
};

}// namespace mpp
