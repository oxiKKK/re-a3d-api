/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * ac3fgraph.h
 *
 * Declares Ac3FilterGraph, the IA3dSource2 implementation used for
 * DirectShow AC-3 fallback. It stores graph, filter, playback, seeking
 * and event-thread interfaces along with notification state.
 *
 * The declarations also describe decoder selection and the DirectShow
 * types needed by the implementation. ac3fgraph.cpp builds and services
 * the graph; CA3dSourceCom presents the selected source implementation to
 * the application.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _AC3FGRAPH_H
#define _AC3FGRAPH_H

#include "A3dPrivate.h"
#include <list>

struct IGraphBuilder;
struct IBaseFilter;
struct IMediaControl;
struct IMediaEvent;
struct IMediaSeeking;
struct IMCAC3;
struct IPin;
struct IEnumPins;
struct REGFILTER;

/* =============================================================
// Class: Ac3OptimalDecoder
//
// Description: Best decoder candidate and its PCM channel count.
//
// Size: 0x08
// =============================================================*/

struct Ac3OptimalDecoder
{
	/* Owned REGFILTER allocation viewed through its first CLSID field. */
	/* 0x00 */ CLSID *pClsid;
	/* PCM channel count; zero if none found. */
	/* 0x04 */ DWORD dwMaxChannels;
};

/* =============================================================
// Class: Ac3FilterGraph
//
// Description: DirectShow AC-3 source with hardware and software decoder paths.
//
// Size: 0x50
//
// (RE) Constructor: dbg:0x10032450; rtl:0x10016220
// =============================================================*/

/* (RE) Vtable: dbg:0x1012adac, IA3dSource2 slots 0..77 and deleting destructor slot 78. */

class Ac3FilterGraph : public IA3dSource2
{
public:
	Ac3FilterGraph(void);
	virtual ~Ac3FilterGraph(void);

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	/* The source slots this class serves for real. */

	STDMETHODIMP_(DWORD) GetAudioSize(void);
	STDMETHODIMP         GetType(LPDWORD pdwType);
	STDMETHODIMP         Play(INT nMode);
	STDMETHODIMP         Stop(void);
	STDMETHODIMP         Rewind(void);
	STDMETHODIMP         SetPlayPosition(DWORD dwPosition);
	STDMETHODIMP         GetPlayPosition(LPDWORD pdwPosition);
	STDMETHODIMP         GetStatus(LPDWORD pdwStatus);
	STDMETHODIMP         SetPlayEvent(DWORD dwPosition, HANDLE hEvent);
	STDMETHODIMP         ClearPlayEvents(void);
	STDMETHODIMP         LoadFile(char *szFile, DWORD dwFormat);
	STDMETHODIMP         GetCaps(LPA3DCAPS_SOURCE lpSourceCaps);

	/* The two time-seek slots, refused with a different A3DERROR. */

	STDMETHODIMP SetPlayTime(A3DVAL fTime);
	STDMETHODIMP GetPlayTime(LPA3DVAL pfTime);

	/* Every other source slot refuses the call. */

	STDMETHODIMP LoadWaveFile(LPSTR pszFileName);
	STDMETHODIMP LoadWaveData(LPVOID pvData, DWORD dwSize);
	STDMETHODIMP AllocateAudioData(INT nBytes);
	STDMETHODIMP FreeAudioData(void);
	STDMETHODIMP SetAudioFormat(LPVOID pvFormat);
	STDMETHODIMP GetAudioFormat(LPVOID pvFormat);
	STDMETHODIMP Lock(DWORD dwOffset, DWORD dwBytes,
	                  LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1,
	                  LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2,
	                  DWORD dwFlags);
	STDMETHODIMP Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
	                    LPVOID pvAudioPtr2, DWORD dwAudioBytes2);
	STDMETHODIMP SetPosition3f(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP GetPosition3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz);
	STDMETHODIMP SetPosition3fv(LPA3DVAL pv);
	STDMETHODIMP GetPosition3fv(LPA3DVAL pv);
	STDMETHODIMP SetOrientationAngles3f(A3DVAL h, A3DVAL p, A3DVAL r);
	STDMETHODIMP GetOrientationAngles3f(LPA3DVAL ph, LPA3DVAL pp, LPA3DVAL pr);
	STDMETHODIMP SetOrientationAngles3fv(LPA3DVAL pv);
	STDMETHODIMP GetOrientationAngles3fv(LPA3DVAL pv);
	STDMETHODIMP SetOrientation6f(A3DVAL fx, A3DVAL fy, A3DVAL fz,
	                              A3DVAL ux, A3DVAL uy, A3DVAL uz);
	STDMETHODIMP GetOrientation6f(LPA3DVAL pfx, LPA3DVAL pfy, LPA3DVAL pfz,
	                              LPA3DVAL pux, LPA3DVAL puy, LPA3DVAL puz);
	STDMETHODIMP SetOrientation6fv(LPA3DVAL pv);
	STDMETHODIMP GetOrientation6fv(LPA3DVAL pv);
	STDMETHODIMP SetVelocity3f(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP GetVelocity3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz);
	STDMETHODIMP SetVelocity3fv(LPA3DVAL pv);
	STDMETHODIMP GetVelocity3fv(LPA3DVAL pv);
	STDMETHODIMP SetCone(A3DVAL fInnerAngle, A3DVAL fOuterAngle,
	                     A3DVAL fOutsideGain);
	STDMETHODIMP GetCone(LPA3DVAL pfInnerAngle, LPA3DVAL pfOuterAngle,
	                     LPA3DVAL pfOutsideGain);
	STDMETHODIMP SetMinMaxDistance(A3DVAL fMin, A3DVAL fMax, DWORD dwMode);
	STDMETHODIMP GetMinMaxDistance(LPA3DVAL pfMin, LPA3DVAL pfMax,
	                               LPDWORD pdwMode);
	STDMETHODIMP SetGain(A3DVAL fGain);
	STDMETHODIMP GetGain(LPA3DVAL pfGain);
	STDMETHODIMP SetPitch(A3DVAL fPitch);
	STDMETHODIMP GetPitch(LPA3DVAL pfPitch);
	STDMETHODIMP SetDopplerScale(A3DVAL fScale);
	STDMETHODIMP GetDopplerScale(LPA3DVAL pfScale);
	STDMETHODIMP SetDistanceModelScale(A3DVAL fScale);
	STDMETHODIMP GetDistanceModelScale(LPA3DVAL pfScale);
	STDMETHODIMP SetEq(A3DVAL fEq);
	STDMETHODIMP GetEq(LPA3DVAL pfEq);
	STDMETHODIMP SetPriority(A3DVAL fPriority);
	STDMETHODIMP GetPriority(LPA3DVAL pfPriority);
	STDMETHODIMP SetRenderMode(DWORD dwMode);
	STDMETHODIMP GetRenderMode(LPDWORD pdwMode);
	STDMETHODIMP GetAudibility(LPA3DVAL pfAudibility);
	STDMETHODIMP GetOcclusionFactor(LPA3DVAL pfFactor);
	STDMETHODIMP SetPanValues(DWORD dwNumValues, LPA3DVAL pfValues);
	STDMETHODIMP GetPanValues(DWORD dwNumValues, LPA3DVAL pfValues);
	STDMETHODIMP SetTransformMode(DWORD dwMode);
	STDMETHODIMP GetTransformMode(LPDWORD pdwMode);
	STDMETHODIMP SetReflectionDelayScale(A3DVAL fScale);
	STDMETHODIMP GetReflectionDelayScale(LPA3DVAL pfScale);
	STDMETHODIMP SetReflectionGainScale(A3DVAL fScale);
	STDMETHODIMP GetReflectionGainScale(LPA3DVAL pfScale);
	STDMETHODIMP SetVolumetricBounds(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP GetVolumetricBounds(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz);
	STDMETHODIMP SetVolumetricDamping(A3DVOLSRCDAMPINFO *pInfo);
	STDMETHODIMP GetVolumetricDamping(A3DVOLSRCDAMPINFO *pInfo);
	STDMETHODIMP SetReverbMix(A3DVAL fMix, A3DVAL fDirectHF);
	STDMETHODIMP GetReverbMix(A3DVAL *pfMix, A3DVAL *pfDirectHF);
	STDMETHODIMP NewManualReflection(LPA3DREFLECTION *ppReflection);
	STDMETHODIMP FreeManualReflections(void);
	STDMETHODIMP GetNumManualReflections(int *pnReflections);

private:
	/* Internal playback and position operations. */
	HRESULT PlayInternal(INT nMode);
	HRESULT StopInternal(void);
	HRESULT GetPlayPositionInternal(LPDWORD pdwPosition);
	HRESULT SetPlayPositionInternal(DWORD dwPosition);

	HRESULT StartAc3EventThread(void);
	void    ReleaseGraph(void);

	static DWORD WINAPI EventThreadProc(LPVOID pvRef);
	static void         ResetPosition(Ac3FilterGraph *pThis);

	/* Software decoder selection using temporary graph connections. */
	HRESULT FindOptimalDecoder(Ac3OptimalDecoder *pBest,
	                           IPin *pParserOut, IPin *pRenderIn);
	HRESULT GetMaxOutputChannels(REGFILTER *pRegFilter, DWORD *pdwMaxChannels,
	                             IPin *pAc3Out, IPin *pPcmIn);
	HRESULT EnumInputPins(IEnumPins *pEnumPins,
	                      std::list<IPin *> *pInputPinList);
	HRESULT EnumOutputPins(IEnumPins *pEnumPins, IPin *pPcmIn,
	                       DWORD *pdwMaxChannels);

public:
	/* 0x04 */ LONG m_cRef; /* Client reference count; not interlocked. */

	/* Owned graph and filter references. */
	/* 0x08 */ IGraphBuilder *m_pGraphBuilder;   /* Graph builder. */
	/* 0x0C */ IBaseFilter  *m_pSourceFilter;   /* Async file source. */
	/* 0x10 */ IBaseFilter  *m_pParserFilter;   /* AC-3 parser. */
	/* 0x14 */ IBaseFilter  *m_pDecoderFilter;  /* Hardware AC-3 decoder. */

	/* Owned hardware-decoder control; null on the software path. */
	/* 0x18 */ IMCAC3 *m_pMcAc3;

	/* Owned playback control. */
	/* 0x1C */ IMediaControl *m_pMediaControl;
	/* Owned event queue read by the worker. */
	/* 0x20 */ IMediaEvent  *m_pMediaEvent;

	/* Owned graph seeking interface for duration and byte/time conversion. */
	/* 0x24 */ IMediaSeeking *m_pMediaSeeking;

	/* File byte length used for media-time conversion. */
	/* 0x28 */ DWORD m_dwStreamSize;
	/* 0x2C */ DWORD m_Unknown_0x2C; /* not initialised by the constructor */

	/* Graph duration used for byte/time conversion. */
	/* 0x30 */ LONGLONG m_llDuration;

	/* 0x38 */ DWORD m_fPlaying;    /* Nonzero while playing. */
	/* 0x3C */ DWORD m_dwPlayFlags; /* Playback flags, including DSBPLAY_LOOPING. */

	/* Owned loaded filename. */
	/* 0x40 */ char  *m_pszFilename;
	/* Owned playback mutex; INVALID_HANDLE_VALUE before worker startup. */
	/* 0x44 */ HANDLE m_hMutex;

	/* Borrowed completion event; signaled by ResetPosition. */
	/* 0x48 */ HANDLE m_hPlayEvent;
	/* 0x4C */ DWORD  m_Unknown_0x4C; /* not touched by the constructor */
};

typedef int Ac3FilterGraphSizeCheck[(sizeof(Ac3FilterGraph) == 0x50) ? 1 : -1];

#endif /* _AC3FGRAPH_H */
