#include "MediaPipeParticles.h"

#include <algorithm>
#include <cstdlib>

static CFFGLPluginInfo PluginInfo(
	PluginFactory< MediaPipeParticles >,// create method
	"MPPT",                             // plugin unique ID (4 chars)
	"MediaPipe Particles",              // plugin name
	2,                                  // API major version
	1,                                  // API minor version
	1,                                  // plugin major version
	0,                                  // plugin minor version
	FF_SOURCE,                          // plugin type
	"Camera pose driven particle system. Feed it with tracker/pose_osc.py.",
	"MediaPipe x Resolume particle bridge" );

namespace
{
struct Range
{
	float min;
	float max;
};

/// Real-world range behind each 0..1 slider. Parameters are left as plain
/// FF_TYPE_STANDARD rather than declaring a host range, because every FFGL 2
/// host agrees on the meaning of 0..1 and not all of them honour ranges.
const Range kRanges[ MediaPipeParticles::PARAM_LAST ] = {
	/* COUNT       */ { 64.0f, 512.0f },
	/* LIFE        */ { 0.2f, 8.0f },
	/* LIFE_VAR    */ { 0.0f, 1.0f },
	/* SPREAD      */ { 0.0f, 2.0f },
	/* INHERIT     */ { 0.0f, 2.0f },
	/* GRAVITY     */ { -2.0f, 2.0f },
	/* TURBULENCE  */ { 0.0f, 3.0f },
	/* TURB_SCALE  */ { 0.2f, 12.0f },
	/* DRAG        */ { 0.0f, 6.0f },
	/* ATTRACT     */ { -4.0f, 4.0f },
	/* SIZE        */ { 0.5f, 24.0f },
	/* SIZE_VAR    */ { 0.0f, 1.0f },
	/* DEPTH       */ { 0.0f, 4.0f },
	/* TRAILS      */ { 0.0f, 1.0f },
	/* BRIGHTNESS  */ { 0.0f, 4.0f },
	/* OPACITY     */ { 0.0f, 1.0f },
	/* COLOR_A_R   */ { 0.0f, 1.0f },
	/* COLOR_A_G   */ { 0.0f, 1.0f },
	/* COLOR_A_B   */ { 0.0f, 1.0f },
	/* COLOR_B_R   */ { 0.0f, 1.0f },
	/* COLOR_B_G   */ { 0.0f, 1.0f },
	/* COLOR_B_B   */ { 0.0f, 1.0f },
	/* COLOR_MODE  */ { 0.0f, 1.0f },
	/* EMIT_MODE   */ { 0.0f, 3.0f },
	/* SMOOTHING   */ { 0.0f, 1.0f },
	/* MIRROR      */ { 0.0f, 1.0f },
	/* ZOOM        */ { 0.2f, 3.0f },
	/* POS_X       */ { -1.0f, 1.0f },
	/* POS_Y       */ { -1.0f, 1.0f },
	/* RESET       */ { 0.0f, 1.0f },
	/* OSC_PORT    */ { 0.0f, 1.0f },
};

float Clamp01( float v )
{
	return v < 0.0f ? 0.0f : ( v > 1.0f ? 1.0f : v );
}

/// 0..1 slider -> the value the simulation actually wants.
float Mapped( const float* raw, unsigned int index )
{
	const Range& r = kRanges[ index ];
	return r.min + ( r.max - r.min ) * Clamp01( raw[ index ] );
}

/// Inverse of Mapped(), used to turn a default into a slider position.
float Normalised( unsigned int index, float value )
{
	const Range& r = kRanges[ index ];
	if( r.max <= r.min )
		return 0.0f;
	return Clamp01( ( value - r.min ) / ( r.max - r.min ) );
}
}// namespace

MediaPipeParticles::MediaPipeParticles()
{
	// A source: Resolume gives us no input textures.
	SetMinInputs( 0 );
	SetMaxInputs( 0 );

	raw[ PARAM_COUNT_ ]     = Normalised( PARAM_COUNT_, 256.0f );
	raw[ PARAM_LIFE ]       = Normalised( PARAM_LIFE, 2.0f );
	raw[ PARAM_LIFE_VAR ]   = 0.5f;
	raw[ PARAM_SPREAD ]     = Normalised( PARAM_SPREAD, 0.35f );
	raw[ PARAM_INHERIT ]    = Normalised( PARAM_INHERIT, 1.0f );
	raw[ PARAM_GRAVITY ]    = Normalised( PARAM_GRAVITY, 0.0f );
	raw[ PARAM_TURBULENCE ] = Normalised( PARAM_TURBULENCE, 0.5f );
	raw[ PARAM_TURB_SCALE ] = Normalised( PARAM_TURB_SCALE, 3.0f );
	raw[ PARAM_DRAG ]       = Normalised( PARAM_DRAG, 1.2f );
	raw[ PARAM_ATTRACT ]    = Normalised( PARAM_ATTRACT, 0.0f );
	raw[ PARAM_SIZE ]       = Normalised( PARAM_SIZE, 3.0f );
	raw[ PARAM_SIZE_VAR ]   = 0.5f;
	raw[ PARAM_DEPTH ]      = Normalised( PARAM_DEPTH, 1.0f );
	raw[ PARAM_TRAILS ]     = 0.0f;
	raw[ PARAM_BRIGHTNESS ] = Normalised( PARAM_BRIGHTNESS, 1.0f );
	raw[ PARAM_OPACITY ]    = 1.0f;
	raw[ PARAM_COLOR_A_R ]  = 1.0f;
	raw[ PARAM_COLOR_A_G ]  = 1.0f;
	raw[ PARAM_COLOR_A_B ]  = 1.0f;
	raw[ PARAM_COLOR_B_R ]  = 0.1f;
	raw[ PARAM_COLOR_B_G ]  = 0.4f;
	raw[ PARAM_COLOR_B_B ]  = 1.0f;
	raw[ PARAM_COLOR_MODE ] = 0.0f;
	raw[ PARAM_EMIT_MODE ]  = 0.0f;
	raw[ PARAM_SMOOTHING ]  = 0.5f;
	raw[ PARAM_MIRROR ]     = 1.0f;
	raw[ PARAM_ZOOM ]       = Normalised( PARAM_ZOOM, 1.0f );
	raw[ PARAM_POS_X ]      = Normalised( PARAM_POS_X, 0.0f );
	raw[ PARAM_POS_Y ]      = Normalised( PARAM_POS_Y, 0.0f );
	raw[ PARAM_RESET ]      = 0.0f;

	SetParamInfo( PARAM_COUNT_, "Particles", FF_TYPE_STANDARD, raw[ PARAM_COUNT_ ] );
	SetParamInfo( PARAM_LIFE, "Life", FF_TYPE_STANDARD, raw[ PARAM_LIFE ] );
	SetParamInfo( PARAM_LIFE_VAR, "Life Random", FF_TYPE_STANDARD, raw[ PARAM_LIFE_VAR ] );
	SetParamInfo( PARAM_SPREAD, "Emit Spread", FF_TYPE_STANDARD, raw[ PARAM_SPREAD ] );
	SetParamInfo( PARAM_INHERIT, "Inherit Motion", FF_TYPE_STANDARD, raw[ PARAM_INHERIT ] );
	SetParamInfo( PARAM_GRAVITY, "Gravity", FF_TYPE_STANDARD, raw[ PARAM_GRAVITY ] );
	SetParamInfo( PARAM_TURBULENCE, "Turbulence", FF_TYPE_STANDARD, raw[ PARAM_TURBULENCE ] );
	SetParamInfo( PARAM_TURB_SCALE, "Turbulence Scale", FF_TYPE_STANDARD, raw[ PARAM_TURB_SCALE ] );
	SetParamInfo( PARAM_DRAG, "Drag", FF_TYPE_STANDARD, raw[ PARAM_DRAG ] );
	SetParamInfo( PARAM_ATTRACT, "Body Attract", FF_TYPE_STANDARD, raw[ PARAM_ATTRACT ] );
	SetParamInfo( PARAM_SIZE, "Size", FF_TYPE_STANDARD, raw[ PARAM_SIZE ] );
	SetParamInfo( PARAM_SIZE_VAR, "Size Random", FF_TYPE_STANDARD, raw[ PARAM_SIZE_VAR ] );
	SetParamInfo( PARAM_DEPTH, "Depth", FF_TYPE_STANDARD, raw[ PARAM_DEPTH ] );
	SetParamInfo( PARAM_TRAILS, "Trails", FF_TYPE_STANDARD, raw[ PARAM_TRAILS ] );
	SetParamInfo( PARAM_BRIGHTNESS, "Brightness", FF_TYPE_STANDARD, raw[ PARAM_BRIGHTNESS ] );
	SetParamInfo( PARAM_OPACITY, "Opacity", FF_TYPE_STANDARD, raw[ PARAM_OPACITY ] );

	SetParamInfo( PARAM_COLOR_A_R, "Color A Red", FF_TYPE_RED, raw[ PARAM_COLOR_A_R ] );
	SetParamInfo( PARAM_COLOR_A_G, "Color A Green", FF_TYPE_GREEN, raw[ PARAM_COLOR_A_G ] );
	SetParamInfo( PARAM_COLOR_A_B, "Color A Blue", FF_TYPE_BLUE, raw[ PARAM_COLOR_A_B ] );
	SetParamInfo( PARAM_COLOR_B_R, "Color B Red", FF_TYPE_RED, raw[ PARAM_COLOR_B_R ] );
	SetParamInfo( PARAM_COLOR_B_G, "Color B Green", FF_TYPE_GREEN, raw[ PARAM_COLOR_B_G ] );
	SetParamInfo( PARAM_COLOR_B_B, "Color B Blue", FF_TYPE_BLUE, raw[ PARAM_COLOR_B_B ] );

	SetOptionParamInfo( PARAM_COLOR_MODE, "Color By", 2, 0.0f );
	SetParamElementInfo( PARAM_COLOR_MODE, 0, "Age", 0.0f );
	SetParamElementInfo( PARAM_COLOR_MODE, 1, "Speed", 1.0f );

	SetOptionParamInfo( PARAM_EMIT_MODE, "Emit From", 4, 0.0f );
	SetParamElementInfo( PARAM_EMIT_MODE, 0, "Whole Body", 0.0f );
	SetParamElementInfo( PARAM_EMIT_MODE, 1, "Limbs", 1.0f );
	SetParamElementInfo( PARAM_EMIT_MODE, 2, "Torso", 2.0f );
	SetParamElementInfo( PARAM_EMIT_MODE, 3, "Joints", 3.0f );

	SetParamInfo( PARAM_SMOOTHING, "Smoothing", FF_TYPE_STANDARD, raw[ PARAM_SMOOTHING ] );
	SetParamInfo( PARAM_MIRROR, "Mirror", FF_TYPE_BOOLEAN, true );
	SetParamInfo( PARAM_ZOOM, "Zoom", FF_TYPE_STANDARD, raw[ PARAM_ZOOM ] );
	SetParamInfo( PARAM_POS_X, "Position X", FF_TYPE_STANDARD, raw[ PARAM_POS_X ] );
	SetParamInfo( PARAM_POS_Y, "Position Y", FF_TYPE_STANDARD, raw[ PARAM_POS_Y ] );
	SetParamInfo( PARAM_RESET, "Reset", FF_TYPE_EVENT, false );
	SetParamInfo( PARAM_OSC_PORT, "OSC Port", FF_TYPE_TEXT, oscPortText.c_str() );
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

	lastTexSize = int( Mapped( raw, PARAM_COUNT_ ) );
	particles.SetTextureSize( lastTexSize );
	lastTexSize = particles.TextureSize();

	portDirty         = true;
	haveLastFrameTime = false;
	elapsed           = 0.0f;
	tracker.Reset();
	return FF_SUCCESS;
}

FFResult MediaPipeParticles::DeInitGL()
{
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
	tracker.SetTransform( Mapped( raw, PARAM_ZOOM ),
						  Mapped( raw, PARAM_POS_X ),
						  Mapped( raw, PARAM_POS_Y ) );
	tracker.SetEmitMode( mpp::EmitMode( int( raw[ PARAM_EMIT_MODE ] + 0.5f ) ) );
}

mpp::ParticleParams MediaPipeParticles::BuildParticleParams() const
{
	mpp::ParticleParams p;
	p.lifeSeconds  = Mapped( raw, PARAM_LIFE );
	p.lifeVariance = Clamp01( raw[ PARAM_LIFE_VAR ] );
	p.spread       = Mapped( raw, PARAM_SPREAD );
	p.inherit      = Mapped( raw, PARAM_INHERIT );
	p.gravity      = Mapped( raw, PARAM_GRAVITY );
	p.turbulence   = Mapped( raw, PARAM_TURBULENCE );
	p.turbScale    = Mapped( raw, PARAM_TURB_SCALE );
	p.drag         = Mapped( raw, PARAM_DRAG );
	p.attract      = Mapped( raw, PARAM_ATTRACT );
	p.pointSize    = Mapped( raw, PARAM_SIZE );
	p.sizeVariance = Clamp01( raw[ PARAM_SIZE_VAR ] );
	p.depth        = Mapped( raw, PARAM_DEPTH );
	p.trails       = Clamp01( raw[ PARAM_TRAILS ] );
	p.brightness   = Mapped( raw, PARAM_BRIGHTNESS );
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
		portDirty = false;
	}

	mpp::PoseUpdate update;
	const bool gotFrames = receiver.PollLatest( update );

	PushParamsToTracker();
	tracker.Update( gotFrames ? &update : nullptr, dt );

	int wantedSize = int( Mapped( raw, PARAM_COUNT_ ) + 0.5f );
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
