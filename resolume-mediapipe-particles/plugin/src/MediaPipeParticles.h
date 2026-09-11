#pragma once
//
// FFGL source plugin: draws a particle system driven by MediaPipe pose data
// that arrives over OSC/UDP from tracker/pose_osc.py.
//
#include "GLInclude.h"
#include "OscPose.h"
#include "ParticleSystem.h"
#include "PoseTracker.h"

#include <chrono>
#include <string>

class MediaPipeParticles : public CFFGLPlugin
{
public:
	MediaPipeParticles();
	~MediaPipeParticles() override = default;

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;

	enum ParamIndex : unsigned int
	{
		PARAM_COUNT_          = 0, ///< number of particles
		PARAM_LIFE            = 1,
		PARAM_LIFE_VAR        = 2,
		PARAM_SPREAD          = 3,
		PARAM_INHERIT         = 4,
		PARAM_GRAVITY         = 5,
		PARAM_TURBULENCE      = 6,
		PARAM_TURB_SCALE      = 7,
		PARAM_DRAG            = 8,
		PARAM_ATTRACT         = 9,
		PARAM_SIZE            = 10,
		PARAM_SIZE_VAR        = 11,
		PARAM_DEPTH           = 12,
		PARAM_TRAILS          = 13,
		PARAM_BRIGHTNESS      = 14,
		PARAM_OPACITY         = 15,
		PARAM_COLOR_A_R       = 16,
		PARAM_COLOR_A_G       = 17,
		PARAM_COLOR_A_B       = 18,
		PARAM_COLOR_B_R       = 19,
		PARAM_COLOR_B_G       = 20,
		PARAM_COLOR_B_B       = 21,
		PARAM_COLOR_MODE      = 22,
		PARAM_EMIT_MODE       = 23,
		PARAM_SMOOTHING       = 24,
		PARAM_MIRROR          = 25,
		PARAM_ZOOM            = 26,
		PARAM_POS_X           = 27,
		PARAM_POS_Y           = 28,
		PARAM_RESET           = 29,
		PARAM_OSC_PORT        = 30,
		PARAM_LAST
	};

private:
	void ApplyPortFromText();
	void PushParamsToTracker();
	mpp::ParticleParams BuildParticleParams() const;

	mpp::PoseReceiver receiver;
	mpp::PoseTracker tracker;
	mpp::ParticleSystem particles;

	// Raw 0..1 slider values, kept so GetFloatParameter round-trips exactly.
	float raw[ PARAM_LAST ] = { 0.0f };

	std::string oscPortText = "9010";
	uint16_t requestedPort  = mpp::DEFAULT_PORT;
	bool portDirty          = true;

	std::chrono::steady_clock::time_point lastFrameTime;
	bool haveLastFrameTime = false;
	float elapsed          = 0.0f;
	int lastTexSize        = 0;
	int initWidth          = 1920;
	int initHeight         = 1080;
};
