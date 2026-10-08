// NOT PART OF THE ORIGINAL. Project tooling.

#include "rmstatbuffer.h"
#include "rmstreambuffer.h"
#include <gtest/gtest.h>

template <typename Buffer>
class ResManBufferLifetime : public testing::Test
{
};

using ResManBufferTypes = testing::Types<ResManStatBuffer, ResManStreamBuffer>;
TYPED_TEST_SUITE(ResManBufferLifetime, ResManBufferTypes);

TYPED_TEST(ResManBufferLifetime, DeleteThroughBaseClosesDerivedAndBaseHandles)
{
	for (bool primary : {false, true})
	{
		TypeParam *buffer = new TypeParam(NULL, 0);
		buffer->m_hNotifyMutex = CreateMutex(NULL, FALSE, NULL);
		HANDLE handles[] = {
			buffer->m_hNotifyMutex,
			buffer->m_hPropSetMutex,
			buffer->m_hPropSetCacheFlushed
		};
		for (HANDLE handle : handles)
			ASSERT_NE(handle, nullptr);

		if (primary)
			delete static_cast<IResManBufferPrimary *>(buffer);
		else
			delete static_cast<ResManBuffer *>(buffer);

		for (HANDLE handle : handles)
		{
			DWORD flags = 0;
			SetLastError(ERROR_SUCCESS);
			EXPECT_FALSE(GetHandleInformation(handle, &flags));
			EXPECT_EQ(GetLastError(), ERROR_INVALID_HANDLE);
		}
	}
}
