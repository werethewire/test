#include "TrackerLauncher.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>

#if defined( _WIN32 )
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
	#include <dshow.h>
#else
	#include <dlfcn.h>
	#include <fcntl.h>
	#include <signal.h>
	#include <spawn.h>
	#include <sys/stat.h>
	#include <sys/wait.h>
	#include <unistd.h>
	#if defined( __APPLE__ )
		// A bundle cannot link against `environ` directly.
		#include <crt_externs.h>
		#define environ ( *_NSGetEnviron() )
	#else
extern char** environ;
	#endif
#endif

namespace mpp
{
namespace
{
#if defined( _WIN32 )
const char kSep = '\\';

std::wstring Widen( const std::string& s )
{
	if( s.empty() )
		return std::wstring();
	int n = MultiByteToWideChar( CP_UTF8, 0, s.data(), int( s.size() ), nullptr, 0 );
	std::wstring out( size_t( n ), L'\0' );
	MultiByteToWideChar( CP_UTF8, 0, s.data(), int( s.size() ), &out[ 0 ], n );
	return out;
}

std::string Narrow( const wchar_t* s )
{
	if( s == nullptr || *s == 0 )
		return std::string();
	int n = WideCharToMultiByte( CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr );
	std::string out( size_t( n ), '\0' );
	WideCharToMultiByte( CP_UTF8, 0, s, -1, &out[ 0 ], n, nullptr, nullptr );
	out.resize( size_t( n > 0 ? n - 1 : 0 ) );
	return out;
}

std::string GetEnv( const char* name )
{
	std::wstring wname = Widen( name );
	DWORD n            = GetEnvironmentVariableW( wname.c_str(), nullptr, 0 );
	if( n == 0 )
		return std::string();
	std::wstring value( n, L'\0' );
	GetEnvironmentVariableW( wname.c_str(), &value[ 0 ], n );
	return Narrow( value.c_str() );
}

bool FileExists( const std::string& path )
{
	DWORD attributes = GetFileAttributesW( Widen( path ).c_str() );
	return attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_DIRECTORY ) == 0;
}

std::string ModuleDirectory()
{
	HMODULE module = nullptr;
	if( !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
							 reinterpret_cast< LPCWSTR >( &ModuleDirectory ), &module ) )
		return std::string();
	wchar_t buffer[ 32768 ];
	DWORD n = GetModuleFileNameW( module, buffer, DWORD( sizeof( buffer ) / sizeof( buffer[ 0 ] ) ) );
	if( n == 0 )
		return std::string();
	std::string path = Narrow( buffer );
	size_t slash     = path.find_last_of( "\\/" );
	return slash == std::string::npos ? std::string() : path.substr( 0, slash );
}

std::string SearchExecutable( const wchar_t* name )
{
	wchar_t buffer[ MAX_PATH * 4 ];
	DWORD n = SearchPathW( nullptr, name, nullptr, DWORD( sizeof( buffer ) / sizeof( buffer[ 0 ] ) ), buffer, nullptr );
	if( n == 0 || n >= sizeof( buffer ) / sizeof( buffer[ 0 ] ) )
		return std::string();
	return Narrow( buffer );
}
#else
const char kSep = '/';

std::string GetEnv( const char* name )
{
	const char* value = std::getenv( name );
	return value != nullptr ? std::string( value ) : std::string();
}

bool FileExists( const std::string& path )
{
	struct stat st;
	return ::stat( path.c_str(), &st ) == 0 && S_ISREG( st.st_mode );
}

std::string ModuleDirectory()
{
	Dl_info info;
	if( dladdr( reinterpret_cast< void* >( &ModuleDirectory ), &info ) == 0 || info.dli_fname == nullptr )
		return std::string();
	std::string path = info.dli_fname;
	size_t slash     = path.find_last_of( '/' );
	return slash == std::string::npos ? std::string() : path.substr( 0, slash );
}
#endif

std::string Join( const std::string& dir, const std::string& name )
{
	if( dir.empty() )
		return name;
	char last = dir[ dir.size() - 1 ];
	if( last == '/' || last == '\\' )
		return dir + name;
	return dir + kSep + name;
}

std::string Parent( const std::string& dir )
{
	size_t slash = dir.find_last_of( "\\/" );
	return slash == std::string::npos ? std::string() : dir.substr( 0, slash );
}

std::string LogDirectory()
{
#if defined( _WIN32 )
	std::string base = GetEnv( "LOCALAPPDATA" );
	if( base.empty() )
		base = GetEnv( "TEMP" );
	std::string dir = Join( base, "MediaPipeParticles" );
	CreateDirectoryW( Widen( dir ).c_str(), nullptr );
	return dir;
#else
	std::string base = GetEnv( "TMPDIR" );
	return base.empty() ? std::string( "/tmp" ) : base;
#endif
}

void AppendToLog( const std::string& path, const std::string& text )
{
#if defined( _WIN32 )
	HANDLE file = CreateFileW( Widen( path ).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
							   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
	if( file == INVALID_HANDLE_VALUE )
		return;
	DWORD written = 0;
	WriteFile( file, text.data(), DWORD( text.size() ), &written, nullptr );
	CloseHandle( file );
#else
	int fd = ::open( path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644 );
	if( fd < 0 )
		return;
	ssize_t ignored = ::write( fd, text.data(), text.size() );
	(void)ignored;
	::close( fd );
#endif
}

void TruncateLog( const std::string& path )
{
#if defined( _WIN32 )
	HANDLE file = CreateFileW( Widen( path ).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
							   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
	if( file != INVALID_HANDLE_VALUE )
		CloseHandle( file );
#else
	int fd = ::open( path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644 );
	if( fd >= 0 )
		::close( fd );
#endif
}

std::string JoinCommand( const std::vector< std::string >& argv )
{
	std::string line;
	for( const std::string& arg : argv )
	{
		if( !line.empty() )
			line += ' ';
		line += QuoteWindowsArgument( arg );
	}
	return line;
}
}// namespace

// ------------------------------------------------------------ child process

/// A process whose output goes to a file and that cannot outlive the plugin.
class ChildProcess
{
public:
	ChildProcess() = default;
	~ChildProcess() { Stop(); }

	ChildProcess( const ChildProcess& )            = delete;
	ChildProcess& operator=( const ChildProcess& ) = delete;

	/// `logPath` empty discards the output.
	bool Start( const std::vector< std::string >& argv, const std::string& logPath );
	bool Running();
	void Stop();
	/// Valid once Running() has returned false.
	int ExitCode() const { return exitCode; }

private:
#if defined( _WIN32 )
	HANDLE process = nullptr;
	HANDLE job     = nullptr;
#else
	pid_t pid = -1;
#endif
	int exitCode = 0;
};

#if defined( _WIN32 )
bool ChildProcess::Start( const std::vector< std::string >& argv, const std::string& logPath )
{
	Stop();
	if( argv.empty() )
		return false;

	SECURITY_ATTRIBUTES inherit = { sizeof( SECURITY_ATTRIBUTES ), nullptr, TRUE };
	HANDLE output               = logPath.empty()
									  ? CreateFileW( L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr )
									  : CreateFileW( Widen( logPath ).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
	HANDLE input = CreateFileW( L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr );
	if( output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE )
	{
		if( output != INVALID_HANDLE_VALUE )
			CloseHandle( output );
		if( input != INVALID_HANDLE_VALUE )
			CloseHandle( input );
		return false;
	}

	// Hand the child exactly these two handles. Inheriting everything would
	// give it Resolume's inheritable handles too -- the plugin's own UDP socket
	// among them, which would then keep the port bound after Resolume quits.
	HANDLE inheritList[ 2 ] = { output, input };
	SIZE_T attrSize         = 0;
	InitializeProcThreadAttributeList( nullptr, 1, 0, &attrSize );
	std::vector< BYTE > attrBuffer( attrSize );
	auto attrList = reinterpret_cast< LPPROC_THREAD_ATTRIBUTE_LIST >( attrBuffer.data() );
	bool attrOk   = InitializeProcThreadAttributeList( attrList, 1, 0, &attrSize ) &&
				  UpdateProcThreadAttribute( attrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritList,
											 sizeof( inheritList ), nullptr, nullptr );

	STARTUPINFOEXW si             = {};
	si.StartupInfo.cb             = sizeof( si );
	si.StartupInfo.dwFlags        = STARTF_USESTDHANDLES;
	si.StartupInfo.hStdInput      = input;
	si.StartupInfo.hStdOutput     = output;
	si.StartupInfo.hStdError      = output;
	si.lpAttributeList            = attrOk ? attrList : nullptr;
	std::wstring commandLine      = Widen( JoinCommand( argv ) );
	PROCESS_INFORMATION processInfo = {};

	// The job kills the whole tree (py.exe starts python.exe) when the plugin
	// closes it -- or when Resolume exits or crashes and Windows closes it for us,
	// so a tracker is never left holding the camera.
	job = CreateJobObjectW( nullptr, nullptr );
	if( job != nullptr )
	{
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
		limits.BasicLimitInformation.LimitFlags     = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject( job, JobObjectExtendedLimitInformation, &limits, sizeof( limits ) );
	}

	BOOL created = attrOk && CreateProcessW( nullptr, &commandLine[ 0 ], nullptr, nullptr, TRUE,
											 CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
											 nullptr, nullptr, &si.StartupInfo, &processInfo );
	if( attrOk )
		DeleteProcThreadAttributeList( attrList );
	CloseHandle( output );
	CloseHandle( input );

	if( !created )
	{
		if( job != nullptr )
			CloseHandle( job );
		job = nullptr;
		return false;
	}
	if( job != nullptr )
		AssignProcessToJobObject( job, processInfo.hProcess );
	ResumeThread( processInfo.hThread );
	CloseHandle( processInfo.hThread );
	process  = processInfo.hProcess;
	exitCode = 0;
	return true;
}

bool ChildProcess::Running()
{
	if( process == nullptr )
		return false;
	if( WaitForSingleObject( process, 0 ) == WAIT_TIMEOUT )
		return true;
	DWORD code = 0;
	GetExitCodeProcess( process, &code );
	exitCode = int( code );
	return false;
}

void ChildProcess::Stop()
{
	if( job != nullptr )
		TerminateJobObject( job, 1 );
	else if( process != nullptr )
		TerminateProcess( process, 1 );
	if( process != nullptr )
	{
		// The camera is only free again once the process is really gone.
		WaitForSingleObject( process, 5000 );
		CloseHandle( process );
		process = nullptr;
	}
	if( job != nullptr )
	{
		CloseHandle( job );
		job = nullptr;
	}
}
#else
bool ChildProcess::Start( const std::vector< std::string >& argv, const std::string& logPath )
{
	Stop();
	if( argv.empty() )
		return false;

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init( &actions );
	posix_spawn_file_actions_addopen( &actions, 0, "/dev/null", O_RDONLY, 0 );
	if( logPath.empty() )
		posix_spawn_file_actions_addopen( &actions, 1, "/dev/null", O_WRONLY, 0 );
	else
		posix_spawn_file_actions_addopen( &actions, 1, logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644 );
	posix_spawn_file_actions_adddup2( &actions, 1, 2 );

	std::vector< char* > args;
	for( const std::string& arg : argv )
		args.push_back( const_cast< char* >( arg.c_str() ) );
	args.push_back( nullptr );

	int result = posix_spawnp( &pid, argv[ 0 ].c_str(), &actions, nullptr, args.data(), environ );
	posix_spawn_file_actions_destroy( &actions );
	if( result != 0 )
	{
		pid = -1;
		return false;
	}
	exitCode = 0;
	return true;
}

bool ChildProcess::Running()
{
	if( pid <= 0 )
		return false;
	int status = 0;
	pid_t done = waitpid( pid, &status, WNOHANG );
	if( done == 0 )
		return true;
	exitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : 128;
	pid      = -1;
	return false;
}

void ChildProcess::Stop()
{
	if( pid <= 0 )
		return;
	kill( pid, SIGTERM );
	for( int i = 0; i < 50; ++i )
	{
		int status = 0;
		if( waitpid( pid, &status, WNOHANG ) != 0 )
		{
			pid = -1;
			return;
		}
		usleep( 100 * 1000 );
	}
	kill( pid, SIGKILL );
	int status = 0;
	waitpid( pid, &status, 0 );
	pid = -1;
}
#endif

// ------------------------------------------------------------ helpers

const char* TrackerStateLabel( TrackerState s )
{
	switch( s )
	{
	case TrackerState::Off:
		return "Off";
	case TrackerState::Starting:
		return "Starting";
	case TrackerState::Running:
		return "Running";
	case TrackerState::NoPython:
		return "No Python";
	case TrackerState::NoScript:
		return "No Script";
	case TrackerState::NoModel:
		return "No Model";
	case TrackerState::Exited:
		return "Stopped";
	}
	return "?";
}

TrackerState FindTrackerFiles( const std::vector< std::string >& dirs, TrackerFiles& out )
{
	static const char* const kModels[] = {
		"pose_landmarker_full.task",
		"pose_landmarker_heavy.task",
		"pose_landmarker_lite.task",
		"pose_landmarker.task",
	};

	bool sawScript = false;
	for( const std::string& dir : dirs )
	{
		if( dir.empty() )
			continue;
		std::string script = Join( dir, "pose_osc.py" );
		if( !FileExists( script ) )
			continue;
		if( !sawScript )
			out.script = script;
		sawScript = true;
		for( const char* model : kModels )
		{
			std::string candidate = Join( dir, model );
			if( FileExists( candidate ) )
			{
				out.script = script;
				out.model  = candidate;
				return TrackerState::Running;
			}
		}
	}
	return sawScript ? TrackerState::NoModel : TrackerState::NoScript;
}

std::vector< std::string > BuildTrackerArguments( const TrackerFiles& files, const TrackerSettings& settings,
												  uint16_t port )
{
	int people = settings.people < 1 ? 1 : ( settings.people > 3 ? 3 : settings.people );
	int camera = settings.camera < 0 ? 0 : settings.camera;

	std::vector< std::string > args = {
		files.script,
		"--model", files.model,
		"--people", std::to_string( people ),
		"--device", std::to_string( camera ),
		"--port", std::to_string( port ),
#if defined( _WIN32 )
		// The camera list comes from DirectShow, so its indices only mean the
		// same device under OpenCV's DirectShow backend.
		"--backend", "dshow",
#endif
	};
	if( settings.preview )
		args.push_back( "--preview" );
	return args;
}

std::string QuoteWindowsArgument( const std::string& arg )
{
	if( !arg.empty() && arg.find_first_of( " \t\n\v\"" ) == std::string::npos )
		return arg;

	std::string out = "\"";
	size_t i        = 0;
	while( true )
	{
		size_t backslashes = 0;
		while( i < arg.size() && arg[ i ] == '\\' )
		{
			++i;
			++backslashes;
		}
		if( i == arg.size() )
		{
			// Backslashes before the closing quote must be doubled.
			out.append( backslashes * 2, '\\' );
			break;
		}
		if( arg[ i ] == '"' )
		{
			out.append( backslashes * 2 + 1, '\\' );
			out.push_back( '"' );
		}
		else
		{
			out.append( backslashes, '\\' );
			out.push_back( arg[ i ] );
		}
		++i;
	}
	out.push_back( '"' );
	return out;
}

std::vector< std::string > DefaultTrackerDirs()
{
	std::vector< std::string > dirs;
	std::string fromEnv = GetEnv( "MPP_TRACKER_DIR" );
	if( !fromEnv.empty() )
		dirs.push_back( fromEnv );

	std::string moduleDir = ModuleDirectory();
	if( !moduleDir.empty() )
	{
		dirs.push_back( Join( moduleDir, "MediaPipeParticles" ) );
#if defined( __APPLE__ )
		// .../MediaPipeParticles.bundle/Contents/MacOS/MediaPipeParticles
		dirs.push_back( Join( Parent( Parent( Parent( moduleDir ) ) ), "MediaPipeParticles" ) );
#endif
		// A build tree: build/Release/<plugin> next to plugin/ and tracker/.
		dirs.push_back( Join( Parent( Parent( moduleDir ) ), "tracker" ) );
		dirs.push_back( Join( Parent( moduleDir ), "tracker" ) );
	}
	return dirs;
}

namespace
{
/// Interpreters worth trying, each as the argv prefix that runs it.
std::vector< std::vector< std::string > > PythonCandidates()
{
	std::vector< std::vector< std::string > > candidates;
	std::string fromEnv = GetEnv( "MPP_PYTHON" );
	if( !fromEnv.empty() )
		candidates.push_back( { fromEnv } );
#if defined( _WIN32 )
	// The py launcher's default is simply the newest Python, which is often
	// newer than any mediapipe wheel; ask for the versions mediapipe ships for.
	std::string py = SearchExecutable( L"py.exe" );
	if( !py.empty() )
	{
		for( const char* version : { "-3.12", "-3.11", "-3.10", "-3.13", "-3.9" } )
			candidates.push_back( { py, version } );
	}
	std::string python = SearchExecutable( L"python.exe" );
	// The WindowsApps python.exe is a Store alias, not an interpreter.
	if( !python.empty() && python.find( "WindowsApps" ) == std::string::npos )
		candidates.push_back( { python } );
	if( !py.empty() )
		candidates.push_back( { py, "-3" } );
#else
	candidates.push_back( { "python3" } );
	candidates.push_back( { "python" } );
#endif
	return candidates;
}

std::vector< std::string > FindPython( const std::string& logPath, const std::function< bool() >& quit )
{
	// find_spec rather than import: importing mediapipe takes seconds, and all
	// that matters here is whether it is installed.
	static const char* const kProbe =
		"import importlib.util as u, sys; "
		"sys.exit(0 if u.find_spec('mediapipe') and u.find_spec('cv2') else 3)";

	for( const std::vector< std::string >& candidate : PythonCandidates() )
	{
		if( quit() )
			return {};
		std::vector< std::string > argv = candidate;
		argv.push_back( "-c" );
		argv.push_back( kProbe );

		ChildProcess probe;
		if( !probe.Start( argv, std::string() ) )
			continue;
		bool finished = false;
		for( int i = 0; i < 300 && !finished; ++i )// 30 s: a cold first run can be slow
		{
			if( quit() )
				return {};
			if( !probe.Running() )
				finished = true;
			else
				std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
		}
		if( !finished )
			continue;// the destructor kills it
		AppendToLog( logPath, "probe " + JoinCommand( candidate ) + " -> exit " + std::to_string( probe.ExitCode() ) + "\n" );
		if( probe.ExitCode() == 0 )
			return candidate;
	}
	return {};
}

struct LauncherRegistry
{
	std::mutex mutex;
	std::map< uint16_t, std::weak_ptr< TrackerLauncher > > byPort;
};

LauncherRegistry& Launchers()
{
	// Leaked on purpose, as with the OSC listener registry.
	static LauncherRegistry* registry = new LauncherRegistry();
	return *registry;
}
}// namespace

// ------------------------------------------------------------ launcher

std::shared_ptr< TrackerLauncher > TrackerLauncher::Acquire( uint16_t port )
{
	LauncherRegistry& registry = Launchers();
	std::lock_guard< std::mutex > lock( registry.mutex );
	auto found = registry.byPort.find( port );
	if( found != registry.byPort.end() )
	{
		if( auto existing = found->second.lock() )
			return existing;
	}
	std::shared_ptr< TrackerLauncher > created( new TrackerLauncher( port ) );
	registry.byPort[ port ] = created;
	return created;
}

TrackerLauncher::TrackerLauncher( uint16_t port_ ) :
	port( port_ )
{
	logPath = Join( LogDirectory(), "tracker-" + std::to_string( port ) + ".log" );
	TruncateLog( logPath );
	worker = std::thread( &TrackerLauncher::Run, this );
}

TrackerLauncher::~TrackerLauncher()
{
	{
		std::lock_guard< std::mutex > lock( mutex );
		quitting = true;
	}
	wake.notify_all();
	if( worker.joinable() )
		worker.join();
}

bool TrackerLauncher::HasSettings() const
{
	std::lock_guard< std::mutex > lock( mutex );
	return haveSettings;
}

void TrackerLauncher::Apply( const TrackerSettings& settings )
{
	{
		std::lock_guard< std::mutex > lock( mutex );
		if( haveSettings && wanted == settings )
			return;
		wanted       = settings;
		wantedAt     = std::chrono::steady_clock::now();
		haveSettings = true;
	}
	wake.notify_all();
}

void TrackerLauncher::Restart()
{
	{
		std::lock_guard< std::mutex > lock( mutex );
		restartRequested = true;
	}
	wake.notify_all();
}

void TrackerLauncher::Run()
{
	using clock = std::chrono::steady_clock;
	const auto settleTime = std::chrono::milliseconds( 400 );

	ChildProcess child;
	bool childLaunched = false;
	TrackerSettings launchedWith;

	std::vector< std::string > python;
	bool pythonSearched = false;

	// After a failure, the same settings are not retried until this passes;
	// different settings are tried straight away.
	bool haveFailure = false;
	TrackerSettings failedWith;
	clock::time_point retryAt;

	while( true )
	{
		TrackerSettings want;
		clock::time_point wantAt;
		bool have    = false;
		bool restart = false;
		{
			std::unique_lock< std::mutex > lock( mutex );
			wake.wait_for( lock, std::chrono::milliseconds( 200 ), [ this ] { return quitting || restartRequested; } );
			if( quitting )
				break;
			want             = wanted;
			wantAt           = wantedAt;
			have             = haveSettings;
			restart          = restartRequested;
			restartRequested = false;
		}
		const auto now = clock::now();

		if( restart )
		{
			AppendToLog( logPath, "--- restart requested\n" );
			child.Stop();
			childLaunched  = false;
			pythonSearched = false;
			haveFailure    = false;
		}
		if( !have )
			continue;

		if( !want.enabled )
		{
			if( childLaunched )
			{
				child.Stop();
				childLaunched = false;
				AppendToLog( logPath, "--- tracker switched off\n" );
			}
			haveFailure = false;
			state.store( TrackerState::Off );
			continue;
		}

		if( childLaunched )
		{
			if( child.Running() )
			{
				if( launchedWith == want )
				{
					state.store( TrackerState::Running );
					continue;
				}
				if( now - wantAt < settleTime )
					continue;
				child.Stop();
				childLaunched = false;
			}
			else
			{
				childLaunched = false;
				AppendToLog( logPath, "--- tracker exited with code " + std::to_string( child.ExitCode() ) +
										  ", retrying in 5 s\n" );
				state.store( TrackerState::Exited );
				haveFailure = true;
				failedWith  = launchedWith;
				retryAt     = now + std::chrono::seconds( 5 );
				continue;
			}
		}

		if( now - wantAt < settleTime )
			continue;
		if( haveFailure && failedWith == want && now < retryAt )
			continue;

		TrackerFiles files;
		TrackerState found = FindTrackerFiles( DefaultTrackerDirs(), files );
		if( found != TrackerState::Running )
		{
			state.store( found );
			haveFailure = true;
			failedWith  = want;
			retryAt     = now + std::chrono::seconds( 5 );
			continue;
		}

		if( !pythonSearched )
		{
			state.store( TrackerState::Starting );
			// Checks for shutdown between probes, so removing the last clip does
			// not stall the host for as long as a slow interpreter takes.
			python = FindPython( logPath, [ this ] {
				std::lock_guard< std::mutex > lock( mutex );
				return quitting;
			} );
			pythonSearched = true;
		}
		if( python.empty() )
		{
			// Searching again means spawning a handful of interpreters, so this
			// waits for Restart Tracker (after installing mediapipe, say).
			state.store( TrackerState::NoPython );
			haveFailure = true;
			failedWith  = want;
			retryAt     = clock::time_point::max();
			continue;
		}

		std::vector< std::string > argv = python;
		for( const char* flag : { "-X", "utf8", "-u" } )
			argv.push_back( flag );
		for( const std::string& arg : BuildTrackerArguments( files, want, port ) )
			argv.push_back( arg );

		AppendToLog( logPath, "--- launching " + JoinCommand( argv ) + "\n" );
		state.store( TrackerState::Starting );
		if( child.Start( argv, logPath ) )
		{
			childLaunched = true;
			launchedWith  = want;
			haveFailure   = false;
		}
		else
		{
			AppendToLog( logPath, "--- could not start the process\n" );
			state.store( TrackerState::Exited );
			haveFailure = true;
			failedWith  = want;
			retryAt     = now + std::chrono::seconds( 5 );
		}
	}

	child.Stop();
}

// ------------------------------------------------------------ cameras

std::vector< std::string > EnumerateCameras()
{
	std::vector< std::string > names;
#if defined( _WIN32 )
	// COM on a thread of our own: the host thread may already be in an
	// apartment of a different kind, and this must not disturb it.
	std::thread enumerate( [ &names ] {
		HRESULT init        = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
		ICreateDevEnum* dev = nullptr;
		if( SUCCEEDED( CoCreateInstance( CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum,
										 reinterpret_cast< void** >( &dev ) ) ) )
		{
			IEnumMoniker* monikers = nullptr;
			if( dev->CreateClassEnumerator( CLSID_VideoInputDeviceCategory, &monikers, 0 ) == S_OK )
			{
				IMoniker* moniker = nullptr;
				while( monikers->Next( 1, &moniker, nullptr ) == S_OK )
				{
					std::string name;
					IPropertyBag* bag = nullptr;
					if( SUCCEEDED( moniker->BindToStorage( nullptr, nullptr, IID_IPropertyBag, reinterpret_cast< void** >( &bag ) ) ) )
					{
						VARIANT value;
						VariantInit( &value );
						if( SUCCEEDED( bag->Read( L"FriendlyName", &value, nullptr ) ) && value.vt == VT_BSTR )
							name = Narrow( value.bstrVal );
						VariantClear( &value );
						bag->Release();
					}
					if( name.empty() )
						name = "Camera " + std::to_string( names.size() );
					names.push_back( name );
					moniker->Release();
				}
				monikers->Release();
			}
			dev->Release();
		}
		if( SUCCEEDED( init ) )
			CoUninitialize();
	} );
	enumerate.join();
#endif
	return names;
}

}// namespace mpp
