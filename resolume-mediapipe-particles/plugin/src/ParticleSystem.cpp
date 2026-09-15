#include "ParticleSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace mpp
{
namespace
{
const int kMinTexSize = 64;
const int kMaxTexSize = 512;

// ------------------------------------------------------------------ shaders

const char* kQuadVertex = R"GLSL(
#version 410 core
layout( location = 0 ) in vec2 aPos;
out vec2 vUv;
void main()
{
	vUv         = aPos * 0.5 + 0.5;
	gl_Position = vec4( aPos, 0.0, 1.0 );
}
)GLSL";

// Common helpers shared by the simulation shader.
const char* kNoiseLib = R"GLSL(
float hash31( vec3 p )
{
	p  = fract( p * 0.1031 );
	p += dot( p, p.zyx + 31.32 );
	return fract( ( p.x + p.y ) * p.z );
}

float hash21( vec2 p )
{
	vec3 q = fract( vec3( p.xyx ) * 0.1031 );
	q += dot( q, q.yzx + 33.33 );
	return fract( ( q.x + q.y ) * q.z );
}

float vnoise( vec2 p )
{
	vec2 i = floor( p );
	vec2 f = fract( p );
	vec2 u = f * f * ( 3.0 - 2.0 * f );
	float a = hash21( i );
	float b = hash21( i + vec2( 1.0, 0.0 ) );
	float c = hash21( i + vec2( 0.0, 1.0 ) );
	float d = hash21( i + vec2( 1.0, 1.0 ) );
	return mix( mix( a, b, u.x ), mix( c, d, u.x ), u.y );
}

float fbm( vec2 p )
{
	return vnoise( p ) * 0.6 + vnoise( p * 2.03 ) * 0.3 + vnoise( p * 4.01 ) * 0.1;
}

// Divergence free flow: particles swirl instead of piling up in sinks.
vec2 curlNoise( vec2 p )
{
	const float e = 0.035;
	float n1 = fbm( p + vec2( 0.0, e ) );
	float n2 = fbm( p - vec2( 0.0, e ) );
	float n3 = fbm( p + vec2( e, 0.0 ) );
	float n4 = fbm( p - vec2( e, 0.0 ) );
	return vec2( n1 - n2, -( n3 - n4 ) ) / ( 2.0 * e );
}

vec2 closestPointOnSegment( vec2 p, vec2 a, vec2 b )
{
	vec2 ab = b - a;
	float d = max( dot( ab, ab ), 1e-8 );
	float t = clamp( dot( p - a, ab ) / d, 0.0, 1.0 );
	return a + ab * t;
}
)GLSL";

const char* kSimFragmentBody = R"GLSL(
in vec2 vUv;
layout( location = 0 ) out vec4 outPos;
layout( location = 1 ) out vec4 outVel;

uniform sampler2D uPosTex;
uniform sampler2D uVelTex;

uniform float uDt;
uniform float uTime;
uniform float uRandomSeed;
uniform float uAspect;

uniform float uLife;
uniform float uLifeVar;
uniform float uSpread;
uniform float uEmitRadius;
uniform float uInherit;
uniform float uGravity;
uniform float uTurbulence;
uniform float uTurbScale;
uniform float uDrag;
uniform float uAttract;
uniform float uSizeVar;

uniform float uPresence;
uniform int uEmitMode;
uniform int uHasEmitters;
// Joint arrays hold every body back to back: body p owns the LANDMARK_COUNT
// entries starting at p * LANDMARK_COUNT.
uniform vec4 uJoint[ JOINT_COUNT ];
uniform float uJointVis[ JOINT_COUNT ];
uniform float uJointZ[ JOINT_COUNT ];
uniform float uBodyPresence[ PERSON_COUNT ];
// One cumulative table across all bodies, so the particle budget is split by
// bone length rather than per person.
uniform float uBoneCdf[ CDF_COUNT ];

const ivec2 kBone[ BONE_COUNT ] = BONE_TABLE;

float rnd( float k )
{
	return hash31( vec3( vUv * 917.0, uRandomSeed + k ) );
}

vec2 sampleSkeleton( float r, float t, out vec2 jointVel, out float depth )
{
	int slot = CDF_COUNT - 1;
	for( int i = 0; i < CDF_COUNT; ++i )
	{
		if( r <= uBoneCdf[ i ] )
		{
			slot = i;
			break;
		}
	}
	int person = slot / BONE_COUNT;
	ivec2 b    = kBone[ slot - person * BONE_COUNT ];
	int ia     = person * LANDMARK_COUNT + b.x;
	int ib     = person * LANDMARK_COUNT + b.y;

	// "Joints" mode snaps to whichever end of the bone is nearer.
	if( uEmitMode == 3 )
		t = t < 0.5 ? 0.0 : 1.0;

	jointVel = mix( uJoint[ ia ].zw, uJoint[ ib ].zw, t );
	depth    = mix( uJointZ[ ia ], uJointZ[ ib ], t );
	return mix( uJoint[ ia ].xy, uJoint[ ib ].xy, t );
}

// Pull toward (or push away from) the closest point on the skeleton.
vec2 bodyForce( vec2 p )
{
	vec2 best   = p;
	float bestD = 1e9;
	for( int person = 0; person < PERSON_COUNT; ++person )
	{
		// Skip bodies that are not on screen; this is the hot loop.
		if( uBodyPresence[ person ] < 0.01 )
			continue;
		int base = person * LANDMARK_COUNT;
		for( int i = 0; i < BONE_COUNT; ++i )
		{
			ivec2 b = kBone[ i ];
			int ia  = base + b.x;
			int ib  = base + b.y;
			if( min( uJointVis[ ia ], uJointVis[ ib ] ) < 0.35 )
				continue;
			vec2 c  = closestPointOnSegment( p, uJoint[ ia ].xy, uJoint[ ib ].xy );
			float d = distance( p, c );
			if( d < bestD )
			{
				bestD = d;
				best  = c;
			}
		}
	}
	if( bestD > 1.5 || bestD < 1e-5 )
		return vec2( 0.0 );
	// Falls off with distance so far away particles are not yanked around.
	return normalize( best - p ) / ( 1.0 + 6.0 * bestD * bestD );
}

void main()
{
	vec4 P = texture( uPosTex, vUv );
	vec4 V = texture( uVelTex, vUv );

	vec2 pos      = P.xy;
	float life    = P.z;
	float depth   = P.w;
	vec2 vel      = V.xy;
	float lifespan = max( V.z, 0.001 );
	float sizeRand = V.w;

	life -= uDt / lifespan;

	if( life <= 0.0 )
	{
		// Births are spread over time instead of happening all at once, so a
		// person walking into frame materialises rather than pops.
		float chance = clamp( uDt * 6.0, 0.0, 1.0 ) * uPresence;
		if( uHasEmitters == 0 || uPresence <= 0.001 || rnd( 11.7 ) > chance )
		{
			// Park it well outside the viewport until it is allowed to spawn.
			outPos = vec4( 10.0, 10.0, 0.0, depth );
			outVel = vec4( 0.0, 0.0, lifespan, sizeRand );
			return;
		}

		vec2 jointVel;
		float spawnDepth;
		vec2 spawn = sampleSkeleton( rnd( 1.3 ), rnd( 2.7 ), jointVel, spawnDepth );

		float ang  = rnd( 3.1 ) * 6.2831853;
		vec2 dir   = vec2( cos( ang ), sin( ang ) );
		float mag  = rnd( 4.9 );

		// Depth is fixed at birth: a particle belongs to the part of the body
		// it came off, and re-sampling it each frame would make it swim.
		outPos = vec4( spawn + dir * uEmitRadius * mag, 1.0, spawnDepth );
		outVel = vec4( jointVel * uInherit + dir * uSpread * mag,
					   uLife * mix( 1.0 - uLifeVar, 1.0 + uLifeVar, rnd( 6.2 ) ),
					   rnd( 7.4 ) );
		return;
	}

	// Keep the noise cells square regardless of the output aspect ratio.
	vec2 noiseP = vec2( pos.x * uAspect, pos.y ) * uTurbScale + vec2( uTime * 0.13, uTime * -0.09 );
	vel += curlNoise( noiseP ) * uTurbulence * uDt;
	vel.y -= uGravity * uDt;
	if( abs( uAttract ) > 0.0001 )
		vel += bodyForce( pos ) * uAttract * uDt;
	vel *= exp( -uDrag * uDt );
	pos += vel * uDt;

	outPos = vec4( pos, life, depth );
	outVel = vec4( vel, lifespan, sizeRand );
}
)GLSL";

const char* kPointVertex = R"GLSL(
#version 410 core
uniform sampler2D uPosTex;
uniform sampler2D uVelTex;
uniform int uTexSize;
uniform float uSize;
uniform float uSizeVar;
uniform vec3 uColorA;
uniform vec3 uColorB;
uniform int uColorMode;
uniform float uSpeedScale;
uniform float uDepth;

out vec4 vColor;

void main()
{
	ivec2 texel = ivec2( gl_VertexID % uTexSize, gl_VertexID / uTexSize );
	vec4 P      = texelFetch( uPosTex, texel, 0 );
	vec4 V      = texelFetch( uVelTex, texel, 0 );

	float life = P.z;
	if( life <= 0.0 )
	{
		// Dead particles are pushed outside the clip volume.
		gl_Position  = vec4( 0.0, 0.0, 2.0, 1.0 );
		gl_PointSize = 0.0;
		vColor       = vec4( 0.0 );
		return;
	}

	// Quick fade in at birth, long fade out toward death.
	float age = 1.0 - life;
	float env = smoothstep( 0.0, 0.12, age ) * smoothstep( 0.0, 0.55, life );

	// MediaPipe's z is negative toward the camera, so a near particle gets a
	// perspective factor above 1. uDepth == 0 disables this entirely.
	float persp = clamp( 1.0 - P.w * uDepth, 0.25, 4.0 );

	float sizeRand = mix( 1.0 - uSizeVar, 1.0 + uSizeVar, V.w );
	gl_Position    = vec4( P.xy, 0.0, 1.0 );
	gl_PointSize   = max( uSize * sizeRand * ( 0.35 + 0.65 * env ) * persp, 1.0 );

	float t = uColorMode == 1 ? clamp( length( V.xy ) * uSpeedScale, 0.0, 1.0 ) : age;
	// Half the depth cue goes into intensity so near particles read as closer
	// rather than merely fatter.
	vColor  = vec4( mix( uColorA, uColorB, t ), env * mix( 1.0, persp, 0.5 ) );
}
)GLSL";

const char* kPointFragment = R"GLSL(
#version 410 core
in vec4 vColor;
out vec4 fragColor;
uniform float uBrightness;

void main()
{
	vec2 d   = gl_PointCoord * 2.0 - 1.0;
	float r2 = dot( d, d );
	if( r2 > 1.0 )
		discard;
	float a = pow( 1.0 - r2, 1.5 ) * vColor.a;
	// Premultiplied: the accumulation buffer is blended additively.
	fragColor = vec4( vColor.rgb * uBrightness * a, a );
}
)GLSL";

const char* kFadeFragment = R"GLSL(
#version 410 core
in vec2 vUv;
out vec4 fragColor;
uniform float uDecay;
void main()
{
	fragColor = vec4( uDecay );
}
)GLSL";

const char* kCompositeFragment = R"GLSL(
#version 410 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uSource;
uniform float uOpacity;
void main()
{
	fragColor = texture( uSource, vUv ) * uOpacity;
}
)GLSL";

// ------------------------------------------------------------------ helpers

std::string BoneTableGlsl()
{
	std::string table = "ivec2[ " + std::to_string( NUM_BONES ) + " ](";
	for( int i = 0; i < NUM_BONES; ++i )
	{
		table += "ivec2( " + std::to_string( BONES[ i ].a ) + ", " + std::to_string( BONES[ i ].b ) + " )";
		if( i + 1 < NUM_BONES )
			table += ", ";
	}
	table += ")";
	return table;
}

void ReplaceAll( std::string& text, const std::string& from, const std::string& to )
{
	size_t at = 0;
	while( ( at = text.find( from, at ) ) != std::string::npos )
	{
		text.replace( at, from.size(), to );
		at += to.size();
	}
}

GLuint CompileStage( GLenum type, const std::string& source )
{
	GLuint shader     = glCreateShader( type );
	const char* cStr  = source.c_str();
	glShaderSource( shader, 1, &cStr, nullptr );
	glCompileShader( shader );

	GLint ok = GL_FALSE;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
	if( ok == GL_FALSE )
	{
		char log[ 2048 ] = { 0 };
		glGetShaderInfoLog( shader, sizeof( log ) - 1, nullptr, log );
		std::fprintf( stderr, "[MediaPipeParticles] shader compile failed:\n%s\n", log );
		glDeleteShader( shader );
		return 0;
	}
	return shader;
}

GLuint LinkProgram( const std::string& vertexSource, const std::string& fragmentSource )
{
	GLuint vs = CompileStage( GL_VERTEX_SHADER, vertexSource );
	if( vs == 0 )
		return 0;
	GLuint fs = CompileStage( GL_FRAGMENT_SHADER, fragmentSource );
	if( fs == 0 )
	{
		glDeleteShader( vs );
		return 0;
	}

	GLuint program = glCreateProgram();
	glAttachShader( program, vs );
	glAttachShader( program, fs );
	glLinkProgram( program );
	glDetachShader( program, vs );
	glDetachShader( program, fs );
	glDeleteShader( vs );
	glDeleteShader( fs );

	GLint ok = GL_FALSE;
	glGetProgramiv( program, GL_LINK_STATUS, &ok );
	if( ok == GL_FALSE )
	{
		char log[ 2048 ] = { 0 };
		glGetProgramInfoLog( program, sizeof( log ) - 1, nullptr, log );
		std::fprintf( stderr, "[MediaPipeParticles] program link failed:\n%s\n", log );
		glDeleteProgram( program );
		return 0;
	}
	return program;
}

void SetUniform1i( GLuint program, const char* name, int value )
{
	glUniform1i( glGetUniformLocation( program, name ), value );
}

void SetUniform1f( GLuint program, const char* name, float value )
{
	glUniform1f( glGetUniformLocation( program, name ), value );
}

void SetUniform3f( GLuint program, const char* name, const float* rgb )
{
	glUniform3f( glGetUniformLocation( program, name ), rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] );
}

void SetEnabled( GLenum capability, GLboolean enabled )
{
	if( enabled )
		glEnable( capability );
	else
		glDisable( capability );
}

/// Saves the host GL state this plugin touches, forces what our own passes
/// need, and puts the host's back afterwards.
///
/// A plugin shares Resolume's context. A scissor box, face culling, a stencil
/// test or a colour write mask left enabled by the host would silently clip or
/// blank our output -- the classic "renders in the test harness, black in the
/// host" failure -- and anything we flip and forget would do the same to the
/// host's own drawing.
struct HostGlState
{
	GLboolean blend            = GL_FALSE;
	GLboolean depthTest        = GL_FALSE;
	GLboolean scissorTest      = GL_FALSE;
	GLboolean cullFace         = GL_FALSE;
	GLboolean stencilTest      = GL_FALSE;
	GLboolean programPointSize = GL_FALSE;
	GLint blendSrcRgb          = GL_ONE;
	GLint blendDstRgb          = GL_ZERO;
	GLint blendSrcAlpha        = GL_ONE;
	GLint blendDstAlpha        = GL_ZERO;
	GLboolean colorMask[ 4 ]   = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };

	void Save()
	{
		blend            = glIsEnabled( GL_BLEND );
		depthTest        = glIsEnabled( GL_DEPTH_TEST );
		scissorTest      = glIsEnabled( GL_SCISSOR_TEST );
		cullFace         = glIsEnabled( GL_CULL_FACE );
		stencilTest      = glIsEnabled( GL_STENCIL_TEST );
		programPointSize = glIsEnabled( GL_PROGRAM_POINT_SIZE );
		glGetIntegerv( GL_BLEND_SRC_RGB, &blendSrcRgb );
		glGetIntegerv( GL_BLEND_DST_RGB, &blendDstRgb );
		glGetIntegerv( GL_BLEND_SRC_ALPHA, &blendSrcAlpha );
		glGetIntegerv( GL_BLEND_DST_ALPHA, &blendDstAlpha );
		glGetBooleanv( GL_COLOR_WRITEMASK, colorMask );
	}

	/// Everything our passes assume. Blending is not here: each pass sets its
	/// own mode.
	static void ApplyOurs()
	{
		glDisable( GL_DEPTH_TEST );
		glDisable( GL_SCISSOR_TEST );
		glDisable( GL_CULL_FACE );
		glDisable( GL_STENCIL_TEST );
		glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	}

	void Restore() const
	{
		glBlendFuncSeparate( blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha );
		glColorMask( colorMask[ 0 ], colorMask[ 1 ], colorMask[ 2 ], colorMask[ 3 ] );
		SetEnabled( GL_BLEND, blend );
		SetEnabled( GL_DEPTH_TEST, depthTest );
		SetEnabled( GL_SCISSOR_TEST, scissorTest );
		SetEnabled( GL_CULL_FACE, cullFace );
		SetEnabled( GL_STENCIL_TEST, stencilTest );
		SetEnabled( GL_PROGRAM_POINT_SIZE, programPointSize );
	}
};
}// namespace

// --------------------------------------------------------------- lifecycle

bool ParticleSystem::Init()
{
	if( initialised )
		return true;

	if( !BuildShaders() )
		return false;

	// Fullscreen triangle strip used by every screen space pass.
	static const float quad[] = {
		-1.0f, -1.0f,
		1.0f, -1.0f,
		-1.0f, 1.0f,
		1.0f, 1.0f,
	};
	glGenVertexArrays( 1, &quadVao );
	glGenBuffers( 1, &quadVbo );
	glBindVertexArray( quadVao );
	glBindBuffer( GL_ARRAY_BUFFER, quadVbo );
	glBufferData( GL_ARRAY_BUFFER, sizeof( quad ), quad, GL_STATIC_DRAW );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof( float ), nullptr );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindVertexArray( 0 );

	// Points pull everything from textures via gl_VertexID, so this VAO
	// intentionally has no attributes -- core profile still requires one.
	glGenVertexArrays( 1, &pointVao );

	if( !AllocSimTargets( false ) )
		return false;

	initialised = true;
	needsSeed   = true;
	return true;
}

bool ParticleSystem::BuildShaders()
{
	std::string simFragment = std::string( "#version 410 core\n" ) + kNoiseLib + kSimFragmentBody;
	ReplaceAll( simFragment, "BONE_TABLE", BoneTableGlsl() );
	ReplaceAll( simFragment, "JOINT_COUNT", std::to_string( NUM_LANDMARKS * MAX_PERSONS ) );
	ReplaceAll( simFragment, "CDF_COUNT", std::to_string( NUM_BONES * MAX_PERSONS ) );
	ReplaceAll( simFragment, "LANDMARK_COUNT", std::to_string( NUM_LANDMARKS ) );
	ReplaceAll( simFragment, "PERSON_COUNT", std::to_string( MAX_PERSONS ) );
	ReplaceAll( simFragment, "BONE_COUNT", std::to_string( NUM_BONES ) );

	simProgram = LinkProgram( kQuadVertex, simFragment );
	if( simProgram == 0 )
		return false;

	pointProgram = LinkProgram( kPointVertex, kPointFragment );
	if( pointProgram == 0 )
		return false;

	fadeProgram = LinkProgram( kQuadVertex, kFadeFragment );
	if( fadeProgram == 0 )
		return false;

	compositeProgram = LinkProgram( kQuadVertex, kCompositeFragment );
	return compositeProgram != 0;
}

void ParticleSystem::ReleaseSimTargets()
{
	if( simFbo[ 0 ] != 0 )
		glDeleteFramebuffers( 2, simFbo );
	if( posTex[ 0 ] != 0 )
		glDeleteTextures( 2, posTex );
	if( velTex[ 0 ] != 0 )
		glDeleteTextures( 2, velTex );
	simFbo[ 0 ] = simFbo[ 1 ] = 0;
	posTex[ 0 ] = posTex[ 1 ] = 0;
	velTex[ 0 ] = velTex[ 1 ] = 0;
	allocatedSize = 0;
}

void ParticleSystem::ReleaseAccumTarget()
{
	if( accumFbo != 0 )
		glDeleteFramebuffers( 1, &accumFbo );
	if( accumTex != 0 )
		glDeleteTextures( 1, &accumTex );
	accumFbo    = 0;
	accumTex    = 0;
	accumWidth  = 0;
	accumHeight = 0;
}

void ParticleSystem::DeInit()
{
	ReleaseSimTargets();
	ReleaseAccumTarget();

	if( quadVbo != 0 )
		glDeleteBuffers( 1, &quadVbo );
	if( quadVao != 0 )
		glDeleteVertexArrays( 1, &quadVao );
	if( pointVao != 0 )
		glDeleteVertexArrays( 1, &pointVao );
	if( simProgram != 0 )
		glDeleteProgram( simProgram );
	if( pointProgram != 0 )
		glDeleteProgram( pointProgram );
	if( fadeProgram != 0 )
		glDeleteProgram( fadeProgram );
	if( compositeProgram != 0 )
		glDeleteProgram( compositeProgram );

	quadVbo = quadVao = pointVao = 0;
	simProgram = pointProgram = fadeProgram = compositeProgram = 0;
	initialised = false;
}

bool ParticleSystem::AllocSimTargets( bool preserve )
{
	// Hold on to the old targets until the new ones are ready, so the live
	// state can be copied across instead of every particle restarting.
	GLuint oldPos[ 2 ]  = { posTex[ 0 ], posTex[ 1 ] };
	GLuint oldVel[ 2 ]  = { velTex[ 0 ], velTex[ 1 ] };
	GLuint oldFbo[ 2 ]  = { simFbo[ 0 ], simFbo[ 1 ] };
	const int oldSize   = allocatedSize;
	const int oldWrite  = writeIndex;
	const bool canCopy  = preserve && oldSize > 0 && oldFbo[ 0 ] != 0;

	posTex[ 0 ] = posTex[ 1 ] = 0;
	velTex[ 0 ] = velTex[ 1 ] = 0;
	simFbo[ 0 ] = simFbo[ 1 ] = 0;

	glGenTextures( 2, posTex );
	glGenTextures( 2, velTex );
	glGenFramebuffers( 2, simFbo );

	auto dropOld = [ & ]() {
		if( oldFbo[ 0 ] != 0 )
			glDeleteFramebuffers( 2, oldFbo );
		if( oldPos[ 0 ] != 0 )
			glDeleteTextures( 2, oldPos );
		if( oldVel[ 0 ] != 0 )
			glDeleteTextures( 2, oldVel );
	};

	for( int i = 0; i < 2; ++i )
	{
		GLuint textures[ 2 ] = { posTex[ i ], velTex[ i ] };
		for( GLuint tex : textures )
		{
			glBindTexture( GL_TEXTURE_2D, tex );
			// Nearest everywhere: these are state buffers, never images.
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, texSize, texSize, 0, GL_RGBA, GL_FLOAT, nullptr );
		}

		glBindFramebuffer( GL_FRAMEBUFFER, simFbo[ i ] );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, posTex[ i ], 0 );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, velTex[ i ], 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
		{
			std::fprintf( stderr, "[MediaPipeParticles] simulation FBO incomplete\n" );
			glBindFramebuffer( GL_FRAMEBUFFER, 0 );
			glBindTexture( GL_TEXTURE_2D, 0 );
			dropOld();
			return false;
		}
	}

	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	writeIndex    = 0;
	allocatedSize = texSize;
	needsSeed     = true;

	if( canCopy )
	{
		// Everything starts dead, then the overlapping square is copied in;
		// texels outside it simply spawn fresh on the next step.
		SeedParticles();

		const GLint copy = GLint( std::min( oldSize, texSize ) );
		glBindFramebuffer( GL_READ_FRAMEBUFFER, oldFbo[ oldWrite ] );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, simFbo[ 0 ] );
		for( int attachment = 0; attachment < 2; ++attachment )
		{
			GLenum slot = GLenum( GL_COLOR_ATTACHMENT0 + attachment );
			glReadBuffer( slot );
			glDrawBuffers( 1, &slot );
			glBlitFramebuffer( 0, 0, copy, copy, 0, 0, copy, copy,
							   GL_COLOR_BUFFER_BIT, GL_NEAREST );
		}
		glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
		needsSeed = false;
	}

	dropOld();
	return true;
}

bool ParticleSystem::AllocAccumTarget( int width, int height )
{
	if( accumTex != 0 && accumWidth == width && accumHeight == height )
		return true;

	ReleaseAccumTarget();
	glGenTextures( 1, &accumTex );
	glBindTexture( GL_TEXTURE_2D, accumTex );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	// 16F keeps additive build-up from clipping the moment two particles overlap.
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr );

	glGenFramebuffers( 1, &accumFbo );
	glBindFramebuffer( GL_FRAMEBUFFER, accumFbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumTex, 0 );
	bool complete = glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glBindTexture( GL_TEXTURE_2D, 0 );

	if( !complete )
	{
		std::fprintf( stderr, "[MediaPipeParticles] accumulation FBO incomplete\n" );
		ReleaseAccumTarget();
		return false;
	}

	accumWidth  = width;
	accumHeight = height;
	accumDirty  = true;
	return true;
}

bool ParticleSystem::SetTextureSize( int size )
{
	size = std::max( kMinTexSize, std::min( kMaxTexSize, size ) );
	// Round to a multiple of 16 so the count moves in sensible steps as the
	// slider is dragged rather than reallocating on every pixel.
	size = ( size / 16 ) * 16;
	size = std::max( kMinTexSize, size );

	if( !initialised || size == allocatedSize )
	{
		texSize = size;
		return true;
	}

	texSize = size;
	return AllocSimTargets( true );
}

void ParticleSystem::SeedParticles()
{
	// Everything starts dead and parked off screen; the simulation's normal
	// spawn path then fills the frame over the next fraction of a second.
	const float posClear[ 4 ] = { 10.0f, 10.0f, 0.0f, 0.0f };
	const float velClear[ 4 ] = { 0.0f, 0.0f, 1.0f, 0.5f };

	for( int i = 0; i < 2; ++i )
	{
		glBindFramebuffer( GL_FRAMEBUFFER, simFbo[ i ] );
		GLenum buffers[ 2 ] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		glDrawBuffers( 2, buffers );
		glClearBufferfv( GL_COLOR, 0, posClear );
		glClearBufferfv( GL_COLOR, 1, velClear );
	}
	needsSeed = false;
}

void ParticleSystem::DrawFullscreenQuad()
{
	glBindVertexArray( quadVao );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	glBindVertexArray( 0 );
}

// ------------------------------------------------------------------- frame

void ParticleSystem::DrawFrame( const PoseTracker& pose,
								const ParticleParams& params,
								float dt,
								float time,
								int viewportWidth,
								int viewportHeight,
								GLuint hostFbo )
{
	if( !initialised || viewportWidth <= 0 || viewportHeight <= 0 )
		return;
	if( !AllocAccumTarget( viewportWidth, viewportHeight ) )
		return;

	// Long stalls (a loading project, a laptop waking up) must not teleport
	// every particle across the frame.
	dt = std::max( 0.0f, std::min( dt, 0.1f ) );

	HostGlState hostState;
	hostState.Save();
	hostState.ApplyOurs();

	if( needsSeed )
		SeedParticles();

	const int readIndex = writeIndex;
	writeIndex          = 1 - writeIndex;

	// -------- 1. simulate
	glBindFramebuffer( GL_FRAMEBUFFER, simFbo[ writeIndex ] );
	GLenum simBuffers[ 2 ] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers( 2, simBuffers );
	glViewport( 0, 0, texSize, texSize );
	glDisable( GL_BLEND );

	glUseProgram( simProgram );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, posTex[ readIndex ] );
	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, velTex[ readIndex ] );
	SetUniform1i( simProgram, "uPosTex", 0 );
	SetUniform1i( simProgram, "uVelTex", 1 );

	const float aspect = float( viewportWidth ) / float( std::max( viewportHeight, 1 ) );
	SetUniform1f( simProgram, "uDt", dt );
	SetUniform1f( simProgram, "uTime", time );
	SetUniform1f( simProgram, "uRandomSeed", std::fmod( time * 37.0f, 1024.0f ) );
	SetUniform1f( simProgram, "uAspect", aspect );
	SetUniform1f( simProgram, "uLife", params.lifeSeconds );
	SetUniform1f( simProgram, "uLifeVar", params.lifeVariance );
	SetUniform1f( simProgram, "uSpread", params.spread );
	SetUniform1f( simProgram, "uEmitRadius", 0.006f + params.spread * 0.05f );
	SetUniform1f( simProgram, "uInherit", params.inherit );
	SetUniform1f( simProgram, "uGravity", params.gravity );
	SetUniform1f( simProgram, "uTurbulence", params.turbulence );
	SetUniform1f( simProgram, "uTurbScale", params.turbScale );
	SetUniform1f( simProgram, "uDrag", params.drag );
	SetUniform1f( simProgram, "uAttract", params.attract );
	SetUniform1f( simProgram, "uSizeVar", params.sizeVariance );
	SetUniform1f( simProgram, "uPresence", pose.Presence() );
	SetUniform1i( simProgram, "uEmitMode", params.emitMode );
	SetUniform1i( simProgram, "uHasEmitters", pose.HasEmitters() ? 1 : 0 );

	// Joint uniforms, every body back to back: xy position, zw velocity.
	const int jointCount = NUM_LANDMARKS * MAX_PERSONS;
	float jointData[ NUM_LANDMARKS * MAX_PERSONS * 4 ];
	float visData[ NUM_LANDMARKS * MAX_PERSONS ];
	float depthData[ NUM_LANDMARKS * MAX_PERSONS ];
	float presenceData[ MAX_PERSONS ];
	for( int person = 0; person < MAX_PERSONS; ++person )
	{
		const JointState* joints = pose.Joints( person );
		presenceData[ person ]   = pose.Presence( person );
		for( int i = 0; i < NUM_LANDMARKS; ++i )
		{
			const int slot             = person * NUM_LANDMARKS + i;
			jointData[ slot * 4 + 0 ]  = joints[ i ].x;
			jointData[ slot * 4 + 1 ]  = joints[ i ].y;
			jointData[ slot * 4 + 2 ]  = joints[ i ].vx;
			jointData[ slot * 4 + 3 ]  = joints[ i ].vy;
			visData[ slot ]            = joints[ i ].vis;
			depthData[ slot ]          = joints[ i ].z;
		}
	}
	glUniform4fv( glGetUniformLocation( simProgram, "uJoint" ), jointCount, jointData );
	glUniform1fv( glGetUniformLocation( simProgram, "uJointVis" ), jointCount, visData );
	glUniform1fv( glGetUniformLocation( simProgram, "uJointZ" ), jointCount, depthData );
	glUniform1fv( glGetUniformLocation( simProgram, "uBodyPresence" ), MAX_PERSONS, presenceData );
	glUniform1fv( glGetUniformLocation( simProgram, "uBoneCdf" ),
				  PoseTracker::BoneCdfCount(), pose.BoneCdf() );

	DrawFullscreenQuad();

	// -------- 2. accumulate into the trail buffer
	glBindFramebuffer( GL_FRAMEBUFFER, accumFbo );
	GLenum accumBuffers[ 1 ] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers( 1, accumBuffers );
	glViewport( 0, 0, accumWidth, accumHeight );

	if( params.trails <= 0.001f || accumDirty )
	{
		// glClearBufferfv rather than glClear, so the host's clear colour is
		// left alone.
		const float black[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
		glDisable( GL_BLEND );
		glClearBufferfv( GL_COLOR, 0, black );
		accumDirty = false;
	}
	else
	{
		// Frame rate independent decay: reach 2% after `trailSeconds`.
		float trailSeconds = 0.05f + params.trails * 2.0f;
		float decay        = std::pow( 0.02f, dt / trailSeconds );
		glEnable( GL_BLEND );
		glBlendFunc( GL_ZERO, GL_SRC_COLOR );
		glUseProgram( fadeProgram );
		SetUniform1f( fadeProgram, "uDecay", decay );
		DrawFullscreenQuad();
	}

	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ONE );// premultiplied additive
	glEnable( GL_PROGRAM_POINT_SIZE );

	glUseProgram( pointProgram );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, posTex[ writeIndex ] );
	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, velTex[ writeIndex ] );
	SetUniform1i( pointProgram, "uPosTex", 0 );
	SetUniform1i( pointProgram, "uVelTex", 1 );
	SetUniform1i( pointProgram, "uTexSize", texSize );
	// Keep the apparent dot size constant across output resolutions.
	SetUniform1f( pointProgram, "uSize", params.pointSize * float( viewportHeight ) / 1080.0f );
	SetUniform1f( pointProgram, "uSizeVar", params.sizeVariance );
	SetUniform3f( pointProgram, "uColorA", params.colorA );
	SetUniform3f( pointProgram, "uColorB", params.colorB );
	SetUniform1i( pointProgram, "uColorMode", params.colorMode );
	SetUniform1f( pointProgram, "uSpeedScale", params.speedScale );
	SetUniform1f( pointProgram, "uDepth", params.depth );
	SetUniform1f( pointProgram, "uBrightness", params.brightness );

	glBindVertexArray( pointVao );
	glDrawArrays( GL_POINTS, 0, ParticleCount() );
	glBindVertexArray( 0 );

	// -------- 3. composite into whatever Resolume handed us
	glBindFramebuffer( GL_FRAMEBUFFER, hostFbo );
	glViewport( 0, 0, viewportWidth, viewportHeight );
	glDisable( GL_BLEND );
	glUseProgram( compositeProgram );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, accumTex );
	SetUniform1i( compositeProgram, "uSource", 0 );
	SetUniform1f( compositeProgram, "uOpacity", params.opacity );
	DrawFullscreenQuad();

	// Put back everything we touched.
	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glUseProgram( 0 );

	hostState.Restore();
}

}// namespace mpp
