// Debug instrumentation (-state-hash-log): each frame, append a hash of candidate global state
// regions to a file, and provide the combined hash that savestates verify against.
//
// Ported to the 2.55 line, where the region list is a little narrower than on main: 2.55 has
// no `Hero::debug_state_hash()`/`debug_state_string()`, no `cur_region` global, and FFCs are
// tracked as flat ffpos*/ffprv* arrays rather than through a `for_every_ffc` handle.
// Everything hashed here exists on 2.55, and is what a savestate has to reproduce.

#include "zc/zelda.h"

#include "base/random.h"
#include "gamedata.h"
#include "zc/ffscript.h"
#include "zc/hero.h"
#include "zc/maps.h"
#include "zc/replay.h"
#include "zc/savestate_snapshot.h"
#include "zc/savestate_serialize.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

// 2.55 declares the player object per translation unit.
extern HeroClass Hero;

static std::string g_state_hash_log;

void set_state_hash_log(const std::string& path)
{
	g_state_hash_log = path;
}

// A frame log named by `<path_env>`, limited to the frames in `<range_env>` ("<lo>-<hi>", or every
// frame without it). Both dumps below are diagnostics for a restore that drifts: they are inert
// unless their environment variables are set.
static FILE* frame_log(const char* path_env, const char* range_env)
{
	const char* path = getenv(path_env);
	if (!path || !replay_is_active())
		return nullptr;

	static std::map<std::string, std::pair<int, int>> ranges;
	auto it = ranges.find(range_env);
	if (it == ranges.end())
	{
		int lo = -1, hi = -1;
		if (const char* r = getenv(range_env))
			sscanf(r, "%d-%d", &lo, &hi);
		it = ranges.emplace(range_env, std::make_pair(lo, hi)).first;
	}
	int frame = replay_get_frame();
	if (it->second.first >= 0 && (frame < it->second.first || frame > it->second.second))
		return nullptr;

	return fopen(path, "a");
}

// ZC_HERO_FIELD_LOG=<path> (with ZC_HERO_FIELD_FRAMES=<lo>-<hi>) appends one line per frame in the
// range holding the hash of every field the hero's state description covers. Diffing two lines from
// a baseline run and a rewound run points at the exact field that failed to restore.
void dump_hero_field_hashes()
{
	static const char* path = getenv("ZC_HERO_FIELD_LOG");
	if (!path || !replay_is_active())
		return;

	static bool read_raw = false;
	static std::vector<int> raw_idx;
	if (!read_raw)
	{
		read_raw = true;
		if (const char* r = getenv("ZC_HERO_FIELD_RAW"))
		{
			const char* p = r;
			while (*p)
			{
				raw_idx.push_back(atoi(p));
				while (*p && *p != ',')
					p++;
				if (*p)
					p++;
			}
		}
	}

	FILE* f = frame_log("ZC_HERO_FIELD_LOG", "ZC_HERO_FIELD_FRAMES");
	if (!f)
		return;

	StateFieldHash h;
	Hero.visit_state(h);

	fprintf(f, "%s frame=%d n=%d",
		replay_get_replay_path().filename().string().c_str(), replay_get_frame(), (int)h.fields.size());
	for (uint64_t v : h.fields)
		fprintf(f, " %016llx", (unsigned long long)v);
	for (int i : raw_idx)
		if (i >= 0 && (size_t)i < h.raws.size())
			fprintf(f, " %d=%lld", i, (long long)h.raws[i]);
	fputc('\n', f);
	fclose(f);
}

// ZC_SPRITE_FIELD_LOG=<path> appends, per frame, every sprite with the hash of every field its
// class's state description covers (see snapshot_sprite_fields_line).
void dump_sprite_field_hashes()
{
	FILE* f = frame_log("ZC_SPRITE_FIELD_LOG", "ZC_SPRITE_FIELD_FRAMES");
	if (!f)
		return;
	fprintf(f, "%s frame=%d%s\n", replay_get_replay_path().filename().string().c_str(),
		replay_get_frame(), snapshot_sprite_fields_line().c_str());
	fclose(f);
}

// ZC_SPRITE_DUMP_LOG=<path> (with the same frame range variable) appends, per frame, every sprite
// list's contents - which is what says *which* sprite a rewound run has that the recorded one does
// not, once the region hash says the lists differ.
void dump_sprite_lists()
{
	FILE* f = frame_log("ZC_SPRITE_DUMP_LOG", "ZC_SPRITE_DUMP_FRAMES");
	if (!f)
		return;

	static const char* names[] = {"guys", "items", "Ewpns", "Lwpns", "chainlinks", "decorations", "portals"};
	int li = 0;
	fprintf(f, "%s frame=%d", replay_get_replay_path().filename().string().c_str(), replay_get_frame());
	for (sprite_list* list : {&guys, &items, &Ewpns, &Lwpns, &chainlinks, &decorations, &portals})
	{
		fprintf(f, " %s=%d[", names[li++], list->Count());
		for (int32_t i = 0; i < list->Count(); i++)
		{
			sprite* s = list->spr(i);
			if (!s)
				continue;
			fprintf(f, " id=%d x=%d y=%d z=%d dir=%d",
				s->id, (int)s->x.getZLong(), (int)s->y.getZLong(), (int)s->z.getZLong(), (int)s->dir);
		}
		fprintf(f, "]");
	}
	fputc('\n', f);
	fclose(f);
}

static uint64_t state_fnv1a(const void* data, size_t len, uint64_t h)
{
	const uint8_t* p = static_cast<const uint8_t*>(data);
	for (size_t i = 0; i < len; i++)
	{
		h ^= p[i];
		h *= 1099511628211ULL;
	}
	return h;
}

// The held-control bits as one number, for comparing what a rewound run feeds the hero against the
// trajectory it is supposed to be reproducing.
static unsigned input_bits(const bool* states)
{
	unsigned bits = 0;
	for (int i = 0; i < ZC_CONTROL_STATES; i++)
		if (states[i])
			bits |= 1u << i;
	return bits;
}

static uint64_t state_hash_zarray(const ZScriptArray& arr, uint64_t h)
{
	// 2.55's ZCArray has no getData(); hash element by element.
	auto& a = const_cast<ZScriptArray&>(arr);
	for (decltype(a.Size()) i = 0; i < a.Size(); i++)
	{
		int32_t v = a[i];
		h = state_fnv1a(&v, sizeof(v), h);
	}
	return h;
}

static uint64_t state_hash_sprite_list(sprite_list& list, uint64_t h)
{
	int32_t count = list.Count();
	h = state_fnv1a(&count, sizeof(count), h);
	for (int32_t i = 0; i < count; i++)
	{
		sprite* s = list.spr(i);
		if (!s)
			continue;
		// uid is private on 2.55, so identity here is the position/type/direction tuple.
		int32_t sd[] = {
			s->x.getZLong(), s->y.getZLong(), s->z.getZLong(),
			(int32_t)s->dir, s->id,
		};
		h = state_fnv1a(sd, sizeof(sd), h);
	}
	return h;
}

// The set of state region hashes that both -state-hash-log and savestate verification use.
struct StateHashes
{
	uint64_t hero = 0;
	uint64_t hero_int = 0;
	uint64_t rng = 0;
	uint64_t globalram = 0;
	uint64_t objectram = 0;
	uint64_t sprites = 0;
	uint64_t viewport = 0;
	uint64_t ffc = 0;
	uint64_t screen = 0;
	uint64_t scroll = 0;
	uint64_t game = 0;

	// A single hash over every region above, so callers can compare whole states.
	uint64_t combined() const
	{
		uint64_t values[] = {
			hero, hero_int, rng, globalram, objectram,
			sprites, viewport, ffc, screen, scroll, game,
		};
		return state_fnv1a(values, sizeof(values), 1469598103934665603ULL);
	}

	// The same, without the quest progress. What the player can see and act on is the world, so
	// this is what the frames *after* a rewind are watched with: a progress difference that
	// matters (an item that changes what the hero can do, a switch that opens a door) shows up in
	// the world a few frames later, while a script rewriting a flag the player cannot see does
	// not send a rewind down the slow path. A restore's progress still has to match exactly at
	// the frame it lands on - see snapshot_frame_hook.
	uint64_t world() const
	{
		uint64_t values[] = {
			hero, hero_int, rng, globalram, objectram,
			sprites, viewport, ffc, screen, scroll,
		};
		return state_fnv1a(values, sizeof(values), 1469598103934665603ULL);
	}
};

static StateHashes compute_state_hashes()
{
	const uint64_t SEED = 1469598103934665603ULL;
	StateHashes hashes;

	int32_t hero_state[] = {
		Hero.getX().getZLong(), Hero.getY().getZLong(), Hero.getZ().getZLong(),
		Hero.getFakeFall().getZLong(),
		Hero.getXOfs().getZLong(), Hero.getYOfs().getZLong(), (int32_t)Hero.dir,
	};
	hashes.hero = state_fnv1a(hero_state, sizeof(hero_state), SEED);
	// 2.55 has no Hero::debug_state_hash(); the same fields again keep the combined hash
	// shape identical to the 3.x oracle so savestates/savestate files stay comparable.
	hashes.hero_int = hashes.hero;

	extern zc_randgen script_rnggens[MAX_USER_RNGS];
	hashes.rng = state_fnv1a(script_rnggens, sizeof(script_rnggens), SEED);

	hashes.globalram = SEED;
	if (game)
		for (auto& arr : game->globalRAM)
			hashes.globalram = state_hash_zarray(arr, hashes.globalram);

	hashes.objectram = SEED;
	for (auto& [id, arr] : objectRAM)
	{
		hashes.objectram = state_fnv1a(&id, sizeof(id), hashes.objectram);
		hashes.objectram = state_hash_zarray(arr, hashes.objectram);
	}

	hashes.sprites = SEED;
	for (sprite_list* list : {&guys, &items, &Ewpns, &Lwpns, &chainlinks, &decorations, &portals})
		hashes.sprites = state_hash_sprite_list(*list, hashes.sprites);

	int32_t vp[] = {viewport.x, viewport.y, viewport.w, viewport.h};
	hashes.viewport = state_fnv1a(vp, sizeof(vp), SEED);

	// 2.55 keeps FFC position/velocity in flat arrays, indexed by FFC slot.
	hashes.ffc = SEED;
	for (int i = 0; i < MAXFFCS; i++)
	{
		int32_t fd[] = {ffposx[i], ffposy[i], ffprvx[i], ffprvy[i]};
		hashes.ffc = state_fnv1a(fd, sizeof(fd), hashes.ffc);
	}

	int32_t scr_idx[] = {currmap, currscr, dlevel};
	hashes.screen = state_fnv1a(scr_idx, sizeof(scr_idx), SEED);

	int32_t scroll_state[] = {
		(int32_t)screenscrolling, scrolling_map, scrolling_scr,
		(int32_t)scrolling_using_new_region_coords,
		newscr_clk,
	};
	hashes.scroll = state_fnv1a(scroll_state, sizeof(scroll_state), SEED);

	// Quest progress (hearts, inventory, counters, keys, maps, switch states, script data):
	// hashed as the exact bytes a snapshot stores, so the check covers exactly what a restore
	// can put back - the part that says what the player has earned or lost.
	hashes.game = snapshot_gamedata_hash();

	return hashes;
}

std::string state_hash_string()
{
	return state_hash_string(StateHashKind::All);
}

std::string state_hash_string(StateHashKind kind)
{
	if (!replay_is_active())
		return "";

	StateHashes hashes = compute_state_hashes();
	uint64_t value = kind == StateHashKind::World ? hashes.world() : hashes.combined();
	char buf[17];
	snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)value);
	return std::string(buf);
}


// A debugging tool for "is anything else part of the state?": a record of every writable byte of
// the program - the engine's globals, wherever they are - taken the first time a frame is played,
// and compared against the state of that same frame when a rewind plays it again.
//
//   ZC_GLOBAL_COMPARE_FRAMES=<lo>-<hi>
//
// Everything the runtime keeps for itself (allocator bookkeeping, stdio buffers, locale) is the
// same in both passes, so what is reported here is game state a restore did not put back. Each
// frame's record is hashed in blocks; when the range is small enough (see ZC_GLOBAL_BLOCKS_RANGE,
// a module-offset range) the bytes are kept too, so the differing *words* can be named from the
// link map with scripts/named_from_map.py.
void dump_global_blocks()
{
#ifdef _WIN32
	static int lo = -1, hi = -1;
	static bool read_env = false;
	if (!read_env)
	{
		read_env = true;
		if (const char* r = getenv("ZC_GLOBAL_COMPARE_FRAMES"))
			sscanf(r, "%d-%d", &lo, &hi);
	}
	if (lo < 0 || !replay_is_active())
		return;
	int frame = replay_get_frame();
	if (frame < lo || frame > hi)
		return;

	static uint8_t* base = nullptr;
	static size_t len = 0;
	static size_t skip = 0;         // bytes of the section before the range of interest
	static size_t span = 0;         // bytes in the range of interest
	static size_t block = 4096;
	static std::vector<std::vector<uint64_t>> first_hashes;
	static std::vector<std::vector<uint8_t>> first_bytes;   // empty unless the range is small
	static std::vector<uint8_t> seen;

	if (!base)
	{
		HMODULE mod = GetModuleHandleW(nullptr);
		auto* dos = (IMAGE_DOS_HEADER*)mod;
		auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)mod + dos->e_lfanew);
		auto* sec = IMAGE_FIRST_SECTION(nt);
		for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
		{
			if (!(sec->Characteristics & IMAGE_SCN_MEM_WRITE))
				continue;
			base = (uint8_t*)mod + sec->VirtualAddress;
			len = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
			break;
		}
		if (!base)
			return;
		// The default is the whole section; a range narrows it to the part being investigated.
		unsigned long long want_lo = 0, want_hi = 0;
		if (const char* r = getenv("ZC_GLOBAL_BLOCKS_RANGE"))
			sscanf(r, "%llx-%llx", &want_lo, &want_hi);
		if (const char* r = getenv("ZC_GLOBAL_BLOCKS_SIZE"))
			block = (size_t)strtoull(r, nullptr, 0);
		if (block < 4)
			block = 4;
		skip = want_lo ? want_lo : 0;
		span = (want_hi && want_hi <= len ? want_hi : len) - skip;
		size_t frames = (size_t)(hi - lo + 1);
		first_hashes.assign(frames, {});
		seen.assign(frames, 0);
		// Word-level detail is only kept for a small window (a single block or a few).
		if (span <= 4u * 1024 * 1024)
			first_bytes.assign(frames, {});
		fprintf(stderr, "state diff: watching %zu bytes of writable data from module offset 0x%zx, blocks of %zu\n",
			span, skip, block);
	}

	size_t index = (size_t)(frame - lo);
	size_t blocks = (span + block - 1) / block;
	if (!seen[index])
	{
		seen[index] = 1;
		auto& hashes = first_hashes[index];
		hashes.resize(blocks);
		for (size_t b = 0; b < blocks; b++)
		{
			size_t off = skip + b * block;
			size_t n = std::min(block, len - off);
			uint64_t h = 1469598103934665603ULL;
			const uint8_t* p = base + off;
			for (size_t i = 0; i < n; i++)
			{
				h ^= p[i];
				h *= 1099511628211ULL;
			}
			hashes[b] = h;
		}
		if (!first_bytes.empty())
			first_bytes[index].assign(base + skip, base + skip + span);
		return;
	}

	// A second visit: compare, and name what differs. Only the *first* re-visit is compared: a
	// session can pass a frame more than twice (a rewind's re-cover, then a re-simulation on top of
	// it), and a later pass differs for reasons that have nothing to do with the restore - reporting
	// those as "what a restore did not put back" sent this tool's audit on a long detour.
	if (seen[index] != 1)
		return;
	seen[index] = 2;
	size_t differing_blocks = 0, differing_words = 0;
	for (size_t b = 0; b < blocks; b++)
	{
		size_t off = skip + b * block;
		size_t n = std::min(block, len - off);
		uint64_t h = 1469598103934665603ULL;
		const uint8_t* p = base + off;
		for (size_t i = 0; i < n; i++)
		{
			h ^= p[i];
			h *= 1099511628211ULL;
		}
		if (h == first_hashes[index][b])
			continue;
		differing_blocks++;
		if (differing_blocks == 1)
			fprintf(stderr, "state diff: frame %d differs from its first pass:\n", frame);
		fprintf(stderr, "  block off=0x%zx\n", off);
		if (first_bytes.empty())
			continue;
		const uint8_t* before = first_bytes[index].data() + (off - skip);
		for (size_t w = 0; w + sizeof(uint64_t) <= n; w += sizeof(uint64_t))
		{
			uint64_t now = 0, was = 0;
			memcpy(&now, p + w, sizeof(now));
			memcpy(&was, before + w, sizeof(was));
			if (now == was)
				continue;
			differing_words++;
			if (differing_words <= 300)
				fprintf(stderr, "    off=0x%zx was=%016llx now=%016llx\n", off + w,
					(unsigned long long)was, (unsigned long long)now);
		}
	}
	if (differing_blocks)
		fprintf(stderr, "state diff: frame %d: %zu block(s), %zu word(s)\n",
			frame, differing_blocks, differing_words);
#else
	// Windows-specific; only used while developing on Windows.
#endif
}

void dump_state_hashes()
{
	dump_global_blocks();
	dump_hero_field_hashes();
	dump_sprite_lists();
	dump_sprite_field_hashes();

	if (g_state_hash_log.empty() || !replay_is_active())
		return;

	StateHashes hashes = compute_state_hashes();

	FILE* f = fopen(g_state_hash_log.c_str(), "a");
	if (!f)
		return;
	fprintf(f,
		"%s frame=%d gframe=%d hero=%016llx rng=%016llx globalRAM=%016llx objectRAM=%016llx sprites=%016llx viewport=%016llx ffc=%016llx screen=%016llx scroll=%016llx game=%016llx heroX=%d heroY=%d heroZ=%d heroDir=%d heroofs=%d,%d heroff=%d scrollv=%d,%d,%d,%d,%d viewportv=%d,%d,%d,%d sprcounts=%d,%d,%d,%d,%d,%d,%d liftv=%d,%d,%d,%d,%d,%d inputs=%x,%x,%x,%x\n",
		replay_get_replay_path().filename().string().c_str(), replay_get_frame(), frame,
		(unsigned long long)hashes.hero,
		(unsigned long long)hashes.rng, (unsigned long long)hashes.globalram,
		(unsigned long long)hashes.objectram, (unsigned long long)hashes.sprites,
		(unsigned long long)hashes.viewport, (unsigned long long)hashes.ffc,
		(unsigned long long)hashes.screen, (unsigned long long)hashes.scroll,
		(unsigned long long)hashes.game,
		(int)Hero.getX().getZLong(), (int)Hero.getY().getZLong(),
		(int)Hero.getZ().getZLong(), (int)Hero.dir,
		(int)Hero.getXOfs().getZLong(), (int)Hero.getYOfs().getZLong(),
		(int)Hero.getFakeFall().getZLong(),
		(int)screenscrolling, scrolling_map, scrolling_scr,
		(int)scrolling_using_new_region_coords, newscr_clk,
		viewport.x, viewport.y, viewport.w, viewport.h,
		guys.Count(), items.Count(), Ewpns.Count(), Lwpns.Count(), chainlinks.Count(),
		decorations.Count(), portals.Count(),
		(int)Hero.holditem, (int)Hero.liftclk, (int)Hero.tliftclk, (int)Hero.liftheight,
		(int)Hero.liftflags, Hero.lift_wpn ? 1 : 0,
		input_bits(raw_control_state), input_bits(control_state),
		input_bits(button_hold), input_bits(button_press));
	fclose(f);
}
