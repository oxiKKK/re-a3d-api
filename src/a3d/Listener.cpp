/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * Listener.cpp
 *
 * Implements the compatibility primary-buffer container and the
 * DirectSound buffer and listener interfaces attached to it. Buffer
 * operations forward to the wrapped primary buffer, while listener
 * operations update the compatibility DSP engine.
 *
 * Listener changes can be immediate or deferred. CommitDeferredSettings
 * updates active sources when their own state or the listener has
 * changed. The interface objects share the primary container's lifetime,
 * and source calculations are delegated to a3ddsp.cpp.
 *
 *---------------------------------------------------------------------------
 */

#include "a3dprv.h"
#include "Listener.h"
#include "A3d.h"
#include "A3dSource.h"
#include "a3ddsp.h"

#include <stdlib.h>

CA3dListener	*g_pListener;

/* Set by listener setters and cleared after the deferred-source sweep. */

DWORD		g_fListenerDirty;

/* =============================================================
// A3dListenerPush()
//
// Send the complete listener transform to the engine.
// =============================================================*/

static void
A3dListenerPush(const DS3DLISTENER *pcL)
{
	A3dListenerSetTransform(pcL->vPosition.x, pcL->vPosition.y,
				pcL->vPosition.z,
				pcL->vOrientFront.x, pcL->vOrientFront.y,
				pcL->vOrientFront.z,
				pcL->vOrientTop.x, pcL->vOrientTop.y,
				pcL->vOrientTop.z,
				pcL->vVelocity.x, pcL->vVelocity.y,
				pcL->vVelocity.z);
}

#define PRIMARY_BUFFER	(m_pOwner->m_pBuffer)

/* =============================================================
// QueryInterface()
// (RE) a3d.dll rtl:0x10004270
//
// Query the owner, then the wrapped buffer if the owner query fails.
//
// Returns: The successful owner query or the wrapped QueryInterface result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::QueryInterface(REFIID riid, void **ppv)
{
HRESULT	hr;

	hr = m_pOwner->QueryInterface(riid, ppv);

	if (FAILED(hr))
		hr = PRIMARY_BUFFER->QueryInterface(riid, ppv);

	return (hr);
}

/* =============================================================
// AddRef()
//
// Increment local and owner reference counts.
//
// Returns: The owner AddRef result.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dPrimaryDSB::AddRef(void)
{
	m_cRef++;

	return (m_pOwner->AddRef());
}

/* =============================================================
// Release()
//
// Decrement local and owner reference counts.
//
// Returns: The owner Release result.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dPrimaryDSB::Release(void)
{
	m_cRef--;

	return (m_pOwner->Release());
}

/* =============================================================
// GetCaps()
//
// Read buffer capabilities.
//
// Returns: The wrapped GetCaps result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::GetCaps(LPDSBCAPS pCaps)
{
	return (PRIMARY_BUFFER->GetCaps(pCaps));
}

/* =============================================================
// GetCurrentPosition()
//
// Read playback and write cursors.
//
// Returns: The wrapped GetCurrentPosition result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::GetCurrentPosition(LPDWORD pdwPlay, LPDWORD pdwWrite)
{
	return (PRIMARY_BUFFER->GetCurrentPosition(pdwPlay, pdwWrite));
}

/* =============================================================
// GetFormat()
//
// Read the buffer format.
//
// Returns: The wrapped GetFormat result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::GetFormat(LPWAVEFORMATEX pwfx, DWORD cbSize, LPDWORD pcbWritten)
{
	return (PRIMARY_BUFFER->GetFormat(pwfx, cbSize, pcbWritten));
}

/* =============================================================
// GetVolume()
//
// Read the buffer volume.
//
// Returns: The wrapped GetVolume result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::GetVolume(LPLONG plVolume)
{
	return (PRIMARY_BUFFER->GetVolume(plVolume));
}

/* =============================================================
// GetPan()
//
// Read the buffer pan.
//
// Returns: The wrapped GetPan result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::GetPan(LPLONG plPan)
{
	return (PRIMARY_BUFFER->GetPan(plPan));
}

/* =============================================================
// GetFrequency()
//
// Read the playback frequency.
//
// Returns: The wrapped GetFrequency result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::GetFrequency(LPDWORD pdwFrequency)
{
	return (PRIMARY_BUFFER->GetFrequency(pdwFrequency));
}

/* =============================================================
// GetStatus()
//
// Read the buffer status.
//
// Returns: The wrapped GetStatus result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::GetStatus(LPDWORD pdwStatus)
{
	return (PRIMARY_BUFFER->GetStatus(pdwStatus));
}

/* =============================================================
// Initialize()
//
// Initialize the wrapped buffer.
//
// Returns: The wrapped Initialize result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::Initialize(LPDIRECTSOUND pDS, LPCDSBUFFERDESC pcDesc)
{
	return (PRIMARY_BUFFER->Initialize(pDS, pcDesc));
}

/* =============================================================
// Lock()
//
// Lock buffer regions for audio access.
//
// Returns: The wrapped Lock result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::Lock(DWORD dwOffset, DWORD dwBytes, LPVOID *ppvAudio1,
		     LPDWORD pdwAudio1, LPVOID *ppvAudio2, LPDWORD pdwAudio2,
		     DWORD dwFlags)
{
	return (PRIMARY_BUFFER->Lock(dwOffset, dwBytes, ppvAudio1, pdwAudio1,
				     ppvAudio2, pdwAudio2, dwFlags));
}

/* =============================================================
// Play()
//
// Start buffer playback.
//
// Returns: The wrapped Play result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::Play(DWORD dwReserved1, DWORD dwReserved2, DWORD dwFlags)
{
	return (PRIMARY_BUFFER->Play(dwReserved1, dwReserved2, dwFlags));
}

/* =============================================================
// SetCurrentPosition()
//
// Set the playback cursor.
//
// Returns: The wrapped SetCurrentPosition result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::SetCurrentPosition(DWORD dwPosition)
{
	return (PRIMARY_BUFFER->SetCurrentPosition(dwPosition));
}

/* =============================================================
// SetFormat()
//
// Set the buffer format.
//
// Returns: The wrapped SetFormat result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::SetFormat(LPCWAVEFORMATEX pcfxFormat)
{
	return (PRIMARY_BUFFER->SetFormat(pcfxFormat));
}

/* =============================================================
// SetVolume()
//
// Set the buffer volume.
//
// Returns: The wrapped SetVolume result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::SetVolume(LONG lVolume)
{
	return (PRIMARY_BUFFER->SetVolume(lVolume));
}

/* =============================================================
// SetPan()
//
// Set the buffer pan.
//
// Returns: The wrapped SetPan result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::SetPan(LONG lPan)
{
	return (PRIMARY_BUFFER->SetPan(lPan));
}

/* =============================================================
// SetFrequency()
//
// Set the playback frequency.
//
// Returns: The wrapped SetFrequency result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::SetFrequency(DWORD dwFrequency)
{
	return (PRIMARY_BUFFER->SetFrequency(dwFrequency));
}

/* =============================================================
// Stop()
//
// Stop buffer playback.
//
// Returns: The wrapped Stop result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::Stop(void)
{
	return (PRIMARY_BUFFER->Stop());
}

/* =============================================================
// Unlock()
//
// Unlock audio regions.
//
// Returns: The wrapped Unlock result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::Unlock(LPVOID pvAudio1, DWORD dwAudio1, LPVOID pvAudio2,
		       DWORD dwAudio2)
{
	return (PRIMARY_BUFFER->Unlock(pvAudio1, dwAudio1, pvAudio2, dwAudio2));
}

/* =============================================================
// Restore()
//
// Restore the wrapped buffer.
//
// Returns: The wrapped Restore result.
// =============================================================*/

STDMETHODIMP
CA3dPrimaryDSB::Restore(void)
{
	return (PRIMARY_BUFFER->Restore());
}

/* =============================================================
// CA3dListener()
//
// Initialize DirectSound listener defaults and the owner reference.
// =============================================================*/

CA3dListener::CA3dListener(CA3dPrimaryBuffer *pOwner)
{
	m_pOwner = pOwner;
	m_cRef   = 0;

	m_ds3dl.dwSize = sizeof(DS3DLISTENER);

	m_ds3dl.vPosition.x = 0.0f;
	m_ds3dl.vPosition.y = 0.0f;
	m_ds3dl.vPosition.z = 0.0f;

	m_ds3dl.vVelocity.x = 0.0f;
	m_ds3dl.vVelocity.y = 0.0f;
	m_ds3dl.vVelocity.z = 0.0f;

	m_ds3dl.vOrientFront.x = 0.0f;
	m_ds3dl.vOrientFront.y = 0.0f;
	m_ds3dl.vOrientFront.z = 1.0f;

	m_ds3dl.vOrientTop.x = 0.0f;
	m_ds3dl.vOrientTop.y = 1.0f;
	m_ds3dl.vOrientTop.z = 0.0f;

	m_ds3dl.flDistanceFactor = DS3D_DEFAULTDISTANCEFACTOR;
	m_ds3dl.flRolloffFactor  = DS3D_DEFAULTROLLOFFFACTOR;
	m_ds3dl.flDopplerFactor  = DS3D_DEFAULTDOPPLERFACTOR;
}

/* =============================================================
// QueryInterface()
// (RE) a3d.dll rtl:0x10004270
//
// Query the owner, then the wrapped buffer if the owner query fails.
//
// Returns: The successful owner query or the wrapped QueryInterface result.
// =============================================================*/

STDMETHODIMP
CA3dListener::QueryInterface(REFIID riid, void **ppv)
{
HRESULT	hr;

	hr = m_pOwner->QueryInterface(riid, ppv);

	if (FAILED(hr))
		hr = m_pOwner->m_pBuffer->QueryInterface(riid, ppv);

	return (hr);
}

/* =============================================================
// AddRef()
//
// Increment local and owner reference counts.
//
// Returns: The owner AddRef result.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dListener::AddRef(void)
{
	m_cRef++;

	return (m_pOwner->AddRef());
}

/* =============================================================
// Release()
//
// Decrement local and owner reference counts.
//
// Returns: The owner Release result.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dListener::Release(void)
{
	m_cRef--;

	return (m_pOwner->Release());
}

/* =============================================================
// GetAllParameters()
// (RE) a3d.dll rtl:0x10004630
//
// Copy listener state. Preserve the original unbounded caller size,
// including copy-length underflow and over-read.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dListener::GetAllParameters(LPDS3DLISTENER pListener)
{
	if (!pListener)
		return (E_INVALIDARG);

	memcpy(&pListener->vPosition, &m_ds3dl.vPosition,
	       pListener->dwSize - sizeof(DWORD));

	return (S_OK);
}

/* =============================================================
// GetDistanceFactor()
//
// Read the stored listener distance factor.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dListener::GetDistanceFactor(D3DVALUE *pflDistanceFactor)
{
	if (!pflDistanceFactor)
		return (E_INVALIDARG);

	*pflDistanceFactor = m_ds3dl.flDistanceFactor;

	return (S_OK);
}

/* =============================================================
// GetDopplerFactor()
//
// Read the stored listener Doppler factor.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dListener::GetDopplerFactor(D3DVALUE *pflDopplerFactor)
{
	if (!pflDopplerFactor)
		return (E_INVALIDARG);

	*pflDopplerFactor = m_ds3dl.flDopplerFactor;

	return (S_OK);
}

/* =============================================================
// GetOrientation()
//
// Read the stored listener front and top vectors.
//
// Returns:
//   S_OK
//   E_INVALIDARG  if either output is null
// =============================================================*/

STDMETHODIMP
CA3dListener::GetOrientation(D3DVECTOR *pvOrientFront, D3DVECTOR *pvOrientTop)
{
	if (!pvOrientFront || !pvOrientTop)
		return (E_INVALIDARG);

	*pvOrientFront = m_ds3dl.vOrientFront;
	*pvOrientTop   = m_ds3dl.vOrientTop;

	return (S_OK);
}

/* =============================================================
// GetPosition()
//
// Read the stored listener position.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dListener::GetPosition(D3DVECTOR *pvPosition)
{
	if (!pvPosition)
		return (E_INVALIDARG);

	*pvPosition = m_ds3dl.vPosition;

	return (S_OK);
}

/* =============================================================
// GetRolloffFactor()
//
// Read the stored listener rolloff factor.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dListener::GetRolloffFactor(D3DVALUE *pflRolloffFactor)
{
	if (!pflRolloffFactor)
		return (E_INVALIDARG);

	*pflRolloffFactor = m_ds3dl.flRolloffFactor;

	return (S_OK);
}

/* =============================================================
// GetVelocity()
//
// Read the stored listener velocity.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dListener::GetVelocity(D3DVECTOR *pvVelocity)
{
	if (!pvVelocity)
		return (E_INVALIDARG);

	*pvVelocity = m_ds3dl.vVelocity;

	return (S_OK);
}

/* =============================================================
// SetAllParameters()
// (RE) a3d.dll rtl:0x10004750
//
// Replace listener state and update the engine, immediately or deferred.
// Preserve the stored-size copy, which over-reads truncated caller structures.
//
// Returns:
//   S_OK
//   E_INVALIDARG  null input or a negative listener factor
// =============================================================*/

STDMETHODIMP
CA3dListener::SetAllParameters(LPCDS3DLISTENER pcListener, DWORD dwApply)
{
	if (!pcListener)
		return (E_INVALIDARG);

	if (pcListener->flDistanceFactor < 0.0f ||
	    pcListener->flRolloffFactor  < 0.0f ||
	    pcListener->flDopplerFactor  < 0.0f)
		return (E_INVALIDARG);

	memcpy(&m_ds3dl.vPosition, &pcListener->vPosition,
	       m_ds3dl.dwSize - sizeof(DWORD));

	A3dSetDopplerAndRolloff(m_ds3dl.flDopplerFactor,
				m_ds3dl.flRolloffFactor,
				m_ds3dl.flRolloffFactor);
	A3dListenerPush(&m_ds3dl);

	g_fListenerDirty = 1;

	if (dwApply == DS3D_IMMEDIATE)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetDistanceFactor()
//
// Set the listener distance factor, immediately or deferred.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a negative factor
// =============================================================*/

STDMETHODIMP
CA3dListener::SetDistanceFactor(D3DVALUE flDistanceFactor, DWORD dwApply)
{
	if (flDistanceFactor < 0.0f)
		return (E_INVALIDARG);

	m_ds3dl.flDistanceFactor = flDistanceFactor;

	A3dSetDistanceFactor(flDistanceFactor);

	g_fListenerDirty = 1;

	if (dwApply == DS3D_IMMEDIATE)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetDopplerFactor()
//
// Set the listener Doppler factor, immediately or deferred.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a negative factor
// =============================================================*/

STDMETHODIMP
CA3dListener::SetDopplerFactor(D3DVALUE flDopplerFactor, DWORD dwApply)
{
	if (flDopplerFactor < 0.0f)
		return (E_INVALIDARG);

	m_ds3dl.flDopplerFactor = flDopplerFactor;

	A3dSetDopplerAndRolloff(m_ds3dl.flDopplerFactor,
				m_ds3dl.flRolloffFactor,
				m_ds3dl.flRolloffFactor);

	g_fListenerDirty = 1;

	if (dwApply == DS3D_IMMEDIATE)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetOrientation()
//
// Set the listener front and top vectors, immediately or deferred.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dListener::SetOrientation(D3DVALUE xFront, D3DVALUE yFront, D3DVALUE zFront,
			     D3DVALUE xTop, D3DVALUE yTop, D3DVALUE zTop,
			     DWORD dwApply)
{
	m_ds3dl.vOrientFront.x = xFront;
	m_ds3dl.vOrientFront.y = yFront;
	m_ds3dl.vOrientFront.z = zFront;

	m_ds3dl.vOrientTop.x = xTop;
	m_ds3dl.vOrientTop.y = yTop;
	m_ds3dl.vOrientTop.z = zTop;

	A3dListenerPush(&m_ds3dl);

	g_fListenerDirty = 1;

	if (dwApply == DS3D_IMMEDIATE)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetPosition()
//
// Set the listener position, immediately or deferred.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dListener::SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
{
	m_ds3dl.vPosition.x = x;
	m_ds3dl.vPosition.y = y;
	m_ds3dl.vPosition.z = z;

	A3dListenerPush(&m_ds3dl);

	g_fListenerDirty = 1;

	if (dwApply == DS3D_IMMEDIATE)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetRolloffFactor()
//
// Set rolloff, clamping to DS3D_MAXROLLOFFFACTOR, immediately or deferred.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a negative factor
// =============================================================*/

STDMETHODIMP
CA3dListener::SetRolloffFactor(D3DVALUE flRolloffFactor, DWORD dwApply)
{
	if (flRolloffFactor < 0.0f)
		return (E_INVALIDARG);

	if (flRolloffFactor > DS3D_MAXROLLOFFFACTOR)
		flRolloffFactor = DS3D_MAXROLLOFFFACTOR;

	m_ds3dl.flRolloffFactor = flRolloffFactor;

	A3dSetDopplerAndRolloff(m_ds3dl.flDopplerFactor,
				m_ds3dl.flRolloffFactor,
				m_ds3dl.flRolloffFactor);

	g_fListenerDirty = 1;

	if (dwApply == DS3D_IMMEDIATE)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// SetVelocity()
//
// Set the listener velocity, immediately or deferred.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dListener::SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
{
	m_ds3dl.vVelocity.x = x;
	m_ds3dl.vVelocity.y = y;
	m_ds3dl.vVelocity.z = z;

	A3dListenerPush(&m_ds3dl);

	g_fListenerDirty = 1;

	if (dwApply == DS3D_IMMEDIATE)
		CommitDeferredSettings();

	return (S_OK);
}

/* =============================================================
// CommitDeferredSettings()
// (RE) a3d.dll rtl:0x100045C0
//
// Commit dirty active sources, or all active sources after a listener change.
//
// Returns: S_OK, including source commit failures.
// =============================================================*/

STDMETHODIMP
CA3dListener::CommitDeferredSettings(void)
{
LONG	cSources;
LONG	i;

	cSources = g_cSources;

	for (i = 0; i < cSources; i++)
	{
		if ((g_fListenerDirty || g_aSourceDirty[i]) &&
		    i >= 0 && i < cSources && g_apSourceActive[i])
		{
			g_apSourceObject[i]->CommitOne();

			cSources = g_cSources;
		}
	}

	g_fListenerDirty = 0;

	return (S_OK);
}

/* =============================================================
// CA3dPrimaryBuffer()
//
// Initialize the primary buffer container and its owning list.
// =============================================================*/

CA3dPrimaryBuffer::CA3dPrimaryBuffer(A3DPLEX *pPlex)
{
	m_cRef         = 0;
	m_Unknown_0x08 = 0;
	m_pDSB         = NULL;
	m_pListener    = NULL;
	m_pBuffer      = NULL;

	m_pPlex			= pPlex;
}

/* =============================================================
// Init()
// (RE) a3d.dll rtl:0x10004050
//
// Take ownership of the wrapped buffer and allocate its two interfaces.
//
// Returns:
//   S_OK
//   E_OUTOFMEMORY  if either interface allocation fails
// =============================================================*/

HRESULT
CA3dPrimaryBuffer::Init(LPDIRECTSOUNDBUFFER pBuffer)
{
	m_pBuffer = pBuffer;

	m_pDSB = new CA3dPrimaryDSB;

	if (m_pDSB)
	{
		m_pDSB->m_cRef   = 0;
		m_pDSB->m_pOwner = this;
	}

	if (!m_pDSB)
		return (E_OUTOFMEMORY);

	m_pListener = new CA3dListener(this);

	if (!m_pListener)
	{
		delete m_pDSB;
		m_pDSB = NULL;

		return (E_OUTOFMEMORY);
	}

	g_pListener = m_pListener;

	return (S_OK);
}

/* =============================================================
// ~CA3dPrimaryBuffer()
//
// Release the wrapped buffer and delete both interface objects.
// =============================================================*/

CA3dPrimaryBuffer::~CA3dPrimaryBuffer(void)
{
	if (m_pBuffer)
	{
		m_pBuffer->Release();
		m_pBuffer = NULL;
	}

	if (m_pDSB)
	{
		delete m_pDSB;
		m_pDSB = NULL;
	}

	if (m_pListener)
	{
		delete m_pListener;
		m_pListener = NULL;
	}
}

/* =============================================================
// QueryInterface()
//
// Acquire the container or either DirectSound interface.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  otherwise
// =============================================================*/

STDMETHODIMP
CA3dPrimaryBuffer::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_INVALIDARG);

	*ppv = NULL;

	if (riid == IID_IUnknown)
		*ppv = (void *) this;

	if (riid == IID_IDirectSoundBuffer)
		*ppv = (void *) m_pDSB;

	if (riid == IID_IDirectSound3DListener)
		*ppv = (void *) m_pListener;

	if (!*ppv)
		return (E_NOINTERFACE);

	((IUnknown *) *ppv)->AddRef();

	return (S_OK);
}

/* =============================================================
// AddRef()
//
// Increment the container reference count.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dPrimaryBuffer::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
//
// Release a reference; at zero, remove the container from its list and delete it.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dPrimaryBuffer::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	A3dPlexRemove(m_pPlex, A3dPlexFind(m_pPlex, this, NULL));

	delete this;

	return (0);
}
