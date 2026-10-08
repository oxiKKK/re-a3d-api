/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dalinfo.cpp
 *
 * Implements the resource manager's records for a DAL device and its
 * reusable audio buffers. DalInfo caches interfaces, capabilities, mode
 * settings and allocated buffers, and selects idle entries for reuse or
 * expiry.
 *
 * DalBufferInfo tracks a buffer binding, queued audio and fill position.
 * Its routines convert source PCM to the device format, refill streaming
 * storage and apply the device's scale-hack compatibility behavior.
 * ResMan uses these records to assign voices without putting
 * device-specific details in every source buffer.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "dalinfo.h"
#include "dal_d2d.h"
#include "dal_d3d.h"
#include "Plex.h"
#include "rmstatbuffer.h"
#include "rmstreambuffer.h"
#include "softmix.h"

#include <stdio.h>
#include <string.h>

/* Refresh byte cap  */
#define A3D_DAL_MAX_REFILL_BYTES 24576

/* Buffer bookkeeping and PCM conversion */
#define A3D_DAL_AUDIO_END_UNSET         (-1)
#define A3D_DAL_BASE_PROBE_BYTES        4
#define A3D_DAL_PCM8_SIGN_BIT           0x80

/* Buffer kinds and local status values */
#define A3D_DALBUFFER_KIND_STATIC       0
#define A3D_DALBUFFER_KIND_STREAMING    2
#define A3D_DALBUFFER_STATE_NOT_PLAYING 1
#define A3D_DALBUFFER_STATE_PLAYING     2

/* Alpha-to-gain adjustment */
#define A3D_DAL_ALPHA_GAIN_THRESHOLD            0.4f
#define A3D_DAL_ALPHA_GAIN_SLOPE                1.67f
#define A3D_DAL_ALPHA_GAIN_INTERCEPT            1.67f
#define A3D_DAL_CAP_ALPHA_GAIN_ADJUSTMENT       0x40000

/* =============================================================
// DalInfo()
// (RE) rtl:0x10020D50; dbg:0x10053BD0
//
// Reference the DAL and initialize its mode and buffer lists.
// =============================================================*/

DalInfo::DalInfo(IA3dDal *pIA3dDal, const DALMODEDESC *pModeDesc)
	: m_listDalBuffers(A3D_LIST_DEFAULT_BLOCK_SIZE),
	  m_listStaticBuffers(A3D_LIST_DEFAULT_BLOCK_SIZE)
{
	m_pIA3dDal = pIA3dDal;

	m_pIA3dDal->AddRef();

	m_sDalModeDesc = *pModeDesc;

	m_cVoices = 0;
	m_pDalDS  = NULL;
	m_pIA3d2  = NULL;

	m_fScaleHackDisabled    = FALSE;
	m_dwAlphaGainAdjustment = 0;
}

/* =============================================================
// ~DalInfo()
// (RE) rtl:0x10020DF0; dbg:0x10053CC0
//
// Delete cached buffers and release the DAL interfaces.
// =============================================================*/

DalInfo::~DalInfo(void)
{
DalBufferInfo	*pDalBufferInfo;

	while ((pDalBufferInfo =
		(DalBufferInfo *) m_listDalBuffers.RemoveHead()) != NULL)
	{
		ASSERT((pDalBufferInfo != 0 &&
		        !IsBadReadPtr(pDalBufferInfo, sizeof(DalBufferInfo))));

		delete pDalBufferInfo;
	}

	while ((pDalBufferInfo =
		(DalBufferInfo *) m_listStaticBuffers.RemoveHead()) != NULL)
	{
		ASSERT((pDalBufferInfo != 0 &&
		        !IsBadReadPtr(pDalBufferInfo, sizeof(DalBufferInfo))));

		delete pDalBufferInfo;
	}

	if (m_pIA3d2)
	{
		m_pIA3d2->Release();

		m_pIA3d2 = NULL;
	}

	if (m_pDalDS)
	{
		m_pDalDS->Release();

		m_pDalDS = NULL;
	}

	if (m_pIA3dDal)
	{
		m_pIA3dDal->Release();

		m_pIA3dDal = NULL;
	}
}

/* =============================================================
// GetNumActiveDalBuffers()
// (RE) dbg:0x10053f60
//
// Count playing entries in the streaming/duplicate buffer list.
//
// Returns: The number of entries whose state has bit 1 set.
// =============================================================*/

int
DalInfo::GetNumActiveDalBuffers(void)
{
DalBufferInfo  *pDalBufferInfo;
POSITION        pos;
DWORD           dwState;
int             cActive;

	cActive = 0;

	pos = m_listDalBuffers.GetHeadPosition();

	while (pos)
	{
		pDalBufferInfo = (DalBufferInfo *) m_listDalBuffers.GetNext(pos);

		ASSERT((pDalBufferInfo != 0 &&
		        !IsBadReadPtr(pDalBufferInfo, sizeof(DalBufferInfo))));

		pDalBufferInfo->GetBufferState(&dwState);

		if (dwState & A3D_DALBUFFER_STATE_PLAYING)
			cActive++;
	}

	return (cActive);
}

/* =============================================================
// GetNumActiveStaticBuffers()
// (RE) dbg:0x10054040
//
// Count playing entries in the static buffer list.
//
// Returns: The number of entries whose state has bit 1 set.
// =============================================================*/

int
DalInfo::GetNumActiveStaticBuffers(void)
{
DalBufferInfo  *pDalBufferInfo;
POSITION        pos;
DWORD           dwState;
int             cActive;

	cActive = 0;

	pos = m_listStaticBuffers.GetHeadPosition();

	while (pos)
	{
		pDalBufferInfo = (DalBufferInfo *) m_listStaticBuffers.GetNext(pos);

		ASSERT((pDalBufferInfo != 0 &&
		        !IsBadReadPtr(pDalBufferInfo, sizeof(DalBufferInfo))));

		pDalBufferInfo->GetBufferState(&dwState);

		if (dwState & A3D_DALBUFFER_STATE_PLAYING)
			cActive++;
	}

	return (cActive);
}

/* =============================================================
// GetNumDalBuffers()
// (RE) dbg:0x1005B8F0; thunk dbg:0x10004359
//
// Count streaming and duplicate buffer entries.
//
// Returns: The list count.
// =============================================================*/

int
DalInfo::GetNumDalBuffers(void)
{
	return (m_listDalBuffers.GetCount());
}

/* =============================================================
// GetNumStaticBuffers()
// (RE) dbg:0x1005B920
//
// Count static buffer entries.
//
// Returns: The list count.
// =============================================================*/

int
DalInfo::GetNumStaticBuffers(void)
{
	return (m_listStaticBuffers.GetCount());
}

/* =============================================================
// GetName()
// (RE) dbg:0x1005B8D0
//
// Read the packed DAL name.
//
// Returns: The three-character name tag in a DWORD.
// =============================================================*/

DWORD
DalInfo::GetName(void)
{
	return (m_sDalModeDesc.dwName);
}

/* =============================================================
// GetNumAvailable()
// (RE) rtl:0x10020F90; dbg:0x10054120; thunk dbg:0x100043D1
//
// Query available hardware 3D voices.
//
// Returns: The free voice count, or zero if GetCaps fails.
// =============================================================*/

DWORD
DalInfo::GetNumAvailable(void)
{
DSCAPS	caps;
HRESULT	hr;

	ZeroMemory(&caps, sizeof(caps));

	caps.dwSize = sizeof(DSCAPS);

	hr = m_pDalDS->GetCaps(&caps);

	if (SUCCEEDED(hr))
		return (caps.dwFreeHw3DAllBuffers);

	DBGSTR("DalInfo::GetNumAvailable - Failed querying for DirectSound caps.\n");

	return (0);
}

/* =============================================================
// Init()
// (RE) rtl:0x10020FD0; dbg:0x100541c0
//
// Query the DAL interfaces and capabilities and disable its scale hack
// when requested by the mode.
//
// Returns: S_OK; a failed QueryInterface, ReadVoiceCount or DisableScaleHack
//          result. A successful query with a null pointer also returns its
//          success code.
// =============================================================*/

HRESULT
DalInfo::Init(void)
{
HRESULT	hr;

	ASSERT((m_pIA3dDal != 0 && !IsBadReadPtr(m_pIA3dDal, sizeof(IA3dDal))));

	m_pDalDS = NULL;

	VERIFY(SUCCEEDED(hr = m_pIA3dDal->QueryInterface(IID_IDirectSound, (void **) &m_pDalDS)));

	if (FAILED(hr) || !m_pDalDS)
		return (hr);

	ASSERT((m_pDalDS != 0 && !IsBadReadPtr(m_pDalDS, sizeof(IDirectSound))));

	m_pIA3d2 = NULL;

	VERIFY(SUCCEEDED(hr = m_pIA3dDal->QueryInterface(IID_IA3d2, (void **) &m_pIA3d2)));

	if (FAILED(hr) || !m_pIA3d2)
	{
		if (m_pDalDS)
		{
			m_pDalDS->Release();

			m_pDalDS = NULL;
		}

		return (hr);
	}

	ASSERT((m_pIA3d2 != 0 && !IsBadReadPtr(m_pIA3d2, sizeof(IID_IA3d2))));

	hr = ReadVoiceCount(&m_cVoices);

	if (FAILED(hr))
	{
		if (m_pIA3d2)
		{
			m_pIA3d2->Release();

			m_pIA3d2 = NULL;
		}

		if (m_pDalDS)
		{
			m_pDalDS->Release();

			m_pDalDS = NULL;
		}

		return (hr);
	}

	if (m_sDalModeDesc.dwFlags & A3D_DALMODE_DISABLE_SCALE_HACK)
	{
		hr = DisableScaleHack();

		if (FAILED(hr))
		{
			if (m_pIA3d2)
			{
				m_pIA3d2->Release();

				m_pIA3d2 = NULL;
			}

			if (m_pDalDS)
			{
				m_pDalDS->Release();

				m_pDalDS = NULL;
			}

			return (hr);
		}
	}

	ReadAlphaGainAdjustment();

	return (S_OK);
}

/* =============================================================
// ReadAlphaGainAdjustment()
// (RE) dbg:0x100545A0; inlined in Retail Init: rtl:0x100210C2
//
// Cache alpha-to-gain adjustment support // Preserve the original uninitialized
// extended caps input.
//
// Returns: S_OK, or a failed GetDalCaps result.
// =============================================================*/

HRESULT
DalInfo::ReadAlphaGainAdjustment(void)
{
A3DDALCAPS564	caps;
DWORD		cbCaps;
HRESULT		hr;

	cbCaps = 564;

	hr = GetDalCaps(&caps, &cbCaps);

	if (FAILED(hr))
		return (hr);

	m_dwAlphaGainAdjustment = (caps.caps.dwFeatureFlags & A3D_DAL_CAP_ALPHA_GAIN_ADJUSTMENT) != 0;

	return (S_OK);
}

/* =============================================================
// GetDalCaps()
// (RE) dbg:0x10054640; rtl:0x10021130
//
// Read DAL capabilities and apply the Vortex control-panel override.
//
// Returns: S_OK, or a failed IA3dDal::GetA3dCaps result.
// =============================================================*/

HRESULT
DalInfo::GetDalCaps(A3DDALCAPS564 *pCaps, DWORD *pcbSize)
{
HKEY	hKey;
DWORD	dwData;
DWORD	cbData;
HRESULT	hr;

	hr = m_pIA3dDal->GetA3dCaps(&pCaps->caps, pcbSize);

	if (FAILED(hr))
		return (hr);

	if (pCaps->caps.wVersion < 0x120)
	{
		if (pCaps->caps.wDeviceType == 0x106)
		{
			pCaps->caps.dwFeatureFlags |= A3D_DAL_CAP_ALPHA_GAIN_ADJUSTMENT;
		}
		else if (pCaps->caps.wDeviceType == 3 ||
			 pCaps->caps.wDeviceType == A3DCAPS_DEVICE_TYPE_AU8820 ||
			 pCaps->caps.wDeviceType == 0x10B ||
			 pCaps->caps.wDeviceType == 0x105)
		{
			pCaps->caps.dwFeatureFlags |= A3D_DAL_CAP_ALPHA_GAIN_ADJUSTMENT;

			if (!(pCaps->caps.wDeviceType == 0x10B
			    ? RegOpenKeyExA(HKEY_LOCAL_MACHINE,
				"Software\\Aureal\\Vortex\\AU8810\\ControlPanel",
				0, KEY_READ, &hKey)
			    : RegOpenKeyExA(HKEY_LOCAL_MACHINE,
				"Software\\Aureal\\Vortex\\ControlPanel",
				0, KEY_READ, &hKey)))
			{
				cbData = 4;

				if ((!RegQueryValueExA(hKey, "A3DMode", 0, 0,
						(LPBYTE) &dwData, &cbData) ||
				     !RegQueryValueExA(hKey, "A3DType", 0, 0,
						(LPBYTE) &dwData, &cbData)) &&
				    dwData == 1 &&
				    pCaps->wUnknown_0x62 == 13)
				{
					pCaps->caps.dwFeatureFlags &= ~A3D_DAL_CAP_ALPHA_GAIN_ADJUSTMENT;
				}

				if (hKey)
					RegCloseKey(hKey);
			}
		}
	}

	return (S_OK);
}

/* =============================================================
// DisableScaleHack()
// (RE) dbg:0x10054850; inlined in Retail Init at rtl:0x1002107F
//
// Disable an exposed, enabled scale hack and remember the change.
// The original leaves the query output unset before QueryInterface.
//
// Returns: S_OK; failures are ignored or asserted.
// =============================================================*/

HRESULT
DalInfo::DisableScaleHack(void)
{
IA3dScaleHack  *lpIA3dScaleHack;
DWORD           dwState;
HRESULT         hr;

	hr = m_pIA3dDal->QueryInterface(IID_IA3dScaleHack,
					(void **) &lpIA3dScaleHack);

	if (SUCCEEDED(hr) && lpIA3dScaleHack)
	{
		lpIA3dScaleHack->GetScaleHackState(&dwState);

		if (dwState == 1)
		{
			VERIFY(SUCCEEDED(hr = lpIA3dScaleHack->ScaleHackDisable()));

			m_fScaleHackDisabled = TRUE;
		}
	}

	if (lpIA3dScaleHack)
	{
		lpIA3dScaleHack->Release();

		lpIA3dScaleHack = NULL;
	}

	return (S_OK);
}

/* =============================================================
// ReadVoiceCount()
// (RE) rtl:0x10021250; dbg:0x10054950
//
// Store the larger DirectSound/DAL voice limit in m_cVoices, ignoring
// the output argument as in the original.
//
// Returns: S_OK, or a failed DirectSound GetCaps result.
// =============================================================*/

HRESULT
DalInfo::ReadVoiceCount(DWORD *pdwVoices)
{
DSCAPS		caps;
A3DDALCAPS564	desc;
DWORD		cbDesc;
DWORD		dwHwVoices;
DWORD		dwDescVoices;
HRESULT		hr;

	ZeroMemory(&caps, sizeof(caps));

	caps.dwSize = sizeof(DSCAPS);

	hr = m_pDalDS->GetCaps(&caps);

	if (FAILED(hr))
	{
		DBGSTR("DalInfo::DalInfo - Failed querying for DirectSound caps.\n");

		return (hr);
	}

	if (m_sDalModeDesc.dwVoiceCountMode == A3D_DAL_VOICE_COUNT_3D)
		dwHwVoices = caps.dwMaxHw3DAllBuffers;
	else
		dwHwVoices = caps.dwMaxHwMixingAllBuffers;

	cbDesc       = 564;
	dwDescVoices = 0;

	ZeroMemory(&desc, sizeof(desc));

	hr = GetDalCaps(&desc, &cbDesc);

	if (SUCCEEDED(hr))
		dwDescVoices = desc.caps.wMaxBuffers;

	if (dwDescVoices <= dwHwVoices)
		m_cVoices = dwHwVoices;
	else
		m_cVoices = dwDescVoices;

	return (S_OK);
}

/* =============================================================
// SetCooperativeLevel()
// (RE) dbg:0x10054ad0
//
// Set the DAL DirectSound cooperative level.
//
// Returns: The DirectSound SetCooperativeLevel result.
// =============================================================*/

HRESULT
DalInfo::SetCooperativeLevel(HWND hWnd, DWORD dwLevel)
{
	return (m_pDalDS->SetCooperativeLevel(hWnd, dwLevel));
}

/* =============================================================
// Reap()
// (RE) rtl:0x10021300; dbg:0x10054b10; thunk dbg:0x1000187a
//
// Delete idle streaming/duplicate entries past their timeout.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
DalInfo::Reap(DWORD dwNow)
{
DalBufferInfo  *pDalBufferInfo;
POSITION        pos;
POSITION        posPrev;

	pos = m_listDalBuffers.GetHeadPosition();

	while (pos)
	{
		posPrev = pos;

		pDalBufferInfo = (DalBufferInfo *) m_listDalBuffers.GetNext(pos);

		ASSERT((pDalBufferInfo != 0 &&
		        !IsBadReadPtr(pDalBufferInfo, sizeof(DalBufferInfo))));

		if (pDalBufferInfo->IsIdle() && pDalBufferInfo->IsExpired(dwNow))
		{
			m_listDalBuffers.RemoveAt(posPrev);

			if (pDalBufferInfo)
				delete pDalBufferInfo;
		}
	}

	return (S_OK);
}

/* =============================================================
// CreateStaticDalBuffer()
// (RE) rtl:0x100213F0; dbg:0x10054c40
//
// Create and cache a hardware static buffer, updating the caller flags.
//
// Returns: S_OK; E_OUTOFMEMORY at the voice limit or on wrapper allocation
//          failure; a failed CreateSoundBuffer or ApplyScaleHack result.
// =============================================================*/

HRESULT
DalInfo::CreateStaticDalBuffer(DSBUFFERDESC1 *pDesc,
			       DalBufferInfo **ppDalBufferInfo)
{
DalBufferInfo          *pDalBufferInfo;
IDirectSoundBuffer     *pDSBuffer;
HRESULT                 hr;

	if (m_listStaticBuffers.GetCount() >= (int) m_cVoices)
		return (E_OUTOFMEMORY);

	pDesc->dwFlags |= DSBCAPS_LOCHARDWARE;

	/* Original leaves the output unset before CreateSoundBuffer. */
	hr = m_pDalDS->CreateSoundBuffer((LPCDSBUFFERDESC) pDesc,
					 (LPDIRECTSOUNDBUFFER *) &pDSBuffer, NULL);

	if (FAILED(hr))
	{
		DBGSTR("DalInfo::CreateStaticDalBuffer() - Failed creating IA3dDal Buffer.\n");

		return (hr);
	}

	ASSERT((pDSBuffer != 0 &&
	        !IsBadReadPtr(pDSBuffer, sizeof(IDirectSoundBuffer))));

	pDalBufferInfo = new DalBufferInfo(this, pDSBuffer, &m_sDalModeDesc, A3D_DALBUFFER_KIND_STATIC, 0);

	if (!pDalBufferInfo)
	{
		DBGSTR("DalInfo::CreateStaticDalBuffer() - Failed to allocate memory for DalBufferInfo class.\n");

		if (pDSBuffer)
		{
			pDSBuffer->Release();

			pDSBuffer = NULL;
		}

		return (E_OUTOFMEMORY);
	}

	if (pDSBuffer)
	{
		pDSBuffer->Release();

		pDSBuffer = NULL;
	}

	if (m_fScaleHackDisabled)
	{
		hr = pDalBufferInfo->ApplyScaleHack();

		if (FAILED(hr))
		{
			if (pDalBufferInfo)
				delete pDalBufferInfo;

			return (hr);
		}
	}

	ASSERT((pDalBufferInfo != 0 &&
	        !IsBadReadPtr(pDalBufferInfo, sizeof(DalBufferInfo))));

	m_listStaticBuffers.AddTail(pDalBufferInfo);

	if (ppDalBufferInfo)
		*ppDalBufferInfo = pDalBufferInfo;

	return (S_OK);
}

/* =============================================================
// CreateDalBuffer()
// (RE) rtl:0x10021600; dbg:0x10054f90
//
// Duplicate and cache a source entry with its captured buffer base.
//
// Returns: S_OK; E_OUTOFMEMORY at the voice limit, on duplication failure or on
//          wrapper allocation failure; a failed ApplyScaleHack result.
// =============================================================*/

HRESULT
DalInfo::CreateDalBuffer(DalBufferInfo *pSource, DalBufferInfo **ppDalBufferInfo)
{
DalBufferInfo          *pDalBufferInfo;
IDirectSoundBuffer     *pDSBuffer;
DWORD                   dwBase;
HRESULT                 hr;

	if (m_listDalBuffers.GetCount() >= (int) m_cVoices)
		return (E_OUTOFMEMORY);

	/* The original leaves the duplication output unset. */
	hr = m_pDalDS->DuplicateSoundBuffer(pSource->GetDSBuffer(),
					    (LPDIRECTSOUNDBUFFER *) &pDSBuffer);

	if (FAILED(hr))
	{
		DBGSTR("DalInfo::CreateDalBuffer() - Failed to allocate memory for DalBufferInfo class.\n");

		return (E_OUTOFMEMORY);
	}

	pSource->GetBufferBase(&dwBase);

	ASSERT((pDSBuffer != 0 &&
	        !IsBadReadPtr(pDSBuffer, sizeof(IDirectSoundBuffer))));

	pDalBufferInfo = new DalBufferInfo(this, pDSBuffer, &m_sDalModeDesc, A3D_DALBUFFER_KIND_STATIC,
					   dwBase);

	if (!pDalBufferInfo)
	{
		DBGSTR("DalInfo::CreateDalBuffer() - Failed to allocate memory for DalBufferInfo class.\n");

		if (pDSBuffer)
		{
			pDSBuffer->Release();

			pDSBuffer = NULL;
		}

		return (E_OUTOFMEMORY);
	}

	if (pDSBuffer)
	{
		pDSBuffer->Release();

		pDSBuffer = NULL;
	}

	if (m_fScaleHackDisabled)
	{
		hr = pDalBufferInfo->ApplyScaleHack();

		if (FAILED(hr))
		{
			if (pDalBufferInfo)
				delete pDalBufferInfo;

			return (hr);
		}
	}

	ASSERT((pDalBufferInfo != 0 &&
	        !IsBadReadPtr(pDalBufferInfo, sizeof(DalBufferInfo))));

	/* Original checks the streaming count but appends to the static list. */
	m_listStaticBuffers.AddTail(pDalBufferInfo);

	if (ppDalBufferInfo)
		*ppDalBufferInfo = pDalBufferInfo;

	return (S_OK);
}

/* =============================================================
// GetBufferBase()
// (RE) dbg:0x10055310
//
// Read the captured audio base.
//
// Returns: S_OK here. The original return type is unresolved; Debug leaves the
//          output pointer in EAX.
// =============================================================*/

HRESULT
DalBufferInfo::GetBufferBase(DWORD *pdwBase)
{
	*pdwBase = m_dwBufferBase;

	return (S_OK);
}

/* =============================================================
// SetA3dSuperCtrl()
// (RE) rtl:0x10022620; dbg:0x10057850
//
// Set the source frequency factor and submit the control block with
// temporary gain/equalization adjustments when the DAL handles occlusion.
//
// Returns: E_FAIL without a DAL buffer; a failed owner GetFrequency result;
//          otherwise the DAL SetA3dSuperCtrl result.
// =============================================================*/

HRESULT
DalBufferInfo::SetA3dSuperCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlSuper, DWORD dwSize)
{
HRESULT	hr;

	if (!m_pIDalBuffer)
	{
		DBGSTR("DalBufferInfo::SetA3dSuperCtrl - m_pIDalBuffer is NULL \n");

		return (E_FAIL);
	}

	if (m_nKind == A3D_DALBUFFER_KIND_STREAMING)
	{
		DWORD	dwFrequency;

		hr = ((IDirectSoundBuffer *) (ResManBuffer *) m_dwOwnerBuffer)->
			GetFrequency(&dwFrequency);

		if (FAILED(hr))
		{
			DBGSTR("DalBufferInfo::SetA3dSuperCtrl - GetFrequency failed\n");

			return (hr);
		}

		lpA3dCtrlSuper->fFreqFactor = (A3DVAL)
			((double) dwFrequency * lpA3dCtrlSuper->fPitch /
			 (double) (int) m_sDalModeDesc.dwSampleRate);
	}
	else
	{
		lpA3dCtrlSuper->fFreqFactor = lpA3dCtrlSuper->fPitch;
	}

	if (!m_pDalInfo->GetAlphaGainAdjustment())
		return (m_pIDalBuffer->SetA3dSuperCtrl(lpA3dCtrlSuper, dwSize));

	{
		A3DVAL	fLeftGain  = lpA3dCtrlSuper->LeftEar.fGain;
		A3DVAL	fRightGain = lpA3dCtrlSuper->RightEar.fGain;
		A3DVAL	fAlpha     = lpA3dCtrlSuper->fAlpha;

		if (lpA3dCtrlSuper->fAlpha > A3D_DAL_ALPHA_GAIN_THRESHOLD)
		{
			A3DVAL	fScale = -A3D_DAL_ALPHA_GAIN_SLOPE * lpA3dCtrlSuper->fAlpha
					 + A3D_DAL_ALPHA_GAIN_INTERCEPT;

			lpA3dCtrlSuper->LeftEar.fGain  *= fScale;
			lpA3dCtrlSuper->RightEar.fGain *= fScale;
		}

		lpA3dCtrlSuper->fAlpha = 0.0f;

		hr = m_pIDalBuffer->SetA3dSuperCtrl(lpA3dCtrlSuper, dwSize);

		lpA3dCtrlSuper->LeftEar.fGain  = fLeftGain;
		lpA3dCtrlSuper->RightEar.fGain = fRightGain;
		lpA3dCtrlSuper->fAlpha         = fAlpha;
	}

	return (hr);
}

/* =============================================================
// CreateDalBuffer()
// (RE) rtl:0x10021820; dbg:0x10055340; thunk dbg:0x10001929
//
// Create and cache a streaming buffer from the DAL mode descriptor.
//
// Returns: S_OK; E_OUTOFMEMORY at the voice limit or on wrapper allocation
//          failure; a failed CreateSoundBuffer or ApplyScaleHack result.
// =============================================================*/

HRESULT
DalInfo::CreateDalBuffer(DalBufferInfo **ppDalBufferInfo)
{
DSBUFFERDESC1           desc;
WAVEFORMATEX            wfx;
DalBufferInfo          *pDalBufferInfo;
IDirectSoundBuffer     *pDSBuffer;
HRESULT                 hr;

	if (m_listDalBuffers.GetCount() >= (int) m_cVoices)
		return (E_OUTOFMEMORY);

	ZeroMemory(&wfx, sizeof(WAVEFORMATEX));

	wfx.wFormatTag      = WAVE_FORMAT_PCM;
	wfx.nChannels       = (WORD) m_sDalModeDesc.dwChannels;
	wfx.nSamplesPerSec  = m_sDalModeDesc.dwSampleRate;
	wfx.wBitsPerSample  = (WORD) m_sDalModeDesc.dwBitsPerSample;
	wfx.nBlockAlign     = (WORD) (wfx.wBitsPerSample / 8 * wfx.nChannels);
	wfx.nAvgBytesPerSec = wfx.nBlockAlign * wfx.nSamplesPerSec;

	ZeroMemory(&desc, sizeof(DSBUFFERDESC1));

	desc.dwSize        = sizeof(DSBUFFERDESC1);
	desc.dwFlags       = m_sDalModeDesc.dwBufferFlags;
	desc.dwBufferBytes = m_sDalModeDesc.dwStreamingBufferSize;
	desc.dwReserved    = 0;
	desc.lpwfxFormat   = &wfx;

	/* The original leaves the creation output unset. */
	hr = m_pDalDS->CreateSoundBuffer((LPCDSBUFFERDESC) &desc,
					 (LPDIRECTSOUNDBUFFER *) &pDSBuffer, NULL);

	if (FAILED(hr))
		return (hr);

	ASSERT((pDSBuffer != 0 &&
	        !IsBadReadPtr(pDSBuffer, sizeof(IDirectSoundBuffer))));

	pDalBufferInfo = new DalBufferInfo(this, pDSBuffer, &m_sDalModeDesc, A3D_DALBUFFER_KIND_STREAMING, 0);

	if (!pDalBufferInfo)
	{
		DBGSTR("DalInfo::CreateDalBuffer() - Failed to allocate memory for DalBufferInfo class.\n");

		if (pDSBuffer)
		{
			pDSBuffer->Release();

			pDSBuffer = NULL;
		}

		return (E_OUTOFMEMORY);
	}

	if (pDSBuffer)
	{
		pDSBuffer->Release();

		pDSBuffer = NULL;
	}

	if (m_fScaleHackDisabled)
	{
		hr = pDalBufferInfo->ApplyScaleHack();

		if (FAILED(hr))
		{
			if (pDalBufferInfo)
				delete pDalBufferInfo;

			return (hr);
		}
	}

	ASSERT((pDalBufferInfo != 0 &&
	        !IsBadReadPtr(pDalBufferInfo, sizeof(DalBufferInfo))));

	m_listDalBuffers.AddTail(pDalBufferInfo);

	if (ppDalBufferInfo)
		*ppDalBufferInfo = pDalBufferInfo;

	return (S_OK);
}

/* =============================================================
// FindIdle()
// (RE) rtl:0x10021AA0; dbg:0x10055710
//
// Find the first idle streaming/duplicate entry, or clear the output.
//
// Returns: S_OK, whether or not an entry was found.
// =============================================================*/

HRESULT
DalInfo::FindIdle(DalBufferInfo **ppDalBufferInfo)
{
DalBufferInfo  *pDalBufferInfo;
POSITION        pos;

	ASSERT((ppDalBufferInfo != 0 &&
	        !IsBadReadPtr(ppDalBufferInfo, sizeof(DalBufferInfo *))));

	*ppDalBufferInfo = NULL;

	pos = m_listDalBuffers.GetHeadPosition();

	while (pos)
	{
		pDalBufferInfo = (DalBufferInfo *) m_listDalBuffers.GetNext(pos);

		ASSERT((pDalBufferInfo != 0 &&
		        !IsBadReadPtr(pDalBufferInfo, sizeof(DalBufferInfo))));

		if (pDalBufferInfo->IsIdle())
		{
			*ppDalBufferInfo = pDalBufferInfo;

			return (S_OK);
		}
	}

	return (S_OK);
}

/* =============================================================
// RemoveBuffer()
// (RE) rtl:0x10021AE0; dbg:0x10055840
//
// Remove the entry from both lists. Preserve the original repeated
// delete if the same entry occurs more than once.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
DalInfo::RemoveBuffer(DalBufferInfo *pDalBufferInfo)
{
DalBufferInfo  *pDalBufferInfoTemp;
POSITION        pos;
POSITION        posPrev;

	pos = m_listDalBuffers.GetHeadPosition();

	while (pos)
	{
		posPrev = pos;

		pDalBufferInfoTemp = (DalBufferInfo *) m_listDalBuffers.GetNext(pos);

		ASSERT((pDalBufferInfoTemp != 0 &&
		        !IsBadReadPtr(pDalBufferInfoTemp, sizeof(DalBufferInfo))));

		if (pDalBufferInfoTemp == pDalBufferInfo)
		{
			m_listDalBuffers.RemoveAt(posPrev);

			if (pDalBufferInfo)
				delete pDalBufferInfo;
		}
	}

	pos = m_listStaticBuffers.GetHeadPosition();

	while (pos)
	{
		posPrev = pos;

		pDalBufferInfoTemp = (DalBufferInfo *) m_listStaticBuffers.GetNext(pos);

		ASSERT((pDalBufferInfoTemp != 0 &&
		        !IsBadReadPtr(pDalBufferInfoTemp, sizeof(DalBufferInfo))));

		if (pDalBufferInfoTemp == pDalBufferInfo)
		{
			m_listStaticBuffers.RemoveAt(posPrev);

			if (pDalBufferInfo)
				delete pDalBufferInfo;
		}
	}

	return (S_OK);
}

/* =============================================================
// GetModeDesc()
// (RE) rtl:0x10021C60; dbg:0x10055ab0
//
// Copy out the DAL mode descriptor.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
DalInfo::GetModeDesc(DALMODEDESC *pDesc)
{
	*pDesc = m_sDalModeDesc;

	return (S_OK);
}

/* =============================================================
// SetModeDesc()
// (RE) rtl:0x10021C80; dbg:0x10055ae0
//
// Replace the DAL mode descriptor.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
DalInfo::SetModeDesc(const DALMODEDESC *pDesc)
{
	m_sDalModeDesc = *pDesc;

	return (S_OK);
}

/* =============================================================
// DalBufferInfo()
// (RE) dbg:0x10055bb0; rtl:0x10021ce0
//
// Reference a DirectSound buffer and capture its audio base. Preserve
// partial construction after Lock/Unlock failure; a failed Lock leaves
// the base uninitialized and both failures skip the DAL-buffer query.
// =============================================================*/

DalBufferInfo::DalBufferInfo(DalInfo *lpDalInfo, IDirectSoundBuffer *lpDSBuffer,
			     const DALMODEDESC *pModeDesc, int nKind, int nBufferBase)
{
LPVOID	lpvAudioPtr1;
LPVOID	lpvAudioPtr2;
DWORD	dwAudioBytes1;
DWORD	dwAudioBytes2;
HRESULT	hr;

	ASSERT((lpDalInfo != 0 && !IsBadReadPtr(lpDalInfo, sizeof(DalInfo))));
	ASSERT((lpDSBuffer != 0 &&
	        !IsBadReadPtr(lpDSBuffer, sizeof(IDirectSoundBuffer))));

	m_pDSBuffer = lpDSBuffer;

	m_pDSBuffer->AddRef();

	m_pDalInfo = lpDalInfo;

	m_sDalModeDesc = *pModeDesc;

	m_dwTick                = GetTickCount();
	m_dwTimeout             = A3D_CACHE_TIMEOUT_MS;
	m_dwLastWriteOffset     = 0;
	m_nAudioEndOffset       = A3D_DAL_AUDIO_END_UNSET;
	m_dwOwnerBuffer         = 0;
	m_pIDalBuffer           = NULL;
	m_dwChannels            = 1;
	m_nKind                 = nKind;
	m_dwScaleHackShift      = 0;

	if (nBufferBase)
	{
		m_dwBufferBase = nBufferBase;
	}
	else
	{
		hr = m_pDSBuffer->Lock(0, A3D_DAL_BASE_PROBE_BYTES, &lpvAudioPtr1, &dwAudioBytes1,
				       &lpvAudioPtr2, &dwAudioBytes2, 0);

		if (FAILED(hr))
		{
			DBGSTR("DalBufferInfo::DalBufferInfo() - Failed call to Lock() buffer.\n");

			return;
		}

		m_dwBufferBase = (DWORD) lpvAudioPtr1;

		hr = m_pDSBuffer->Unlock(lpvAudioPtr1, dwAudioBytes1,
					 lpvAudioPtr2, dwAudioBytes2);

		if (FAILED(hr))
		{
			DBGSTR("DalBufferInfo::DalBufferInfo() - Failed call to Unlock() buffer.\n");

			return;
		}
	}

	hr = m_pDSBuffer->QueryInterface(IID_IA3dDalBuffer,
					 (void **) &m_pIDalBuffer);

	if (FAILED(hr))
	{
		DBGSTR("DalBufferInfo::DalBufferInfo() - Failed call to Query for IA3dDalBuffer interface.\n");

		m_pIDalBuffer = NULL;
	}
	else
	{
		ASSERT((m_pIDalBuffer != 0 &&
		        !IsBadReadPtr(m_pIDalBuffer, sizeof(IA3dDalBuffer))));
	}
}

/* =============================================================
// ~DalBufferInfo()
// (RE) dbg:0x10055f50; inlined in Retail at rtl:0x100213A1
//
// Release the DAL-buffer and DirectSound-buffer interfaces.
// =============================================================*/

DalBufferInfo::~DalBufferInfo(void)
{
	if (m_pIDalBuffer)
	{
		m_pIDalBuffer->Release();

		m_pIDalBuffer = NULL;
	}

	if (m_pDSBuffer)
	{
		m_pDSBuffer->Release();

		m_pDSBuffer = NULL;
	}
}

/* =============================================================
// IsIdle()
// (RE) dbg:0x10055fd0; inlined in Retail at rtl:0x10021ABB
//
// Test whether the entry has no owner.
//
// Returns: TRUE if idle, otherwise FALSE.
// =============================================================*/

BOOL
DalBufferInfo::IsIdle(void) const
{
	return (m_dwOwnerBuffer == 0);
}

/* =============================================================
// IsExpired()
// (RE) dbg:0x10056000; inlined in Retail at rtl:0x10021334
//
// Test whether an idle entry has exceeded its unsigned tick timeout.
//
// Returns: TRUE if expired, otherwise FALSE.
// =============================================================*/

BOOL
DalBufferInfo::IsExpired(DWORD dwNow) const
{
	return (IsIdle() && (dwNow - m_dwTick) > m_dwTimeout);
}

/* =============================================================
// Refresh()
// (RE) rtl:0x10021DB0; dbg:0x10056060
//
// Measure refill space against the mode thresholds, capped at 24576 bytes.
//
// Returns:
//   TRUE   a positive fill count
//   FALSE  no space or a failed GetBytesQueued call, which leaves the
//          fill-count output untouched
// =============================================================*/

BOOL
DalBufferInfo::Refresh(DWORD *lpdwBytesToFill, DWORD *pdwPlayCursor)
{
DWORD	dwBytesQueued;
int	nBytesToFill;

	ASSERT((lpdwBytesToFill != 0 &&
	        !IsBadReadPtr(lpdwBytesToFill, sizeof(DWORD))));

	if (FAILED(GetBytesQueued(&dwBytesQueued, pdwPlayCursor)))
	{
		DBGSTR("DalBufferInfo::Refresh - ERROR . GetBytesQueued failed\n");

		return (FALSE);
	}

	if (dwBytesQueued >= m_sDalModeDesc.dwRefillThreshold)
	{
		*lpdwBytesToFill = 0;

		return (FALSE);
	}

	nBytesToFill = m_sDalModeDesc.dwRefillTarget - dwBytesQueued;

	if (nBytesToFill <= 0)
	{
		*lpdwBytesToFill = 0;

		return (FALSE);
	}

	if (nBytesToFill > A3D_DAL_MAX_REFILL_BYTES)
		nBytesToFill = A3D_DAL_MAX_REFILL_BYTES;

	*lpdwBytesToFill = nBytesToFill;

	return (TRUE);
}

/* =============================================================
// GetBytesQueued()
// (RE) dbg:0x10056180; inlined in Retail Refresh at rtl:0x10021DCC
//
// Measure queued DAL bytes and optionally return the play cursor.
//
// Returns: S_OK, or a failed DirectSound GetCurrentPosition result.
// =============================================================*/

HRESULT
DalBufferInfo::GetBytesQueued(DWORD *lpdwBytesQueued, DWORD *pdwPlayCursor)
{
DWORD	dwCurrentPlayCursor;
DWORD	dwWriteCursor;
HRESULT	hr;

	ASSERT((lpdwBytesQueued != 0 &&
	        !IsBadReadPtr(lpdwBytesQueued, sizeof(DWORD))));
	ASSERT((m_pDSBuffer != 0 &&
	        !IsBadReadPtr(m_pDSBuffer, sizeof(IDirectSoundBuffer))));

	hr = m_pDSBuffer->GetCurrentPosition(&dwCurrentPlayCursor, &dwWriteCursor);

	if (FAILED(hr))
	{
		DBGSTR("DalBufferInfo::GetBytesQueued - GetCurrentPosition failed\n");

		return (hr);
	}

	if (pdwPlayCursor)
		*pdwPlayCursor = dwCurrentPlayCursor;

	if (dwCurrentPlayCursor == m_sDalModeDesc.dwStreamingBufferSize)
	{
		*lpdwBytesQueued = 0;

		return (S_OK);
	}

	if (dwCurrentPlayCursor <= m_dwLastWriteOffset)
	{
		ASSERT(m_dwLastWriteOffset >= dwCurrentPlayCursor);

		*lpdwBytesQueued = m_dwLastWriteOffset - dwCurrentPlayCursor;
	}
	else
	{
		ASSERT(m_sDalModeDesc.dwStreamingBufferSize - dwCurrentPlayCursor +
		       m_dwLastWriteOffset <= m_sDalModeDesc.dwStreamingBufferSize);

		*lpdwBytesQueued = m_dwLastWriteOffset +
				  m_sDalModeDesc.dwStreamingBufferSize -
				  dwCurrentPlayCursor;
	}

	return (S_OK);
}

/* =============================================================
// GetSourceBytesQueued()
// (RE) rtl:0x10021E40; dbg:0x100563c0
//
// Convert queued audio to source-format byte units.
//
// Returns: S_OK, or a failed GetAudioBytesQueued result.
// =============================================================*/

HRESULT
DalBufferInfo::GetSourceBytesQueued(DWORD *lpdwSourceBytesQueued, DWORD dwFlags)
{
DWORD	dwAudioBytesQueued;
HRESULT	hr;

	ASSERT((lpdwSourceBytesQueued != 0 &&
	        !IsBadReadPtr(lpdwSourceBytesQueued, sizeof(DWORD))));

	hr = GetAudioBytesQueued(&dwAudioBytesQueued);

	if (FAILED(hr))
	{
		DBGSTR("DalBufferInfo::GetSourceBytesQueued - GetAudioBytesQueued failed\n");

		return (hr);
	}

	if (dwFlags & A3DVOICE_FLAG_16BIT)
		dwAudioBytesQueued *= 2;

	if (dwFlags & A3DVOICE_FLAG_STEREO)
		dwAudioBytesQueued *= 2;

	*lpdwSourceBytesQueued = dwAudioBytesQueued >> 1;

	if (m_sDalModeDesc.dwChannels == 2)
		*lpdwSourceBytesQueued >>= 1;

	return (S_OK);
}

/* =============================================================
// GetAudioBytesQueued()
// (RE) rtl:0x10021E90; dbg:0x100564e0
//
// Measure queued audio up to the recorded source end. Preserve the
// original copied diagnostic names and ignored GetBytesQueued failure.
//
// Returns: S_OK, or a failed GetCurrentPosition or GetStatus result.
// =============================================================*/

HRESULT
DalBufferInfo::GetAudioBytesQueued(DWORD *lpdwAudioBytesQueued)
{
DWORD	dwPlayCursor;
DWORD	dwWriteCursor;
DWORD	dwStatus;
HRESULT	hr;

	ASSERT((lpdwAudioBytesQueued != 0 &&
	        !IsBadReadPtr(lpdwAudioBytesQueued, sizeof(DWORD))));
	ASSERT((m_pDSBuffer != 0 &&
	        !IsBadReadPtr(m_pDSBuffer, sizeof(IDirectSoundBuffer))));

	if (m_nAudioEndOffset == A3D_DAL_AUDIO_END_UNSET)
	{
		GetBytesQueued(lpdwAudioBytesQueued, NULL);

		return (S_OK);
	}

	hr = m_pDSBuffer->GetCurrentPosition(&dwPlayCursor, &dwWriteCursor);

	if (FAILED(hr))
	{
		DBGSTR("DalBufferInfo::GetBytesQueued - GetCurrentPosition failed\n");

		return (hr);
	}

	if (dwPlayCursor <= (DWORD) m_nAudioEndOffset)
	{
		*lpdwAudioBytesQueued = (DWORD) m_nAudioEndOffset - dwPlayCursor;

		return (S_OK);
	}

	hr = m_pDSBuffer->GetStatus(&dwStatus);

	if (FAILED(hr))
	{
		DBGSTR("DalBufferInfo::FormatAndFillBuffer - Failed to get buffer status\n");

		return (hr);
	}

	if (dwStatus & DSBSTATUS_LOOPING)
		*lpdwAudioBytesQueued = (DWORD) m_nAudioEndOffset +
					m_sDalModeDesc.dwStreamingBufferSize -
					dwPlayCursor;
	else
		*lpdwAudioBytesQueued = 0;

	return (S_OK);
}

/* =============================================================
// FormatToMono()
// (RE) rtl:0x10021F60; dbg:0x100566F0
//
// Convert source frames to signed 16-bit mono with the scale-hack shift.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
DalBufferInfo::FormatToMono(short *lvpDest, const void *lvpSrc,
				  DWORD dwSamples, DWORD dwFlags)
{
short          *psDest;
const BYTE     *pbSrc;
const short    *psSrc;
DWORD           i;

	ASSERT(lvpDest != 0);
	ASSERT(lvpSrc != 0);

	psDest = lvpDest;

	if (!(dwFlags & A3DVOICE_FLAG_16BIT) && !(dwFlags & A3DVOICE_FLAG_STEREO))
	{

		pbSrc = (const BYTE *) lvpSrc;

		for (i = 0; i < dwSamples; i++)
		{
			*psDest = (short) (*pbSrc ^ A3D_DAL_PCM8_SIGN_BIT);
			*psDest <<= 8;
			*psDest += *pbSrc;
			*psDest++ >>= m_dwScaleHackShift;

			pbSrc++;
		}

		return (S_OK);
	}

	if ((dwFlags & A3DVOICE_FLAG_16BIT) && !(dwFlags & A3DVOICE_FLAG_STEREO))
	{

		if (m_dwScaleHackShift)
		{
			psSrc = (const short *) lvpSrc;

			for (i = 0; i < dwSamples; i++)
				*psDest++ = (short) (*psSrc++ >> m_dwScaleHackShift);

			return (S_OK);
		}

		memcpy(lvpDest, lvpSrc, 2 * dwSamples);

		return (S_OK);
	}

	if ((dwFlags & A3DVOICE_FLAG_16BIT) && (dwFlags & A3DVOICE_FLAG_STEREO))
	{

		psSrc = (const short *) lvpSrc;

		for (i = 0; i < dwSamples; i++)
		{
			*psDest = (short) (*psSrc >> 1);
			*psDest += psSrc[1] >> 1;
			*psDest++ >>= m_dwScaleHackShift;

			psSrc += 2;
		}

		return (S_OK);
	}

	pbSrc = (const BYTE *) lvpSrc;

	for (i = 0; i < dwSamples; i++)
	{
	int	nSample;

		nSample = (pbSrc[1] >> 1) + (pbSrc[0] >> 1);

		*psDest = (short) (nSample ^ A3D_DAL_PCM8_SIGN_BIT);
		*psDest <<= 8;
		*psDest += nSample;
		*psDest++ >>= m_dwScaleHackShift;

		pbSrc += 2;
	}

	return (S_OK);
}

/* =============================================================
// FormatToStereo()
// (RE) rtl:0x100220D0; dbg:0x10056AC0
//
// Convert source frames to signed 16-bit stereo with the scale-hack shift.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
DalBufferInfo::FormatToStereo(short *lvpDest, const void *lvpSrc,
				  DWORD dwSamples, DWORD dwFlags)
{
short          *psDest;
const BYTE     *pbSrc;
const short    *psSrc;
DWORD           i;

	ASSERT(lvpDest != 0);
	ASSERT(lvpSrc != 0);

	psDest = lvpDest;

	if ((dwFlags & A3DVOICE_FLAG_16BIT) && (dwFlags & A3DVOICE_FLAG_STEREO))
	{

		psSrc = (const short *) lvpSrc;

		for (i = 0; i < dwSamples; i++)
		{
			psDest[0] = (short) (psSrc[0] >> m_dwScaleHackShift);
			psDest[1] = (short) (psSrc[1] >> m_dwScaleHackShift);

			psDest += 2;
			psSrc  += 2;
		}

		return (S_OK);
	}

	if ((dwFlags & A3DVOICE_FLAG_16BIT) && !(dwFlags & A3DVOICE_FLAG_STEREO))
	{

		psSrc = (const short *) lvpSrc;

		for (i = 0; i < dwSamples; i++)
		{
			psDest[0] = (short) (*psSrc >> m_dwScaleHackShift);
			psDest[1] = (short) (*psSrc >> m_dwScaleHackShift);

			psDest += 2;
			psSrc++;
		}

		return (S_OK);
	}

	if (!(dwFlags & A3DVOICE_FLAG_16BIT) && (dwFlags & A3DVOICE_FLAG_STEREO))
	{

		pbSrc = (const BYTE *) lvpSrc;

		for (i = 0; i < dwSamples; i++)
		{
			psDest[0] = (short) (pbSrc[0] ^ A3D_DAL_PCM8_SIGN_BIT);
			psDest[0] <<= 8;
			psDest[0] += pbSrc[0];
			psDest[0] >>= m_dwScaleHackShift;

			psDest[1] = (short) (pbSrc[1] ^ A3D_DAL_PCM8_SIGN_BIT);
			psDest[1] <<= 8;
			psDest[1] += pbSrc[1];
			psDest[1] >>= m_dwScaleHackShift;

			psDest += 2;
			pbSrc  += 2;
		}

		return (S_OK);
	}

	pbSrc = (const BYTE *) lvpSrc;

	for (i = 0; i < dwSamples; i++)
	{
		psDest[0] = (short) (pbSrc[0] ^ A3D_DAL_PCM8_SIGN_BIT);
		psDest[0] <<= 8;
		psDest[0] += pbSrc[0];
		psDest[0] >>= m_dwScaleHackShift;

		psDest[1] = psDest[0];

		psDest += 2;
		pbSrc++;
	}

	return (S_OK);
}

/* =============================================================
// SilenceAndRewind()
// (RE) rtl:0x10022250; dbg:0x10056F00
//
// Silence and rewind a locking DAL buffer, or attempt restoration if
// lost. Preserve the original first-pointer reuse for both Unlock spans.
// =============================================================*/

void
DalBufferInfo::SilenceAndRewind(void)
{
LPVOID	lpvAudioPtr1;
LPVOID	lpvAudioPtr2;
DWORD	dwBytes1;
DWORD	dwAudioBytes2;
HRESULT	hr;

	ASSERT((m_pDSBuffer != 0 &&
	        !IsBadReadPtr(m_pDSBuffer, sizeof(IDirectSoundBuffer))));

	if (!(m_sDalModeDesc.dwFlags & A3D_DALMODE_LOCKED_BUFFER_FILL))
		return;

	hr = m_pDSBuffer->Lock(0, m_sDalModeDesc.dwStreamingBufferSize,
			       &lpvAudioPtr1, &dwBytes1,
			       &lpvAudioPtr2, &dwAudioBytes2, 0);

	if (SUCCEEDED(hr))
	{
		ASSERT(dwBytes1 == m_sDalModeDesc.dwStreamingBufferSize);

		memset(lpvAudioPtr1, 0, m_sDalModeDesc.dwStreamingBufferSize);

		m_pDSBuffer->Unlock(lpvAudioPtr1, dwBytes1,
				    lpvAudioPtr1, dwAudioBytes2);

		hr = m_pDSBuffer->SetCurrentPosition(0);

		ASSERT(SUCCEEDED(hr));

		m_dwLastWriteOffset = 0;
		m_nAudioEndOffset   = A3D_DAL_AUDIO_END_UNSET;
	}
	else if (hr == DSERR_BUFFERLOST)
	{
		DBGSTR("Buffer lost!!!\n");

		if (SUCCEEDED(m_pDSBuffer->Restore()))
			DBGSTR("Buffer restored!!!\n");
	}
	else
	{
		TRACE("Lock Failed hr=%d!!!\n", hr);
	}
}

/* =============================================================
// FormatAndFillBuffer()
// (RE) rtl:0x100222F0; dbg:0x10057170
//
// Convert source audio into a locked span, wrapping looping sources
// or padding their end with silence and adjusting playback.
//
// Returns: S_OK; E_INVALIDARG for a null target, source or position pointer; a
//          failed DirectSound GetStatus result.
// =============================================================*/

HRESULT
DalBufferInfo::FormatAndFillBuffer(void *lpvTarget, DWORD dwTargetBytes,
				   const void *lpvSource, DWORD dwSourceSize,
				   DWORD *lpdwSourcePosition, DWORD dwFlags,
				   DWORD dwPlayBase)
{
DWORD	dwTargetPos;
DWORD	nSourceShift;
DWORD	nTargetShift;
DWORD	dwTargetRun;
DWORD	dwSourceRun;
DWORD	dwStatus;
HRESULT	hr;

	ASSERT(lpvTarget != 0);
	ASSERT(lpvSource != 0);
	ASSERT((lpdwSourcePosition != 0 &&
	        !IsBadReadPtr(lpdwSourcePosition, sizeof(DWORD))));

	dwTargetPos  = 0;
	nSourceShift = 0;

	if (m_dwChannels & 2)
		nTargetShift = 2;
	else
		nTargetShift = 1;

	if (!lpvTarget || !lpvSource || !lpdwSourcePosition)
		return (E_INVALIDARG);

	if (dwFlags & A3DVOICE_FLAG_16BIT)
		nSourceShift++;

	if (dwFlags & A3DVOICE_FLAG_STEREO)
		nSourceShift++;

	while (dwTargetPos < dwTargetBytes)
	{
		dwTargetRun = (dwTargetBytes - dwTargetPos) >> nTargetShift;
		dwSourceRun = (dwSourceSize - *lpdwSourcePosition) >> nSourceShift;

		if (dwTargetRun <= dwSourceRun)
		{
			if (nTargetShift == 1)
				FormatToMono(
					(short *) ((BYTE *) lpvTarget + dwTargetPos),
					(const BYTE *) lpvSource + *lpdwSourcePosition,
					dwTargetRun, dwFlags);
			else
				FormatToStereo(
					(short *) ((BYTE *) lpvTarget + dwTargetPos),
					(const BYTE *) lpvSource + *lpdwSourcePosition,
					dwTargetRun, dwFlags);

			*lpdwSourcePosition += dwTargetRun << nSourceShift;
			dwTargetPos         += dwTargetRun << nTargetShift;

			break;
		}

		if (nTargetShift == 1)
			FormatToMono(
				(short *) ((BYTE *) lpvTarget + dwTargetPos),
				(const BYTE *) lpvSource + *lpdwSourcePosition,
				dwSourceRun, dwFlags);
		else
			FormatToStereo(
				(short *) ((BYTE *) lpvTarget + dwTargetPos),
				(const BYTE *) lpvSource + *lpdwSourcePosition,
				dwSourceRun, dwFlags);

		dwTargetPos += dwSourceRun << nTargetShift;

		if (dwFlags & A3DVOICE_FLAG_LOOPING)
		{
			*lpdwSourcePosition = 0;
		}
		else
		{
			memset((BYTE *) lpvTarget + dwTargetPos, 0,
			       dwTargetBytes - dwTargetPos);

			*lpdwSourcePosition = dwSourceSize;

			if (m_nAudioEndOffset == A3D_DAL_AUDIO_END_UNSET)
				m_nAudioEndOffset = m_dwLastWriteOffset + dwTargetPos;

			break;
		}
	}

	if (*lpdwSourcePosition == dwSourceSize && !(dwFlags & A3DVOICE_FLAG_LOOPING) &&
	    (DWORD) lpvTarget >= dwPlayBase)
	{
		dwStatus = 0;

		hr = m_pDSBuffer->GetStatus(&dwStatus);

		if (FAILED(hr))
		{
			DBGSTR("DalBufferInfo::FormatAndFillBuffer - Failed to get buffer status\n");

			return (hr);
		}

		if ((dwStatus & DSBSTATUS_PLAYING) && (dwStatus & DSBSTATUS_LOOPING))
			m_pDSBuffer->Play(0, 0, 0);
	}

	return (S_OK);
}

/* =============================================================
// FillFromSource()
// (RE) rtl:0x100224B0; dbg:0x10057560
//
// Fill or advance the streaming buffer and update its write cursor.
// Preserve the original unchecked Lock outputs and ignored fill errors.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
DalBufferInfo::FillFromSource(void *lpvDest, const void *lpvSource,
				  DWORD dwSourceSize, DWORD *lpdwSourcePosition,
				  DWORD dwFlags)
{
LPVOID	lpvAudioPtr1;
LPVOID	lpvAudioPtr2;
DWORD	dwAudioBytes1;
DWORD	dwAudioBytes2;
DWORD	dwBytesToFill;
DWORD	dwPlayCursor;
DWORD	dwPlayBase;
DWORD	dwAdvance;

	(void) lpvDest;

	ASSERT(lpvSource != 0);
	ASSERT((lpdwSourcePosition != 0 &&
	        !IsBadReadPtr(lpdwSourcePosition, sizeof(DWORD))));
	ASSERT((m_pDSBuffer != 0 &&
	        !IsBadReadPtr(m_pDSBuffer, sizeof(IDirectSoundBuffer))));

	if (!Refresh(&dwBytesToFill, &dwPlayCursor))
		return (S_OK);

	if (m_sDalModeDesc.dwFlags & A3D_DALMODE_LOCKED_BUFFER_FILL)
	{
		dwPlayBase = dwPlayCursor + m_dwBufferBase;

		m_pDSBuffer->Lock(m_dwLastWriteOffset, dwBytesToFill,
				  &lpvAudioPtr1, &dwAudioBytes1,
				  &lpvAudioPtr2, &dwAudioBytes2, 0);

		if (lpvAudioPtr1)
			FormatAndFillBuffer(lpvAudioPtr1, dwAudioBytes1,
					    lpvSource, dwSourceSize,
					    lpdwSourcePosition, dwFlags,
					    dwPlayBase);

		if (lpvAudioPtr2)
			FormatAndFillBuffer(lpvAudioPtr2, dwAudioBytes2,
					    lpvSource, dwSourceSize,
					    lpdwSourcePosition, dwFlags,
					    dwPlayBase);

		m_pDSBuffer->Unlock(lpvAudioPtr1, dwAudioBytes1,
				    lpvAudioPtr2, dwAudioBytes2);
	}
	else
	{
		dwAdvance = dwBytesToFill;

		if (!(dwFlags & A3DVOICE_FLAG_16BIT))
			dwAdvance >>= 1;

		if (dwFlags & A3DVOICE_FLAG_STEREO)
			dwAdvance <<= 1;

		if (!(dwFlags & A3DVOICE_FLAG_LOOPING) &&
		    *lpdwSourcePosition + dwAdvance >= dwSourceSize)
		{
			*lpdwSourcePosition = dwSourceSize;

			m_nAudioEndOffset = m_dwLastWriteOffset + dwBytesToFill;

			m_pDSBuffer->Play(0, 0, 0);
		}
		else
		{
			*lpdwSourcePosition =
				(*lpdwSourcePosition + dwAdvance) % dwSourceSize;
		}
	}

	m_dwLastWriteOffset = (dwBytesToFill + m_dwLastWriteOffset) %
			      m_sDalModeDesc.dwStreamingBufferSize;

	return (S_OK);
}

/* =============================================================
// GetBufferState()
// (RE) dbg:0x10057b50
//
// Write 2 for a playing buffer or 1 otherwise.
//
// Returns: The GetStatus result here. The original return type is unresolved;
//          Debug leaves different values in EAX on the two branches.
// =============================================================*/

HRESULT
DalBufferInfo::GetBufferState(DWORD *lpdwBufferState)
{
DWORD	dwStatus;
HRESULT	hr;

	ASSERT((lpdwBufferState != 0 &&
	        !IsBadReadPtr(lpdwBufferState, sizeof(DWORD))));

	hr = m_pDSBuffer->GetStatus(&dwStatus);

	if (dwStatus & DSBSTATUS_PLAYING)
		*lpdwBufferState = A3D_DALBUFFER_STATE_PLAYING;
	else
		*lpdwBufferState = A3D_DALBUFFER_STATE_NOT_PLAYING;

	return (hr);
}

/* =============================================================
// ApplyScaleHack()
// (RE) dbg:0x10057C00; inlined in Retail CreateStaticDalBuffer: rtl:0x10021548
//
// Enable software sample shifting for streaming buffers or request the
// buffer scale-hack interface.
//
// Returns: S_OK, including SetScaleHack E_NOTIMPL; otherwise a failed
//          QueryInterface or SetScaleHack result. A null interface returns hr.
// =============================================================*/

HRESULT
DalBufferInfo::ApplyScaleHack(void)
{
IA3dScaleHackBuffer    *lpIA3dScaleHackBuffer;
HRESULT                 hr;

	if (m_nKind == A3D_DALBUFFER_KIND_STREAMING)
	{
		m_dwScaleHackShift = 1;

		return (S_OK);
	}

	/* Original leaves the interface output unset before QueryInterface. */

	VERIFY(SUCCEEDED(hr = m_pDSBuffer->QueryInterface(
		IID_IA3dScaleHackBuffer, (void**) &lpIA3dScaleHackBuffer)));

	if (FAILED(hr) || !lpIA3dScaleHackBuffer)
		return (hr);

	VERIFY(SUCCEEDED(hr = lpIA3dScaleHackBuffer->SetScaleHack(1)));

	if (FAILED(hr) && hr != E_NOTIMPL)
	{
		lpIA3dScaleHackBuffer->Release();

		return (hr);
	}

	lpIA3dScaleHackBuffer->Release();

	return (S_OK);
}

/* =============================================================
// IsD3dHardware()
// (RE) rtl:0x10021CA0; dbg:0x10055B10; thunk dbg:0x1000326A
//
// Test the hardware flag of a DAL named D3D.
//
// Returns: FALSE for another DAL name, otherwise DAL_D3D::m_fHardware.
// =============================================================*/

BOOL
DalInfo::IsD3dHardware(void)
{
DAL_D3D	*pDalD3d;

	if (lstrcmpiA((char *) &m_sDalModeDesc.dwName, "D3D"))
		return (FALSE);

	pDalD3d = static_cast<DAL_D3D *>(m_pIA3dDal);

	return (pDalD3d->m_fHardware);
}
