/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3dsourcecom.h
 *
 * Declares CA3dSourceCom, the application-facing IA3dSource2 wrapper
 * created by CA3dRoot. It stores the owning root, active implementation
 * and source-type state used when audio is loaded or released.
 *
 * The implementation may be a CA3dSource or an Ac3FilterGraph. This class
 * owns that choice and forwards the source interface, while the selected
 * object handles playback. a3dsourcecom.cpp implements allocation and
 * lifetime coordination.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DSOURCECOM_H
#define _A3DSOURCECOM_H

#include "A3dPrivate.h"
#include "refaudbin.h"

/* Implementation discriminator */
#define A3D_SOURCE_POINTER_NONE         0
#define A3D_SOURCE_POINTER_WAVE         1
#define A3D_SOURCE_POINTER_AC3_GRAPH    2

class CA3dRoot;
class CA3dSource;

/* =============================================================
// Class: CA3dSourceCom
//
// Description: COM wrapper owning a wave source or AC-3 filter graph.
//
// Size: 0x28
//
// (RE) Constructor: rtl:0x100151c0; dbg:0x10030350
// =============================================================*/

/* (RE) Vtables: IA3dSource2 at 0x00, dbg:0x1012a820, slots 0..77;
 * CA3dChained at 0x04, dbg:0x1012a81c, deleting destructor slot 0.
 */

class CA3dSourceCom : public IA3dSource2,
		       public CA3dChained
{
public:
	CA3dSourceCom(IDirectSound *pDS, CA3dRoot *pApi, DWORD dwFlags);

	/* Duplicate a wave source; report construction status through phr. */
	CA3dSourceCom(CA3dSourceCom *pOriginal, HRESULT *phr);

	/* The deleting destructor belongs to the secondary CA3dChained table;
	   IA3dSource2 has no destructor slot. */
	~CA3dSourceCom(void);

	STDMETHODIMP		QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG)	AddRef(void);
	STDMETHODIMP_(ULONG)	Release(void);

	/* IA3dSource2 slots 3..77; LoadFile is slot 76, GetCaps slot 77.
	   Forwarders require a nonnull source, as in the original. */
	STDMETHODIMP	LoadWaveFile(LPSTR pszFileName);
	STDMETHODIMP	LoadWaveData(LPVOID pvData, DWORD dwSize);
	STDMETHODIMP	AllocateAudioData(INT nBytes);
	STDMETHODIMP	FreeAudioData(void);
	STDMETHODIMP	SetAudioFormat(LPVOID pvFormat);
	STDMETHODIMP	GetAudioFormat(LPVOID pvFormat);
	STDMETHODIMP_(DWORD)	GetAudioSize(void);
	STDMETHODIMP	GetType(LPDWORD pdwType);
	STDMETHODIMP	Lock(DWORD dwOffset, DWORD dwBytes,
			     LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1,
			     LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2,
			     DWORD dwFlags);
	STDMETHODIMP	Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
			       LPVOID pvAudioPtr2, DWORD dwAudioBytes2);
	STDMETHODIMP	Play(INT nMode);
	STDMETHODIMP	Stop(void);
	STDMETHODIMP	Rewind(void);
	STDMETHODIMP	SetPlayTime(A3DVAL fTime);
	STDMETHODIMP	GetPlayTime(LPA3DVAL pfTime);
	STDMETHODIMP	SetPlayPosition(DWORD dwPosition);
	STDMETHODIMP	GetPlayPosition(LPDWORD pdwPosition);
	STDMETHODIMP	SetPosition3f(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP	GetPosition3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz);
	STDMETHODIMP	SetPosition3fv(LPA3DVAL pfPosition);
	STDMETHODIMP	GetPosition3fv(LPA3DVAL pfPosition);
	STDMETHODIMP	SetOrientationAngles3f(A3DVAL fHeading, A3DVAL fPitch,
					       A3DVAL fRoll);
	STDMETHODIMP	GetOrientationAngles3f(LPA3DVAL pfHeading, LPA3DVAL pfPitch,
					       LPA3DVAL pfRoll);
	STDMETHODIMP	SetOrientationAngles3fv(LPA3DVAL pfAngles);
	STDMETHODIMP	GetOrientationAngles3fv(LPA3DVAL pfAngles);
	STDMETHODIMP	SetOrientation6f(A3DVAL fXfront, A3DVAL fYfront,
					 A3DVAL fZfront, A3DVAL fXup,
					 A3DVAL fYup, A3DVAL fZup);
	STDMETHODIMP	GetOrientation6f(LPA3DVAL pfXfront, LPA3DVAL pfYfront,
					 LPA3DVAL pfZfront, LPA3DVAL pfXup,
					 LPA3DVAL pfYup, LPA3DVAL pfZup);
	STDMETHODIMP	SetOrientation6fv(LPA3DVAL pfOrientation);
	STDMETHODIMP	GetOrientation6fv(LPA3DVAL pfOrientation);
	STDMETHODIMP	SetVelocity3f(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP	GetVelocity3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz);
	STDMETHODIMP	SetVelocity3fv(LPA3DVAL pfVelocity);
	STDMETHODIMP	GetVelocity3fv(LPA3DVAL pfVelocity);
	STDMETHODIMP	SetCone(A3DVAL fInnerAngle, A3DVAL fOuterAngle,
				A3DVAL fOutsideGain);
	STDMETHODIMP	GetCone(LPA3DVAL pfInnerAngle, LPA3DVAL pfOuterAngle,
				LPA3DVAL pfOutsideGain);
	STDMETHODIMP	SetMinMaxDistance(A3DVAL fMin, A3DVAL fMax, DWORD dwFlags);
	STDMETHODIMP	GetMinMaxDistance(LPA3DVAL pfMin, LPA3DVAL pfMax,
					  LPDWORD pdwFlags);
	STDMETHODIMP	SetGain(A3DVAL fGain);
	STDMETHODIMP	GetGain(LPA3DVAL pfGain);
	STDMETHODIMP	SetPitch(A3DVAL fPitch);
	STDMETHODIMP	GetPitch(LPA3DVAL pfPitch);
	STDMETHODIMP	SetDopplerScale(A3DVAL fScale);
	STDMETHODIMP	GetDopplerScale(LPA3DVAL pfScale);
	STDMETHODIMP	SetDistanceModelScale(A3DVAL fScale);
	STDMETHODIMP	GetDistanceModelScale(LPA3DVAL pfScale);
	STDMETHODIMP	SetEq(A3DVAL fEq);
	STDMETHODIMP	GetEq(LPA3DVAL pfEq);
	STDMETHODIMP	SetPriority(A3DVAL fPriority);
	STDMETHODIMP	GetPriority(LPA3DVAL pfPriority);
	STDMETHODIMP	SetRenderMode(DWORD dwMode);
	STDMETHODIMP	GetRenderMode(LPDWORD pdwMode);
	STDMETHODIMP	GetAudibility(LPA3DVAL pfAudibility);
	STDMETHODIMP	GetOcclusionFactor(LPA3DVAL pfOcclusion);
	STDMETHODIMP	GetStatus(LPDWORD pdwStatus);	/* slot 56 */
	STDMETHODIMP	SetPanValues(DWORD dwNumFields, LPA3DVAL pfPanValues);
	STDMETHODIMP	GetPanValues(DWORD dwNumFields, LPA3DVAL pfPanValues);
	STDMETHODIMP	SetPlayEvent(DWORD dwPosition, HANDLE hEvent);
	STDMETHODIMP	ClearPlayEvents(void);
	STDMETHODIMP	SetTransformMode(DWORD dwMode);
	STDMETHODIMP	GetTransformMode(LPDWORD pdwMode);
	STDMETHODIMP	SetReflectionDelayScale(A3DVAL fScale);
	STDMETHODIMP	GetReflectionDelayScale(LPA3DVAL pfScale);
	STDMETHODIMP	SetReflectionGainScale(A3DVAL fScale);
	STDMETHODIMP	GetReflectionGainScale(LPA3DVAL pfScale);
	STDMETHODIMP	SetVolumetricBounds(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP	GetVolumetricBounds(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz);
	STDMETHODIMP	SetVolumetricDamping(A3DVOLSRCDAMPINFO *pDampInfo);
	STDMETHODIMP	GetVolumetricDamping(A3DVOLSRCDAMPINFO *pDampInfo);

	STDMETHODIMP	SetReverbMix(A3DVAL fMix, A3DVAL fDirectHF);
	STDMETHODIMP	GetReverbMix(A3DVAL *pfMix, A3DVAL *pfDirectHF);
	STDMETHODIMP	NewManualReflection(LPA3DREFLECTION *ppReflection);
	STDMETHODIMP	FreeManualReflections(void);
	STDMETHODIMP	GetNumManualReflections(int *pnReflections);
	STDMETHODIMP	LoadFile(char *szFile, DWORD dwFormat);
	STDMETHODIMP	GetCaps(LPA3DCAPS_SOURCE pCaps);

	/* Source selection and lifetime helpers; not vtable members. */
	HRESULT		AllocateSource(DWORD dwType);
	void		ReleaseSource(void);
	void		SourceDestroyed(CA3dSource *pSource);
	BOOL		InUse(void);

	/* 0x10 */ LONG		m_cRef;			/* Client reference count; not interlocked. */

	/* 0x14 */ IA3dSource2	*m_pSource;	/* Owned wave source or AC-3 graph. */
	/* 0x18 */ DWORD	m_dwPointerType;	/* 0 none, 1 wave, 2 AC-3 */

	/* 0x1C */ CA3dRoot	*m_pApi;		/* Borrowed creator. */
	/* 0x20 */ IDirectSound	*m_pDS;			/* Borrowed DirectSound interface for wave sources. */
	/* 0x24 */ DWORD	m_dwFlags;		/* Creation flags forwarded to the source. */
};

typedef int CA3dSourceComSizeCheck[(sizeof(CA3dSourceCom) == 0x28) ? 1 : -1];

#endif /* _A3DSOURCECOM_H */
