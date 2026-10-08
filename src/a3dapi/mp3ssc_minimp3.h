/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * mp3ssc_minimp3.h
 *
 * NOT ONE OF AUREAL'S FILES.  Project-added, compiled only when A3D_FIXES
 * is on.  Declares CMinimp3Decoder, the concrete IA3dMp3Decoder the loader glue
 * drives, backed by the public-domain minimp3 decoder in place of Aureal's
 * proprietary "Mp3Ssc".  docs/DECODERS.md describes the boundary.
 *
 * The class holds compressed input, decoder state and the output-format
 * record returned to source loading and streaming. Its methods accept
 * input, produce PCM blocks and reset or finish a decoding session.
 *
 * mp3ssc_minimp3.cpp implements the adapter. The IA3dMp3Decoder contract
 * is declared in A3dSource.h, keeping playback code independent of the
 * selected decoder. PCM parity with Aureal's decoder is unverified.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _MP3SSC_MINIMP3_H
#define _MP3SSC_MINIMP3_H

#include "A3dPrivate.h"
#include "A3dSource.h"
#include "minimp3.h"

/* SSC severity bits: 00 success, 01 warning, 10/11 failure. */

#define MP3SSC_OK    0x00000000
#define MP3SSC_MORE  0x40000000 /* needs more input */
#define MP3SSC_ERROR 0xC0000000

#define MP3SSC_INPUT_CAP   0x10000
#define MP3SSC_FRAME_BYTES (MINIMP3_MAX_SAMPLES_PER_FRAME * 2)

/* =============================================================
// Class: CMinimp3Decoder
//
// Description: minimp3 implementation of the MP3 decoder interface.
// =============================================================*/

class CMinimp3Decoder : public IA3dMp3Decoder
{
public:
	CMinimp3Decoder(void);

	/* IA3dMp3Decoder, in the slot order A3dSource.h declares. */

	virtual void Destroy(void);
	virtual void Reset(void);
	virtual void DecodeFloatBlock(void);
	virtual int  DecodeBlock(void *pvOut, DWORD cbOut,
	                         DWORD *pcbProduced);
	virtual IA3dMp3Format * GetFormat(void);
	virtual void            SetInputSource(void);
	virtual DWORD           SupplyInput(void *pvIn, DWORD cbIn);
	virtual void            GetInputFreeBytes(void);
	virtual void            GetInputBufferedBytes(void);
	virtual void            EndOfInput(void);

private:
	void StoreFormat(const mp3dec_frame_info_t *pInfo);
	void Consume(int cbFrame);

	mp3dec_t      m_dec;
	IA3dMp3Format m_Format;      /* Decoder-owned output format. */
	DWORD         m_dwInputLen;  /* bytes buffered in m_abInput */
	BOOL          m_bEndOfInput; /* No more input will be supplied. */
	unsigned char m_abInput[MP3SSC_INPUT_CAP];
};

#endif /* _MP3SSC_MINIMP3_H */
