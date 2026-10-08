/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * mp3ssc_minimp3.cpp
 *
 * NOT ONE OF AUREAL'S FILES.  Project-added, compiled only when the build
 * option A3D_FIXES is on.
 * minimp3 implementation of the MP3 decoder interface. PCM parity with
 * Aureal's decoder is unverified; see docs/DECODERS.md.
 *
 * CMinimp3Decoder buffers compressed input, decodes MP3 frames into 16-bit
 * PCM and reports the resulting channel count and sample rate through
 * IA3dMp3Decoder. Source loading and streaming use that interface without
 * depending on the decoder implementation.
 *
 * This file owns the minimp3 implementation inclusion, input consumption,
 * reset and end-of-input handling. mp3ssc_minimp3.h declares the adapter
 * state, and A3dSource.h defines the interface it implements.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"

#if defined(A3D_FIXES)
#include "A3dSource.h"

#include <string.h>

#define MINIMP3_NO_SIMD		/* Required for /arch:IA32. */
#define MINIMP3_IMPLEMENTATION
#include "mp3ssc_minimp3.h"

/* =============================================================
// CMinimp3Decoder()
//
// Initialize the decoder and its 16-bit output format.
// =============================================================*/

CMinimp3Decoder::CMinimp3Decoder(void)
{
	mp3dec_init(&m_dec);

	m_dwInputLen  = 0;
	m_bEndOfInput = FALSE;

	ZeroMemory(&m_Format, sizeof(m_Format));
	m_Format.nBitsPerSample = 16;
}

/* =============================================================
// Destroy()
//
// Delete the decoder.
// =============================================================*/

void
CMinimp3Decoder::Destroy(void)
{
	delete this;
}

/* =============================================================
// Reset()
//
// Reset decoder state and discard buffered input.
// =============================================================*/

void
CMinimp3Decoder::Reset(void)
{
	mp3dec_init(&m_dec);

	m_dwInputLen  = 0;
	m_bEndOfInput = FALSE;
}

/* =============================================================
// SupplyInput()
//
// Copy input into the available reservoir space.
//
// Returns: Number of bytes accepted.
// =============================================================*/

DWORD
CMinimp3Decoder::SupplyInput(void *pvIn, DWORD cbIn)
{
	DWORD	cbTake;

	cbTake = cbIn;

	if (cbTake > (DWORD) MP3SSC_INPUT_CAP - m_dwInputLen)
		cbTake = (DWORD) MP3SSC_INPUT_CAP - m_dwInputLen;

	memcpy(m_abInput + m_dwInputLen, pvIn, cbTake);
	m_dwInputLen += cbTake;

	return (cbTake);
}

/* =============================================================
// EndOfInput()
//
// Mark the input stream complete.
// =============================================================*/

void
CMinimp3Decoder::EndOfInput(void)
{
	m_bEndOfInput = TRUE;
}

/* =============================================================
// DecodeBlock()
//
// Decode buffered frames, or probe the format when no output space is supplied.
// The probe leaves input unconsumed and reports the frame offset.
//
// Returns:
//   MP3SSC_OK    if a frame is found by the probe or PCM is produced
//   MP3SSC_MORE  otherwise, or MP3SSC_ERROR after end of input
// =============================================================*/

int
CMinimp3Decoder::DecodeBlock(void *pvOut, DWORD cbOut, DWORD *pcbProduced)
{
	mp3dec_frame_info_t	info;
	DWORD			cbProduced;

	if (!pvOut || !cbOut)
	{
		mp3dec_decode_frame(&m_dec, m_abInput, (int) m_dwInputLen,
				    NULL, &info);

		if (info.frame_bytes > 0)
		{
			StoreFormat(&info);

			if (pcbProduced)
				*pcbProduced = (DWORD) info.frame_offset;

			return (MP3SSC_OK);
		}

		if (pcbProduced)
			*pcbProduced = 0;

		return (m_bEndOfInput ? MP3SSC_ERROR : MP3SSC_MORE);
	}

	cbProduced = 0;

	while (cbOut - cbProduced >= (DWORD) MP3SSC_FRAME_BYTES)
	{
		short  *pcm;
		int     nSamples;

		pcm = (short *) ((BYTE *) pvOut + cbProduced);

		nSamples = mp3dec_decode_frame(&m_dec, m_abInput,
					       (int) m_dwInputLen, pcm, &info);

		if (info.frame_bytes == 0)
			break;

		Consume(info.frame_bytes);

		if (nSamples > 0)
		{
			StoreFormat(&info);
			cbProduced += (DWORD) nSamples * info.channels * 2;
		}
	}

	if (pcbProduced)
		*pcbProduced = cbProduced;

	if (cbProduced)
		return (MP3SSC_OK);

	return (m_bEndOfInput ? MP3SSC_ERROR : MP3SSC_MORE);
}

/* =============================================================
// GetFormat()
//
// Read the decoded stream format.
//
// Returns: Pointer to the decoder-owned format.
// =============================================================*/

IA3dMp3Format *
CMinimp3Decoder::GetFormat(void)
{
	return (&m_Format);
}

/* =============================================================
// StoreFormat()
//
// Store the frame format with bitrate in bytes per second.
// =============================================================*/

void
CMinimp3Decoder::StoreFormat(const mp3dec_frame_info_t *pInfo)
{
	m_Format.nMpegLayer     = pInfo->layer;
	m_Format.nMpegVersion   = (pInfo->hz >= 32000) ? 1 : 2;
	m_Format.nChannels      = pInfo->channels;
	m_Format.nSamplerate    = pInfo->hz;
	m_Format.nBitrate       = pInfo->bitrate_kbps * 125;
	m_Format.nBitsPerSample = 16;
}

/* =============================================================
// Consume()
//
// Discard consumed bytes from the input reservoir.
// =============================================================*/

void
CMinimp3Decoder::Consume(int cbFrame)
{
	memmove(m_abInput, m_abInput + cbFrame, m_dwInputLen - cbFrame);
	m_dwInputLen -= (DWORD) cbFrame;
}

/* Unused interface slots have unresolved signatures; see A3dSource.h. */

/* =============================================================
// DecodeFloatBlock()
//
// Leave decoder state unchanged.
// =============================================================*/

void	CMinimp3Decoder::DecodeFloatBlock(void)	{}

/* =============================================================
// SetInputSource()
//
// Leave decoder state unchanged.
// =============================================================*/

void	CMinimp3Decoder::SetInputSource(void)	{}

/* =============================================================
// GetInputFreeBytes()
//
// Leave decoder state unchanged.
// =============================================================*/

void	CMinimp3Decoder::GetInputFreeBytes(void)	{}

/* =============================================================
// GetInputBufferedBytes()
//
// Leave decoder state unchanged.
// =============================================================*/

void	CMinimp3Decoder::GetInputBufferedBytes(void)	{}

/* =============================================================
// Mp3SscCreateDecoder()
//
// Create a minimp3 decoder.
//
// Returns:
//   MP3SSC_OK
//   MP3SSC_ERROR  a null output pointer or failed allocation
// =============================================================*/

int
Mp3SscCreateDecoder(LPA3DMP3DECODER *ppDecoder)
{
	if (!ppDecoder)
		return (MP3SSC_ERROR);

	*ppDecoder = NULL;
	if (!A3dGetConfig().bEnableMP3Decoder)
		return (MP3SSC_ERROR);

	*ppDecoder = new CMinimp3Decoder;

	return (*ppDecoder ? MP3SSC_OK : MP3SSC_ERROR);
}

/* =============================================================
// Mp3SscErrorString()
//
// Describe the status severity.
//
// Returns: Static status message.
// =============================================================*/

const char *
Mp3SscErrorString(int nStatus)
{
	if (!A3dGetConfig().bEnableMP3Decoder)
		return ("(Mp3Ssc) decoder not present in this build");

	switch ((DWORD) nStatus & A3D_MP3_STATUS_SEVERITY_MASK)
	{
	case MP3SSC_OK:
		return ("(minimp3) success: no error");

	case MP3SSC_MORE:
		return ("(minimp3) info: needs more input");

	default:
		return ("(minimp3) error: decode failed");
	}
}

#endif /* A3D_FIXES */
