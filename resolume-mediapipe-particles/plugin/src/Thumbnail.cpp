#include "Thumbnail.h"

#include <cmath>

namespace mpp
{
namespace
{
/// Pose the thumbnail figure strikes, in normalised image space with the
/// origin at the top left. Arms are raised a little: a plain T-pose reads as a
/// diagram, this reads as someone dancing.
struct PosedJoint
{
	int landmark;
	float x;
	float y;
};

const PosedJoint kPose[] = {
	{ LM_NOSE, 0.50f, 0.14f },
	{ LM_LEFT_EAR, 0.455f, 0.155f },
	{ LM_RIGHT_EAR, 0.545f, 0.155f },
	{ LM_LEFT_SHOULDER, 0.415f, 0.29f },
	{ LM_RIGHT_SHOULDER, 0.585f, 0.29f },
	{ LM_LEFT_ELBOW, 0.275f, 0.235f },
	{ LM_RIGHT_ELBOW, 0.725f, 0.325f },
	{ LM_LEFT_WRIST, 0.165f, 0.125f },
	{ LM_RIGHT_WRIST, 0.845f, 0.425f },
	{ LM_LEFT_HIP, 0.445f, 0.565f },
	{ LM_RIGHT_HIP, 0.555f, 0.565f },
	{ LM_LEFT_KNEE, 0.415f, 0.745f },
	{ LM_RIGHT_KNEE, 0.585f, 0.735f },
	{ LM_LEFT_ANKLE, 0.395f, 0.915f },
	{ LM_RIGHT_ANKLE, 0.615f, 0.905f },
	{ LM_LEFT_FOOT, 0.355f, 0.945f },
	{ LM_RIGHT_FOOT, 0.655f, 0.935f },
};

float Hash( unsigned int seed )
{
	// Integer avalanche, then take the mantissa. Deterministic across
	// platforms, which matters because the icon should not differ per machine.
	seed ^= seed >> 16;
	seed *= 0x7feb352du;
	seed ^= seed >> 15;
	seed *= 0x846ca68bu;
	seed ^= seed >> 16;
	return float( seed & 0x00ffffffu ) / float( 0x01000000u );
}

unsigned char ToByte( float linear )
{
	// Filmic-ish knee so overlapping particles bloom instead of clipping flat.
	float mapped = 1.0f - std::exp( -linear );
	mapped       = std::pow( mapped, 1.0f / 2.2f );
	int value    = int( mapped * 255.0f + 0.5f );
	return (unsigned char)( value < 0 ? 0 : ( value > 255 ? 255 : value ) );
}
}// namespace

std::vector< CFFGLColor > GenerateThumbnail( unsigned int width, unsigned int height )
{
	std::vector< CFFGLColor > pixels( size_t( width ) * height );
	if( width == 0 || height == 0 )
		return pixels;

	// Scatter the posed joints into a landmark-indexed table so the shared
	// bone list can be walked directly.
	float jointX[ NUM_LANDMARKS ] = { 0.0f };
	float jointY[ NUM_LANDMARKS ] = { 0.0f };
	bool posed[ NUM_LANDMARKS ]   = { false };
	for( const PosedJoint& joint : kPose )
	{
		jointX[ joint.landmark ] = joint.x;
		jointY[ joint.landmark ] = joint.y;
		posed[ joint.landmark ]  = true;
	}

	// Length weighted emission, exactly as the plugin does at runtime.
	float cdf[ NUM_BONES ];
	float total = 0.0f;
	for( int i = 0; i < NUM_BONES; ++i )
	{
		const Bone& bone = BONES[ i ];
		float length     = 0.0f;
		// The icon shows the Whole Body look, which leaves the face and hand
		// detail bones out.
		const bool detail = bone.group == GROUP_FACE || bone.group == GROUP_HANDS;
		if( posed[ bone.a ] && posed[ bone.b ] && !detail )
		{
			float dx = jointX[ bone.b ] - jointX[ bone.a ];
			float dy = jointY[ bone.b ] - jointY[ bone.a ];
			length   = std::sqrt( dx * dx + dy * dy );
			if( bone.group == GROUP_HEAD || bone.group == GROUP_NECK )
				length *= 1.6f;
		}
		total += length;
		cdf[ i ] = total;
	}
	if( total <= 0.0f )
		return pixels;
	for( int i = 0; i < NUM_BONES; ++i )
		cdf[ i ] /= total;

	std::vector< float > accum( size_t( width ) * height * 3, 0.0f );

	// The plugin's default ramp: young particles white, old ones blue.
	const float colorA[ 3 ] = { 1.0f, 1.0f, 1.0f };
	const float colorB[ 3 ] = { 0.1f, 0.4f, 1.0f };

	// Sparse enough that individual dots still read at 160x120; any denser and
	// the bones fuse into a glowing wireframe instead of a particle system.
	const int particleCount = 2600;
	const float radius      = 0.95f * float( height ) / 120.0f;
	const float aspect      = float( width ) / float( height );

	for( int i = 0; i < particleCount; ++i )
	{
		const unsigned int base = (unsigned int)i * 9u;
		float pick              = Hash( base + 1u );
		int boneIndex           = NUM_BONES - 1;
		for( int b = 0; b < NUM_BONES; ++b )
		{
			if( pick <= cdf[ b ] )
			{
				boneIndex = b;
				break;
			}
		}

		const Bone& bone = BONES[ boneIndex ];
		float t          = Hash( base + 2u );
		float x          = jointX[ bone.a ] + ( jointX[ bone.b ] - jointX[ bone.a ] ) * t;
		float y          = jointY[ bone.a ] + ( jointY[ bone.b ] - jointY[ bone.a ] ) * t;

		// Drift the particle away from its birth point by its age, so the
		// figure dissolves at the edges the way it does on screen.
		float age   = Hash( base + 3u );
		float angle = Hash( base + 4u ) * 6.2831853f;
		float reach = 0.10f * age * age * ( 0.35f + Hash( base + 5u ) );
		x += std::cos( angle ) * reach / aspect;
		y += std::sin( angle ) * reach - 0.02f * age;

		float px = x * float( width );
		float py = y * float( height );

		// Kept well under 1 so the tone curve has headroom and the blue end of
		// the ramp survives instead of clipping to white.
		float fade = 0.5f * ( 1.0f - age ) * ( 0.35f + 0.65f * Hash( base + 6u ) );
		float rgb[ 3 ];
		for( int c = 0; c < 3; ++c )
			rgb[ c ] = ( colorA[ c ] + ( colorB[ c ] - colorA[ c ] ) * age ) * fade;

		// Splat a small gaussian.
		int minX = int( std::floor( px - radius * 2.0f ) );
		int maxX = int( std::ceil( px + radius * 2.0f ) );
		int minY = int( std::floor( py - radius * 2.0f ) );
		int maxY = int( std::ceil( py + radius * 2.0f ) );
		for( int sy = minY; sy <= maxY; ++sy )
		{
			if( sy < 0 || sy >= int( height ) )
				continue;
			for( int sx = minX; sx <= maxX; ++sx )
			{
				if( sx < 0 || sx >= int( width ) )
					continue;
				float dx     = ( float( sx ) + 0.5f ) - px;
				float dy     = ( float( sy ) + 0.5f ) - py;
				float weight = std::exp( -( dx * dx + dy * dy ) / ( 2.0f * radius * radius ) );
				if( weight < 0.004f )
					continue;
				float* target = &accum[ ( size_t( sy ) * width + sx ) * 3 ];
				for( int c = 0; c < 3; ++c )
					target[ c ] += rgb[ c ] * weight;
			}
		}
	}

	for( size_t i = 0; i < pixels.size(); ++i )
	{
		const float* source = &accum[ i * 3 ];
		pixels[ i ]         = CFFGLColor( ToByte( source[ 0 ] ),
										  ToByte( source[ 1 ] ),
										  ToByte( source[ 2 ] ),
										  255 );
	}
	return pixels;
}

}// namespace mpp
