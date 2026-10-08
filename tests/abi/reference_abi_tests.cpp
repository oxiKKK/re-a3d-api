/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * reference_abi_tests.cpp - binary structure comparison.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 * Compares the reconstruction's built DLL to the reference binary with pe_image.hpp:
 *   gate   export table matches exactly
 *   gate   every published interface's declared slot count exists in the reference
 *   figure imports, vtable inventory, and Debug build's assert and label coverage
 *
 *---------------------------------------------------------------------------
 */

#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ia3dapi.h"

#include "result_comparison.hpp"
#include "interface_descriptors.hpp"
#include "pe_image.hpp"

using namespace a3dtest;

namespace {

std::string slurp(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return std::string();
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

std::map<int, int> length_histogram(const std::vector<pe32::vtable> &vts)
{
	std::map<int, int> h;
	for (const pe32::vtable &v : vts)
		++h[(int) v.slots()];
	return h;
}

struct hitcount { int found = 0, total = 0; };

hitcount reproduce_asserts(const std::string &tsv_path, const std::string &bytes)
{
	hitcount hc;
	std::ifstream f(tsv_path);
	std::string line;
	while (std::getline(f, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty() || line[0] == '#')
			continue;
		std::vector<std::string> col;
		std::string cur;
		std::istringstream ls(line);
		while (std::getline(ls, cur, '\t'))
			col.push_back(cur);
		if (col.size() < 5 || col[1] != "assert")
			continue;
		std::string text = col[4];
		if (text.size() >= 2 && text.front() == '(' && text.back() == ')')
			text = text.substr(1, text.size() - 2);
		if (text.empty())
			continue;
		++hc.total;
		if (bytes.find(text) != std::string::npos)
			++hc.found;
	}
	return hc;
}

hitcount reproduce_labels(const std::string &tsv_path, const std::string &bytes)
{
	hitcount hc;
	std::ifstream f(tsv_path);
	std::string line;
	while (std::getline(f, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty() || line[0] == '#')
			continue;
		std::vector<std::string> col;
		std::string cur;
		std::istringstream ls(line);
		while (std::getline(ls, cur, '\t'))
			col.push_back(cur);
		if (col.size() < 3)
			continue;
		std::string lbl = col[1] + "::" + col[2];
		++hc.total;
		if (bytes.find(lbl) != std::string::npos)
			++hc.found;
	}
	return hc;
}

std::vector<std::string> map_vtable_symbols(const std::string &path)
{
	std::vector<std::string> out;
	std::set<std::string> seen;
	std::ifstream f(path);
	std::string line;
	while (std::getline(f, line)) {
		std::size_t p = line.find("??_7");
		if (p == std::string::npos)
			continue;
		std::size_t e = p;
		while (e < line.size() && line[e] != ' ' && line[e] != '\t' &&
		       line[e] != '\r')
			++e;
		std::string sym = line.substr(p, e - p);
		if (seen.insert(sym).second)
			out.push_back(sym);
	}
	return out;
}

std::string join_first(const std::vector<std::string> &v, std::size_t k)
{
	std::string s;
	for (std::size_t i = 0; i < v.size() && i < k; ++i)
		s += (i ? ", " : "") + v[i];
	if (v.size() > k)
		s += ", +" + std::to_string(v.size() - k) + " more";
	return s;
}

/* The two images, loaded once for the whole suite. */
class AbiVsReference : public ::testing::Test {
protected:
	static pe32 *our;
	static pe32 *ref;

	static void SetUpTestSuite()
	{
		our = new pe32(A3D_OUR_DLL);
		ref = new pe32(A3D_REF_DLL);
	}
	static void TearDownTestSuite()
	{
		delete our;
		delete ref;
		our = nullptr;
		ref = nullptr;
	}
};

pe32 *AbiVsReference::our = nullptr;
pe32 *AbiVsReference::ref = nullptr;

}	/* namespace */

TEST_F(AbiVsReference, BothDllsLoad)
{
	ASSERT_TRUE(our && our->ok()) << "our: " << A3D_OUR_DLL;
	ASSERT_TRUE(ref && ref->ok()) << "ref: " << A3D_REF_DLL;
}

TEST_F(AbiVsReference, ExportTable)
{
	ASSERT_TRUE(our->ok() && ref->ok());

	std::vector<std::string> a, b;
	for (const auto &e : our->exports())
		a.push_back(e.name + "@" + std::to_string(e.ordinal));
	for (const auto &e : ref->exports())
		b.push_back(e.name + "@" + std::to_string(e.ordinal));

	auto d = set_diff(a, b);
	EXPECT_TRUE(d.only_a.empty()) << "only ours: " << join_first(d.only_a, 8);
	EXPECT_TRUE(d.only_b.empty()) << "only ref: " << join_first(d.only_b, 8);
}

TEST_F(AbiVsReference, EasterEggPayload)
{
	ASSERT_TRUE(ref->ok());
	// (RE) The same packed program is present in all three 3.3.677 images.
#ifdef _DEBUG
	const auto offset = ref->rva_to_off(0x1480E8);
#else
	const auto offset = ref->rva_to_off(0x5F924);
#endif
	const std::string reference = slurp(A3D_REF_DLL);
	const size_t packed_size = 0x36CE;
	ASSERT_TRUE(offset.has_value());
	ASSERT_GE(reference.size(), *offset + packed_size);
	const std::string packed = reference.substr(*offset, packed_size);
	ASSERT_EQ(packed.substr(0, 2), "MZ");
	EXPECT_NE(slurp(A3D_OUR_DLL).find(packed), std::string::npos)
		<< "The DLL must contain the complete original credits executable.";
}

TEST_F(AbiVsReference, VtableSlotCounts)
{
	ASSERT_TRUE(ref->ok());

	std::map<int, int> h_ref = length_histogram(ref->vtables());
	std::vector<std::string> our_syms = map_vtable_symbols(A3D_OUR_MAP);

	int n = 0;
	const iface_desc *tbl = published_interfaces(n);
	for (int i = 0; i < n; ++i) {
		int want = tbl[i].slots;
		int in_ref = h_ref.count(want) ? h_ref[want] : 0;

		std::string tag = std::string("6B") + tbl[i].name + "@@@";
		bool our_named = false;
		for (const std::string &s : our_syms)
			if (s.find(tag) != std::string::npos) {
				our_named = true;
				break;
			}

		EXPECT_GE(in_ref, 1)
			<< tbl[i].name << " declares " << want
			<< " slots but the reference has no vtable of that length"
			<< " (our map names it: " << (our_named ? "yes" : "on impl class")
			<< ")";
	}
}

TEST_F(AbiVsReference, AssertAndLabelCoverage)
{
	ASSERT_TRUE(our->ok() && ref->ok());

	std::vector<std::string> a, b;
	for (const auto &kv : our->imports())
		for (const auto &s : kv.second)
			a.push_back(kv.first + "!" + s);
	for (const auto &kv : ref->imports())
		for (const auto &s : kv.second)
			b.push_back(kv.first + "!" + s);
	auto d = set_diff(a, b);
	GTEST_LOG_(INFO) << "imports: " << d.both.size() << " shared; only ours "
			 << d.only_a.size() << ", only ref " << d.only_b.size()
			 << " (" << join_first(d.only_b, 4) << ")";

	auto vt_our = our->vtables();
	auto vt_ref = ref->vtables();
	std::size_t map_syms = map_vtable_symbols(A3D_OUR_MAP).size();
	GTEST_LOG_(INFO) << "vtables: our map " << map_syms << ", our scan "
			 << vt_our.size() << " (Aureal-calibrated), ref scan "
			 << vt_ref.size();

	std::string bytes = slurp(A3D_OUR_DLL);
	hitcount ac = reproduce_asserts(std::string(A3D_GROUNDTRUTH) +
					"/filelines-dbg.tsv", bytes);
	hitcount lc = reproduce_labels(std::string(A3D_GROUNDTRUTH) +
				       "/labels-dbg.tsv", bytes);
	GTEST_LOG_(INFO) << "reproduced: " << ac.found << "/" << ac.total
			 << " assert expressions, " << lc.found << "/" << lc.total
			 << " Class::Method labels (0 in a Release build)";

	SUCCEED();
}

#if defined(A3D_FIXES)
TEST_F(AbiVsReference, NewEasterEggPayload)
{
	pe32 credits(A3D_CREDITS_REF_DLL);
	ASSERT_TRUE(credits.ok());
	const auto offset = credits.rva_to_off(0x5F9E4);
	ASSERT_TRUE(offset.has_value());
	const std::string reference = slurp(A3D_CREDITS_REF_DLL);
	ASSERT_GE(reference.size(), *offset + 0x4293);
	EXPECT_NE(slurp(A3D_OUR_DLL).find(reference.substr(*offset, 0x4293)),
		std::string::npos);
}
#endif
