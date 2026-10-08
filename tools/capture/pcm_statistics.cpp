// Project-added A3D development tooling.
#include "capture_internal.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>

namespace a3dcapture {
void
AccumRegion(const void *pv, DWORD cb)
{
const short	*ps;
DWORD		n;
DWORD		i;
long		v;

	ps = (const short *) pv;
	n  = cb / 2;

	for (i = 0; i < n; i++)
	{
		v = ps[i];

		if (v < 0)
			v = -v;

		capture_state.absolute_sample_sum += (unsigned) v;
		capture_state.squared_sample_sum  += (unsigned __int64) ((long) ps[i] * (long) ps[i]);

		if (v > capture_state.peak_amplitude)
			capture_state.peak_amplitude = v;
	}

	capture_state.sample_count += n;
	capture_state.byte_count   += cb;
}

void
StreamStats(const CapturedBuffer *pInfo, long *pnPeak,
	    unsigned __int64 *pSumAbs, unsigned __int64 *pSumSq,
	    unsigned __int64 *pcSamples)
{
const short		*ps;
DWORD			n;
DWORD			i;
long			v;
long			nPeak;
unsigned __int64	absolute_sample_sum;
unsigned __int64	squared_sample_sum;

	ps     = (const short *) pInfo->pStream;
	n      = pInfo->cbStream / 2;
	nPeak  = 0;
	absolute_sample_sum = 0;
	squared_sample_sum  = 0;

	for (i = 0; i < n; i++)
	{
		v = ps[i];

		if (v < 0)
			v = -v;

		absolute_sample_sum += (unsigned) v;
		squared_sample_sum  += (unsigned __int64) ((long) ps[i] * (long) ps[i]);

		if (v > nPeak)
			nPeak = v;
	}

	*pnPeak    = nPeak;
	*pSumAbs   = absolute_sample_sum;
	*pSumSq    = squared_sample_sum;
	*pcSamples = n;
}


} // namespace a3dcapture
