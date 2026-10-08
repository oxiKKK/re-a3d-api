// NOT PART OF THE ORIGINAL. Project tooling.

#include "dal_a2d.h"
#include "dalinfo.h"
#include <gtest/gtest.h>

TEST(LinkedEngine, A2dReflectionCapabilities)
{
	DAL_A2D dal;
	A3DDALCAPS564 caps;
	DWORD size = sizeof(caps);
	ZeroMemory(&caps, sizeof(caps));

	ASSERT_EQ(dal.GetA3dCaps(&caps.caps, &size), S_OK);
#if defined(A3D_FIXES)
	if (A3dGetConfig().bSoftwareReflections)
	{
		// InitResMan requires a positive total when reflection support is granted.
		EXPECT_GT(caps.caps.dwMaxReflections, 0u);
		EXPECT_EQ(caps.caps.dwMaxSrcReflections, A3D_MAX_SOURCE_REFLECTIONS);
		EXPECT_EQ(caps.caps.dwMaxReflections,
			A3D_MIX_VOICES * caps.caps.dwMaxSrcReflections);
	}
	else
#endif
	{
		EXPECT_EQ(caps.caps.dwMaxReflections, 0u);
		EXPECT_EQ(caps.caps.dwMaxSrcReflections, 0u);
	}
	EXPECT_EQ(caps.caps.dwFeatureFlags, 0x00040007u);
	EXPECT_EQ(caps.caps.dwMaxSampleRate, A3D_MIX_SAMPLE_RATE);
}
