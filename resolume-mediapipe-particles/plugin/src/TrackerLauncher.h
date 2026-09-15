#pragma once
//
// Starts tracker/pose_osc.py from inside the plugin, so placing the source in
// Resolume is all it takes to get a camera tracked -- no terminal on a show
// machine. One tracker process per OSC port, shared by every instance on that
// port, and it dies with Resolume.
//
// The tracker still needs a Python with mediapipe and opencv-python installed;
// the plugin finds one, it does not ship one. (A Python dropped next to the
// plugin would put its DLLs in the folder Resolume scans for plugins.)
//
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mpp
{
/// What the instance's Camera parameters ask for.
struct TrackerSettings
{
	bool enabled = true;
	int camera   = 0;///< DirectShow order on Windows, which is OpenCV's CAP_DSHOW index
	int people   = 1;
	bool preview = false;

	bool operator==( const TrackerSettings& o ) const
	{
		return enabled == o.enabled && camera == o.camera && people == o.people && preview == o.preview;
	}
	bool operator!=( const TrackerSettings& o ) const { return !( *this == o ); }
};

enum class TrackerState
{
	Off,      ///< disabled by the Tracker parameter
	Starting, ///< looking for Python / launching
	Running,
	NoPython, ///< no interpreter with mediapipe and cv2
	NoScript, ///< pose_osc.py not found
	NoModel,  ///< no pose_landmarker .task file next to it
	Exited,   ///< the tracker quit on its own; retried after a pause
};

/// Short label for the parameter display name, e.g. "Running".
const char* TrackerStateLabel( TrackerState state );

struct TrackerFiles
{
	std::string script;///< UTF-8 path to pose_osc.py
	std::string model; ///< UTF-8 path to a pose_landmarker .task
};

/// Looks through `dirs` (UTF-8) for pose_osc.py and a model beside it,
/// preferring pose_landmarker_full over heavy over lite. Returns Running when
/// both were found, otherwise NoScript or NoModel.
TrackerState FindTrackerFiles( const std::vector< std::string >& dirs, TrackerFiles& out );

/// Arguments that follow the interpreter (and its own flags) on the command line.
std::vector< std::string > BuildTrackerArguments( const TrackerFiles& files, const TrackerSettings& settings,
												  uint16_t port );

/// Quotes one argument the way CommandLineToArgvW / the MSVC runtime parse it.
std::string QuoteWindowsArgument( const std::string& arg );

/// Directories searched for the tracker, most specific first: $MPP_TRACKER_DIR,
/// then a MediaPipeParticles folder next to the plugin binary, then the
/// repository's tracker/ folder relative to a build tree.
std::vector< std::string > DefaultTrackerDirs();

/// Camera names in capture index order. Windows lists DirectShow video inputs,
/// which is the order OpenCV's CAP_DSHOW numbers them in. Other platforms
/// return an empty list; the caller falls back to plain indices.
std::vector< std::string > EnumerateCameras();

class ChildProcess;

class TrackerLauncher
{
public:
	/// The launcher for `port`, shared by every instance using that port.
	static std::shared_ptr< TrackerLauncher > Acquire( uint16_t port );

	~TrackerLauncher();

	TrackerLauncher( const TrackerLauncher& )            = delete;
	TrackerLauncher& operator=( const TrackerLauncher& ) = delete;

	/// False until some instance has applied settings.
	bool HasSettings() const;
	/// Takes effect after a short settle time, so scrolling through the camera
	/// list does not start and kill a tracker for every entry passed.
	void Apply( const TrackerSettings& settings );
	/// Kill and relaunch, and look for Python and the tracker files again.
	void Restart();

	TrackerState State() const { return state.load( std::memory_order_relaxed ); }
	/// Where the tracker's output goes, for the README and bug reports.
	std::string LogPath() const { return logPath; }

private:
	explicit TrackerLauncher( uint16_t port );
	void Run();

	const uint16_t port;
	std::string logPath;

	mutable std::mutex mutex;
	std::condition_variable wake;
	bool haveSettings = false;
	TrackerSettings wanted;
	std::chrono::steady_clock::time_point wantedAt;
	bool restartRequested = false;
	bool quitting         = false;

	std::atomic< TrackerState > state{ TrackerState::Starting };
	std::thread worker;
};

}// namespace mpp
