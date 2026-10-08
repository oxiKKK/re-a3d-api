/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * interface_descriptors.hpp - the published A3D interfaces, their IIDs and slot counts.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * Records interface names, IIDs, SDK slot counts (including IUnknown)
 * and construction paths. SDK declarations require binary validation:
 * the A3D 2.0 header over-declared IA3dSource by 10 slots.
 *
 * The static test checks these counts against the Retail vtable inventory
 * in docs/llm/groundtruth/vtables-rtl.tsv: IA3d5=51 once, IA3dSource2=78 twice
 * and IA3dGeom2=52 three times. Matching counts alone do not verify
 * method signatures.
 *
 * inc/ia3dapi.h declares the IIDs; tests/guids.cpp defines them. This
 * header includes ia3dapi.h without <initguid.h>.
 *
 *---------------------------------------------------------------------------
 */

#ifndef A3DTEST_IFACE_DESC_HPP
#define A3DTEST_IFACE_DESC_HPP

#include <windows.h>
#include <objbase.h>

#include "ia3dapi.h"

namespace a3dtest {

/* How the harness obtains an instance of an interface. */
enum class reach {
	root,		/* QueryInterface from the IA3d5 root object */
	source,		/* QueryInterface from a source made by NewSource */
	factory,	/* the class the factory creates directly */
};

struct iface_desc {
	const char *name;
	const IID  *iid;
	int         slots;	/* declared vtable slots, IUnknown included */
	reach       how;
};

/*
 * The interfaces the static case walks.  IA3d5 is the object the factory
 * creates; the rest are reached by QueryInterface from it or from a source.
 * The legacy IA3d..IA3d4, IA3dGeom and IA3dSource are answered by the same
 * objects and are added as the differential harness grows.
 */
inline const iface_desc *published_interfaces(int &count)
{
	static const iface_desc table[] = {
		{ "IA3d5",           &IID_IA3d5,           51, reach::factory },
		{ "IA3dGeom2",       &IID_IA3dGeom2,       52, reach::root    },
		{ "IA3dListener",    &IID_IA3dListener,    19, reach::root    },
		{ "IA3dReverb",      &IID_IA3dReverb,      13, reach::root    },
		{ "IA3dReflection",  &IID_IA3dReflection,  15, reach::root    },
		{ "IA3dPropertySet", &IID_IA3dPropertySet,  7, reach::root    },
		{ "IA3dSource2",     &IID_IA3dSource2,     78, reach::source  },
	};
	count = static_cast<int>(sizeof table / sizeof table[0]);
	return table;
}

}	/* namespace a3dtest */

#endif	/* A3DTEST_IFACE_DESC_HPP */
