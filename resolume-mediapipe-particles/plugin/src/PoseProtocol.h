#pragma once
//
// Wire format shared between the Python tracker and the FFGL plugin.
//
// The tracker sends one OSC message per detected body per camera frame:
//
//   /mp/pose  ,ii ffff...    frameId, personId, then NUM_LANDMARKS * 4 floats
//                            (x, y, z, visibility) per landmark
//
//   /mp/clear ,i             frameId            -- nobody in frame at all
//   /mp/clear ,ii            frameId, personId  -- that body specifically left
//
// personId is optional for backwards compatibility: a message with a single
// int argument is treated as person 0, which is what a single person tracker
// sends.
//
// Coordinates are normalised: x/y in [0,1] with the origin at the top-left of
// the camera image (MediaPipe convention). z is roughly in the same scale as x
// with the hip midpoint at 0 and smaller values closer to the camera.
// visibility is [0,1].
//
#include <cstdint>

namespace mpp
{
static const int NUM_LANDMARKS      = 33;
static const int FLOATS_PER_LANDMARK = 4;
static const int POSE_FLOAT_COUNT   = NUM_LANDMARKS * FLOATS_PER_LANDMARK;

static const uint16_t DEFAULT_PORT = 9010;

// How many simultaneous bodies the pipeline carries. Every extra body costs
// 216 shader uniform components, and GL 4.1 only guarantees 1024 of them, so
// this cannot grow much without switching the joint data to a texture.
static const int MAX_PERSONS = 3;

// MediaPipe Pose landmark indices we care about.
enum Landmark : int
{
	LM_NOSE          = 0,
	LM_LEFT_EAR      = 7,
	LM_RIGHT_EAR     = 8,
	LM_LEFT_SHOULDER = 11,
	LM_RIGHT_SHOULDER= 12,
	LM_LEFT_ELBOW    = 13,
	LM_RIGHT_ELBOW   = 14,
	LM_LEFT_WRIST    = 15,
	LM_RIGHT_WRIST   = 16,
	LM_LEFT_HIP      = 23,
	LM_RIGHT_HIP     = 24,
	LM_LEFT_KNEE     = 25,
	LM_RIGHT_KNEE    = 26,
	LM_LEFT_ANKLE    = 27,
	LM_RIGHT_ANKLE   = 28,
	LM_LEFT_FOOT     = 31,
	LM_RIGHT_FOOT    = 32,
};

struct Bone
{
	int a;
	int b;
	int group;// see BoneGroup
};

enum BoneGroup : int
{
	GROUP_TORSO = 0,
	GROUP_LIMBS = 1,
	GROUP_HEAD  = 2,
};

// The skeleton the particles are emitted along. Kept small on purpose: the
// finger/eye landmarks are noisy and add nothing at particle scale.
static const Bone BONES[] = {
	{ LM_LEFT_SHOULDER,  LM_RIGHT_SHOULDER, GROUP_TORSO },
	{ LM_LEFT_SHOULDER,  LM_LEFT_HIP,       GROUP_TORSO },
	{ LM_RIGHT_SHOULDER, LM_RIGHT_HIP,      GROUP_TORSO },
	{ LM_LEFT_HIP,       LM_RIGHT_HIP,      GROUP_TORSO },
	{ LM_LEFT_SHOULDER,  LM_LEFT_ELBOW,     GROUP_LIMBS },
	{ LM_LEFT_ELBOW,     LM_LEFT_WRIST,     GROUP_LIMBS },
	{ LM_RIGHT_SHOULDER, LM_RIGHT_ELBOW,    GROUP_LIMBS },
	{ LM_RIGHT_ELBOW,    LM_RIGHT_WRIST,    GROUP_LIMBS },
	{ LM_LEFT_HIP,       LM_LEFT_KNEE,      GROUP_LIMBS },
	{ LM_LEFT_KNEE,      LM_LEFT_ANKLE,     GROUP_LIMBS },
	{ LM_LEFT_ANKLE,     LM_LEFT_FOOT,      GROUP_LIMBS },
	{ LM_RIGHT_HIP,      LM_RIGHT_KNEE,     GROUP_LIMBS },
	{ LM_RIGHT_KNEE,     LM_RIGHT_ANKLE,    GROUP_LIMBS },
	{ LM_RIGHT_ANKLE,    LM_RIGHT_FOOT,     GROUP_LIMBS },
	{ LM_NOSE,           LM_LEFT_EAR,       GROUP_HEAD  },
	{ LM_NOSE,           LM_RIGHT_EAR,      GROUP_HEAD  },
	{ LM_LEFT_EAR,       LM_LEFT_SHOULDER,  GROUP_HEAD  },
	{ LM_RIGHT_EAR,      LM_RIGHT_SHOULDER, GROUP_HEAD  },
};
static const int NUM_BONES = int( sizeof( BONES ) / sizeof( BONES[ 0 ] ) );

}// namespace mpp
