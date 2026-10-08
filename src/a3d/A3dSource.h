/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dSource.h
 *
 * Declares the compatibility CA3dSource, the IDirectSound3DBuffer object
 * attached to CA3dSecondaryBuffer. It stores DirectSound3D parameters,
 * deferred-change state, an engine slot and the solution and DAL
 * interfaces needed to commit controls.
 *
 * Its lifetime is linked to the owning buffer. A3dSource.cpp implements
 * parameter access and commits using a3ddsp.cpp. This class is distinct
 * from the audio-source implementation with the same name in a3dapi.dll.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3D_A3DSOURCE_H
#define _A3D_A3DSOURCE_H

#include "a3dprv.h"
#include "a3ddsp.h"

class CA3dSecondaryBuffer;

/* =============================================================
// Class: CA3dSource
//
// Description: Secondary-buffer 3D state and DAL submission interface.
//
// Size: 0x88C
// =============================================================*/

class CA3dSource : public IDirectSound3DBuffer
{
public:
	CA3dSource(CA3dSecondaryBuffer *pOwner);
	~CA3dSource(void);

	HRESULT Init(DWORD dwSampleRate, IA3dDalBuffer *pDal);
	HRESULT CommitOne(void);

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	STDMETHODIMP GetAllParameters(LPDS3DBUFFER pDs3dBuffer);
	STDMETHODIMP GetConeAngles(LPDWORD pdwInside, LPDWORD pdwOutside);
	STDMETHODIMP GetConeOrientation(D3DVECTOR *pvOrientation);
	STDMETHODIMP GetConeOutsideVolume(LPLONG plVolume);
	STDMETHODIMP GetMaxDistance(D3DVALUE *pflMaxDistance);
	STDMETHODIMP GetMinDistance(D3DVALUE *pflMinDistance);
	STDMETHODIMP GetMode(LPDWORD pdwMode);
	STDMETHODIMP GetPosition(D3DVECTOR *pvPosition);
	STDMETHODIMP GetVelocity(D3DVECTOR *pvVelocity);
	STDMETHODIMP SetAllParameters(LPCDS3DBUFFER pcDs3dBuffer,
	                              DWORD dwApply);
	STDMETHODIMP SetConeAngles(DWORD dwInside, DWORD dwOutside,
	                           DWORD dwApply);
	STDMETHODIMP SetConeOrientation(D3DVALUE x, D3DVALUE y, D3DVALUE z,
	                                DWORD dwApply);
	STDMETHODIMP SetConeOutsideVolume(LONG lVolume, DWORD dwApply);
	STDMETHODIMP SetMaxDistance(D3DVALUE flMaxDistance, DWORD dwApply);
	STDMETHODIMP SetMinDistance(D3DVALUE flMinDistance, DWORD dwApply);
	STDMETHODIMP SetMode(DWORD dwMode, DWORD dwApply);
	STDMETHODIMP SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z,
	                         DWORD dwApply);
	STDMETHODIMP SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z,
	                         DWORD dwApply);

public:
	LONG                 m_cRef;
	CA3dSecondaryBuffer *m_pOwner;

 /* Half-angles are stored in [0,179]; the 360-degree default becomes zero. */
	DS3DBUFFER m_ds3db; /* 0x0C..0x4B; dwSize 64. */

	LONG         m_iSource; /* Process-global source-table index; -1 when unallocated. */
	DWORD        m_Unknown_0x50;
	A3DSOLUTION *m_pSolution; /* 0x54; owned 492-byte integer solution. */
	BYTE         m_Unknown_0x58[0x64 - 0x58];
	void        *m_pvAuxAllocation; /* 0x64; freed on destruction, no source allocator.  */
	BYTE         m_Unknown_0x68[0x6C - 0x68];

 /* Two 0x40C-byte DAL banks at 0x6C and 0x478; second-bank purpose unknown. */
	A3DDALBANK m_aBank[2];

	IA3dDalBuffer *m_pDal; /* 0x884; retained DAL buffer. */
	DWORD          m_iBank; /* bank selected by CommitOne() */
};

extern LONG        g_cSources;
extern DWORD       g_apSourceActive[];
extern CA3dSource *g_apSourceObject[];

#endif /* _A3D_A3DSOURCE_H */
