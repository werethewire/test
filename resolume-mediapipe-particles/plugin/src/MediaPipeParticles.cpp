#include "MediaPipeParticles.h"

#include "Thumbnail.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>

static CFFGLPluginInfo PluginInfo(
	PluginFactory< MediaPipeParticles >,// create method
	"MPPT",                             // plugin unique ID (4 chars)
	"Pose Particles",                   // plugin name: FFGL keeps only the first 16 chars
	2,                                  // API major version
	1,                                  // API minor version
	1,                                  // plugin major version
	0,                                  // plugin minor version
	FF_SOURCE,                          // plugin type
	"Camera pose driven particle system. Feed it with tracker/pose_osc.py.",
	"MediaPipe x Resolume particle bridge" );

// Resolume draws source thumbnails at 160x120; matching that exactly means the
// host never has to rescale ours.
static CFFGLThumbnailInfo ThumbnailInfo( 160, 120, mpp::GenerateThumbnail( 160, 120 ) );

namespace
{
float Clamp01( float v )
{
	return v < 0.0f ? 0.0f : ( v > 1.0f ? 1.0f : v );
}

/// Camera names shared by every instance. Enumerated once when the first
/// instance is created (which is also when Resolume reads the option list),
/// and again when someone presses Restart Tracker after plugging a camera in.
struct CameraCatalog
{
	std::mutex mutex;
	std::vector< std::string > names;
	int generation = 0;
};

CameraCatalog& Cameras()
{
	static CameraCatalog* catalog = new CameraCatalog();
	return *catalog;
}

std::vector< std::string > WithFallback( std::vector< std::string > names )
{
	if( !names.empty() )
		return names;
#if defined( _WIN32 )
	return { "No Camera Found" };
#else
	// No enumeration here: offer indices, which is what OpenCV takes anyway.
	return { "Camera 0", "Camera 1", "Camera 2", "Camera 3" };
#endif
}

std::vector< std::string > CameraNames( int& generation )
{
	CameraCatalog& catalog = Cameras();
	std::lock_guard< std::mutex > lock( catalog.mutex );
	if( catalog.generation == 0 )
	{
		catalog.names      = WithFallback( mpp::EnumerateCameras() );
		catalog.generation = 1;
	}
	generation = catalog.generation;
	return catalog.names;
}

void RefreshCameras()
{
	std::vector< std::string > names = WithFallback( mpp::EnumerateCameras() );
	CameraCatalog& catalog           = Cameras();
	std::lock_guard< std::mutex > lock( catalog.mutex );
	if( names != catalog.names )
	{
		catalog.names = names;
		++catalog.generation;
	}
}

int CameraGeneration()
{
	CameraCatalog& catalog = Cameras();
	std::lock_guard< std::mutex > lock( catalog.mutex );
	return catalog.generation;
}
}// namespace

// ------------------------------------------------------------ declaration

float MediaPipeParticles::Mapped( unsigned int index ) const
{
	const Range& r = ranges[ index ];
	return r.min + ( r.max - r.min ) * Clamp01( raw[ index ] );
}

float MediaPipeParticles::Normalised( unsigned int index, float value ) const
{
	const Range& r = ranges[ index ];
	if( r.max <= r.min )
		return 0.0f;
	return Clamp01( ( value - r.min ) / ( r.max - r.min ) );
}

void MediaPipeParticles::AddSlider( unsigned int index, const char* name, const char* group,
									float minValue, float maxValue, float defaultValue )
{
	ranges[ index ].min = minValue;
	ranges[ index ].max = maxValue;
	raw[ index ]        = Normalised( index, defaultValue );
	SetParamInfo( index, name, FF_TYPE_STANDARD, raw[ index ] );
	SetParamGroup( index, group );
}

void MediaPipeParticles::AddColor( unsigned int index, const char* name, const char* group,
								   unsigned int type, float defaultValue )
{
	raw[ index ] = Clamp01( defaultValue );
	SetParamInfo( index, name, type, raw[ index ] );
	SetParamGroup( index, group );
}

MediaPipeParticles::MediaPipeParticles()
{
	// A source: Resolume gives us no input textures.
	SetMinInputs( 0 );
	SetMaxInputs( 0 );

	const char* emission = "Emission";
	const char* forces   = "Forces";
	const char* look     = "Look";
	const char* tracking = "Tracking";

	AddSlider( PARAM_COUNT_, "Particles", emission, 64.0f, 512.0f, 256.0f );
	AddSlider( PARAM_LIFE, "Life", emission, 0.2f, 8.0f, 2.0f );
	AddSlider( PARAM_LIFE_VAR, "Life Random", emission, 0.0f, 1.0f, 0.5f );
	AddSlider( PARAM_SPREAD, "Emit Spread", emission, 0.0f, 2.0f, 0.35f );
	AddSlider( PARAM_INHERIT, "Inherit Motion", emission, 0.0f, 2.0f, 1.0f );

	SetOptionParamInfo( PARAM_EMIT_MODE, "Emit From", 4, 0.0f );
	SetParamElementInfo( PARAM_EMIT_MODE, 0, "Whole Body", 0.0f );
	SetParamElementInfo( PARAM_EMIT_MODE, 1, "Limbs", 1.0f );
	SetParamElementInfo( PARAM_EMIT_MODE, 2, "Torso", 2.0f );
	SetParamElementInfo( PARAM_EMIT_MODE, 3, "Joints", 3.0f );
	SetParamGroup( PARAM_EMIT_MODE, emission );

	SetParamInfo( PARAM_RESET, "Reset", FF_TYPE_EVENT, false );
	SetParamGroup( PARAM_RESET, emission );

	AddSlider( PARAM_GRAVITY, "Gravity", forces, -2.0f, 2.0f, 0.0f );
	AddSlider( PARAM_TURBULENCE, "Turbulence", forces, 0.0f, 3.0f, 0.5f );
	AddSlider( PARAM_TURB_SCALE, "Turbulence Scale", forces, 0.2f, 12.0f, 3.0f );
	AddSlider( PARAM_DRAG, "Drag", forces, 0.0f, 6.0f, 1.2f );
	AddSlider( PARAM_ATTRACT, "Body Attract", forces, -4.0f, 4.0f, 0.0f );

	AddSlider( PARAM_SIZE, "Size", look, 0.5f, 24.0f, 3.0f );
	AddSlider( PARAM_SIZE_VAR, "Size Random", look, 0.0f, 1.0f, 0.5f );
	AddSlider( PARAM_DEPTH, "Depth", look, 0.0f, 4.0f, 1.0f );
	AddSlider( PARAM_TRAILS, "Trails", look, 0.0f, 1.0f, 0.0f );
	AddSlider( PARAM_BRIGHTNESS, "Brightness", look, 0.0f, 4.0f, 1.0f );
	AddSlider( PARAM_OPACITY, "Opacity", look, 0.0f, 1.0f, 1.0f );

	AddColor( PARAM_COLOR_A_R, "Color A Red", look, FF_TYPE_RED, 1.0f );
	AddColor( PARAM_COLOR_A_G, "Color A Green", look, FF_TYPE_GREEN, 1.0f );
	AddColor( PARAM_COLOR_A_B, "Color A Blue", look, FF_TYPE_BLUE, 1.0f );
	AddColor( PARAM_COLOR_B_R, "Color B Red", look, FF_TYPE_RED, 0.1f );
	AddColor( PARAM_COLOR_B_G, "Color B Green", look, FF_TYPE_GREEN, 0.4f );
	AddColor( PARAM_COLOR_B_B, "Color B Blue", look, FF_TYPE_BLUE, 1.0f );

	SetOptionParamInfo( PARAM_COLOR_MODE, "Color By", 2, 0.0f );
	SetParamElementInfo( PARAM_COLOR_MODE, 0, "Age", 0.0f );
	SetParamElementInfo( PARAM_COLOR_MODE, 1, "Speed", 1.0f );
	SetParamGroup( PARAM_COLOR_MODE, look );

	AddSlider( PARAM_SMOOTHING, "Smoothing", tracking, 0.0f, 1.0f, 0.5f );

	raw[ PARAM_MIRROR ] = 1.0f;
	SetParamInfo( PARAM_MIRROR, "Mirror", FF_TYPE_BOOLEAN, true );
	SetParamGroup( PARAM_MIRROR, tracking );

	AddSlider( PARAM_ZOOM, "Zoom", tracking, 0.2f, 3.0f, 1.0f );
	AddSlider( PARAM_POS_X, "Position X", tracking, -1.0f, 1.0f, 0.0f );
	AddSlider( PARAM_POS_Y, "Position Y", tracking, -1.0f, 1.0f, 0.0f );

	SetParamInfo( PARAM_OSC_PORT, "OSC Port", FF_TYPE_TEXT, oscPortText.c_str() );
	SetParamGroup( PARAM_OSC_PORT, tracking );

	const char* camera = "Camera";

	raw[ PARAM_TRACKER ] = 1.0f;
	SetParamInfo( PARAM_TRACKER, "Tracker", FF_TYPE_BOOLEAN, true );
	SetParamGroup( PARAM_TRACKER, camera );

	std::vector< std::string > cameraNames = CameraNames( cameraListGeneration );
	const int defaultCamera                = mpp::PreferredCameraIndex( cameraNames );
	raw[ PARAM_CAMERA ]                    = float( defaultCamera );
	SetOptionParamInfo( PARAM_CAMERA, "Camera", unsigned( cameraNames.size() ), float( defaultCamera ) );
	for( size_t i = 0; i < cameraNames.size(); ++i )
		SetParamElementInfo( PARAM_CAMERA, unsigned( i ), cameraNames[ i ].c_str(), float( i ) );
	SetParamGroup( PARAM_CAMERA, camera );

	raw[ PARAM_PEOPLE ] = 1.0f;
	SetOptionParamInfo( PARAM_PEOPLE, "People", 3, 1.0f );
	SetParamElementInfo( PARAM_PEOPLE, 0, "1", 1.0f );
	SetParamElementInfo( PARAM_PEOPLE, 1, "2", 2.0f );
	SetParamElementInfo( PARAM_PEOPLE, 2, "3", 3.0f );
	SetParamGroup( PARAM_PEOPLE, camera );

	SetParamInfo( PARAM_PREVIEW, "Preview Window", FF_TYPE_BOOLEAN, false );
	SetParamGroup( PARAM_PREVIEW, camera );

	SetParamInfo( PARAM_TRACKER_RESTART, "Restart Tracker", FF_TYPE_EVENT, false );
	SetParamGroup( PARAM_TRACKER_RESTART, camera );
}

void MediaPipeParticles::SetCameraElements( const std::vector< std::string >& names, bool raiseEvent )
{
	std::vector< float > values;
	for( size_t i = 0; i < names.size(); ++i )
		values.push_back( float( i ) );
	SetParamElements( PARAM_CAMERA, names, values, raiseEvent );
}

mpp::TrackerSettings MediaPipeParticles::CurrentTrackerSettings() const
{
	mpp::TrackerSettings s;
	s.enabled = raw[ PARAM_TRACKER ] > 0.5f;
	s.camera  = std::max( 0, int( raw[ PARAM_CAMERA ] + 0.5f ) );
	s.people  = std::min( 3, std::max( 1, int( raw[ PARAM_PEOPLE ] + 0.5f ) ) );
	s.preview = raw[ PARAM_PREVIEW ] > 0.5f;
	return s;
}

void MediaPipeParticles::UpdateTrackerLauncher( float dt )
{
	if( !launcher || launcherPort != requestedPort )
	{
		launcher     = mpp::TrackerLauncher::Acquire( requestedPort );
		launcherPort = requestedPort;
		// The first instance on a port decides; one added later (a new clip
		// with default values) must not yank a running tracker to camera 0.
		ownsInitialSettings = !launcher->HasSettings();
		if( ownsInitialSettings )
			launcher->Apply( CurrentTrackerSettings() );
		stateShown = false;
	}

	if( trackerSettingsDirty )
	{
		// Before the first frame the host is still restoring saved values:
		// those count only for the instance that started the tracker.
		if( processedAFrame || ownsInitialSettings )
			launcher->Apply( CurrentTrackerSettings() );
		trackerSettingsDirty = false;
	}

	if( raw[ PARAM_TRACKER_RESTART ] > 0.5f )
	{
		raw[ PARAM_TRACKER_RESTART ] = 0.0f;
		RefreshCameras();
		launcher->Restart();
	}

	if( CameraGeneration() != cameraListGeneration )
		SetCameraElements( CameraNames( cameraListGeneration ), true );

	sinceStatePoll += dt;
	if( !stateShown || sinceStatePoll >= 0.25f )
	{
		sinceStatePoll                 = 0.0f;
		const mpp::TrackerState state = launcher->State();
		if( !stateShown || state != shownState )
		{
			SetParamDisplayName( PARAM_TRACKER, std::string( "Tracker: " ) + mpp::TrackerStateLabel( state ), true );
			shownState = state;
			stateShown = true;
		}
	}
}

// ------------------------------------------------------------------- GL

FFResult MediaPipeParticles::InitGL( const FFGLViewportStruct* viewport )
{
	if( viewport != nullptr )
	{
		initWidth  = int( viewport->width );
		initHeight = int( viewport->height );
	}

	if( !particles.Init() )
		return FF_FAIL;

	lastTexSize = int( Mapped( PARAM_COUNT_ ) );
	particles.SetTextureSize( lastTexSize );
	lastTexSize = particles.TextureSize();

	portDirty         = true;
	sinceBindAttempt  = 0.0f;
	haveLastFrameTime = false;
	elapsed           = 0.0f;
	tracker.Reset();

	// Placing the source is what starts the camera tracker, not playing it.
	UpdateTrackerLauncher( 0.0f );
	return FF_SUCCESS;
}

FFResult MediaPipeParticles::DeInitGL()
{
	// The last instance on the port letting go stops the tracker.
	launcher.reset();
	receiver.Stop();
	particles.DeInit();
	return FF_SUCCESS;
}

void MediaPipeParticles::ApplyPortFromText()
{
	long parsed = std::strtol( oscPortText.c_str(), nullptr, 10 );
	if( parsed < 1024 || parsed > 65535 )
		parsed = mpp::DEFAULT_PORT;
	requestedPort = uint16_t( parsed );
	portDirty     = true;
}

void MediaPipeParticles::PushParamsToTracker()
{
	tracker.SetSmoothing( Clamp01( raw[ PARAM_SMOOTHING ] ) );
	tracker.SetMirror( raw[ PARAM_MIRROR ] > 0.5f );
	tracker.SetTransform( Mapped( PARAM_ZOOM ),
						  Mapped( PARAM_POS_X ),
						  Mapped( PARAM_POS_Y ) );
	tracker.SetEmitMode( mpp::EmitMode( int( raw[ PARAM_EMIT_MODE ] + 0.5f ) ) );
}

mpp::ParticleParams MediaPipeParticles::BuildParticleParams() const
{
	mpp::ParticleParams p;
	p.lifeSeconds  = Mapped( PARAM_LIFE );
	p.lifeVariance = Clamp01( raw[ PARAM_LIFE_VAR ] );
	p.spread       = Mapped( PARAM_SPREAD );
	p.inherit      = Mapped( PARAM_INHERIT );
	p.gravity      = Mapped( PARAM_GRAVITY );
	p.turbulence   = Mapped( PARAM_TURBULENCE );
	p.turbScale    = Mapped( PARAM_TURB_SCALE );
	p.drag         = Mapped( PARAM_DRAG );
	p.attract      = Mapped( PARAM_ATTRACT );
	p.pointSize    = Mapped( PARAM_SIZE );
	p.sizeVariance = Clamp01( raw[ PARAM_SIZE_VAR ] );
	p.depth        = Mapped( PARAM_DEPTH );
	p.trails       = Clamp01( raw[ PARAM_TRAILS ] );
	p.brightness   = Mapped( PARAM_BRIGHTNESS );
	p.opacity      = Clamp01( raw[ PARAM_OPACITY ] );
	p.colorA[ 0 ]  = Clamp01( raw[ PARAM_COLOR_A_R ] );
	p.colorA[ 1 ]  = Clamp01( raw[ PARAM_COLOR_A_G ] );
	p.colorA[ 2 ]  = Clamp01( raw[ PARAM_COLOR_A_B ] );
	p.colorB[ 0 ]  = Clamp01( raw[ PARAM_COLOR_B_R ] );
	p.colorB[ 1 ]  = Clamp01( raw[ PARAM_COLOR_B_G ] );
	p.colorB[ 2 ]  = Clamp01( raw[ PARAM_COLOR_B_B ] );
	p.colorMode    = int( raw[ PARAM_COLOR_MODE ] + 0.5f );
	p.emitMode     = int( raw[ PARAM_EMIT_MODE ] + 0.5f );
	// Roughly one screen height per second reads as "full speed".
	p.speedScale   = 0.5f;
	return p;
}

FFResult MediaPipeParticles::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL == nullptr )
		return FF_FAIL;

	auto now = std::chrono::steady_clock::now();
	float dt = 1.0f / 60.0f;
	if( haveLastFrameTime )
		dt = std::chrono::duration< float >( now - lastFrameTime ).count();
	lastFrameTime     = now;
	haveLastFrameTime = true;
	dt                = std::max( 1.0f / 1000.0f, std::min( dt, 0.1f ) );
	elapsed += dt;

	if( portDirty )
	{
		receiver.Start( requestedPort );
		portDirty        = false;
		sinceBindAttempt = 0.0f;
	}
	else if( !receiver.IsListening() )
	{
		// The port can be busy for a while: another process (a second Resolume,
		// some other OSC tool) holds it, or the OS has not released a socket
		// that was just closed. Instances in this process share the port. Retrying beats going deaf for the rest of
		// the show, which is what a single failed bind used to mean.
		sinceBindAttempt += dt;
		if( sinceBindAttempt >= 2.0f )
		{
			receiver.Start( requestedPort );
			sinceBindAttempt = 0.0f;
		}
	}

	mpp::PoseUpdate update;
	const bool gotFrames = receiver.PollLatest( update );

	PushParamsToTracker();
	tracker.Update( gotFrames ? &update : nullptr, dt );

	UpdateTrackerLauncher( dt );
	processedAFrame = true;

	int wantedSize = int( Mapped( PARAM_COUNT_ ) + 0.5f );
	if( wantedSize != lastTexSize )
	{
		particles.SetTextureSize( wantedSize );
		lastTexSize = particles.TextureSize();
	}

	if( raw[ PARAM_RESET ] > 0.5f )
	{
		particles.RequestReset();
		raw[ PARAM_RESET ] = 0.0f;
	}

	GLint viewport[ 4 ] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, viewport );
	int width  = viewport[ 2 ] > 0 ? viewport[ 2 ] : initWidth;
	int height = viewport[ 3 ] > 0 ? viewport[ 3 ] : initHeight;

	particles.DrawFrame( tracker, BuildParticleParams(), dt, elapsed, width, height, pGL->HostFBO );
	return FF_SUCCESS;
}

// ------------------------------------------------------------ parameters

FFResult MediaPipeParticles::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PARAM_LAST )
		return FF_FAIL;
	const bool trackerParam = index == PARAM_TRACKER || index == PARAM_CAMERA ||
							  index == PARAM_PEOPLE || index == PARAM_PREVIEW;
	if( trackerParam && raw[ index ] != value )
		trackerSettingsDirty = true;
	raw[ index ] = value;
	return FF_SUCCESS;
}

float MediaPipeParticles::GetFloatParameter( unsigned int index )
{
	if( index >= PARAM_LAST )
		return 0.0f;
	return raw[ index ];
}

FFResult MediaPipeParticles::SetTextParameter( unsigned int index, const char* value )
{
	if( index != PARAM_OSC_PORT )
		return FF_FAIL;
	oscPortText = value != nullptr ? value : "";
	ApplyPortFromText();
	return FF_SUCCESS;
}

char* MediaPipeParticles::GetTextParameter( unsigned int index )
{
	if( index != PARAM_OSC_PORT )
		return nullptr;
	return const_cast< char* >( oscPortText.c_str() );
}
