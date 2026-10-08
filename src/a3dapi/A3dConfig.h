/* Project-added runtime settings; not part of the original DLL. */
#ifndef _A3DCONFIG_H
#define _A3DCONFIG_H

#if defined(A3D_FIXES)
#include <windows.h>

/* Project settings, 0x20 bytes; no reconstructed layout. */
struct A3DCONFIG
{
	BOOL bEmulateHardware;
	BOOL bSoftwareReverb;
	BOOL bSoftwareReflections;
	BOOL bFixPropertyDeadlocks;
	BOOL bEnableMP3Decoder;
	BOOL bEnableAC3Decoder;
	BOOL bUseNewCredits;
	BOOL bRelaxCreditsActivation;
};

/* Immutable settings beside the containing module, read on first use.
 * Call outside DllMain, before starting audio workers. No reload or ownership
 * transfer; the returned reference remains valid until module unload.
 */
const A3DCONFIG &A3dGetConfig(void);
#endif
#endif
