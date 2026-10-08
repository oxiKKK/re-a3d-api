/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * Waveform.h
 *
 * Declares CWaveForm, the shared audio-byte allocation used by playback
 * buffers. It stores the buffer address, byte count and non-atomic
 * reference count, with methods for allocation and lifetime management.
 *
 * The object contains no playback cursor or audio-format interpretation.
 * A2DBuffer and resource-manager streams keep that state separately and
 * can share one CWaveForm across duplicates. Waveform.cpp implements the
 * allocation.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _WAVEFORM_H
#define _WAVEFORM_H

#include "A3dPrivate.h"

/* =============================================================
// Class: CWaveForm
//
// Description: Reference-counted wave-data allocation.
//
// Size: 0x0C
//
// (RE) Constructor: dbg:0x100745F0
// =============================================================*/

class CWaveForm
{
public:
	CWaveForm(void);
	~CWaveForm(void);

	int Create(DWORD dwBytes);

	BYTE *GetBuffer(void);
	DWORD GetBufferSize(void);

	LONG AddRef(void);
	LONG Release(void);

private:
	/* 0x00 */ LONG  m_cRef;     /* Non-atomic reference count. */
	/* 0x04 */ BYTE *m_pBuffer;  /* Owned sample storage. */
	/* 0x08 */ DWORD m_cbBuffer; /* Allocated byte count. */
};

#endif /* _WAVEFORM_H */
