// Project-added A3D development tooling.
#include "capture_internal.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>

namespace a3dcapture {
const CaptureScene	capture_scenes[] =
{
	/* At the listener, so A3dSourceStepEnd's near-field collapse runs. */
	{ "default", CAPTURE_KEEP_RENDER_MODE, 0, { 0.0f, 0.0f, 0.0f },
	  1.0f, 1.0f, 0.0f, 0, { 0.0f, 0.0f } },

	/* The mono branch: no HRTF direction, both ears the same gain. */
	{ "mono", A3DSOURCE_RENDERMODE_MONO, 0, { 0.0f, 0.0f, 0.0f },
	  1.0f, 1.0f, 0.0f, 0, { 0.0f, 0.0f } },

	/* Five units away and to the right: the positioned branch, the distance
	   gain curve, an HRTF row off the axis and a real interaural delay. */
	{ "right", CAPTURE_KEEP_RENDER_MODE, 1, { 3.0f, 0.0f, -4.0f },
	  1.0f, 1.0f, 0.0f, 0, { 0.0f, 0.0f } },

	/* Behind, left and above, with both gains off the round numbers. */
	{ "behind", CAPTURE_KEEP_RENDER_MODE, 1, { -1.0f, 0.5f, 2.0f },
	  0.35f, 0.6f, 0.0f, 0, { 0.0f, 0.0f } },

	/* The native branch, weighted by the two pan values. */
	{ "native", A3DSOURCE_RENDERMODE_NATIVE, 0, { 0.0f, 0.0f, 0.0f },
	  1.0f, 1.0f, 0.0f, 1, { 0.8f, 0.3f } },

	/* The resampler at a step the other scenes do not use. */
	{ "pitch", CAPTURE_KEEP_RENDER_MODE, 1, { 3.0f, 0.0f, -4.0f },
	  1.0f, 1.0f, 0.75f, 0, { 0.0f, 0.0f } },
	{ "doppler", CAPTURE_KEEP_RENDER_MODE, 1, { 3.0f, 0.0f, -4.0f },
	  1.0f, 1.0f, 0.0f, 0, { 0.0f, 0.0f }, { -6.0f, 0.0f, 8.0f } },
	{ "eq", CAPTURE_KEEP_RENDER_MODE, 1, { 3.0f, 0.0f, -4.0f },
	  1.0f, 1.0f, 0.0f, 0, { 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.25f },
	{ "far", CAPTURE_KEEP_RENDER_MODE, 1, { 0.0f, 0.0f, -12.0f },
	  1.0f, 1.0f, 0.0f, 0, { 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f,
	  { 1.0f, 8.0f } },
	{ "near", CAPTURE_KEEP_RENDER_MODE, 1, { 1.0f, 0.0f, -1.0f },
	  1.0f, 1.0f, 0.0f, 0, { 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f,
	  { 4.0f, 50.0f } },
	{ "listener", CAPTURE_KEEP_RENDER_MODE, 1, { 3.0f, 0.0f, -4.0f },
	  1.0f, 1.0f, 0.0f, 0, { 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f,
	  { 0.0f, 0.0f }, 1, { 1.0f, 0.0f, 1.0f }, 45.0f },
};


} // namespace a3dcapture
