// NOT PART OF THE ORIGINAL. Project tooling.

#include "A3dPrivate.h"
#if defined(A3D_FIXES)
#include "A3dSource.h"
#include "resman.h"
#include "rmstatbuffer.h"
#include "dal_a2d.h"
#include "dalinfo.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <thread>
#include <vector>
#include <dbghelp.h>
#include <fstream>

extern void CheckEasterEgg(LPCSTR lpszMessage);

namespace {
bool hasCalculator;
bool hasComputer;
std::string creditsFile;

HWND WINAPI FindCreditsWindow(LPCSTR, LPCSTR title)
{
	return ((!strcmp(title, "Calculator") && hasCalculator) ||
		(!strcmp(title, "My Computer") && hasComputer)) ? (HWND) 1 : NULL;
}

UINT WINAPI CaptureCredits(LPCSTR path, UINT)
{
	creditsFile = path;
	return 33;
}

// Patch only this test process's import slot, restoring it before teardown.
class ImportOverride
{
	ULONG_PTR *slot = nullptr;
	ULONG_PTR original = 0;
public:
	ImportOverride(const char *module, const char *name, ULONG_PTR replacement)
	{
		HMODULE image = GetModuleHandle(NULL);
		ULONG bytes = 0;
		auto imports = static_cast<IMAGE_IMPORT_DESCRIPTOR *>(
			ImageDirectoryEntryToData(image, TRUE,
				IMAGE_DIRECTORY_ENTRY_IMPORT, &bytes));
		ULONG_PTR target = (ULONG_PTR) GetProcAddress(GetModuleHandleA(module), name);
		if (!imports || !target)
			return;
		for (; imports->Name && !slot; ++imports)
		{
			auto thunk = reinterpret_cast<IMAGE_THUNK_DATA *>(
				reinterpret_cast<ULONG_PTR>(image) + imports->FirstThunk);
			for (; thunk && thunk->u1.Function; ++thunk)
				if (thunk->u1.Function == target)
				{
					slot = &thunk->u1.Function;
					DWORD protection;
					if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection))
					{
						slot = nullptr;
						return;
					}
					original = *slot;
					*slot = replacement;
					VirtualProtect(slot, sizeof(*slot), protection, &protection);
					break;
				}
		}
	}

	~ImportOverride()
	{
		if (slot)
		{
			DWORD protection;
			VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection);
			*slot = original;
			VirtualProtect(slot, sizeof(*slot), protection, &protection);
		}
	}
	bool installed() const { return slot != nullptr; }
};
}

TEST(RuntimeConfig, SnapshotAndDecoders)
{
	const char *expected = std::getenv("A3D_TEST_CONFIG_BITS");
	if (!expected)
		GTEST_SKIP() << "Run through runtime_config_matrix.py.";
	ASSERT_EQ(strlen(expected), 8u);

	const A3DCONFIG *snapshots[16] = {};
	HANDLE start = CreateEvent(NULL, TRUE, FALSE, NULL);
	ASSERT_NE(start, nullptr);
	std::vector<std::thread> threads;
	for (int i = 0; i < 16; ++i)
		threads.emplace_back([&, i] {
			WaitForSingleObject(start, INFINITE);
			snapshots[i] = &A3dGetConfig();
		});
	SetEvent(start);
	for (auto &thread : threads)
		thread.join();
	CloseHandle(start);
	for (const auto *snapshot : snapshots)
		ASSERT_EQ(snapshot, snapshots[0]);
	const A3DCONFIG &config = *snapshots[0];
	const BOOL values[] = {
		config.bEmulateHardware, config.bSoftwareReverb,
		config.bSoftwareReflections, config.bFixPropertyDeadlocks,
		config.bEnableMP3Decoder, config.bEnableAC3Decoder,
		config.bUseNewCredits, config.bRelaxCreditsActivation
	};
	for (int i = 0; i < 8; ++i)
		EXPECT_EQ(values[i] != FALSE, expected[i] == '1') << i;
	DAL_A2D dal;
	A3DDALCAPS564 caps = {};
	DWORD size = sizeof(caps);
	ASSERT_EQ(dal.GetA3dCaps(&caps.caps, &size), S_OK);
	EXPECT_EQ(caps.caps.dwMaxSrcReflections,
		config.bSoftwareReflections ? A3D_MAX_SOURCE_REFLECTIONS : 0u);
	EXPECT_EQ(caps.caps.dwMaxReflections,
		A3D_MIX_VOICES * caps.caps.dwMaxSrcReflections);

	LPA3DMP3DECODER decoder = NULL;
	const int result = Mp3SscCreateDecoder(&decoder);
	EXPECT_EQ(decoder != NULL, config.bEnableMP3Decoder != FALSE);
	EXPECT_EQ(result, config.bEnableMP3Decoder ? 0 : (int) 0xC0000000);
	if (decoder)
		decoder->Destroy();
	DWORD ac3 = Ac3OpenAudio(0);
	EXPECT_EQ(ac3 != 0, config.bEnableAC3Decoder != FALSE);
	if (ac3)
		Ac3CloseAudio(ac3);

	ResMan manager;
	ResManStatBuffer buffer(NULL, 0);
	ResetEvent(buffer.m_hPropSetCacheFlushed);
	ASSERT_EQ(manager.ReplayPropertySetItems(&buffer, 0), S_OK);
	EXPECT_EQ(WaitForSingleObject(buffer.m_hPropSetCacheFlushed, 0),
		config.bFixPropertyDeadlocks ? WAIT_OBJECT_0 : WAIT_TIMEOUT);

	manager.m_dwPropSetCacheValid = 0;
	manager.m_hPropSetMutex = CreateMutex(NULL, FALSE, NULL);
	manager.m_hPropSetCacheFlushed = CreateEvent(NULL, FALSE, FALSE, NULL);
	ASSERT_NE(manager.m_hPropSetMutex, nullptr);
	ASSERT_NE(manager.m_hPropSetCacheFlushed, nullptr);
	HANDLE done = CreateEvent(NULL, FALSE, FALSE, NULL);
	ASSERT_NE(done, nullptr);
	std::thread waiter([&] {
		ULONG support = 0;
		manager.QuerySupport(GUID_NULL, 0, &support);
		SetEvent(done);
	});
	EXPECT_EQ(WaitForSingleObject(done,
		config.bFixPropertyDeadlocks ? 2000 : 100),
		config.bFixPropertyDeadlocks ? WAIT_OBJECT_0 : WAIT_TIMEOUT);
	// Release the original wait too, so both modes cleanly join.
	SetEvent(manager.m_hPropSetCacheFlushed);
	waiter.join();
	CloseHandle(done);

	// No desktop windows or executable launches: intercept both imports first.
	ImportOverride launch("kernel32.dll", "WinExec", (ULONG_PTR) CaptureCredits);
	ImportOverride windows("user32.dll", "FindWindowA", (ULONG_PTR) FindCreditsWindow);
	ASSERT_TRUE(launch.installed());
	ASSERT_TRUE(windows.installed());
	for (bool calculator : {false, true})
		for (bool computer : {false, true})
		{
			hasCalculator = calculator;
			hasComputer = computer;
			creditsFile.clear();
			CheckEasterEgg("runtime configuration test");
			bool activated = calculator && (computer || config.bRelaxCreditsActivation);
			EXPECT_EQ(!creditsFile.empty(), activated);
			if (!creditsFile.empty())
			{
				std::ifstream payload(creditsFile, std::ios::binary | std::ios::ate);
				EXPECT_EQ(payload.tellg(), config.bUseNewCredits ? 28672 : 24576);
				payload.close();
				EXPECT_TRUE(DeleteFileA(creditsFile.c_str()));
			}
		}

	// The runner owns this isolated module directory. An edit must not reload.
	WCHAR path[32768];
	DWORD length = GetModuleFileNameW(NULL, path, 32768);
	ASSERT_GT(length, 0u);
	ASSERT_LT(length, 32768u);
	WCHAR *name = wcsrchr(path, L'\\');
	ASSERT_NE(name, nullptr);
	lstrcpyW(name + 1, L"a3dapi.conf");
	ASSERT_TRUE(WritePrivateProfileStringW(L"A3D", L"EmulateHardware",
		config.bEmulateHardware ? L"false" : L"true", path));
	EXPECT_EQ(A3dGetConfig().bEmulateHardware, values[0]);
}
#endif
