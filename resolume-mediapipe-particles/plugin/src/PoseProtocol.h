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
	LM_LEFT_EYE_INNER  = 1,
	LM_LEFT_EYE        = 2,
	LM_LEFT_EYE_OUTER  = 3,
	LM_RIGHT_EYE_INNER = 4,
	LM_RIGHT_EYE       = 5,
	LM_RIGHT_EYE_OUTER = 6,
	LM_LEFT_EAR      = 7,
	LM_RIGHT_EAR     = 8,
	LM_MOUTH_LEFT    = 9,
	LM_MOUTH_RIGHT   = 10,
	LM_LEFT_SHOULDER = 11,
	LM_RIGHT_SHOULDER= 12,
	LM_LEFT_ELBOW    = 13,
	LM_RIGHT_ELBOW   = 14,
	LM_LEFT_WRIST    = 15,
	LM_RIGHT_WRIST   = 16,
	LM_LEFT_PINKY    = 17,
	LM_RIGHT_PINKY   = 18,
	LM_LEFT_INDEX    = 19,
	LM_RIGHT_INDEX   = 20,
	LM_LEFT_THUMB    = 21,
	LM_RIGHT_THUMB   = 22,
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
	GROUP_HEAD  = 2,///< nose to ears
	GROUP_NECK  = 3,///< ears to shoulders
	GROUP_FACE  = 4,///< eyes, mouth, jaw line -- only for Emit From = Head
	GROUP_HANDS = 5,///< wrist to fingertips -- only for Emit From = Hands
};

// The skeleton the particles are emitted along.
//
// The first 18 bones are the body, and their order is part of the look: the
// shader turns one random number into a bone by walking the cumulative table,
// so reordering them would move every particle of an existing preset. The
// face and hand bones are appended after them and carry zero weight in the
// body modes, which leaves those modes exactly as they were. Pose only gives
// three points per hand and a handful on the face; at particle scale that is
// enough to read as a hand or a face when nothing else emits.
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
	{ LM_LEFT_EAR,       LM_LEFT_SHOULDER,  GROUP_NECK  },
	{ LM_RIGHT_EAR,      LM_RIGHT_SHOULDER, GROUP_NECK  },
	// ---- face (FIRST_FACE_BONE)
	{ LM_LEFT_EYE_INNER,  LM_LEFT_EYE,       GROUP_FACE },
	{ LM_LEFT_EYE,        LM_LEFT_EYE_OUTER, GROUP_FACE },
	{ LM_RIGHT_EYE_INNER, LM_RIGHT_EYE,      GROUP_FACE },
	{ LM_RIGHT_EYE,       LM_RIGHT_EYE_OUTER,GROUP_FACE },
	{ LM_NOSE,            LM_LEFT_EYE_INNER, GROUP_FACE },
	{ LM_NOSE,            LM_RIGHT_EYE_INNER,GROUP_FACE },
	{ LM_LEFT_EYE_OUTER,  LM_LEFT_EAR,       GROUP_FACE },
	{ LM_RIGHT_EYE_OUTER, LM_RIGHT_EAR,      GROUP_FACE },
	{ LM_MOUTH_LEFT,      LM_MOUTH_RIGHT,    GROUP_FACE },
	{ LM_NOSE,            LM_MOUTH_LEFT,     GROUP_FACE },
	{ LM_NOSE,            LM_MOUTH_RIGHT,    GROUP_FACE },
	{ LM_LEFT_EAR,        LM_MOUTH_LEFT,     GROUP_FACE },
	{ LM_RIGHT_EAR,       LM_MOUTH_RIGHT,    GROUP_FACE },
	// ---- hands (FIRST_HAND_BONE)
	{ LM_LEFT_WRIST,  LM_LEFT_THUMB,  GROUP_HANDS },
	{ LM_LEFT_WRIST,  LM_LEFT_INDEX,  GROUP_HANDS },
	{ LM_LEFT_WRIST,  LM_LEFT_PINKY,  GROUP_HANDS },
	{ LM_LEFT_INDEX,  LM_LEFT_PINKY,  GROUP_HANDS },
	{ LM_RIGHT_WRIST, LM_RIGHT_THUMB, GROUP_HANDS },
	{ LM_RIGHT_WRIST, LM_RIGHT_INDEX, GROUP_HANDS },
	{ LM_RIGHT_WRIST, LM_RIGHT_PINKY, GROUP_HANDS },
	{ LM_RIGHT_INDEX, LM_RIGHT_PINKY, GROUP_HANDS },
};
static const int NUM_BONES = int( sizeof( BONES ) / sizeof( BONES[ 0 ] ) );

// Contiguous ranges, used to limit Body Attract to the part that emits
// without walking every bone in the shader's hot loop.
static const int NUM_BODY_BONES  = 18;///< everything before the face bones
static const int FIRST_HEAD_BONE = 14;///< nose-to-ear, then the neck
static const int FIRST_FACE_BONE = 18;
static const int FIRST_HAND_BONE = 31;

}// namespace mpp
