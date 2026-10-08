/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * Waveform.cpp
 *
 * Implements CWaveForm, reference-counted storage for raw audio bytes. It
 * allocates and clears a byte buffer, exposes its address and size, and
 * frees the allocation when the final reference is released.
 *
 * Software voices and resource-manager streams use this object so
 * duplicated playback buffers can share sample data while keeping
 * separate playback state. Format interpretation and cursor handling
 * remain with those buffer classes.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "Waveform.h"

typedef int CWaveFormSizeCheck[(sizeof(CWaveForm) == 0xC) ? 1 : -1];

/* =============================================================
// CWaveForm()
// (RE) dbg:0x100745F0
//
// Initialize one reference and no wave buffer.
// =============================================================*/

CWaveForm::CWaveForm(void)
{
	m_cRef     = 1;
	m_pBuffer  = NULL;
	m_cbBuffer = 0;
}

/* =============================================================
// CWaveForm::~CWaveForm() scalar deleting destructor
// (RE) dbg:0x10074730
// =============================================================*/

/* =============================================================
// ~CWaveForm()
// (RE) dbg:0x10074630
//
// Free the wave buffer.
// =============================================================*/

CWaveForm::~CWaveForm(void)
{
	if (m_pBuffer != NULL)
	{
		operator delete(m_pBuffer);

		m_pBuffer = NULL;
	}
}

/* =============================================================
// Create()
// (RE) dbg:0x10074780
//
// Allocate and clear the wave buffer. The original allocator returns
// NULL on failure.
//
// Returns: 1 on success; 0 on allocation failure.
// =============================================================*/

int
CWaveForm::Create(DWORD dwBytes)
{
	m_pBuffer = (BYTE *) operator new(dwBytes);

	if (m_pBuffer != NULL)
	{
		m_cbBuffer = dwBytes;

		ZeroMemory(m_pBuffer, dwBytes);

		return (1);
	}

	m_pBuffer = NULL;

	return (0);
}

/* =============================================================
// GetBuffer()
// (RE) dbg:0x10074810; thunk dbg:0x10003585
//
// Read the wave buffer pointer.
//
// Returns: The borrowed buffer pointer; NULL before allocation.
// =============================================================*/

BYTE *
CWaveForm::GetBuffer(void)
{
	return (m_pBuffer);
}

/* =============================================================
// GetBufferSize()
// (RE) dbg:0x10074830; thunk dbg:0x10003a67
//
// Read the allocated buffer size.
//
// Returns: The buffer size in bytes.
// =============================================================*/

DWORD
CWaveForm::GetBufferSize(void)
{
	return (m_cbBuffer);
}

/* =============================================================
// AddRef()
// (RE) dbg:0x10074680; thunk dbg:0x1000366b
//
// Take a reference only when the wave buffer exists.
//
// Returns: The resulting reference count.
// =============================================================*/

LONG
CWaveForm::AddRef(void)
{
	if (m_pBuffer != NULL)
		m_cRef++;

	return (m_cRef);
}

/* =============================================================
// Release()
// (RE) dbg:0x100746C0; thunk dbg:0x10002577
//
// Release a reference and delete the waveform at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

LONG
CWaveForm::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}
