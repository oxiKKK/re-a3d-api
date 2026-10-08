/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * com_lifetime.hpp - small RAII helpers for COM in the test framework.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * com_init manages COM initialization for its scope. com_ptr owns one
 * interface reference and releases it on destruction, including early
 * failure paths.
 *
 *---------------------------------------------------------------------------
 */

#ifndef A3DTEST_COM_HPP
#define A3DTEST_COM_HPP

#include <windows.h>
#include <objbase.h>

namespace a3dtest {

struct com_init {
	HRESULT hr;
	com_init() : hr(CoInitialize(nullptr)) {}
	~com_init() { if (SUCCEEDED(hr)) CoUninitialize(); }
	com_init(const com_init &) = delete;
	com_init &operator=(const com_init &) = delete;
};

template <class T>
class com_ptr {
public:
	com_ptr() = default;
	explicit com_ptr(T *p) : p_(p) {}
	com_ptr(com_ptr &&o) noexcept : p_(o.p_) { o.p_ = nullptr; }
	com_ptr &operator=(com_ptr &&o) noexcept
	{
		if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; }
		return *this;
	}
	com_ptr(const com_ptr &) = delete;
	com_ptr &operator=(const com_ptr &) = delete;
	~com_ptr() { reset(); }

	T *get() const { return p_; }
	T *operator->() const { return p_; }
	explicit operator bool() const { return p_ != nullptr; }

	/* Address for a creator that writes through void**. */
	void **put_void()
	{
		reset();
		return reinterpret_cast<void **>(&p_);
	}
	T **put()
	{
		reset();
		return &p_;
	}

	void reset()
	{
		if (p_) { p_->Release(); p_ = nullptr; }
	}

private:
	T *p_ = nullptr;
};

}	/* namespace a3dtest */

#endif	/* A3DTEST_COM_HPP */
