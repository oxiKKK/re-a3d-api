// Project-added A3D development tooling.
#include "capture_internal.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>

namespace a3dcapture {
int
WriteWav(const char *pszPath, const CapturedBuffer *pInfo)
{
FILE	*fp;
DWORD	cbData;
DWORD	dwRate;
WORD	nCh;
WORD	nBits;
WORD	nAlign;
DWORD	dwAvg;
DWORD	dw;
WORD	w;

	if (pInfo->cbStream == 0)
		return (0);

	fp = fopen(pszPath, "wb");

	if (fp == NULL)
		return (0);

	cbData = pInfo->cbStream;
	dwRate = pInfo->dwSamplesPerSec ? pInfo->dwSamplesPerSec : 22050;
	nCh    = pInfo->nChannels ? pInfo->nChannels : 2;
	nBits  = pInfo->wBitsPerSample ? pInfo->wBitsPerSample : 16;
	nAlign = (WORD) (nCh * (nBits / 8));
	dwAvg  = dwRate * nAlign;

	fwrite("RIFF", 1, 4, fp);
	dw = 36 + cbData;		fwrite(&dw, 4, 1, fp);
	fwrite("WAVEfmt ", 1, 8, fp);
	dw = 16;			fwrite(&dw, 4, 1, fp);
	w  = WAVE_FORMAT_PCM;		fwrite(&w,  2, 1, fp);
	w  = nCh;			fwrite(&w,  2, 1, fp);
	dw = dwRate;			fwrite(&dw, 4, 1, fp);
	dw = dwAvg;			fwrite(&dw, 4, 1, fp);
	w  = nAlign;			fwrite(&w,  2, 1, fp);
	w  = nBits;			fwrite(&w,  2, 1, fp);
	fwrite("data", 1, 4, fp);
	dw = cbData;			fwrite(&dw, 4, 1, fp);

	fwrite(pInfo->pStream, 1, cbData, fp);

	fclose(fp);

	return (1);
}

/* -------------------------------------------------------------------------- */

} // namespace a3dcapture
