#pragma once

// The serializer a savestate is written with and read from.
//
// State is stored as *values*, never as memory images: a class describes its own state in
// `template <typename S> void visit_state(S& s)`, and the same description is used to write it,
// to walk past it (StateSkip, for a validating pass that must not change the game) and to read it
// back (StateReader). Pointers are not values: a reference to another object is written as that
// object's UID and resolved after every object exists again (see savestate_snapshot.cpp).
//
// The stream is in-process only; the caller versions it.

#include "base/zc_array.h"
#include "snapshot_run_scan.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <list>
#include <string>
#include <type_traits>
#include <vector>

struct SaveStateBlob
{
	std::vector<uint8_t> data;
	size_t pos = 0;
	bool failed = false;

	void put(const void* p, size_t n)
	{
		const uint8_t* b = static_cast<const uint8_t*>(p);
		data.insert(data.end(), b, b + n);
	}
	template <typename T> void put(const T& v) { put(&v, sizeof(T)); }
	void put_str(const std::string& s)
	{
		uint32_t n = (uint32_t)s.size();
		put(n);
		put(s.data(), n);
	}

	void get(void* p, size_t n)
	{
		if (failed || pos > data.size() || n > data.size() - pos)
		{
			failed = true;
			memset(p, 0, n);
			return;
		}
		memcpy(p, data.data() + pos, n);
		pos += n;
	}
	template <typename T> void get(T& v) { get(&v, sizeof(T)); }
	void get_str(std::string& s)
	{
		uint32_t n = 0;
		get(n);
		if (failed || pos > data.size() || n > data.size() - pos)
		{
			failed = true;
			s.clear();
			return;
		}
		s.assign((const char*)data.data() + pos, n);
		pos += n;
	}
};

// Sanity bound for a container length read out of a snapshot: a corrupted or foreign stream must
// not make us allocate wildly.
const uint32_t STATE_MAX_ELEMENTS = 1u << 26;

struct StateWriter
{
	SaveStateBlob& b;

	template <typename... T> void operator()(const T&... values) {
        constexpr size_t bytes=(size_t(0)+...+sizeof(T));
        if constexpr(sizeof...(T)>1 && bytes<=512
            && (std::is_trivially_copyable_v<T> && ...)) {
            // Match the legacy sequence of individual object-value writes,
            // without adding alignment/padding between fields. Only the final
            // vector append can allocate. Larger/non-POD groups keep the old path.
            uint8_t packed[bytes];size_t offset=0;
            auto append=[&](const auto& value) {
                memcpy(packed+offset,&value,sizeof(value));offset+=sizeof(value);
            };
            (append(values),...);b.put(packed,bytes);
        } else (b.put(values),...);
    }

	// A contiguous block of plain values whose length is fixed by the caller.
	template <typename T> void pod_array(const T* p, size_t n)
	{
		b.put((uint32_t)n);
		if (n)
			b.put(p, n * sizeof(T));
	}

	// A block that is mostly runs of the same value (an interpreter stack, a palette bank): stored
	// as runs, so a snapshot does not pay for the parts of it nothing has touched.
	template <typename T> void runs(const T* data, size_t count)
	{
		b.put((uint32_t)count);
		size_t i = 0;
		while (i < count)
		{
            const size_t run=zc255_run_length(data+i,count-i);
			b.put((uint32_t)run);
			b.put((const void*)(data + i), sizeof(T));
			i += run;
		}
	}

	template <typename T> void vec(const std::vector<T>& v)
	{
		static_assert(std::is_trivially_copyable_v<T>, "only plain values can be written this way");
		b.put((uint32_t)v.size());
		if (!v.empty())
			b.put(v.data(), v.size() * sizeof(T));
	}

	void i32_deque(const std::deque<int32_t>& d)
	{
		b.put((uint32_t)d.size());
		for (int32_t v : d)
			b.put(v);
	}

	// A std::list of plain records, element by element: the nodes are the list's own business, so
	// only the values travel.
	template <typename T> void list(const std::list<T>& l)
	{
		b.put((uint32_t)l.size());
		for (auto& e : l)
			b.put((const void*)&e, sizeof(T));
	}

	// A script array (ZScriptArray): whether the slot is live, and its contents.
	void zarray(const ZScriptArray& a)
	{
		b.put((uint8_t)(a.Valid() ? 1 : 0));
		uint32_t n = (uint32_t)a.Size();
		b.put(n);
		for (uint32_t i = 0; i < n; i++)
		{
			int32_t v = const_cast<ZScriptArray&>(a)[i];
			b.put(v);
		}
	}

	// The interpreter's stack of variadic-argument vectors.
	void vargs(const std::vector<std::vector<int32_t>>& v)
	{
		b.put((uint32_t)v.size());
		for (auto& inner : v)
		{
			b.put((uint32_t)inner.size());
			for (int32_t x : inner)
				b.put(x);
		}
	}
};

struct StateReader
{
	SaveStateBlob& b;

	template <typename... T> void operator()(T&... values) { (b.get(values), ...); }

	template <typename T> void pod_array(T* p, size_t n)
	{
		uint32_t stored = 0;
		b.get(stored);
		if (b.failed || stored != n)
		{
			b.failed = true;
			return;
		}
		if (n)
			b.get(p, n * sizeof(T));
	}

	template <typename T> void vec(std::vector<T>& v)
	{
		static_assert(std::is_trivially_copyable_v<T>, "only plain values can be read this way");
		uint32_t n = 0;
		b.get(n);
		if (b.failed || n > STATE_MAX_ELEMENTS || n > b.data.size() - b.pos)
		{
			b.failed = true;
			return;
		}
		v.resize(n);
		if (n)
			b.get(v.data(), n * sizeof(T));
	}

	template <typename T> void runs(T* data, size_t count)
	{		uint32_t stored = 0;
		b.get(stored);
		if (b.failed || stored != count)
		{
			b.failed = true;
			return;
		}
		size_t i = 0;
		while (i < count)
		{
			uint32_t run = 0;
			b.get(run);
			if (b.failed || run == 0 || run > count - i)
			{
				b.failed = true;
				return;
			}
			b.get((void*)(data + i), sizeof(T));
            if(b.failed) return;
            if(run>1) {
            uint8_t* first=reinterpret_cast<uint8_t*>(data+i);
            const size_t bytes=size_t(run)*sizeof(T);
            bool uniform=true;
            for(size_t byte=1;byte<sizeof(T);++byte)
                if(first[byte]!=first[0]) { uniform=false; break; }
            if(uniform) memset(first,first[0],bytes);
            else {
                size_t filled=sizeof(T);
                while(filled<bytes) {
                    const size_t chunk=std::min(filled,bytes-filled);
                    memcpy(first+filled,first,chunk);
                    filled+=chunk;
                }
            }
            }
			i += run;
		}
	}

	void i32_deque(std::deque<int32_t>& d)
	{
		uint32_t n = 0;
		b.get(n);
		if (b.failed || n > STATE_MAX_ELEMENTS || n > b.data.size() - b.pos)
		{
			b.failed = true;
			return;
		}
		d.clear();
		for (uint32_t i = 0; i < n; i++)
		{
			int32_t v = 0;
			b.get(v);
			d.push_back(v);
		}
	}

	template <typename T> void list(std::list<T>& l)
	{
		uint32_t n = 0;
		b.get(n);
		if (b.failed || n > STATE_MAX_ELEMENTS || n > b.data.size() - b.pos)
		{
			b.failed = true;
			return;
		}
		l.clear();
		for (uint32_t i = 0; i < n; i++)
		{
			T e{};
			b.get((void*)&e, sizeof(T));
			if (b.failed)
				return;
			l.push_back(e);
		}
	}

	void zarray(ZScriptArray& a)
	{
		uint8_t valid = 0;
		uint32_t n = 0;
		b.get(valid);
		b.get(n);
		if (b.failed || n > STATE_MAX_ELEMENTS || n > b.data.size() - b.pos)
		{
			b.failed = true;
			return;
		}
		a.Clear();
		if (n)
			a.Resize(n);
		a.setValid(valid != 0);
		for (uint32_t i = 0; i < n; i++)
		{
			int32_t v = 0;
			b.get(v);
			a[i] = v;
		}
	}

	void vargs(std::vector<std::vector<int32_t>>& v)
	{
		uint32_t n = 0;
		b.get(n);
		if (b.failed || n > STATE_MAX_ELEMENTS || n > b.data.size() - b.pos)
		{
			b.failed = true;
			return;
		}
		v.clear();
		for (uint32_t i = 0; i < n; i++)
		{
			uint32_t m = 0;
			b.get(m);
			if (b.failed || m > STATE_MAX_ELEMENTS || m > (b.data.size() - b.pos) / sizeof(int32_t))
			{
				b.failed = true;
				return;
			}
			v.emplace_back();
			auto& inner = v.back();
			inner.resize(m);
			for (uint32_t j = 0; j < m; j++)
				b.get(inner[j]);
		}
	}
};

// A debugging aid, never used by the snapshot path: hashes every value a `visit_state` describes
// as its own entry, so two runs of the same frame can be diffed field by field when a restore
// diverges somewhere the region hashes are too coarse to see.
struct StateFieldHash
{
	static constexpr uint64_t SEED = 1469598103934665603ULL;
	std::vector<uint64_t> fields;
	// The first bytes of each field, zero extended, so a caller can print the value of a field by
	// index as well as notice that it changed.
	std::vector<uint64_t> raws;
	uint64_t cur = SEED;
	uint64_t raw = 0;

	void mix(const void* p, size_t n)
	{
		memcpy(&raw, p, n < sizeof(raw) ? n : sizeof(raw));
		const uint8_t* b = static_cast<const uint8_t*>(p);
		for (size_t i = 0; i < n; i++)
		{
			cur ^= b[i];
			cur *= 1099511628211ULL;
		}
	}
	void end_field() { fields.push_back(cur); raws.push_back(raw); cur = SEED; raw = 0; }

	// A polymorphic value (zfix is one) starts with a vtable pointer, and the value after it can be
	// followed by padding: hashing the word after the pointer compares the *value*, so two objects
	// of the same class cannot differ merely in what the allocator left behind them.
	template <typename T> void one(const T& v)
	{
		hash_element(v);
		end_field();
	}

	// One value, without anything the allocator happens to leave next to it: an array is hashed
	// element by element, and a polymorphic value (zfix) by the word after its vtable pointer.
	template <typename T> void hash_element(const T& v)
	{
		if constexpr (std::is_array_v<T>)
		{
			using E = std::remove_all_extents_t<T>;
			const E* p = reinterpret_cast<const E*>(&v);
			for (size_t i = 0; i < sizeof(T) / sizeof(E); i++)
				hash_element(p[i]);
		}
		else if constexpr (std::is_polymorphic_v<T>)
		{
			const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
			size_t skip = sizeof(void*);
			if (sizeof(T) > skip)
				mix(p + skip, std::min(sizeof(T) - skip, sizeof(int32_t)));
		}
		else
			mix(&v, sizeof(T));
	}
	template <typename... T> void operator()(const T&... values) { (one(values), ...); }

	template <typename T> void pod_array(const T* p, size_t n) { mix(p, n * sizeof(T)); end_field(); }
	template <typename T> void runs(const T* p, size_t n) { mix(p, n * sizeof(T)); end_field(); }
	template <typename T> void vec(const std::vector<T>& v)
	{
		mix(v.data(), v.size() * sizeof(T));
		end_field();
	}
	void i32_deque(const std::deque<int32_t>& d)
	{
		for (int32_t v : d)
			mix(&v, sizeof(v));
		end_field();
	}
	template <typename T> void list(const std::list<T>& l)
	{
		for (auto& e : l)
			mix(&e, sizeof(T));
		end_field();
	}
	void zarray(const ZScriptArray& a)
	{
		uint8_t valid = a.Valid() ? 1 : 0;
		mix(&valid, sizeof(valid));
		uint32_t n = (uint32_t)a.Size();
		mix(&n, sizeof(n));
		for (uint32_t i = 0; i < n; i++)
		{
			int32_t v = const_cast<ZScriptArray&>(a)[i];
			mix(&v, sizeof(v));
		}
		end_field();
	}
	void vargs(const std::vector<std::vector<int32_t>>& v)
	{
		for (auto& inner : v)
			mix(inner.data(), inner.size() * sizeof(int32_t));
		end_field();
	}
};

// Walks past a state block without touching any object, so a first pass can find everything a
// snapshot describes before the game is changed at all.
struct StateSkip
{
	SaveStateBlob& b;

	template <typename... T> void operator()(const T&... values) { (skip_one(values), ...); }

	template <typename T> void pod_array(const T*, size_t n)
	{
		uint32_t stored = 0;
		b.get(stored);
		if (b.failed)
			return;
		// `n` is what the caller knows the block should be (the reader, which has the object the
		// state belongs to); 0 means "the length is only in the stream", which is all a skipping
		// pass can know.
		if (n && stored != n)
		{
			b.failed = true;
			return;
		}
		skip_bytes((size_t)stored * sizeof(T));
	}

	template <typename T> void vec(const std::vector<T>&)
	{
		uint32_t n = 0;
		b.get(n);
		skip_bytes((size_t)n * sizeof(T));
	}

	template <typename T> void runs(const T*, size_t)
	{
		uint32_t count = 0;
		b.get(count);
		size_t i = 0;
		while (i < count && !b.failed)
		{
			uint32_t run = 0;
			b.get(run);
			if (run == 0 || run > count - i)
			{
				b.failed = true;
				return;
			}
			skip_bytes(sizeof(T));
			i += run;
		}
	}

	void i32_deque(const std::deque<int32_t>&)
	{
		uint32_t n = 0;
		b.get(n);
		skip_bytes((size_t)n * sizeof(int32_t));
	}

	template <typename T> void list(const std::list<T>&)
	{
		uint32_t n = 0;
		b.get(n);
		skip_bytes((size_t)n * sizeof(T));
	}

	void zarray(const ZScriptArray&)
	{
		uint8_t valid = 0;
		uint32_t n = 0;
		b.get(valid);
		b.get(n);
		skip_bytes((size_t)n * sizeof(int32_t));
	}

	void vargs(const std::vector<std::vector<int32_t>>&)
	{
		uint32_t n = 0;
		b.get(n);
		for (uint32_t i = 0; i < n && !b.failed; i++)
		{
			uint32_t m = 0;
			b.get(m);
			skip_bytes((size_t)m * sizeof(int32_t));
		}
	}

	// Move past bytes whose length the skipping pass read out of the stream itself.
	void skip_bytes(size_t n)
	{
		if (b.pos > b.data.size() || n > b.data.size() - b.pos)
			b.failed = true;
		else
			b.pos += n;
	}

private:
	template <typename T> void skip_one(const T&) { skip_bytes(sizeof(T)); }
	template <typename T, size_t N> void skip_one(const T (&)[N]) { skip_bytes(sizeof(T) * N); }
};
