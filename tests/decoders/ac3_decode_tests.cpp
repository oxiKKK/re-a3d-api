/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * ac3_decode_tests.cpp - check AC-3 decoding through liba52.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.  Compiled only when
 * A3D_FIXES is on.
 *
 * Exercises the Ac3* entry points: Ac3OpenAudio, Ac3DecodeAudio over each sync frame,
 * Ac3CloseAudio. Finds sync frames itself (0x0B77 word, a52_syncinfo for length).
 * Checks decoded output; does not compare PCM with the proprietary decoder.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dSource.h"

#include <stdint.h>

extern "C" {
#include "a52.h"
}

#include <cstdio>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

TEST(Decoder, Ac3ViaLiba52)
{
	const char *pszPath = A3D_AC3_SAMPLE;

	std::ifstream f(pszPath, std::ios::binary);
	ASSERT_TRUE((bool) f) << "cannot open " << pszPath;
	std::vector<unsigned char> in((std::istreambuf_iterator<char>(f)),
				      std::istreambuf_iterator<char>());
	ASSERT_GT(in.size(), 0u);

	DWORD hDec = Ac3OpenAudio(2);
	ASSERT_NE(hDec, (DWORD) 0);

	std::vector<unsigned char> decode(36864);
	A3DAC3CONFIG cfg;
	ZeroMemory(&cfg, sizeof(cfg));
	cfg.dwMode       = 2;
	cfg.dwDecodeSize = (DWORD) decode.size();
	cfg.pDecode      = decode.data();
	cfg.pInput0      = in.data();

	std::size_t total = 0;
	long peak = 0;
	int frames = 0;
	int srate = 0;

	std::size_t p = 0;
	while (p + 7 <= in.size()) {
		if (!(in[p] == 0x0B && in[p + 1] == 0x77)) {
			++p;
			continue;
		}
		int flags, sr, br;
		int flen = a52_syncinfo(in.data() + p, &flags, &sr, &br);
		if (flen <= 0) {
			++p;
			continue;
		}
		if (p + (std::size_t) flen > in.size())
			break;

		srate = sr;
		cfg.pFrameStart = in.data() + p;
		cfg.pFrameEnd   = in.data() + p + flen;
		cfg.dwProduced  = 0;

		int st = Ac3DecodeAudio(hDec, &cfg);
		if (st == 0 && cfg.dwProduced) {
			short *s = (short *) decode.data();
			std::size_t n = cfg.dwProduced / 2;
			for (std::size_t i = 0; i < n; ++i) {
				long v = s[i] < 0 ? -(long) s[i] : s[i];
				if (v > peak)
					peak = v;
			}
			total += cfg.dwProduced;
			++frames;
		}
		p += flen;
	}

	Ac3CloseAudio(hDec);

	GTEST_LOG_(INFO) << "ac3: " << frames << " frames, " << total
			 << " bytes PCM, peak " << peak << ", " << srate << " Hz";

	EXPECT_GT(frames, 0);
	EXPECT_GT(total, 0u);
	EXPECT_GT(peak, 0);
	EXPECT_TRUE(srate == 48000 || srate == 44100 || srate == 32000);
}
