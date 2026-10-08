/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3dsourcecom.cpp
 *
 * Implements the IA3dSource2 wrapper returned to applications by the
 * root. It owns the active source implementation and forwards playback,
 * audio-data and spatial-control calls to it.
 *
 * The wrapper selects CA3dSource for the normal waveform path or
 * Ac3FilterGraph for DirectShow AC-3 fallback. Allocation, replacement
 * and destruction tracking keep the public wrapper connected to the
 * current implementation. The actual source behavior is defined in
 * A3dSource.cpp and ac3fgraph.cpp.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "a3dsourcecom.h"
#include "ac3fgraph.h"
#include "A3dSource.h"
#include "A3d3.h"

/* (RE) Secondary-base adjustors: dbg:0x10030320, dbg:0x10030330,
 * dbg:0x10030340. Vector deleting destructor: dbg:0x10030430.
 * IA3dSource2 constructor thunk: dbg:0x10001104.
 */

/* =============================================================
// CA3dSourceCom::CA3dSourceCom()
// (RE) rtl:0x100151c0; dbg:0x10030350
//
// Retain creation parameters and allocate an initial wave source.
// The allocation result is ignored.
// =============================================================*/

CA3dSourceCom::CA3dSourceCom(IDirectSound *pDS, CA3dRoot *pApi, DWORD dwFlags)
{
	m_pSource       = NULL;
	m_dwPointerType = A3D_SOURCE_POINTER_NONE;
	m_pDS           = pDS;
	m_pApi          = pApi;
	m_dwFlags       = dwFlags;
	m_cRef          = 1;

	AllocateSource(A3D_SOURCE_POINTER_WAVE);
}

/* =============================================================
// CA3dSourceCom::CA3dSourceCom()
// (RE) rtl:0x10015280; dbg:0x10030480
//
// Duplicate a wave source and report its result through phr. The original frees
// a failed copy through IA3dSource2 without running its destructor.
// =============================================================*/

CA3dSourceCom::CA3dSourceCom(CA3dSourceCom *pOriginal, HRESULT *phr)
{
	ASSERT((phr != 0 && !IsBadReadPtr(phr, sizeof(HRESULT))));
	ASSERT((pOriginal != 0 &&
	       !IsBadReadPtr(pOriginal, sizeof(CA3dSourceCom))));

	m_dwPointerType = pOriginal->m_dwPointerType;
	m_pApi          = pOriginal->m_pApi;
	m_pDS           = pOriginal->m_pDS;
	m_dwFlags       = pOriginal->m_dwFlags;
	m_pSource       = NULL;

	m_pSource = new CA3dSource((CA3dSource *) pOriginal->m_pSource, this, phr);

	if (!m_pSource)
		m_dwPointerType = A3D_SOURCE_POINTER_NONE;

	if (FAILED(*phr))
	{
		delete m_pSource;
		m_pSource       = NULL;
		m_dwPointerType = A3D_SOURCE_POINTER_NONE;
	}

	m_cRef = 1;
}

/* =============================================================
// CA3dSourceCom::~CA3dSourceCom()
// (RE) rtl:0x10015350; dbg:0x100306e0
//
// Release the owned source.
// =============================================================*/

CA3dSourceCom::~CA3dSourceCom(void)
{
	if (m_pSource)
	{
		m_pSource->Release();
		m_pSource = NULL;
	}
}

/* =============================================================
// CA3dSourceCom::QueryInterface()
// (RE) rtl:0x100153d0; dbg:0x100307a0
//
// Query the underlying source for an interface.
//
// Returns: The source QueryInterface result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::QueryInterface(REFIID riid, void **ppv)
{
	return (m_pSource->QueryInterface(riid, ppv));
}

/* =============================================================
// CA3dSourceCom::AddRef()
// (RE) rtl:0x10008f50; dbg:0x100307e0
//
// Add a reference to the wrapper.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dSourceCom::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// CA3dSourceCom::Release()
// (RE) rtl:0x100153f0; dbg:0x10030810
//
// Release a reference, releasing the source and deleting the wrapper at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dSourceCom::Release(void)
{
	if (--m_cRef != 0)
		return (m_cRef);

	ReleaseSource();
	delete this;

	return (0);
}

/* =============================================================
// CA3dSourceCom::AllocateSource()
// (RE) rtl:0x10015430; dbg:0x100308a0
//
// Keep the requested source type or replace the current implementation.
//
// Returns: S_OK; E_INVALIDARG unless type is 1 (wave) or 2 (AC-3);
//          A3DERROR_MEMORY_ALLOCATION if allocation fails; wave initialization
//          errors.
// =============================================================*/

HRESULT
CA3dSourceCom::AllocateSource(DWORD dwType)
{
HRESULT	hr;

	if (m_pSource)
	{
		if (m_dwPointerType == dwType)
			return (S_OK);

		ReleaseSource();
	}

	if (dwType == A3D_SOURCE_POINTER_WAVE)
	{
		m_pSource = new CA3dSource(m_pDS, m_pApi, this, m_dwFlags, &hr);

		if (!m_pSource)
		{
			DBGSTR(
				"CA3dSourceCom::AllocateSource - Not enough memory to new a source.\n");
			return (A3DERROR_MEMORY_ALLOCATION);
		}

		if (FAILED(hr))
			return (hr);

		m_pApi->m_SourceArray.push_back((CA3dSource *) m_pSource);
	}
	else
	{
		if (dwType != A3D_SOURCE_POINTER_AC3_GRAPH)
		{
			DBGSTR(
				"CA3dSourceCom::AllocateSource - Source format not specified!\n");
			return (E_INVALIDARG);
		}

		m_pSource = new Ac3FilterGraph;

		if (!m_pSource)
		{
			DBGSTR(
				"CA3dSourceCom::AllocateSource - Not enough memory to new a source.\n");
			return (A3DERROR_MEMORY_ALLOCATION);
		}
	}

	ASSERT(m_pSource);

	m_dwPointerType = dwType;

	return (S_OK);
}

/* =============================================================
// CA3dSourceCom::ReleaseSource()
// (RE) dbg:0x10030b30; inlined at rtl:0x10015469
//
// Release the owned source and clear its pointer and type.
// =============================================================*/

void
CA3dSourceCom::ReleaseSource(void)
{
ULONG	cRef;

	if (m_pSource)
	{
		ASSERT(m_dwPointerType != 0);

		cRef = m_pSource->Release();
		ASSERT(cRef == 0);
	}

	m_pSource       = NULL;
	m_dwPointerType = A3D_SOURCE_POINTER_NONE;
}

/* =============================================================
// CA3dSourceCom::SourceDestroyed()
// (RE) rtl:0x100155b0; dbg:0x10030cf0
//
// Clear the destroyed source without releasing it again.
// the source destructor passes this argument, which the helper does not read.
// =============================================================*/

void
CA3dSourceCom::SourceDestroyed(CA3dSource *pSource)
{
	m_pSource = NULL;
	m_dwPointerType = A3D_SOURCE_POINTER_NONE;
}

/* =============================================================
// CA3dSourceCom::InUse()
// (RE) dbg:0x10030c20; inlined at rtl:0x100155C7
//
// Check whether the wave source owns a DirectSound buffer.
// Check only DirectSound buffer ownership. The AC-3 graph field at offset 0x08
// is not reconciled with m_pGraphBuilder.
//
// Returns:
//   TRUE   a wave source with a buffer
//   FALSE  otherwise
// =============================================================*/

BOOL
CA3dSourceCom::InUse(void)
{
	switch (m_dwPointerType)
	{
	case A3D_SOURCE_POINTER_NONE:
		return (FALSE);

	case A3D_SOURCE_POINTER_WAVE:
		return (((CA3dSource *) m_pSource)->m_pBuffer != 0);

	case A3D_SOURCE_POINTER_AC3_GRAPH:
		ASSERT(m_pSource);
		return (FALSE);

	default:
		DBGSTR(
			"CA3dSourceCom::InUse() - Unknown source type.\n");
		return (FALSE);
	}
}

/* =============================================================
// CA3dSourceCom::LoadWaveFile()
// (RE) rtl:0x100155c0; dbg:0x10030d30
//
// Allocate a wave source and load the requested file.
//
// Returns: The source LoadWaveFile result or allocation error;
//          A3DERROR_SOURCE_IN_USE when in use and the API version exceeds 4.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::LoadWaveFile(LPSTR pszFileName)
{
HRESULT	hr;

	if (InUse() && m_pApi->m_dwInterfaceVersion > 4)
		return (A3DERROR_SOURCE_IN_USE);

	ASSERT(m_dwPointerType != 2);

	hr = AllocateSource(A3D_SOURCE_POINTER_WAVE);

	if (FAILED(hr))
	{
		DBGSTR(
			"CA3dSourceCom::LoadWaveFile() - Couldn't allocate source.\n");
		return (hr);
	}

	return (m_pSource->LoadWaveFile(pszFileName));
}

/* =============================================================
// CA3dSourceCom::LoadWaveData()
// (RE) rtl:0x10015620; dbg:0x10030e10
//
// Load waveform data through the source.
//
// Returns: The source LoadWaveData result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::LoadWaveData(LPVOID pvData, DWORD dwSize)
{
	return (m_pSource->LoadWaveData(pvData, dwSize));
}

/* =============================================================
// CA3dSourceCom::AllocateAudioData()
// (RE) rtl:0x10015640; dbg:0x10030e50
//
// Allocate audio storage through the source.
//
// Returns: The source AllocateAudioData result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::AllocateAudioData(INT nBytes)
{
	return (m_pSource->AllocateAudioData(nBytes));
}

/* =============================================================
// CA3dSourceCom::FreeAudioData()
// (RE) rtl:0x10015660; dbg:0x10030e90
//
// Free audio storage through the source.
//
// Returns: The source FreeAudioData result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::FreeAudioData(void)
{
	return (m_pSource->FreeAudioData());
}

/* =============================================================
// CA3dSourceCom::SetAudioFormat()
// (RE) rtl:0x10015680; dbg:0x10030ec0
//
// Set the source audio format.
//
// Returns: The source SetAudioFormat result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetAudioFormat(LPVOID pvFormat)
{
	return (m_pSource->SetAudioFormat(pvFormat));
}

/* =============================================================
// CA3dSourceCom::GetAudioFormat()
// (RE) rtl:0x100156a0; dbg:0x10030f20
//
// Read the source audio format.
//
// Returns: The source GetAudioFormat result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetAudioFormat(LPVOID pvFormat)
{
	return (m_pSource->GetAudioFormat(pvFormat));
}

/* =============================================================
// CA3dSourceCom::GetAudioSize()
// (RE) rtl:0x100156c0; dbg:0x10030f60
//
// Read the source audio-data size.
//
// Returns: The source GetAudioSize result.
// =============================================================*/

STDMETHODIMP_(DWORD)
CA3dSourceCom::GetAudioSize(void)
{
	return (m_pSource->GetAudioSize());
}

/* =============================================================
// CA3dSourceCom::GetType()
// (RE) rtl:0x100156e0; dbg:0x10030f90
//
// Read the source type.
//
// Returns: The source GetType result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetType(LPDWORD pdwType)
{
	return (m_pSource->GetType(pdwType));
}

/* =============================================================
// CA3dSourceCom::Lock()
// (RE) rtl:0x10015700; dbg:0x10030fd0
//
// Lock source audio storage.
//
// Returns: The source Lock result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::Lock(DWORD dwOffset, DWORD dwBytes,
		    LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1,
		    LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2,
		    DWORD dwFlags)
{
	return (m_pSource->Lock(dwOffset, dwBytes,
				ppvAudioPtr1, pdwAudioBytes1,
				ppvAudioPtr2, pdwAudioBytes2,
				dwFlags));
}

/* =============================================================
// CA3dSourceCom::Unlock()
// (RE) rtl:0x10015730; dbg:0x10031020
//
// Unlock source audio storage.
//
// Returns: The source Unlock result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
		      LPVOID pvAudioPtr2, DWORD dwAudioBytes2)
{
	return (m_pSource->Unlock(pvAudioPtr1, dwAudioBytes1,
				  pvAudioPtr2, dwAudioBytes2));
}

/* =============================================================
// CA3dSourceCom::Play()
// (RE) rtl:0x10015760; dbg:0x10031070
//
// Start source playback.
//
// Returns: The source Play result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::Play(INT nMode)
{
	return (m_pSource->Play(nMode));
}

/* =============================================================
// CA3dSourceCom::Stop()
// (RE) rtl:0x10015780; dbg:0x100310b0
//
// Stop source playback.
//
// Returns: The source Stop result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::Stop(void)
{
	return (m_pSource->Stop());
}

/* =============================================================
// CA3dSourceCom::Rewind()
// (RE) rtl:0x100157a0; dbg:0x100310e0
//
// Rewind source playback.
//
// Returns: The source Rewind result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::Rewind(void)
{
	return (m_pSource->Rewind());
}

/* =============================================================
// CA3dSourceCom::SetPlayTime()
// (RE) rtl:0x100157c0; dbg:0x10031110
//
// Set the source playback time.
//
// Returns: The source SetPlayTime result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetPlayTime(A3DVAL fTime)
{
	return (m_pSource->SetPlayTime(fTime));
}

/* =============================================================
// CA3dSourceCom::SetPlayPosition()
// (RE) rtl:0x100157e0; dbg:0x10031150
//
// Set the source playback byte position.
//
// Returns: The source SetPlayPosition result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetPlayPosition(DWORD dwPosition)
{
	return (m_pSource->SetPlayPosition(dwPosition));
}

/* =============================================================
// CA3dSourceCom::SetPosition3f()
// (RE) rtl:0x10015800; dbg:0x10031190
//
// Set the source position components.
//
// Returns: The source SetPosition3f result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetPosition3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
	return (m_pSource->SetPosition3f(x, y, z));
}

/* =============================================================
// CA3dSourceCom::SetPosition3fv()
// (RE) rtl:0x10015820; dbg:0x100311d0
//
// Set the source position vector.
//
// Returns: The source SetPosition3fv result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetPosition3fv(LPA3DVAL pfPosition)
{
	return (m_pSource->SetPosition3fv(pfPosition));
}

/* =============================================================
// CA3dSourceCom::SetOrientationAngles3f()
// (RE) rtl:0x10015840; dbg:0x10031210
//
// Set the source orientation angles.
//
// Returns: The source SetOrientationAngles3f result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetOrientationAngles3f(A3DVAL fHeading, A3DVAL fPitch, A3DVAL fRoll)
{
	return (m_pSource->SetOrientationAngles3f(fHeading, fPitch, fRoll));
}

/* =============================================================
// CA3dSourceCom::SetOrientationAngles3fv()
// (RE) rtl:0x10015860; dbg:0x10031250
//
// Set the source orientation-angle vector.
//
// Returns: The source SetOrientationAngles3fv result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetOrientationAngles3fv(LPA3DVAL pfAngles)
{
	return (m_pSource->SetOrientationAngles3fv(pfAngles));
}

/* =============================================================
// CA3dSourceCom::SetOrientation6f()
// (RE) rtl:0x10015880; dbg:0x10031290
//
// Set the source front and up components.
//
// Returns: The source SetOrientation6f result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetOrientation6f(A3DVAL fXfront, A3DVAL fYfront, A3DVAL fZfront,
				A3DVAL fXup, A3DVAL fYup, A3DVAL fZup)
{
	return (m_pSource->SetOrientation6f(fXfront, fYfront, fZfront,
					    fXup, fYup, fZup));
}

/* =============================================================
// CA3dSourceCom::SetOrientation6fv()
// (RE) rtl:0x100158b0; dbg:0x100312e0
//
// Set the source front and up vectors.
//
// Returns: The source SetOrientation6fv result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetOrientation6fv(LPA3DVAL pfOrientation)
{
	return (m_pSource->SetOrientation6fv(pfOrientation));
}

/* =============================================================
// CA3dSourceCom::SetVelocity3f()
// (RE) rtl:0x100158d0; dbg:0x10031320
//
// Set the source velocity components.
//
// Returns: The source SetVelocity3f result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetVelocity3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
	return (m_pSource->SetVelocity3f(x, y, z));
}

/* =============================================================
// CA3dSourceCom::SetVelocity3fv()
// (RE) rtl:0x10015900; dbg:0x10031360
//
// Set the source velocity vector.
//
// Returns: The source SetVelocity3fv result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetVelocity3fv(LPA3DVAL pfVelocity)
{
	return (m_pSource->SetVelocity3fv(pfVelocity));
}

/* =============================================================
// CA3dSourceCom::SetCone()
// (RE) rtl:0x10015920; dbg:0x100313a0
//
// Set the source directional cone.
//
// Returns: The source SetCone result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetCone(A3DVAL fInnerAngle, A3DVAL fOuterAngle, A3DVAL fOutsideGain)
{
	return (m_pSource->SetCone(fInnerAngle, fOuterAngle, fOutsideGain));
}

/* =============================================================
// CA3dSourceCom::SetMinMaxDistance()
// (RE) rtl:0x10015950; dbg:0x100313e0
//
// Set the source distance limits and flags.
//
// Returns: The source SetMinMaxDistance result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetMinMaxDistance(A3DVAL fMin, A3DVAL fMax, DWORD dwFlags)
{
	return (m_pSource->SetMinMaxDistance(fMin, fMax, dwFlags));
}

/* =============================================================
// CA3dSourceCom::SetGain()
// (RE) rtl:0x10015980; dbg:0x10031420
//
// Set the source gain.
//
// Returns: The source SetGain result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetGain(A3DVAL fGain)
{
	return (m_pSource->SetGain(fGain));
}

/* =============================================================
// CA3dSourceCom::SetPitch()
// (RE) rtl:0x100159a0; dbg:0x10031460
//
// Set the source pitch.
//
// Returns: The source SetPitch result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetPitch(A3DVAL fPitch)
{
	return (m_pSource->SetPitch(fPitch));
}

/* =============================================================
// CA3dSourceCom::SetDopplerScale()
// (RE) rtl:0x100159c0; dbg:0x100314a0
//
// Set the source Doppler scale.
//
// Returns: The source SetDopplerScale result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetDopplerScale(A3DVAL fScale)
{
	return (m_pSource->SetDopplerScale(fScale));
}

/* =============================================================
// CA3dSourceCom::SetDistanceModelScale()
// (RE) rtl:0x100159e0; dbg:0x100314e0
//
// Set the source distance-model scale.
//
// Returns: The source SetDistanceModelScale result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetDistanceModelScale(A3DVAL fScale)
{
	return (m_pSource->SetDistanceModelScale(fScale));
}

/* =============================================================
// CA3dSourceCom::SetEq()
// (RE) rtl:0x10015a00; dbg:0x10031520
//
// Set the source equalization.
//
// Returns: The source SetEq result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetEq(A3DVAL fEq)
{
	return (m_pSource->SetEq(fEq));
}

/* =============================================================
// CA3dSourceCom::SetPriority()
// (RE) rtl:0x10015a20; dbg:0x10031560
//
// Set the source priority.
//
// Returns: The source SetPriority result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetPriority(A3DVAL fPriority)
{
	return (m_pSource->SetPriority(fPriority));
}

/* =============================================================
// CA3dSourceCom::SetRenderMode()
// (RE) rtl:0x10015a40; dbg:0x100315a0
//
// Set the source render mode.
//
// Returns: The source SetRenderMode result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetRenderMode(DWORD dwMode)
{
	return (m_pSource->SetRenderMode(dwMode));
}

/* =============================================================
// CA3dSourceCom::GetPlayTime()
// (RE) rtl:0x10015a60; dbg:0x100315e0
//
// Read the source playback time.
//
// Returns: The source GetPlayTime result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetPlayTime(LPA3DVAL pfTime)
{
	return (m_pSource->GetPlayTime(pfTime));
}

/* =============================================================
// CA3dSourceCom::GetPlayPosition()
// (RE) rtl:0x10015a80; dbg:0x10031620
//
// Read the source playback byte position.
//
// Returns: The source GetPlayPosition result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetPlayPosition(LPDWORD pdwPosition)
{
	return (m_pSource->GetPlayPosition(pdwPosition));
}

/* =============================================================
// CA3dSourceCom::GetPosition3f()
// (RE) rtl:0x10015aa0; dbg:0x10031660
//
// Read the source position components.
//
// Returns: The source GetPosition3f result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetPosition3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz)
{
	return (m_pSource->GetPosition3f(px, py, pz));
}

/* =============================================================
// CA3dSourceCom::GetPosition3fv()
// (RE) rtl:0x10015ac0; dbg:0x100316a0
//
// Read the source position vector.
//
// Returns: The source GetPosition3fv result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetPosition3fv(LPA3DVAL pfPosition)
{
	return (m_pSource->GetPosition3fv(pfPosition));
}

/* =============================================================
// CA3dSourceCom::GetOrientationAngles3f()
// (RE) rtl:0x10015ae0; dbg:0x100316e0
//
// Read the source orientation angles.
//
// Returns: The source GetOrientationAngles3f result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetOrientationAngles3f(LPA3DVAL pfHeading, LPA3DVAL pfPitch,
				      LPA3DVAL pfRoll)
{
	return (m_pSource->GetOrientationAngles3f(pfHeading, pfPitch, pfRoll));
}

/* =============================================================
// CA3dSourceCom::GetOrientationAngles3fv()
// (RE) rtl:0x10015b00; dbg:0x10031720
//
// Read the source orientation-angle vector.
//
// Returns: The source GetOrientationAngles3fv result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetOrientationAngles3fv(LPA3DVAL pfAngles)
{
	return (m_pSource->GetOrientationAngles3fv(pfAngles));
}

/* =============================================================
// CA3dSourceCom::GetOrientation6f()
// (RE) rtl:0x10015b20; dbg:0x10031760
//
// Read the source front and up components.
//
// Returns: The source GetOrientation6f result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetOrientation6f(LPA3DVAL pfXfront, LPA3DVAL pfYfront,
				LPA3DVAL pfZfront, LPA3DVAL pfXup,
				LPA3DVAL pfYup, LPA3DVAL pfZup)
{
	return (m_pSource->GetOrientation6f(pfXfront, pfYfront, pfZfront,
					    pfXup, pfYup, pfZup));
}

/* =============================================================
// CA3dSourceCom::GetOrientation6fv()
// (RE) rtl:0x10015b50; dbg:0x100317b0
//
// Read the source front and up vectors.
//
// Returns: The source GetOrientation6fv result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetOrientation6fv(LPA3DVAL pfOrientation)
{
	return (m_pSource->GetOrientation6fv(pfOrientation));
}

/* =============================================================
// CA3dSourceCom::GetVelocity3f()
// (RE) rtl:0x10015b70; dbg:0x100317f0
//
// Read the source velocity components.
//
// Returns: The source GetVelocity3f result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetVelocity3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz)
{
	return (m_pSource->GetVelocity3f(px, py, pz));
}

/* =============================================================
// CA3dSourceCom::GetVelocity3fv()
// (RE) rtl:0x10015ba0; dbg:0x10031830
//
// Read the source velocity vector.
//
// Returns: The source GetVelocity3fv result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetVelocity3fv(LPA3DVAL pfVelocity)
{
	return (m_pSource->GetVelocity3fv(pfVelocity));
}

/* =============================================================
// CA3dSourceCom::GetCone()
// (RE) rtl:0x10015bc0; dbg:0x10031870
//
// Read the source directional cone.
//
// Returns: The source GetCone result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetCone(LPA3DVAL pfInnerAngle, LPA3DVAL pfOuterAngle,
		       LPA3DVAL pfOutsideGain)
{
	return (m_pSource->GetCone(pfInnerAngle, pfOuterAngle, pfOutsideGain));
}

/* =============================================================
// CA3dSourceCom::GetMinMaxDistance()
// (RE) rtl:0x10015bf0; dbg:0x100318b0
//
// Read the source distance limits and flags.
//
// Returns: The source GetMinMaxDistance result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetMinMaxDistance(LPA3DVAL pfMin, LPA3DVAL pfMax, LPDWORD pdwFlags)
{
	return (m_pSource->GetMinMaxDistance(pfMin, pfMax, pdwFlags));
}

/* =============================================================
// CA3dSourceCom::GetGain()
// (RE) rtl:0x10015c20; dbg:0x100318f0
//
// Read the source gain.
//
// Returns: The source GetGain result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetGain(LPA3DVAL pfGain)
{
	return (m_pSource->GetGain(pfGain));
}

/* =============================================================
// CA3dSourceCom::GetPitch()
// (RE) rtl:0x10015c40; dbg:0x10031930
//
// Read the source pitch.
//
// Returns: The source GetPitch result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetPitch(LPA3DVAL pfPitch)
{
	return (m_pSource->GetPitch(pfPitch));
}

/* =============================================================
// CA3dSourceCom::GetDopplerScale()
// (RE) rtl:0x10015c60; dbg:0x10031970
//
// Read the source Doppler scale.
//
// Returns: The source GetDopplerScale result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetDopplerScale(LPA3DVAL pfScale)
{
	return (m_pSource->GetDopplerScale(pfScale));
}

/* =============================================================
// CA3dSourceCom::GetDistanceModelScale()
// (RE) rtl:0x10015c80; dbg:0x100319b0
//
// Read the source distance-model scale.
//
// Returns: The source GetDistanceModelScale result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetDistanceModelScale(LPA3DVAL pfScale)
{
	return (m_pSource->GetDistanceModelScale(pfScale));
}

/* =============================================================
// CA3dSourceCom::GetEq()
// (RE) rtl:0x10015ca0; dbg:0x100319f0
//
// Read the source equalization.
//
// Returns: The source GetEq result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetEq(LPA3DVAL pfEq)
{
	return (m_pSource->GetEq(pfEq));
}

/* =============================================================
// CA3dSourceCom::GetPriority()
// (RE) rtl:0x10015cc0; dbg:0x10031a30
//
// Read the source priority.
//
// Returns: The source GetPriority result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetPriority(LPA3DVAL pfPriority)
{
	return (m_pSource->GetPriority(pfPriority));
}

/* =============================================================
// CA3dSourceCom::GetRenderMode()
// (RE) rtl:0x10015ce0; dbg:0x10031a70
//
// Read the source render mode.
//
// Returns: The source GetRenderMode result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetRenderMode(LPDWORD pdwMode)
{
	return (m_pSource->GetRenderMode(pdwMode));
}

/* =============================================================
// CA3dSourceCom::GetStatus()
// (RE) rtl:0x10015d00; dbg:0x10031ab0
//
// Read the source playback status.
//
// Returns: The source GetStatus result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetStatus(LPDWORD pdwStatus)
{
	return (m_pSource->GetStatus(pdwStatus));
}

/* =============================================================
// CA3dSourceCom::GetAudibility()
// (RE) rtl:0x10015d20; dbg:0x10031af0
//
// Read the source audibility.
//
// Returns: The source GetAudibility result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetAudibility(LPA3DVAL pfAudibility)
{
	return (m_pSource->GetAudibility(pfAudibility));
}

/* =============================================================
// CA3dSourceCom::GetOcclusionFactor()
// (RE) rtl:0x10015d40; dbg:0x10031b30
//
// Read the source occlusion factor.
//
// Returns: The source GetOcclusionFactor result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetOcclusionFactor(LPA3DVAL pfOcclusion)
{
	return (m_pSource->GetOcclusionFactor(pfOcclusion));
}

/* =============================================================
// CA3dSourceCom::SetPanValues()
// (RE) rtl:0x10015d60; dbg:0x10031b70
//
// Set the source pan values.
//
// Returns: The source SetPanValues result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetPanValues(DWORD dwNumFields, LPA3DVAL pfPanValues)
{
	return (m_pSource->SetPanValues(dwNumFields, pfPanValues));
}

/* =============================================================
// CA3dSourceCom::GetPanValues()
// (RE) rtl:0x10015d80; dbg:0x10031bb0
//
// Read the source pan values.
//
// Returns: The source GetPanValues result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetPanValues(DWORD dwNumFields, LPA3DVAL pfPanValues)
{
	return (m_pSource->GetPanValues(dwNumFields, pfPanValues));
}

/* =============================================================
// CA3dSourceCom::SetPlayEvent()
// (RE) rtl:0x10015da0; dbg:0x10031bf0
//
// Register a source playback event.
//
// Returns: The source SetPlayEvent result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetPlayEvent(DWORD dwPosition, HANDLE hEvent)
{
	return (m_pSource->SetPlayEvent(dwPosition, hEvent));
}

/* =============================================================
// CA3dSourceCom::ClearPlayEvents()
// (RE) rtl:0x10015dc0; dbg:0x10031c30
//
// Clear source playback events.
//
// Returns: The source ClearPlayEvents result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::ClearPlayEvents(void)
{
	return (m_pSource->ClearPlayEvents());
}

/* =============================================================
// CA3dSourceCom::SetTransformMode()
// (RE) rtl:0x10015de0; dbg:0x10031c60
//
// Set the source transform mode.
//
// Returns: The source SetTransformMode result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetTransformMode(DWORD dwMode)
{
	return (m_pSource->SetTransformMode(dwMode));
}

/* =============================================================
// CA3dSourceCom::GetTransformMode()
// (RE) rtl:0x10015e00; dbg:0x10031ca0
//
// Read the source transform mode.
//
// Returns: The source GetTransformMode result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetTransformMode(LPDWORD pdwMode)
{
	return (m_pSource->GetTransformMode(pdwMode));
}

/* =============================================================
// CA3dSourceCom::SetReflectionDelayScale()
// (RE) rtl:0x10015e20; dbg:0x10031ce0
//
// Set the source reflection delay scale.
//
// Returns: The source SetReflectionDelayScale result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetReflectionDelayScale(A3DVAL fScale)
{
	return (m_pSource->SetReflectionDelayScale(fScale));
}

/* =============================================================
// CA3dSourceCom::GetReflectionDelayScale()
// (RE) rtl:0x10015e40; dbg:0x10031d20
//
// Read the source reflection delay scale.
//
// Returns: The source GetReflectionDelayScale result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetReflectionDelayScale(LPA3DVAL pfScale)
{
	return (m_pSource->GetReflectionDelayScale(pfScale));
}

/* =============================================================
// CA3dSourceCom::SetReflectionGainScale()
// (RE) rtl:0x10015e60; dbg:0x10031d60
//
// Set the source reflection gain scale.
//
// Returns: The source SetReflectionGainScale result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetReflectionGainScale(A3DVAL fScale)
{
	return (m_pSource->SetReflectionGainScale(fScale));
}

/* =============================================================
// CA3dSourceCom::GetReflectionGainScale()
// (RE) rtl:0x10015e80; dbg:0x10031da0
//
// Read the source reflection gain scale.
//
// Returns: The source GetReflectionGainScale result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetReflectionGainScale(LPA3DVAL pfScale)
{
	return (m_pSource->GetReflectionGainScale(pfScale));
}

/* =============================================================
// CA3dSourceCom::SetVolumetricBounds()
// (RE) rtl:0x10015ea0; dbg:0x10031de0
//
// Set the source volumetric bounds.
//
// Returns: The source SetVolumetricBounds result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetVolumetricBounds(A3DVAL x, A3DVAL y, A3DVAL z)
{
	return (m_pSource->SetVolumetricBounds(x, y, z));
}

/* =============================================================
// CA3dSourceCom::GetVolumetricBounds()
// (RE) rtl:0x10015ed0; dbg:0x10031e20
//
// Read the source volumetric bounds.
//
// Returns: The source GetVolumetricBounds result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetVolumetricBounds(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz)
{
	return (m_pSource->GetVolumetricBounds(px, py, pz));
}

/* =============================================================
// CA3dSourceCom::SetVolumetricDamping()
// (RE) rtl:0x10015f00; dbg:0x10031e60
//
// Set the source volumetric damping.
//
// Returns: The source SetVolumetricDamping result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetVolumetricDamping(A3DVOLSRCDAMPINFO *pDampInfo)
{
	return (m_pSource->SetVolumetricDamping(pDampInfo));
}

/* =============================================================
// CA3dSourceCom::GetVolumetricDamping()
// (RE) rtl:0x10015f20; dbg:0x10031ea0
//
// Read the source volumetric damping.
//
// Returns: The source GetVolumetricDamping result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetVolumetricDamping(A3DVOLSRCDAMPINFO *pDampInfo)
{
	return (m_pSource->GetVolumetricDamping(pDampInfo));
}

/* =============================================================
// CA3dSourceCom::SetReverbMix()
// (RE) rtl:0x10015f40; dbg:0x10031ee0
//
// Set the source reverb mix.
//
// Returns: The source SetReverbMix result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::SetReverbMix(A3DVAL fMix, A3DVAL fDirectHF)
{
	return (m_pSource->SetReverbMix(fMix, fDirectHF));
}

/* =============================================================
// CA3dSourceCom::GetReverbMix()
// (RE) rtl:0x10015f60; dbg:0x10031f20
//
// Read the source reverb mix.
//
// Returns: The source GetReverbMix result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetReverbMix(A3DVAL *pfMix, A3DVAL *pfDirectHF)
{
	return (m_pSource->GetReverbMix(pfMix, pfDirectHF));
}

/* =============================================================
// CA3dSourceCom::NewManualReflection()
// (RE) rtl:0x10015f80; dbg:0x10031f60
//
// Create a manual reflection through the source.
//
// Returns: The source NewManualReflection result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::NewManualReflection(LPA3DREFLECTION *ppReflection)
{
	return (m_pSource->NewManualReflection(ppReflection));
}

/* =============================================================
// CA3dSourceCom::FreeManualReflections()
// (RE) rtl:0x10015fa0; dbg:0x10031fa0
//
// Free the source manual reflections.
//
// Returns: The source FreeManualReflections result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::FreeManualReflections(void)
{
	return (m_pSource->FreeManualReflections());
}

/* =============================================================
// CA3dSourceCom::GetNumManualReflections()
// (RE) rtl:0x10015fc0; dbg:0x10031fd0
//
// Read the number of source manual reflections.
//
// Returns: The source GetNumManualReflections result.
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetNumManualReflections(int *pnReflections)
{
	return (m_pSource->GetNumManualReflections(pnReflections));
}

/* =============================================================
// CA3dSourceCom::LoadFile()
// (RE) rtl:0x10015fe0; dbg:0x10032010
//
// Select a source implementation and load the file, falling back to software
// AC-3 when unlocked. The absent instance-count gate makes every call attempt
// the hardware graph.
//
// Returns:
//   The source LoadFile or allocation result
//   A3DERROR_SOURCE_IN_USE              when in use
//   E_POINTER                           a null filename
//   A3DERROR_INCORRECT_FORMAT_SPECIFIED
//                                       unsupported formats
//   A3DERROR_MUST_UNLOCK_SOFTAC3_BEFORE_USE
//                                       locked fallback
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::LoadFile(char *szFile, DWORD dwFormat)
{
HRESULT	hr;

	if (InUse())
		return (A3DERROR_SOURCE_IN_USE);

	if (!szFile)
	{
		DBGSTR(
			"CA3dSourceCom::LoadFile() - Null filename pointer.\n");
		return (E_POINTER);
	}

	hr = S_OK;

	try
	{
		switch (dwFormat)
		{
		case A3DSOURCE_FORMAT_WAVE:
		case A3DSOURCE_FORMAT_MP3:
		case A3DSOURCE_FORMAT_WAVE | A3DSOURCE_STREAMING:
		case A3DSOURCE_FORMAT_MP3  | A3DSOURCE_STREAMING:
			hr = AllocateSource(A3D_SOURCE_POINTER_WAVE);
			if (FAILED(hr))
				throw "Failed to allocate source.";

			hr = m_pSource->LoadFile(szFile, dwFormat);
			if (FAILED(hr))
				throw "Failed load source.";

			return (hr);

		case A3DSOURCE_FORMAT_AC3:
		case A3DSOURCE_FORMAT_AC3 | A3DSOURCE_STREAMING:
			break;

		default:
			hr = A3DERROR_INCORRECT_FORMAT_SPECIFIED;
			throw "Unknown file format";
		}

		hr = AllocateSource(A3D_SOURCE_POINTER_AC3_GRAPH);

		if (SUCCEEDED(hr))
			hr = m_pSource->LoadFile(szFile, dwFormat);

		if (SUCCEEDED(hr))
			return (hr);

		DBGSTR(
			"CA3dSourceCom::LoadFile - Failed hardware Ac3.\n");

		if (!m_pApi->m_dwSoftAC3Unlocked)
		{
			hr = A3DERROR_MUST_UNLOCK_SOFTAC3_BEFORE_USE;

			throw "Software AC3 decoder must be unlocked before use.\n";
		}

		hr = AllocateSource(A3D_SOURCE_POINTER_WAVE);
		if (FAILED(hr))
			throw "Unable to allocate fallback ac3 source.";

		hr = m_pSource->LoadFile(szFile, dwFormat);
		if (FAILED(hr))
			throw "Failed to load stream AC3 file";
	}
	/* (RE) Catch handler: dbg:0x100322a2. */
	catch (const char *pszWhy)
	{
		TRACE("CA3dSourceCom::LoadFile - %s\n", pszWhy);
	}

	return (hr);
}

/* =============================================================
// CA3dSourceCom::GetCaps()
// (RE) dbg:0x100323d0; rtl:0x100161e0
//
// Validate the caller's capability structure and read source capabilities.
//
// Returns:
//   The source GetCaps result
//   E_POINTER                  a null structure
//   E_INVALIDARG               a dwSize other than sizeof(A3DCAPS_SOURCE)
// =============================================================*/

STDMETHODIMP
CA3dSourceCom::GetCaps(LPA3DCAPS_SOURCE pCaps)
{
	if (!pCaps)
	{
		TRACE("CA3dSource::GetCaps - caps pointer is NULL.\n");

		return (E_POINTER);
	}

	if (pCaps->dwSize != sizeof(A3DCAPS_SOURCE))
	{
		TRACE("CA3dSource::GetCaps - Invalid size set.\n");

		return (E_INVALIDARG);
	}

	return (m_pSource->GetCaps(pCaps));
}
