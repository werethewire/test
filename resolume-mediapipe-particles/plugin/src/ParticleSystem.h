#pragma once
//
// GPU particle system driven by a PoseTracker.
//
// State lives in two RGBA32F textures that are ping-ponged through a
// simulation shader, so nothing is read back to the CPU and the particle count
// is limited by texture size rather than by draw-call overhead.
//
//   position texture : xy = position (NDC), z = remaining life 0..1, w = seed
//   velocity texture : xy = velocity, z = lifespan seconds, w = size random
//
#include "GLInclude.h"
#include "PoseTracker.h"

namespace mpp
{
struct ParticleParams
{
	float lifeSeconds  = 2.0f;
	float lifeVariance = 0.5f;///< 0..1, fraction of lifespan randomised per particle
	float spread       = 0.35f;///< random birth velocity
	float inherit      = 1.0f; ///< how much of the joint's velocity a particle takes
	float gravity      = 0.0f;
	float turbulence   = 0.5f;
	float turbScale    = 3.0f;
	float drag         = 1.2f;
	float attract      = 0.0f;///< positive pulls to the body, negative pushes away
	float pointSize    = 3.0f;///< pixels at 1080p, scaled with the real height
	float sizeVariance = 0.5f;
	float trails       = 0.0f;///< 0 = none, ->1 = long decay
	float brightness   = 1.0f;
	float opacity      = 1.0f;
	float colorA[ 3 ]  = { 1.0f, 1.0f, 1.0f };
	float colorB[ 3 ]  = { 0.1f, 0.4f, 1.0f };
	int colorMode      = 0;///< 0 = by age, 1 = by speed
	float speedScale   = 1.0f;
	int emitMode       = EMIT_WHOLE_BODY;///< must match the tracker's mode
};

class ParticleSystem
{
public:
	bool Init();
	void DeInit();

	/// Reallocates the simulation textures. `size` is clamped to [64, 512],
	/// i.e. 4096 to 262144 particles. Existing particles are discarded.
	bool SetTextureSize( int size );
	int TextureSize() const { return texSize; }
	int ParticleCount() const { return texSize * texSize; }

	/// Restarts every particle on the next simulation step.
	void RequestReset() { needsSeed = true; }

	/// Steps the simulation and composites the result into `hostFbo`.
	/// Leaves `hostFbo` bound with the viewport restored.
	void DrawFrame( const PoseTracker& pose,
					const ParticleParams& params,
					float dt,
					float time,
					int viewportWidth,
					int viewportHeight,
					GLuint hostFbo );

private:
	bool BuildShaders();
	bool AllocSimTargets();
	bool AllocAccumTarget( int width, int height );
	void ReleaseSimTargets();
	void ReleaseAccumTarget();
	void SeedParticles();
	void DrawFullscreenQuad();

	// simulation
	GLuint simProgram = 0;
	GLuint posTex[ 2 ] = { 0, 0 };
	GLuint velTex[ 2 ] = { 0, 0 };
	GLuint simFbo[ 2 ] = { 0, 0 };
	int writeIndex     = 0;
	int texSize        = 256;
	int pendingSize    = 256;
	bool needsSeed     = true;

	// rendering
	GLuint pointProgram   = 0;
	GLuint compositeProgram = 0;
	GLuint fadeProgram    = 0;
	GLuint quadVao        = 0;
	GLuint quadVbo        = 0;
	GLuint pointVao       = 0;
	GLuint accumTex       = 0;
	GLuint accumFbo       = 0;
	int accumWidth        = 0;
	int accumHeight       = 0;
	bool accumDirty       = true;

	bool initialised = false;
};

}// namespace mpp
