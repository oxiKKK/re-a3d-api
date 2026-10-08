/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * apimapper.cpp
 *
 * Implements the DirectSound compatibility interfaces exposed by
 * a3dapi.dll. CA3dMapper routes device calls to CA3dRoot, while primary
 * and secondary buffer wrappers translate buffer, listener and 3D source
 * operations into the A3D engine's interfaces.
 *
 * The wrappers manage the references connecting DirectSound objects to
 * the root and sources, allowing a DirectSound client to use the same
 * rendering engine. This file also handles the optional mapper debug log.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "apimapper.h"
#include "A3d3.h"

#include <math.h>
#include <stdio.h>

/* Hundredths-of-decibel to amplitude exponent
 *  Keep the float before widening. */
#define A3D_MAPPER_VOLUME_EXPONENT_SCALE 0.0005f

/* (RE) dbg:0x1015295C. Process-wide Debug log. */
static FILE *g_pMapperDebugLog = NULL;

/* =============================================================
// A3dMapperOpenDebugLog()
// (RE) dbg:0x1000E800
//
// Open the Debug log configured in the registry. The original leaks the
// registry key and any overwritten log handle.
//
// Returns: The current process log pointer, possibly null.
// =============================================================*/

static FILE *
A3dMapperOpenDebugLog(void)
{
HKEY	hKey;
DWORD	dwEnabled;
DWORD	cbData;
char	szPath[256];

	if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Aureal\\A3D", 0,
			   KEY_READ, &hKey))
	{
		cbData = sizeof(dwEnabled);

		if (!RegQueryValueExA(hKey, "debug_enabled", 0, 0,
				      (BYTE *) &dwEnabled, &cbData) &&
		    dwEnabled == 1)
		{
			cbData = sizeof(szPath);

			if (!RegQueryValueExA(hKey, "debug_outputfile", 0, 0,
					      (BYTE *) szPath, &cbData))
			{
				if (cbData)
					g_pMapperDebugLog = fopen(szPath, "w");
			}
		}
	}

	return (g_pMapperDebugLog);
}

/* =============================================================
// A3dMapperCloseDebugLog()
// (RE) dbg:0x1000E910; thunk dbg:0x10002A68; folded Retail rtl:0x10004180
//
// Close the process Debug log. The original leaves the pointer uncleared.
// =============================================================*/

void
A3dMapperCloseDebugLog(void)
{
	if (g_pMapperDebugLog)
		fclose(g_pMapperDebugLog);
}

/* =============================================================
// CA3dMapper()
// (RE) dbg:0x10037060
//
// Reference the API object and open the Debug log. The original leaves
// m_cRef uninitialized.
// =============================================================*/

CA3dMapper::CA3dMapper(CA3dRoot *pApi)
{
	m_pApi = pApi;

	pApi->AddRef();

	m_pListener = NULL;

	A3dMapperOpenDebugLog();
}

/* =============================================================
// CA3dMapper scalar deleting destructor
// (RE) dbg:0x100370f0; rtl:0x10017e20
// =============================================================*/

/* =============================================================
// ~CA3dMapper()
// (RE) dbg:0x100371a0
//
// Release the API object. The original leaks the primary buffer reference.
// =============================================================*/

CA3dMapper::~CA3dMapper(void)
{
	m_pApi->Release();
}

/* =============================================================
// QueryInterface()
// (RE) dbg:0x100371f0; rtl:0x10017e60
//
// Return a referenced interface supported by the mapper.
//
// Returns:
//   S_OK
//   E_NOINTERFACE  an unsupported IID
//   E_INVALIDARG   if the output pointer is null
// =============================================================*/

STDMETHODIMP
CA3dMapper::QueryInterface(REFIID riid, void **ppv)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::QueryInterface\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	if (!ppv)
		return (E_INVALIDARG);

	if (!memcmp(&riid, &IID_IUnknown, sizeof(IID)))
	{
		*ppv = this;

		((IUnknown *) *ppv)->AddRef();

		return (S_OK);
	}

	if (!memcmp(&riid, &IID_IA3d, sizeof(IID)) ||
	    !memcmp(&riid, &IID_IA3d2, sizeof(IID)))
	{
		*ppv = this ? (void *) (IA3d2 *) this : NULL;

		((IUnknown *) *ppv)->AddRef();

		return (S_OK);
	}

	if (!memcmp(&riid, &IID_IDirectSound, sizeof(IID)))
	{
		*ppv = this;

		((IUnknown *) *ppv)->AddRef();

		return (S_OK);
	}

	*ppv = NULL;

	return (E_NOINTERFACE);
}

/* =============================================================
// CA3dMapper::AddRef()
// (RE) dbg:0x10037390; rtl:0x10017f50
//
// Increment the reference count.
//
// Returns: The count reread after the interlocked increment.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dMapper::AddRef(void)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::AddRef\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	InterlockedIncrement(&m_cRef);

	return (m_cRef);
}

/* =============================================================
// CA3dMapper::Release()
// (RE) dbg:0x10037420; rtl:0x10017f70
//
// Release a reference and delete at zero. Preserve the original race
// from rereading the count after the interlocked decrement.
//
// Returns: The remaining count, or zero after deletion.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dMapper::Release(void)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::Release\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	InterlockedDecrement(&m_cRef);

	if (m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// CreateSoundBuffer()
// (RE) dbg:0x10037650; rtl:0x10017fe0
//
// Return the primary buffer or wrap a new A3D source. Preserve the
// original unreachable primary-buffer validation failure.
//
// Returns: A failed NewSource result, otherwise S_OK; subsequent errors are
//          ignored.
// =============================================================*/

STDMETHODIMP
CA3dMapper::CreateSoundBuffer(LPCDSBUFFERDESC pcDesc,
			      LPDIRECTSOUNDBUFFER *ppBuffer,
			      LPUNKNOWN pUnkOuter)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::CreateSoundBuffer\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

LPA3DSOURCE2            pSource;
CA3dMapperSecBuffer    *pWrap;
WAVEFORMATEX            wfx;
HRESULT                 hr;

	*ppBuffer = NULL;

	if (!m_pListener)
	{
		m_pListener = new CA3dMapperPrimBuffer(m_pApi);

		m_pListener->AddRef();
	}

	if (pcDesc->dwFlags & DSBCAPS_PRIMARYBUFFER)
	{
		if (pcDesc->dwFlags & (DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D))
		{
			m_pListener->QueryInterface(IID_IDirectSoundBuffer,
						    (void **) ppBuffer);

			return (S_OK);
		}

		return (E_INVALIDARG);
	}

	hr = m_pApi->NewSource(A3DSOURCE_INITIAL_RENDERMODE_A3D, &pSource);
	if (FAILED(hr))
		return (hr);

	pWrap = new CA3dMapperSecBuffer(pSource, m_pListener,
					m_pApi);

	*ppBuffer = pWrap;

	pWrap->AddRef();

	wfx = *pcDesc->lpwfxFormat;

	pSource->SetAudioFormat(&wfx);
	pSource->AllocateAudioData(pcDesc->dwBufferBytes);
	pSource->Release();

	return (S_OK);
}

/* =============================================================
// DuplicateSoundBuffer()
// (RE) dbg:0x10037a30; rtl:0x10018250
//
// Duplicate the source in an assumed secondary mapper buffer. The
// original dereferences a null listener if no buffer was created first.
//
// Returns: E_FAIL for a null source; a failed DuplicateSource result; otherwise
//          S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapper::DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER pOriginal,
				 LPDIRECTSOUNDBUFFER *ppDuplicate)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::DuplicateSoundBuffer\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

CA3dMapperSecBuffer    *pWrap;
LPA3DSOURCE2            pSource;
LPA3DSOURCE2            pDup;
HRESULT                 hr;

	*ppDuplicate = NULL;

	pSource = ((CA3dMapperSecBuffer *) pOriginal)->m_pSource;
	if (!pSource)
		return (E_FAIL);

	hr = m_pApi->DuplicateSource(pSource, &pDup);
	if (FAILED(hr))
		return (hr);

	pWrap = new CA3dMapperSecBuffer(pDup, m_pListener,
					m_pApi);

	*ppDuplicate = pWrap;

	pWrap->AddRef();

	pDup->Release();

	return (S_OK);
}

/* =============================================================
// Initialize()
// (RE) dbg:0x10037510; rtl:0x10017fa0
//
// Forward the request to CA3dRoot::Init().
//
// Returns: The result of CA3dRoot::Init().
// =============================================================*/

STDMETHODIMP
CA3dMapper::Initialize(LPCGUID pcGuidDevice)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::Initialize\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->Init((LPGUID) pcGuidDevice, 0, 0));
}

/* =============================================================
// SetCooperativeLevel()
// (RE) dbg:0x10037c10; rtl:0x10018380
//
// Forward the request to CA3dRoot::SetCooperativeLevel().
//
// Returns: The result of CA3dRoot::SetCooperativeLevel().
// =============================================================*/

STDMETHODIMP
CA3dMapper::SetCooperativeLevel(HWND hWnd, DWORD dwLevel)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::SetCooperativeLevel\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->SetCooperativeLevel(hWnd, dwLevel));
}

/* =============================================================
// GetCaps()
// (RE) dbg:0x100375b0; rtl:0x10017fc0
//
// Forward the request to CA3dRoot::DsGetCaps().
//
// Returns: The result of CA3dRoot::DsGetCaps().
// =============================================================*/

STDMETHODIMP
CA3dMapper::GetCaps(LPDSCAPS pCaps)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::GetCaps\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->DsGetCaps(pCaps));
}

/* =============================================================
// Compact()
// (RE) dbg:0x10037cb0; rtl:0x100183a0
//
// Forward the request to CA3dRoot::DsCompact().
//
// Returns: The result of CA3dRoot::DsCompact().
// =============================================================*/

STDMETHODIMP
CA3dMapper::Compact(void)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::Compact\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->DsCompact());
}

/* =============================================================
// GetSpeakerConfig()
// (RE) dbg:0x10037d40; rtl:0x100183c0
//
// Forward the request to CA3dRoot::DsGetSpeakerConfig().
//
// Returns: The result of CA3dRoot::DsGetSpeakerConfig().
// =============================================================*/

STDMETHODIMP
CA3dMapper::GetSpeakerConfig(LPDWORD pdwConfig)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::GetSpeakerConfig\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->DsGetSpeakerConfig(pdwConfig));
}

/* =============================================================
// SetSpeakerConfig()
// (RE) dbg:0x10037de0; rtl:0x100183e0
//
// Forward the request to CA3dRoot::DsSetSpeakerConfig().
//
// Returns: The result of CA3dRoot::DsSetSpeakerConfig().
// =============================================================*/

STDMETHODIMP
CA3dMapper::SetSpeakerConfig(DWORD dwConfig)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::SetSpeakerConfig\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->DsSetSpeakerConfig(dwConfig));
}

/* =============================================================
// SetOutputMode()
// (RE) dbg:0x10037e80; rtl:0x10018400
//
// Forward the request to CA3dRoot::SetOutputMode().
//
// Returns: The result of CA3dRoot::SetOutputMode().
// =============================================================*/

STDMETHODIMP
CA3dMapper::SetOutputMode(DWORD dwRelation, DWORD dwMode, DWORD dwChannels)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::SetOutputMode\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->SetOutputMode(dwRelation, dwMode, dwChannels));
}

/* =============================================================
// GetOutputMode()
// (RE) dbg:0x10037f30; rtl:0x10018420
//
// Forward the request to CA3dRoot::GetOutputMode().
//
// Returns: The result of CA3dRoot::GetOutputMode().
// =============================================================*/

STDMETHODIMP
CA3dMapper::GetOutputMode(LPDWORD pdwRelation, LPDWORD pdwMode,
			  LPDWORD pdwChannels)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::GetOutputMode\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->GetOutputMode(pdwRelation, pdwMode, pdwChannels));
}

/* =============================================================
// SetResourceManagerMode()
// (RE) dbg:0x10037fe0; rtl:0x10018440
//
// Forward the request to CA3dRoot::SetResourceManagerMode().
//
// Returns: The result of CA3dRoot::SetResourceManagerMode().
// =============================================================*/

STDMETHODIMP
CA3dMapper::SetResourceManagerMode(DWORD dwMode)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::SetResourceManagerMode\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->SetResourceManagerMode(dwMode));
}

/* =============================================================
// GetResourceManagerMode()
// (RE) dbg:0x10038080; rtl:0x10018460
//
// Forward the request to CA3dRoot::GetResourceManagerMode().
//
// Returns: The result of CA3dRoot::GetResourceManagerMode().
// =============================================================*/

STDMETHODIMP
CA3dMapper::GetResourceManagerMode(LPDWORD pdwMode)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::GetResourceManagerMode\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->GetResourceManagerMode(pdwMode));
}

/* =============================================================
// SetHFAbsorbFactor()
// (RE) dbg:0x10038120; rtl:0x100034f0
//
// Original stub; perform no operation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapper::SetHFAbsorbFactor(FLOAT fFactor)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::SetHFAbsorbFactor\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetHFAbsorbFactor()
// (RE) dbg:0x100381a0; rtl:0x10018480
//
// Forward the request to CA3dRoot::GetHFAbsorbFactor().
//
// Returns: The result of CA3dRoot::GetHFAbsorbFactor().
// =============================================================*/

STDMETHODIMP
CA3dMapper::GetHFAbsorbFactor(FLOAT *pfFactor)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::GetHFAbsorbFactor\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->GetHFAbsorbFactor(pfFactor));
}

/* =============================================================
// RegisterVersion()
// (RE) dbg:0x10038240; rtl:0x100184a0
//
// Forward the request to CA3dRoot::RegisterVersion().
//
// Returns: The result of CA3dRoot::RegisterVersion().
// =============================================================*/

STDMETHODIMP
CA3dMapper::RegisterVersion(DWORD dwVersion)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::RegisterVersion\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->RegisterVersion(dwVersion));
}

/* =============================================================
// GetSoftwareCaps()
// (RE) dbg:0x100382e0; rtl:0x100184c0
//
// Forward the request to CA3dRoot::GetSoftwareCaps().
//
// Returns: The result of CA3dRoot::GetSoftwareCaps().
// =============================================================*/

STDMETHODIMP
CA3dMapper::GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::GetSoftwareCaps\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->GetSoftwareCaps(pCaps));
}

/* =============================================================
// GetHardwareCaps()
// (RE) dbg:0x10038380; rtl:0x100184e0
//
// Forward the request to CA3dRoot::GetHardwareCaps().
//
// Returns: The result of CA3dRoot::GetHardwareCaps().
// =============================================================*/

STDMETHODIMP
CA3dMapper::GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapper::GetHardwareCaps\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pApi->GetHardwareCaps(pCaps));
}

typedef int A3dMapperCheck[(sizeof(CA3dMapper) == 0x14) ? 1 : -1];

/* =============================================================
// CA3dMapperPrimBuffer()
// (RE) dbg:0x10038420
//
// Reference the owner and listener and initialize the scale factors.
// The original takes an extra listener reference after QueryInterface.
// =============================================================*/

CA3dMapperPrimBuffer::CA3dMapperPrimBuffer(CA3dRoot *pOwner)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::CA3dMapperPrimBuffer\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	m_pOwner = pOwner;

	pOwner->AddRef();

	m_cRef = 0;

	m_fDistanceFactor = DS3D_DEFAULTDISTANCEFACTOR;
	m_fDopplerFactor  = DS3D_DEFAULTDOPPLERFACTOR;
	m_fRolloffFactor  = DS3D_DEFAULTROLLOFFFACTOR;

	m_pOwner->QueryInterface(IID_IA3dListener, (void **) &m_pListener);

	m_pListener->AddRef();
}

/* (RE) Compiler-generated base-adjustor destructors: dbg:0x100385a0; dbg:0x100385d0. */

/* =============================================================
// CA3dMapperPrimBuffer scalar deleting destructor
// (RE) dbg:0x10038550; rtl:0x10018500
// =============================================================*/

/* =============================================================
// ~CA3dMapperPrimBuffer()
// (RE) dbg:0x10038600
//
// Release the listener and owner references.
// =============================================================*/

CA3dMapperPrimBuffer::~CA3dMapperPrimBuffer(void)
{
	m_pListener->Release();

	m_pOwner->Release();
}

typedef int A3dMapperPrimBufferCheck[(sizeof(CA3dMapperPrimBuffer) == 0x20) ? 1 : -1];

/* =============================================================
// QueryInterface()
// (RE) dbg:0x10038660; rtl:0x10018540
//
// Return a referenced interface supported by the mapper.
//
// Returns:
//   S_OK
//   E_NOINTERFACE  an unsupported IID
//   E_INVALIDARG   if the output pointer is null
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::QueryInterface(REFIID riid, void **ppv)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::QueryInterface\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	if (!ppv)
		return (E_INVALIDARG);

	*ppv = NULL;

	if (!memcmp(&riid, &IID_IUnknown, sizeof(IID)))
		*ppv = this;
	else if (!memcmp(&riid, &IID_IDirectSoundBuffer, sizeof(IID)))
		*ppv = this;
	else if (!memcmp(&riid, &IID_IDirectSound3DListener, sizeof(IID)))
		*ppv = this ? (void *) (IDirectSound3DListener *) this : NULL;

	if (!*ppv)
		return (E_NOINTERFACE);

	((IUnknown *) *ppv)->AddRef();

	return (S_OK);
}

/* =============================================================
// CA3dMapperPrimBuffer::AddRef()
// (RE) dbg:0x100387c0; rtl:0x100185d0
//
// Increment the reference count.
//
// Returns: The count reread after the interlocked increment.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dMapperPrimBuffer::AddRef(void)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::AddRef\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	InterlockedIncrement(&m_cRef);

	return (m_cRef);
}

/* =============================================================
// CA3dMapperPrimBuffer::Release()
// (RE) dbg:0x10038850; rtl:0x100185f0
//
// Release a reference and delete at zero. Preserve the original race
// from rereading the count after the interlocked decrement.
//
// Returns: The remaining count, or zero after deletion.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dMapperPrimBuffer::Release(void)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::Release\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	InterlockedDecrement(&m_cRef);

	if (m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// Lock()
// (RE) dbg:0x10038d60; rtl:0x10018620
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::Lock(DWORD dwOffset, DWORD dwBytes,
			   LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1,
			   LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2,
			   DWORD dwFlags)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::Lock\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// CommitDeferredSettings()
// (RE) dbg:0x100392a0; rtl:0x10018630
//
// Flush and clear the owner, ignoring both results.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::CommitDeferredSettings(void)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::CommitDeferredSettings\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	m_pOwner->Flush();
	m_pOwner->Clear();

	return (S_OK);
}

/* =============================================================
// SetAllParameters()
// (RE) dbg:0x10039350; rtl:0x10018650
//
// Preserve the original ignored parameter structure; commit only for
// DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::SetAllParameters(LPCDS3DLISTENER pcDs3dl, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::SetAllParameters\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetDistanceFactor()
// (RE) dbg:0x10039400; rtl:0x10018680
//
// Cache the distance factor. The original neither applies nor commits it.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::SetDistanceFactor(D3DVALUE fFactor, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::SetDistanceFactor\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	m_fDistanceFactor = fFactor;

	return (S_OK);
}

/* =============================================================
// SetDopplerFactor()
// (RE) dbg:0x100394e0; rtl:0x100186a0
//
// Cache and broadcast the factor, then commit for DS3D_IMMEDIATE.
// Preserve the original broadcast even for deferred changes.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::SetDopplerFactor(D3DVALUE fFactor, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::SetDopplerFactor\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	m_fDopplerFactor = fFactor;

	A3dBroadcastDoppler(m_pOwner, fFactor);

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetRolloffFactor()
// (RE) dbg:0x10039940; rtl:0x10018790
//
// Cache and broadcast the factor, then commit for DS3D_IMMEDIATE.
// Preserve the original broadcast even for deferred changes.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::SetRolloffFactor(D3DVALUE fFactor, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::SetRolloffFactor\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	m_fRolloffFactor = fFactor;

	A3dBroadcastRolloff(m_pOwner, fFactor);

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetOrientation()
// (RE) dbg:0x10039610; rtl:0x100186e0
//
// Set the orientation vectors with z negated; commit for
// DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::SetOrientation(D3DVALUE fFx, D3DVALUE fFy, D3DVALUE fFz,
				     D3DVALUE fTx, D3DVALUE fTy, D3DVALUE fTz,
				     DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::SetOrientation\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	((IA3dListener *) m_pOwner)->SetOrientation6f(fFx, fFy, -fFz,
						      fTx, fTy, -fTz);

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetPosition()
// (RE) dbg:0x100397f0; rtl:0x10018740
//
// Set the position with z negated; commit for
// DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::SetPosition\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	((IA3dListener *) m_pOwner)->SetPosition3f(x, y, -z);

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetVelocity()
// (RE) dbg:0x10039a70; rtl:0x100187d0
//
// Set the velocity with z negated; commit for
// DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::SetVelocity\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	((IA3dListener *) m_pOwner)->SetVelocity3f(x, y, -z);

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// GetAllParameters()
// (RE) dbg:0x10039bc0; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetAllParameters(LPDS3DLISTENER pDs3dl)	{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetAllParameters\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetOrientation()
// (RE) dbg:0x10039d60; rtl:0x1001b380
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetOrientation(D3DVECTOR *pvFront,
						  D3DVECTOR *pvTop)		{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetOrientation\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetPosition()
// (RE) dbg:0x10039de0; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetPosition(D3DVECTOR *pvPosition)		{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetPosition\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetVelocity()
// (RE) dbg:0x10039ef0; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetVelocity(D3DVECTOR *pvVelocity)		{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetVelocity\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetCaps()
// (RE) dbg:0x10038940; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetCaps(LPDSBCAPS pCaps)			{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetCaps\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetCurrentPosition()
// (RE) dbg:0x100389d0; rtl:0x1001b380
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetCurrentPosition(DWORD *pdwPlay, DWORD *pdwWrite)
										{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetCurrentPosition\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetFormat()
// (RE) dbg:0x10038a50; rtl:0x10018b60
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetFormat(LPWAVEFORMATEX pwfx, DWORD cb, DWORD *pcb)
										{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetFormat\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetVolume()
// (RE) dbg:0x10038c50; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetVolume(LONG *plVolume)			{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetVolume\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetPan()
// (RE) dbg:0x10038b50; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetPan(LONG *plPan)				{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetPan\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetFrequency()
// (RE) dbg:0x10038ad0; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetFrequency(DWORD *pdwFrequency)		{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetFrequency\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetStatus()
// (RE) dbg:0x10038bd0; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetStatus(DWORD *pdwStatus)			{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetStatus\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// Initialize()
// (RE) dbg:0x10038cd0; rtl:0x1001b380
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::Initialize(LPDIRECTSOUND pDs, LPCDSBUFFERDESC pDesc)
										{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::Initialize\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// Play()
// (RE) dbg:0x10038df0; rtl:0x10018b60
//
// Original stub; perform no operation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::Play(DWORD dwReserved1, DWORD dwPriority,
					DWORD dwFlags)				{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::Play\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// SetCurrentPosition()
// (RE) dbg:0x10038f00; rtl:0x100034f0
//
// Original stub; perform no operation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::SetCurrentPosition(DWORD dwPlay)		{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::SetCurrentPosition\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// SetFormat()
// (RE) dbg:0x10038f80; rtl:0x100034f0
//
// Original stub; perform no operation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::SetFormat(LPCWAVEFORMATEX pwfx)		{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::SetFormat\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// SetVolume()
// (RE) dbg:0x10039110; rtl:0x100034f0
//
// Original stub; perform no operation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::SetVolume(LONG lVolume)			{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::SetVolume\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// SetPan()
// (RE) dbg:0x10039090; rtl:0x100034f0
//
// Original stub; perform no operation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::SetPan(LONG lPan)				{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::SetPan\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// SetFrequency()
// (RE) dbg:0x10039010; rtl:0x100034f0
//
// Original stub; perform no operation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::SetFrequency(DWORD dwFrequency)		{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::SetFrequency\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// Stop()
// (RE) dbg:0x10039190; rtl:0x1001b1c0
//
// Original stub; perform no operation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::Stop(void)					{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::Stop\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// Restore()
// (RE) dbg:0x10038e70; rtl:0x1001b1c0
//
// Original stub; perform no operation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::Restore(void)				{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::Restore\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// Unlock()
// (RE) dbg:0x10039210; rtl:0x1001b1b0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
					  LPVOID pvAudioPtr2, DWORD dwAudioBytes2)
										{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::Unlock\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetDistanceFactor()
// (RE) dbg:0x10039c40; rtl:0x10018820
//
// Read the cached distance factor.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetDistanceFactor(D3DVALUE *pfFactor)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetDistanceFactor\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	*pfFactor = m_fDistanceFactor;

	return (S_OK);
}

/* =============================================================
// GetDopplerFactor()
// (RE) dbg:0x10039cd0; rtl:0x10018840
//
// Read the cached Doppler factor.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetDopplerFactor(D3DVALUE *pfFactor)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetDopplerFactor\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	*pfFactor = m_fDopplerFactor;

	return (S_OK);
}

/* =============================================================
// GetRolloffFactor()
// (RE) dbg:0x10039e60; rtl:0x10018860
//
// Read the cached rolloff factor.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperPrimBuffer::GetRolloffFactor(D3DVALUE *pfFactor)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperPrimBuffer::GetRolloffFactor\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	*pfFactor = m_fRolloffFactor;

	return (S_OK);
}

/* =============================================================
// GetVolume()
// (RE) dbg:0x1003b0e0; rtl:0x10018840
//
// Read the cached DirectSound volume in hundredths of a decibel.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetVolume(LONG *plVolume)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetVolume\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	*plVolume = m_lVolume;

	return (S_OK);
}

/* =============================================================
// Lock()
// (RE) dbg:0x1003a4b0; rtl:0x100189a0
//
// Lock source audio data.
//
// Returns: The source Lock() result.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::Lock(DWORD dwOffset, DWORD dwBytes,
			  LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1,
			  LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2,
			  DWORD dwFlags)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::Lock\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pSource->Lock(dwOffset, dwBytes, ppvAudioPtr1, pdwAudioBytes1,
				ppvAudioPtr2, pdwAudioBytes2, dwFlags));
}

/* =============================================================
// Unlock()
// (RE) dbg:0x1003a570; rtl:0x100189d0
//
// Unlock source audio data.
//
// Returns: The source Unlock() result.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
			    LPVOID pvAudioPtr2, DWORD dwAudioBytes2)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::Unlock\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (m_pSource->Unlock(pvAudioPtr1, dwAudioBytes1,
				  pvAudioPtr2, dwAudioBytes2));
}

/* =============================================================
// Play()
// (RE) dbg:0x1003a620; rtl:0x10018a00
//
// Start the source with the requested looping flag, ignoring playback errors.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::Play\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

IA3dSource2	*pSource = m_pSource;

	if (dwFlags & DSBPLAY_LOOPING)
		pSource->Play(A3D_LOOPED);
	else
		pSource->Play(0);

	return (S_OK);
}

/* =============================================================
// Stop()
// (RE) dbg:0x1003a700; rtl:0x10018a40
//
// Stop the source, ignoring its result.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::Stop(void)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::Stop\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	m_pSource->Stop();

	return (S_OK);
}

/* =============================================================
// SetCurrentPosition()
// (RE) dbg:0x1003a820; rtl:0x10018a60
//
// Set the source play position, ignoring its result.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetCurrentPosition(DWORD dwPlay)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetCurrentPosition\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	m_pSource->SetPlayPosition(dwPlay);

	return (S_OK);
}

/* =============================================================
// GetCurrentPosition()
// (RE) dbg:0x1003ada0; rtl:0x10018b30
//
// Read the source play position and report a zero write cursor.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetCurrentPosition(DWORD *pdwPlay, DWORD *pdwWrite)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetCurrentPosition\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	m_pSource->GetPlayPosition(pdwPlay);

	*pdwWrite = 0;

	return (S_OK);
}

/* =============================================================
// GetStatus()
// (RE) dbg:0x1003b030; rtl:0x10018bc0
//
// Read the source status, ignoring the source result.
//
// Returns: E_INVALIDARG for a null output pointer, otherwise S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetStatus(DWORD *pdwStatus)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetStatus\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	if (!pdwStatus)
		return (E_INVALIDARG);

	m_pSource->GetStatus(pdwStatus);

	return (S_OK);
}

/* =============================================================
// CA3dMapperSecBuffer()
// (RE) dbg:0x10039f70
//
// Reference the source and owner and copy the listener scale factors.
// The original leaves both cached volume fields uninitialized.
// =============================================================*/

CA3dMapperSecBuffer::CA3dMapperSecBuffer(IA3dSource2 *pSource,
					 CA3dMapperPrimBuffer *pListener,
					 CA3dRoot *pOwner)
{

	m_cRef = 0;

	m_pSource = pSource;

	pSource->AddRef();

	m_pOwner = pOwner;

	pOwner->AddRef();

	m_pSource->SetDopplerScale(pListener->m_fDopplerFactor);
	m_pSource->SetDistanceModelScale(pListener->m_fRolloffFactor);
}

/* (RE) Compiler-generated base-adjustor destructors: dbg:0x1003a0b0. */

/* =============================================================
// CA3dMapperSecBuffer scalar deleting destructor
// (RE) dbg:0x1003a060; rtl:0x10018880
// =============================================================*/

/* =============================================================
// ~CA3dMapperSecBuffer()
// (RE) dbg:0x1003a0e0
//
// Release the source and owner references.
// =============================================================*/

CA3dMapperSecBuffer::~CA3dMapperSecBuffer(void)
{
	m_pSource->Release();

	m_pOwner->Release();
}

/* =============================================================
// QueryInterface()
// (RE) dbg:0x1003a140; rtl:0x100188c0
//
// Return a referenced interface supported by the mapper.
//
// Returns:
//   S_OK
//   E_NOINTERFACE  an unsupported IID
//   E_INVALIDARG   if the output pointer is null
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::QueryInterface(REFIID riid, void **ppv)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::QueryInterface\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	if (!ppv)
		return (E_INVALIDARG);

	*ppv = NULL;

	if (!memcmp(&riid, &IID_IUnknown, sizeof(IID)))
		*ppv = this;
	else if (!memcmp(&riid, &IID_IDirectSoundBuffer, sizeof(IID)))
		*ppv = this;
	else if (!memcmp(&riid, &IID_IDirectSound3DBuffer, sizeof(IID)))
		*ppv = this ? (void *) (IDirectSound3DBuffer *) this : NULL;

	if (!*ppv)
		return (E_NOINTERFACE);

	((IUnknown *) *ppv)->AddRef();

	return (S_OK);
}

/* =============================================================
// CA3dMapperSecBuffer::AddRef()
// (RE) dbg:0x1003a2a0; rtl:0x10018950
//
// Increment the reference count.
//
// Returns: The count reread after the interlocked increment.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dMapperSecBuffer::AddRef(void)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::AddRef\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	InterlockedIncrement(&m_cRef);

	return (m_cRef);
}

/* =============================================================
// CA3dMapperSecBuffer::Release()
// (RE) dbg:0x1003a330; rtl:0x10018970
//
// Release a reference and delete at zero. Preserve the original race
// from rereading the count after the interlocked decrement.
//
// Returns: The remaining count, or zero after deletion.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dMapperSecBuffer::Release(void)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::Release\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	InterlockedDecrement(&m_cRef);

	if (m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// CommitDeferredSettings()
//
// Flush and clear the owner, ignoring both results.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
CA3dMapperSecBuffer::CommitDeferredSettings(void)
{
	m_pOwner->Flush();
	m_pOwner->Clear();

	return (S_OK);
}

/* =============================================================
// SetFrequency()
// (RE) dbg:0x1003a950; rtl:0x10018a80
//
// Convert hertz to a pitch multiplier and commit the change.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetFrequency(DWORD dwFrequency)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetFrequency\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

IA3dSource2	*pSource = m_pSource;
WAVEFORMATEX	wfx;

	pSource->GetAudioFormat(&wfx);

	pSource->SetPitch((A3DVAL) ((double) dwFrequency /
				    (double) wfx.nSamplesPerSec));

	CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetVolume()
// (RE) dbg:0x1003abb0; rtl:0x10018ae0
//
// Convert hundredths of a decibel to gain, commit and cache the volume.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetVolume(LONG lVolume)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetVolume\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

IA3dSource2	*pSource = m_pSource;

	/* (RE) Preserve the float coefficient before widening to double. */
	pSource->SetGain((A3DVAL) pow(10.0, (double) lVolume * A3D_MAPPER_VOLUME_EXPONENT_SCALE));

	CommitDeferredSettings();

	m_lVolume = lVolume;

	return (S_OK);
}

/* =============================================================
// GetFrequency()
// (RE) dbg:0x1003aec0; rtl:0x10018b70
//
// Convert source pitch to hertz, truncating to an integer.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetFrequency(DWORD *pdwFrequency)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetFrequency\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

IA3dSource2	*pSource = m_pSource;
WAVEFORMATEX	wfx;
A3DVAL		fPitch;

	pSource->GetAudioFormat(&wfx);

	pSource->GetPitch(&fPitch);

	*pdwFrequency = (DWORD) ((double) wfx.nSamplesPerSec * fPitch);

	return (S_OK);
}

/* =============================================================
// SetConeAngles()
// (RE) dbg:0x1003b210; rtl:0x10018c20
//
// Set half-angles from the DirectSound full cone angles; commit for
// DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetConeAngles(DWORD dwInside, DWORD dwOutside, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetConeAngles\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

IA3dSource2	*pSource = m_pSource;
A3DVAL		fInside;
A3DVAL		fOutside;
A3DVAL		fOutsideGain;

	pSource->GetCone(&fInside, &fOutside, &fOutsideGain);

	pSource->SetCone((A3DVAL) ((double) dwInside * 0.5),
			 (A3DVAL) ((double) dwOutside * 0.5),
			 fOutsideGain);

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetAllParameters()
// (RE) dbg:0x1003b160; rtl:0x10018bf0
//
// Preserve the original ignored parameter structure; commit only for
// DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetAllParameters(LPCDS3DBUFFER pcDs3db, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetAllParameters\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetMaxDistance()
// (RE) dbg:0x1003b6a0; rtl:0x10018d80
//
// Set the maximum source distance; commit for DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetMaxDistance(D3DVALUE fMax, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetMaxDistance\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

IA3dSource2	*pSource = m_pSource;
A3DVAL		fMin;
A3DVAL		fOld;
DWORD		dwMode;

	pSource->GetMinMaxDistance(&fMin, &fOld, &dwMode);

	pSource->SetMinMaxDistance(fMin, fMax, dwMode);

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetMinDistance()
// (RE) dbg:0x1003b810; rtl:0x10018df0
//
// Set the minimum source distance; commit for DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetMinDistance(D3DVALUE fMin, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetMinDistance\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

IA3dSource2	*pSource = m_pSource;
A3DVAL		fOld;
A3DVAL		fMax;
DWORD		dwMode;

	pSource->GetMinMaxDistance(&fOld, &fMax, &dwMode);

	pSource->SetMinMaxDistance(fMin, fMax, dwMode);

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// GetMaxDistance()
// (RE) dbg:0x1003bf20; rtl:0x10018f30
//
// Read the maximum source distance.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetMaxDistance(D3DVALUE *pfMax)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetMaxDistance\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

A3DVAL	fMin;
A3DVAL	fMax;
DWORD	dwMode;

	m_pSource->GetMinMaxDistance(&fMin, &fMax, &dwMode);

	*pfMax = fMax;

	return (S_OK);
}

/* =============================================================
// GetMinDistance()
// (RE) dbg:0x1003bfd0; rtl:0x10018f70
//
// Read the minimum source distance.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetMinDistance(D3DVALUE *pfMin)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetMinDistance\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

A3DVAL	fMin;
A3DVAL	fMax;
DWORD	dwMode;

	m_pSource->GetMinMaxDistance(&fMin, &fMax, &dwMode);

	*pfMin = fMin;

	return (S_OK);
}

typedef int A3dMapperSecBufferCheck[(sizeof(CA3dMapperSecBuffer) == 0x1C) ? 1 : -1];

/* =============================================================
// SetConeOutsideVolume()
// (RE) dbg:0x1003b510; rtl:0x10018d00
//
// Convert outside volume to cone gain and cache it; commit for
// DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetConeOutsideVolume(LONG lVolume, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetConeOutsideVolume\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

IA3dSource2	*pSource = m_pSource;
A3DVAL		fGain;
A3DVAL		fInside;
A3DVAL		fOutside;
A3DVAL		fOutsideGain;

	/* (RE) Preserve the float coefficient before widening to double. */
	fGain = (A3DVAL) pow(10.0, (double) lVolume * A3D_MAPPER_VOLUME_EXPONENT_SCALE);

	pSource->GetCone(&fInside, &fOutside, &fOutsideGain);

	pSource->SetCone(fInside, fOutside, fGain);

	m_lConeOutsideVolume = lVolume;

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetMode()
// (RE) dbg:0x1003b980; rtl:0x10018e60
//
// Preserve the original ignored mode; commit only for DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetMode(DWORD dwMode, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetMode\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// GetCaps()
// (RE) dbg:0x1003ad10; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetCaps(LPDSBCAPS pCaps)			{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetCaps\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetFormat()
// (RE) dbg:0x1003ae40; rtl:0x10018b60
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetFormat(LPWAVEFORMATEX pwfx, DWORD cb, DWORD *pcb)
										{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetFormat\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetPan()
// (RE) dbg:0x1003afb0; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetPan(LONG *plPan)				{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetPan\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// Initialize()
// (RE) dbg:0x1003a420; rtl:0x1001b380
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::Initialize(LPDIRECTSOUND pDs, LPCDSBUFFERDESC pDesc)
										{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::Initialize\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// SetFormat()
// (RE) dbg:0x1003a8c0; rtl:0x100034f0
//
// Original stub; perform no operation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetFormat(LPCWAVEFORMATEX pwfx)		{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetFormat\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// SetPan()
// (RE) dbg:0x1003aae0; rtl:0x100034f0
//
// Original stub; perform no operation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetPan(LONG lPan)				{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetPan\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// Restore()
// (RE) dbg:0x1003a790; rtl:0x1001b1c0
//
// Original stub; perform no operation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::Restore(void)					{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::Restore\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetAllParameters()
// (RE) dbg:0x1003bd20; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetAllParameters(LPDS3DBUFFER pDs3db)		{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetAllParameters\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetConeAngles()
// (RE) dbg:0x1003bda0; rtl:0x1001b380
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetConeAngles(LPDWORD pdwIn,
						LPDWORD pdwOut)			{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetConeAngles\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetConeOrientation()
// (RE) dbg:0x1003be20; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetConeOrientation(D3DVECTOR *pvOrientation)	{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetConeOrientation\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetConeOutsideVolume()
// (RE) dbg:0x1003bea0; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetConeOutsideVolume(LPLONG plVolume)		{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetConeOutsideVolume\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetMode()
// (RE) dbg:0x1003c080; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetMode(LPDWORD pdwMode)			{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetMode\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetPosition()
// (RE) dbg:0x1003c100; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetPosition(D3DVECTOR *pvPosition)		{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetPosition\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// GetVelocity()
// (RE) dbg:0x1003c180; rtl:0x100034f0
//
// Original stub; ignore the arguments and leave outputs untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::GetVelocity(D3DVECTOR *pvVelocity)		{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::GetVelocity\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	return (S_OK);
}

/* =============================================================
// SetPosition()
// (RE) dbg:0x1003ba80; rtl:0x10018e90
//
// Set the position with z negated; commit for
// DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetPosition\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	m_pSource->SetPosition3f(x, y, -z);

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetVelocity()
// (RE) dbg:0x1003bbd0; rtl:0x10018ee0
//
// Set the velocity with z negated; commit for
// DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetVelocity\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	m_pSource->SetVelocity3f(x, y, -z);

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetConeOrientation()
// (RE) dbg:0x1003b3c0; rtl:0x10018cb0
//
// Preserve the original direction-vector argument to SetOrientationAngles3f,
// with z negated; commit for DS3D_IMMEDIATE.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dMapperSecBuffer::SetConeOrientation(D3DVALUE x, D3DVALUE y, D3DVALUE z,
					DWORD dwApply)
{
	if (g_pMapperDebugLog)
	{
	char	szBuffer[128];

		sprintf(szBuffer, "\n%x:    %s", (DWORD) this, "CA3dMapperSecBuffer::SetConeOrientation\n");
		fprintf(g_pMapperDebugLog, szBuffer);
		fflush(g_pMapperDebugLog);
	}

	m_pSource->SetOrientationAngles3f(x, y, -z);

	if (!dwApply)
		CommitDeferredSettings();

	return (S_OK);
}
