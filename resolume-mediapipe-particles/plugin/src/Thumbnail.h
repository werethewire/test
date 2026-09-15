#pragma once
//
// The image Resolume shows for this plugin in its source browser.
//
// Generated on the CPU at load rather than embedded as a pixel blob: it is a
// few hundred microseconds, it keeps a quarter megabyte of hex out of the
// repository, and it is drawn from the same bone table the shader emits along,
// so the icon cannot end up depicting a skeleton the plugin no longer uses.
//
#include "GLInclude.h"
#include "PoseProtocol.h"

#include <vector>

namespace mpp
{
/// Particles scattered along a standing figure, in the plugin's default
/// white-to-blue ramp on black. Pixels are top-left origin, as FFGL expects.
std::vector< CFFGLColor > GenerateThumbnail( unsigned int width, unsigned int height );
}// namespace mpp
