#pragma once
//
// FFGL source plugin: draws a particle system driven by MediaPipe pose data
// that arrives over OSC/UDP from tracker/pose_osc.py.
//
#include "GLInclude.h"
#include "OscPose.h"
#include "ParticleSystem.h"
#include "PoseTracker.h"
#include "TrackerLauncher.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

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

	/// Resolume lays parameters out in index order and starts a new section
	/// when the group name changes, so each group's indices stay contiguous.
	enum ParamIndex : unsigned int
	{
		// Emission
		PARAM_COUNT_          = 0, ///< number of particles
		PARAM_LIFE            = 1,
		PARAM_LIFE_VAR        = 2,
		PARAM_SPREAD          = 3,
		PARAM_INHERIT         = 4,
		PARAM_EMIT_MODE       = 5,
		PARAM_RESET           = 6,
		// Forces
		PARAM_GRAVITY         = 7,
		PARAM_TURBULENCE      = 8,
		PARAM_TURB_SCALE      = 9,
		PARAM_DRAG            = 10,
		PARAM_ATTRACT         = 11,
		// Look
		PARAM_SIZE            = 12,
		PARAM_SIZE_VAR        = 13,
		PARAM_DEPTH           = 14,
		PARAM_TRAILS          = 15,
		PARAM_BRIGHTNESS      = 16,
		PARAM_OPACITY         = 17,
		PARAM_COLOR_A_R       = 18,
		PARAM_COLOR_A_G       = 19,
		PARAM_COLOR_A_B       = 20,
		PARAM_COLOR_B_R       = 21,
		PARAM_COLOR_B_G       = 22,
		PARAM_COLOR_B_B       = 23,
		PARAM_COLOR_MODE      = 24,
		// Tracking
		PARAM_SMOOTHING       = 25,
		PARAM_MIRROR          = 26,
		PARAM_ZOOM            = 27,
		PARAM_POS_X           = 28,
		PARAM_POS_Y           = 29,
		PARAM_OSC_PORT        = 30,
		// Camera -- appended, so saved compositions keep their indices
		PARAM_TRACKER         = 31,///< launch pose_osc.py from the plugin
		PARAM_CAMERA          = 32,
		PARAM_PEOPLE          = 33,
		PARAM_PREVIEW         = 34,
		PARAM_TRACKER_RESTART = 35,
		PARAM_LAST
	};

private:
	/// Real-world range behind a 0..1 slider. Parameters are declared as plain
	/// FF_TYPE_STANDARD rather than with a host range, because every FFGL 2
	/// host agrees on the meaning of 0..1 and not all of them honour ranges.
	struct Range
	{
		float min = 0.0f;
		float max = 1.0f;
	};

	/// Declares a slider's name, group, range and default in one place, so
	/// they cannot drift apart when parameters are reordered.
	void AddSlider( unsigned int index, const char* name, const char* group,
					float minValue, float maxValue, float defaultValue );
	/// Same, for one channel of a colour picker (already 0..1).
	void AddColor( unsigned int index, const char* name, const char* group,
				   unsigned int type, float defaultValue );

	float Mapped( unsigned int index ) const;
	float Normalised( unsigned int index, float value ) const;

	void ApplyPortFromText();
	void PushParamsToTracker();
	mpp::ParticleParams BuildParticleParams() const;

	mpp::TrackerSettings CurrentTrackerSettings() const;
	/// Launch/steer the shared tracker and reflect its state in the UI.
	void UpdateTrackerLauncher( float dt );
	void SetCameraElements( const std::vector< std::string >& names, bool raiseEvent );

	mpp::PoseReceiver receiver;
	std::shared_ptr< mpp::TrackerLauncher > launcher;
	uint16_t launcherPort = 0;
	/// Set when the user changes a Camera parameter; pushed on the next frame.
	bool trackerSettingsDirty = false;
	/// Frames before the first one belong to the host restoring values.
	bool processedAFrame      = false;
	/// This instance applied the tracker's first settings on its port.
	bool ownsInitialSettings  = false;
	mpp::TrackerState shownState = mpp::TrackerState::Starting;
	bool stateShown              = false;
	float sinceStatePoll         = 0.0f;
	int cameraListGeneration     = 0;
	mpp::PoseTracker tracker;
	mpp::ParticleSystem particles;

	Range ranges[ PARAM_LAST ];
	// Raw slider values as the host sent them, kept so GetFloatParameter
	// round-trips exactly.
	float raw[ PARAM_LAST ] = { 0.0f };

	std::string oscPortText  = "9010";
	uint16_t requestedPort   = mpp::DEFAULT_PORT;
	bool portDirty           = true;
	float sinceBindAttempt   = 0.0f;

	std::chrono::steady_clock::time_point lastFrameTime;
	bool haveLastFrameTime = false;
	float elapsed          = 0.0f;
	int lastTexSize        = 0;
	int initWidth          = 1920;
	int initHeight         = 1080;
};
