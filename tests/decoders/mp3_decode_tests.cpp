/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * mp3_decode_tests.cpp - check MP3 decoding through minimp3.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.  Compiled only when
 * A3D_FIXES is on.
 *
 * Drives IA3dMp3Decoder: Mp3SscCreateDecoder, SupplyInput/DecodeBlock/GetFormat/
 * EndOfInput (same sequence as CA3dSource). Decodes MP3 to PCM, checks plausible
 * output (valid channels/rate, non-trivial 16-bit count, non-zero peak).
 * Checks decoded output; does not compare PCM with the proprietary decoder.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dSource.h"

#include <cstdio>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

TEST(Decoder, Mp3ViaMinimp3)
{
	const char *pszPath = A3D_MP3_SAMPLE;

	std::ifstream f(pszPath, std::ios::binary);
	ASSERT_TRUE((bool) f) << "cannot open " << pszPath;
	std::vector<unsigned char> in((std::istreambuf_iterator<char>(f)),
				      std::istreambuf_iterator<char>());
	ASSERT_GT(in.size(), 0u);

	LPA3DMP3DECODER pDec = NULL;
	int st = Mp3SscCreateDecoder(&pDec);
	ASSERT_FALSE(((DWORD) st & 0xC0000000) == 0xC0000000);
	ASSERT_TRUE(pDec != NULL);

	std::size_t pos = 0;
	const DWORD CHUNK = 0x2000;
	auto supply = [&]() {
		if (pos >= in.size()) {
			pDec->EndOfInput();
			return (DWORD) 0;
		}
		DWORD want = (DWORD) (in.size() - pos);
		if (want > CHUNK)
			want = CHUNK;
		DWORD took = pDec->SupplyInput(in.data() + pos, want);
		pos += took;
		if (pos >= in.size())
			pDec->EndOfInput();
		return took;
	};

	supply();
	DWORD dwHeader = 0;
	pDec->DecodeBlock(NULL, 0, &dwHeader);
	IA3dMp3Format *fmt = pDec->GetFormat();

	std::vector<short> pcm;
	unsigned char out[4608];
	long peak = 0;
	int guard = 0;
	for (;;) {
		supply();
		DWORD produced = 0;
		pDec->DecodeBlock(out, sizeof out, &produced);
		if (produced) {
			short *p = (short *) out;
			std::size_t n = produced / 2;
			for (std::size_t i = 0; i < n; ++i) {
				long v = p[i] < 0 ? -(long) p[i] : p[i];
				if (v > peak)
					peak = v;
			}
			pcm.insert(pcm.end(), p, p + n);
		}
		if (produced == 0 && pos >= in.size())
			break;
		ASSERT_LT(++guard, 5000000) << "decode did not terminate";
	}

	/* Copy the format before Destroy frees it. Debug CRT fills the freed
	   allocation with 0xDDDDDDDD. */
	const int nChannels	= fmt->nChannels;
	const int nSamplerate	= fmt->nSamplerate;
	const int nMpegLayer	= fmt->nMpegLayer;
	const int nBitsPerSample = fmt->nBitsPerSample;

	pDec->Destroy();

	GTEST_LOG_(INFO) << "mp3: " << nChannels << " ch, " << nSamplerate
			 << " Hz, layer " << nMpegLayer << ", " << pcm.size()
			 << " samples, peak " << peak;

	EXPECT_TRUE(nChannels == 1 || nChannels == 2);
	EXPECT_GE(nSamplerate, 8000);
	EXPECT_LE(nSamplerate, 48000);
	EXPECT_EQ(nBitsPerSample, 16);
	EXPECT_GT(pcm.size(), (std::size_t) 100000);
	EXPECT_GT(peak, 0);
}
