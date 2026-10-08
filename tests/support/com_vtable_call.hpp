/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * com_vtable_call.hpp - call a COM method by vtable slot on an object of unknown type.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * CallComVtableSlot<Ret>(obj, slot, args...) invokes the selected __stdcall
 * entry with obj as the implicit `this`. It supports reference objects whose
 * type is unavailable. Callers must establish each side's slot arity
 * independently to avoid concealing a shared declaration error.
 *
 *---------------------------------------------------------------------------
 */

#ifndef A3DTEST_BLINDCALL_HPP
#define A3DTEST_BLINDCALL_HPP

namespace a3dtest {


inline void *const *vtable_of(const void *obj)
{
	return *reinterpret_cast<void *const *const *>(obj);
}


inline void *vtable_slot(const void *obj, unsigned slot)
{
	return vtable_of(obj)[slot];
}

template <class Ret, class... Args>
Ret CallComVtableSlot(void *obj, unsigned slot, Args... args)
{
	void **vtbl = *reinterpret_cast<void ***>(obj);
	typedef Ret(__stdcall *fn)(void *, Args...);
	return reinterpret_cast<fn>(vtbl[slot])(obj, args...);
}

}	/* namespace a3dtest */

#endif	/* A3DTEST_BLINDCALL_HPP */
