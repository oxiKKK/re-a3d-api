/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * result_comparison.hpp - set and sequence comparison for the test framework.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * set_diff reports names present on only one side. lcs_length measures
 * the common subsequence of vtable slot-count sequences.
 *
 *---------------------------------------------------------------------------
 */

#ifndef A3DTEST_DIFF_HPP
#define A3DTEST_DIFF_HPP

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace a3dtest {

template <class T>
struct set_diff_result {
	std::vector<T> only_a;
	std::vector<T> only_b;
	std::vector<T> both;
	bool equal() const { return only_a.empty() && only_b.empty(); }
};

template <class T>
set_diff_result<T> set_diff(const std::vector<T> &a, const std::vector<T> &b)
{
	std::set<T> sa(a.begin(), a.end()), sb(b.begin(), b.end());
	set_diff_result<T> r;
	for (const T &x : sa) {
		if (sb.count(x))
			r.both.push_back(x);
		else
			r.only_a.push_back(x);
	}
	for (const T &x : sb)
		if (!sa.count(x))
			r.only_b.push_back(x);
	return r;
}

/* Length of the longest common subsequence of two integer sequences. */
inline std::size_t lcs_length(const std::vector<int> &a,
			      const std::vector<int> &b)
{
	if (a.empty() || b.empty())
		return 0;
	std::vector<std::size_t> prev(b.size() + 1, 0), cur(b.size() + 1, 0);
	for (std::size_t i = 1; i <= a.size(); ++i) {
		for (std::size_t j = 1; j <= b.size(); ++j) {
			if (a[i - 1] == b[j - 1])
				cur[j] = prev[j - 1] + 1;
			else
				cur[j] = std::max(prev[j], cur[j - 1]);
		}
		std::swap(prev, cur);
	}
	return prev[b.size()];
}

}	/* namespace a3dtest */

#endif	/* A3DTEST_DIFF_HPP */
