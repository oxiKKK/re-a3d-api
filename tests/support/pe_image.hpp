/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * pe_image.hpp - a read-only PE32 reader for the test framework.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.  Nothing here
 * corresponds to an address in any Aureal binary.
 *
 * Reads PE32 sections, exports, imports, relocations and candidate
 * vtables, using the scan from tools/analysis/extract_vtables.py.
 *
 * A candidate is a consecutive run of relocated .text pointers installed
 * by mov dword ptr [reg+disp], offset vtbl (C7 /0). Its end is the next
 * installed vtable address or the end of the pointer run.
 *
 * Vtables without a recognized store are missed. An undetected adjacent
 * table can inflate the preceding count. Comparisons share these limits.
 *
 *---------------------------------------------------------------------------
 */

#ifndef A3DTEST_PE_HPP
#define A3DTEST_PE_HPP

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace a3dtest {

class pe32 {
public:
	struct section {
		std::string  name;
		std::uint32_t rva = 0, vsize = 0, off = 0, rsize = 0, chars = 0;
	};

	struct export_entry {
		std::uint32_t ordinal = 0;
		std::string   name;
		std::uint32_t rva = 0;
	};

	struct vtable {
		std::uint32_t              rva = 0;	/* address of slot 0 */
		std::vector<std::uint32_t> targets;	/* one target RVA per slot */
		std::size_t slots() const { return targets.size(); }
	};

	/* Load an image from its file bytes.  ok() reports whether it parsed. */
	explicit pe32(const std::string &path) : path_(path)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
			return;
		data_.assign(std::istreambuf_iterator<char>(f),
			     std::istreambuf_iterator<char>());
		parse();
	}

	bool ok() const { return ok_; }
	const std::string &path() const { return path_; }
	std::uint32_t imagebase() const { return imagebase_; }
	std::uint32_t va(std::uint32_t rva) const { return imagebase_ + rva; }

	/* -- address conversion ------------------------------------------- */

	std::optional<std::size_t> rva_to_off(std::uint32_t rva) const
	{
		for (const section &s : sections_) {
			std::uint32_t span = s.vsize > s.rsize ? s.vsize : s.rsize;
			if (rva >= s.rva && rva < s.rva + span) {
				std::size_t o = s.off + (rva - s.rva);
				if (o < data_.size())
					return o;
				return std::nullopt;
			}
		}
		return std::nullopt;
	}

	std::optional<std::uint32_t> off_to_rva(std::size_t off) const
	{
		for (const section &s : sections_)
			if (off >= s.off && off < s.off + s.rsize)
				return s.rva + static_cast<std::uint32_t>(off - s.off);
		return std::nullopt;
	}

	const section *section_of_rva(std::uint32_t rva) const
	{
		for (const section &s : sections_) {
			std::uint32_t span = s.vsize > s.rsize ? s.vsize : s.rsize;
			if (rva >= s.rva && rva < s.rva + span)
				return &s;
		}
		return nullptr;
	}

	/* -- reading ------------------------------------------------------ */

	std::uint32_t u32(std::size_t off) const
	{
		std::uint32_t v = 0;
		if (off + 4 <= data_.size())
			std::memcpy(&v, data_.data() + off, 4);
		return v;
	}
	std::uint16_t u16(std::size_t off) const
	{
		std::uint16_t v = 0;
		if (off + 2 <= data_.size())
			std::memcpy(&v, data_.data() + off, 2);
		return v;
	}
	std::uint8_t u8(std::size_t off) const
	{
		return off < data_.size() ? data_[off] : 0;
	}

	std::optional<std::string> cstr_at_rva(std::uint32_t rva,
					       std::size_t limit = 400) const
	{
		auto o = rva_to_off(rva);
		if (!o)
			return std::nullopt;
		std::size_t e = *o;
		while (e < data_.size() && e < *o + limit && data_[e])
			++e;
		if (e >= data_.size() || data_[e])
			return std::nullopt;
		return std::string(reinterpret_cast<const char *>(data_.data() + *o),
				   e - *o);
	}

	/* -- tables ------------------------------------------------------- */

	std::vector<export_entry> exports() const
	{
		std::vector<export_entry> out;
		if (ddir_.size() < 1 || !ddir_[0].first)
			return out;
		auto eo = rva_to_off(ddir_[0].first);
		if (!eo)
			return out;
		std::uint32_t base = u32(*eo + 16);
		std::uint32_t nnames = u32(*eo + 24);
		std::uint32_t afun = u32(*eo + 28);
		std::uint32_t anam = u32(*eo + 32);
		std::uint32_t aord = u32(*eo + 36);
		auto ofun = rva_to_off(afun), onam = rva_to_off(anam),
		     oord = rva_to_off(aord);
		if (!ofun || !onam || !oord)
			return out;
		for (std::uint32_t i = 0; i < nnames; ++i) {
			std::uint32_t nrva = u32(*onam + i * 4);
			std::uint16_t idx = u16(*oord + i * 2);
			std::uint32_t rva = u32(*ofun + idx * 4);
			export_entry e;
			e.ordinal = base + idx;
			e.name = cstr_at_rva(nrva).value_or(std::string());
			e.rva = rva;
			out.push_back(e);
		}
		std::sort(out.begin(), out.end(),
			  [](const export_entry &a, const export_entry &b) {
				  return a.ordinal < b.ordinal;
			  });
		return out;
	}

	/* {dll name: [imported symbol or #ordinal]} */
	std::map<std::string, std::vector<std::string>> imports() const
	{
		std::map<std::string, std::vector<std::string>> out;
		if (ddir_.size() < 2 || !ddir_[1].first)
			return out;
		auto off = rva_to_off(ddir_[1].first);
		if (!off)
			return out;
		std::size_t o = *off;
		for (;;) {
			if (o + 20 > data_.size())
				break;
			std::uint32_t oft = u32(o), nrva = u32(o + 12),
				      first = u32(o + 16);
			if (!oft && !nrva && !first && !u32(o + 4) && !u32(o + 8))
				break;
			std::string dll = cstr_at_rva(nrva).value_or(std::string());
			std::vector<std::string> names;
			auto t = rva_to_off(oft ? oft : first);
			while (t) {
				std::uint32_t v = u32(*t);
				if (!v)
					break;
				if (v & 0x80000000u)
					names.push_back("#" + std::to_string(v & 0xffff));
				else
					names.push_back(cstr_at_rva(v + 2).value_or(std::string()));
				t = *t + 4;
			}
			out[dll] = names;
			o += 20;
		}
		return out;
	}

	/* Sorted RVAs of every HIGHLOW base relocation site. */
	std::vector<std::uint32_t> relocations() const
	{
		std::vector<std::uint32_t> out;
		if (ddir_.size() < 6 || !ddir_[5].first)
			return out;
		auto off = rva_to_off(ddir_[5].first);
		if (!off)
			return out;
		std::size_t o = *off, end = *off + ddir_[5].second;
		while (o + 8 <= data_.size() && o < end) {
			std::uint32_t page = u32(o), blk = u32(o + 4);
			if (blk < 8)
				break;
			std::uint32_t n = (blk - 8) / 2;
			for (std::uint32_t i = 0; i < n; ++i) {
				std::uint16_t e = u16(o + 8 + i * 2);
				if ((e >> 12) == 3)
					out.push_back(page + (e & 0xfff));
			}
			o += blk;
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	/* -- vtables (port of extract_vtables.py) ------------------------- */

	/* {location RVA of a relocated dword: the .text RVA it points at}. */
	std::map<std::uint32_t, std::uint32_t> text_pointers() const
	{
		std::map<std::uint32_t, std::uint32_t> out;
		const section *code = section_named(".text");
		if (!code)
			return out;
		std::uint32_t lo = code->rva, hi = code->rva + code->vsize;
		for (std::uint32_t rva : relocations()) {
			const section *s = section_of_rva(rva);
			if (!s || s->name == ".text")
				continue;
			auto o = rva_to_off(rva);
			if (!o || *o + 4 > data_.size())
				continue;
			std::uint32_t v = u32(*o) - imagebase_;
			if (v >= lo && v < hi)
				out[rva] = v;
		}
		return out;
	}

	/*
	 * {installed vtable RVA: [store-site RVAs]}.  A store is the imm32 of
	 * `mov r/m32, imm32` (C7 /0) whose destination is memory through a
	 * register and whose immediate equals a vtable's slot-0 RVA.
	 */
	std::map<std::uint32_t, std::vector<std::uint32_t>>
	install_sites(const std::map<std::uint32_t, std::uint32_t> &ptr) const
	{
		std::map<std::uint32_t, std::vector<std::uint32_t>> out;
		const section *code = section_named(".text");
		if (!code)
			return out;
		std::uint32_t lo = code->rva, hi = code->rva + code->vsize;
		for (std::uint32_t rva : relocations()) {
			if (rva < lo || rva >= hi)
				continue;
			auto oo = rva_to_off(rva);
			if (!oo || *oo + 4 > data_.size())
				continue;
			std::size_t o = *oo;
			std::uint32_t v = u32(o) - imagebase_;
			if (!ptr.count(v))
				continue;
			for (std::size_t p = (o >= 8 ? o - 8 : 0); p < o - 1; ++p) {
				if (data_[p] != 0xc7)
					continue;
				std::uint8_t modrm = data_[p + 1];
				int mod = modrm >> 6, rm = modrm & 7;
				if (mod == 3 || (mod == 0 && rm == 5))
					continue;	/* register, or [disp32] */
				std::size_t q = p + 2;
				if (rm == 4)
					q += 1;		/* SIB */
				if (mod == 1)
					q += 1;
				else if (mod == 2)
					q += 4;
				if (q == o) {
					out[v].push_back(rva);
					break;
				}
			}
		}
		return out;
	}

	/* Every vtable, in address order. */
	std::vector<vtable> vtables() const
	{
		std::map<std::uint32_t, std::uint32_t> ptr = text_pointers();
		std::map<std::uint32_t, std::vector<std::uint32_t>> sites =
			install_sites(ptr);

		/* runs of consecutive slot locations */
		std::vector<std::vector<std::uint32_t>> runs;
		for (const auto &kv : ptr) {
			std::uint32_t a = kv.first;
			if (!runs.empty() && a == runs.back().back() + 4)
				runs.back().push_back(a);
			else
				runs.push_back({a});
		}

		std::vector<vtable> out;
		for (const auto &r : runs) {
			std::vector<std::size_t> hits;
			for (std::size_t i = 0; i < r.size(); ++i)
				if (sites.count(r[i]))
					hits.push_back(i);
			if (hits.empty())
				continue;
			std::vector<std::size_t> edge = hits;
			edge.push_back(r.size());
			for (std::size_t j = 0; j < hits.size(); ++j) {
				std::size_t s = hits[j], e = edge[j + 1];
				vtable vt;
				vt.rva = r[s];
				for (std::size_t k = s; k < e; ++k)
					vt.targets.push_back(ptr.at(r[k]));
				out.push_back(std::move(vt));
			}
		}
		return out;
	}

	/* The vtable whose slot 0 is at rva, or nullptr. */
	const vtable *vtable_at(const std::vector<vtable> &vts,
				std::uint32_t rva) const
	{
		for (const vtable &v : vts)
			if (v.rva == rva)
				return &v;
		return nullptr;
	}

private:
	const section *section_named(const char *name) const
	{
		for (const section &s : sections_)
			if (s.name == name)
				return &s;
		return nullptr;
	}

	void parse()
	{
		const std::vector<std::uint8_t> &d = data_;
		if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z')
			return;
		std::uint32_t pe = u32(0x3c);
		if (pe + 24 > d.size() || u32(pe) != 0x00004550u)	/* "PE\0\0" */
			return;
		std::uint16_t nsec = u16(pe + 6);
		std::uint16_t optsz = u16(pe + 20);
		std::size_t opt = pe + 24;
		if (u16(opt) != 0x10b)		/* PE32 */
			return;
		imagebase_ = u32(opt + 28);
		std::uint32_t nddir = u32(opt + 92);
		std::size_t ddoff = opt + 96;
		for (std::uint32_t i = 0; i < nddir; ++i)
			ddir_.emplace_back(u32(ddoff + i * 8), u32(ddoff + i * 8 + 4));

		std::size_t so = pe + 24 + optsz;
		for (std::uint16_t i = 0; i < nsec; ++i) {
			std::size_t o = so + i * 40;
			if (o + 40 > d.size())
				break;
			section s;
			std::size_t n = 0;
			while (n < 8 && d[o + n])
				++n;
			s.name.assign(reinterpret_cast<const char *>(d.data() + o), n);
			s.vsize = u32(o + 8);
			s.rva = u32(o + 12);
			s.rsize = u32(o + 16);
			s.off = u32(o + 20);
			s.chars = u32(o + 36);
			sections_.push_back(std::move(s));
		}
		ok_ = true;
	}

	std::string path_;
	std::vector<std::uint8_t> data_;
	std::vector<section> sections_;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> ddir_;
	std::uint32_t imagebase_ = 0x10000000;
	bool ok_ = false;
};

}	/* namespace a3dtest */

#endif	/* A3DTEST_PE_HPP */
