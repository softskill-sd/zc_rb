#include "zc/savestate_snapshot.h"

#include "base/mapscr.h"
#include "base/random.h"
#include "gamedata.h"
#include "items.h"
#include "zc/combos.h"
#include "zc/decorations.h"
#include "zc/ffscript.h"
#include "zc/guys.h"
#include "zc/hero.h"
#include "zc/maps.h"
#include "zc/replay.h"
#include "zc/savestate_serialize.h"
#include "zc/script_drawing.h"
#include "zc/zc_sys.h"
#include "zconfig.h"
#include "zc/weapons.h"
#include "zc/zelda.h"
#include "snapshot_runtime.h"
#include "snapshot_ui.h"
#include "snapshot_definitions.h"
#include "snapshot_extra_definitions.h"
#include "snapshot_collision_state.h"
#include "snapshot_profile.h"
#include "snapshot_build.h"
#include "player_bridge.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <deque>
#include <map>
#include <new>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 2.55 declares the player object per translation unit.
extern HeroClass Hero;
extern zc_randgen script_rnggens[MAX_USER_RNGS];
extern int32_t div_prot_item, magicitem, magiccastclk, heart_beep_timer, quakeclk, wavy;
extern int32_t new_enemy_tile_start, nets, idle_count, active_count, hero_count, fairy_cnt;
extern int32_t subscr_item_clk, subscr_pg_clk;
extern portal mirror_portal;

// The engine's own per-frame globals, carried by name in the engine-globals table below. They live
// in hero.cpp and new_subscr.cpp, which do not publish them in a header.
extern int32_t dowpn, directItemA, directItemB, directItemX, directItemY, directWpn, whistleitem;
extern bool did_scripta, did_scriptb, did_scriptl;
extern int btnitem_ids[4], btnitem_clks[4], subscr_override_clkoffsets[MAXITEMS];
extern byte subscr_pg_from, subscr_pg_to;
extern bool subscr_itemless, subscr_pg_animating;
// Declared only where it is used in 2.55, not in a header.
extern int32_t skipcont;

namespace
{
	// Temporary diagnostics for the self-test: which hashed fields did not round-trip.
	std::string hero_fields_string()
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "x=%d y=%d z=%d fakefall=%d xofs=%d yofs=%d dir=%d",
			(int)Hero.getX().getZLong(), (int)Hero.getY().getZLong(), (int)Hero.getZ().getZLong(),
			(int)Hero.getFakeFall().getZLong(), (int)Hero.getXOfs().getZLong(),
			(int)Hero.getYOfs().getZLong(), (int)Hero.dir);
		return buf;
	}

	std::string sprite_counts_string()
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "guys=%d items=%d Ewpns=%d Lwpns=%d chainlinks=%d decorations=%d portals=%d",
			guys.Count(), items.Count(), Ewpns.Count(), Lwpns.Count(), chainlinks.Count(),
			decorations.Count(), portals.Count());
		return buf;
	}
	// The stream itself lives in zc/savestate_serialize.h so that the systems a snapshot covers
	// (sprites, the script VM) can describe their own state with the same visitors.
	using Blob = SaveStateBlob;

	const uint32_t SNAPSHOT_FORMAT = 19;

	struct Snapshot
	{
		int frame = -1;
		Blob blob;
	};

	// The ring of states, sized the way an emulator sizes its rewind buffer: a byte budget, not a
	// fixed number of entries, with the *oldest* entries dropped only when a new one does not fit
	// (RetroArch's `rewind_buffer_size`). A snapshot is taken every `capture_interval` frames - the
	// rewind `granularity`, which is also how far one rewind step goes - and the configured window
	// is an upper bound on top of the budget: whichever binds first decides how much history is
	// kept. Both are reported, so a player can see what the settings actually buy.
	//
	// Nothing here is ever thrown away wholesale: a rewind moves the session's position, and the
	// frames it plays again replace their own entries (see snapshot_capture). That is the property
	// that makes repeated rewinds safe.
	int rewind_window_seconds()
	{
		int seconds = zc_get_config("zeldadx", "replay_rewind_seconds", 180);
		if (seconds <= 0)
			seconds = 180;
		return seconds;
	}

	size_t rewind_budget_bytes()
	{
		int megabytes = zc_get_config("zeldadx", "replay_rewind_megabytes", 300);
		if (megabytes <= 0)
			megabytes = 300;
		return (size_t)megabytes * 1024 * 1024;
	}

	std::deque<Snapshot> ring;
	int capture_interval = 0;
	size_t total_bytes = 0;

	int selftest_frame = -1;
	int selftest_gap = 0;
	int selftest_stage = 0;
	std::string selftest_hash_at_capture;
	std::string selftest_hero_at_capture;
	std::string selftest_sprites_at_capture;

	// The state hash each frame had when the run first passed it, so a snapshot rewind can be
	// verified against the trajectory it is supposed to rejoin. Kept for the whole rewind window,
	// so every frame the player can rewind to has a hash to be checked against.
	struct FrameHashes
{
	std::string all;   // everything a snapshot carries, quest progress included
	std::string world; // the world alone - what the frames after a rewind are watched with
};
std::deque<std::pair<int, FrameHashes>> frame_hashes;
	size_t frame_hash_capacity()
	{
		return (size_t)rewind_window_seconds() * 60 + 64;
	}

	// A snapshot rewind in flight: restore, re-cover the gap, then verify.
	bool rewind_active = false;
	bool rewind_restored = false;
	int rewind_from = -1;
	int rewind_target = -1;
	// After a snapshot rewind verifies at its target, keep checking the trajectory for a
	// while: a snapshot that reproduces the target frame but then drifts (un-captured sprite
	// internals, script state) must not be trusted, and falls back to re-simulation.
	//
	// The window is a full five seconds rather than a token few frames: a rewind that reaches
	// back far compares every frame the session had already played, and the drift that has been
	// measured (a quest script a frame out of phase after a restore, which shows up as a
	// script-created weapon spawning one frame late) did not become visible until 148 frames
	// after the target. Frames beyond the point the session was rewound from have no recorded
	// hash to compare against, so those comparisons are simply skipped. Watching costs one state
	// hash per frame and, when it fires, one re-simulation to the target - the same fallback a
	// rewind takes when no snapshot can be used.
	// The frame the rewind's restoration is verified at: the first frame at or after the target
	// that the run has a recorded hash for. The frame ring holds every frame the player saw, and
	// frames the engine renders inside a nested loop (a fade, a screen transition) are seen but
	// never hashed, so a target can be one of those; the check happens at the next frame that has
	// something to compare against instead of turning the whole rewind down.
	int rewind_verify = -1;
	int watch_target = -1;
	int watch_until = -1;
	const int WATCH_FRAMES = 300;

	std::string hash_for_frame(int frame, StateHashKind kind)
	{
		auto it = std::lower_bound(frame_hashes.begin(), frame_hashes.end(), frame,
			[](const std::pair<int, FrameHashes>& e, int value) { return e.first < value; });
		if (it == frame_hashes.end() || it->first != frame)
			return "";
		return kind == StateHashKind::World ? it->second.world : it->second.all;
	}

	// Byte runs, for blocks that are mostly repeated (the 256 script RNGs are, which is what
	// makes a short capture interval affordable: uncompressed they are ~1.3 MB per snapshot).
	void put_runs(Blob& b, const uint8_t* data, size_t len, size_t unit)
	{
		b.put((uint32_t)len);
		size_t i = 0;
		while (i < len)
		{
			uint32_t run = 1;
			while (i + (size_t)(run + 1) * unit <= len &&
				   memcmp(data + i, data + i + (size_t)run * unit, unit) == 0)
				run++;
			b.put(run);
			b.put(data + i, unit);
			i += (size_t)run * unit;
		}
	}

	void get_runs(Blob& b, uint8_t* data, size_t len, size_t unit)
	{
		uint32_t stored = 0;
		b.get(stored);
		if (b.failed || stored != len)
		{
			b.failed = true;
			return;
		}
		size_t i = 0;
		while (i < len)
		{
			uint32_t run = 0;
			b.get(run);
			if (b.failed || run == 0 || i + (size_t)run * unit > len)
			{
				b.failed = true;
				return;
			}
			b.get(data + i, unit);
			for (uint32_t k = 1; k < run; k++)
				memcpy(data + i + (size_t)k * unit, data + i, unit);
			i += (size_t)run * unit;
		}
	}

	void put_zarray(Blob& b, ZScriptArray& arr)
	{
		uint32_t n = (uint32_t)arr.Size();
		b.put(n);
		for (uint32_t i = 0; i < n; i++)
		{
			int32_t v = arr[i];
			b.put(v);
		}
	}

	void get_zarray(Blob& b, ZScriptArray& arr)
	{
		uint32_t n = 0;
		b.get(n);
		if (b.failed || n > STATE_MAX_ELEMENTS || n > (b.data.size() - b.pos) / sizeof(int32_t))
		{
			b.failed = true;
			return;
		}
		arr.Clear();
		arr.Resize(n);
		for (uint32_t i = 0; i < n; i++)
		{
			int32_t v = 0;
			b.get(v);
			arr[i] = v;
		}
	}

	// ---- Field serialization ----------------------------------------------------------------
	// A snapshot stores state as *values*, not as bytes. Each class describes its own state in
	// visit_state() (see solidobject.h), and the same function writes, skips and reads it, so
	// nothing depends on memory layout, on vtable addresses, or on which object happens to be
	// alive. The visitors themselves live in zc/savestate_serialize.h.
	//
	// Every sprite class is in the registry below. A class that is not has no description, so a
	// snapshot that would contain one is simply not taken - there is no byte-image fallback.

	// A reference to another object is state too, but it is stored as that object's UID: an address
	// means nothing once objects have been rebuilt, and a UID is what the game itself uses to refer
	// to a sprite.
	struct RefWriter
	{
		Blob& b;
		void operator()(sprite* p) { b.put((uint32_t)(p ? p->getUID() : 0)); }
	};

	struct RefReader
	{
		Blob& b;
		// Filled in as the state is read, resolved once every list has an object for every UID.
		std::vector<std::pair<sprite**, int32_t>> pending;

		void operator()(sprite*& p)
		{
			uint32_t uid = 0;
			b.get(uid);
			p = nullptr;
			if (uid)
				pending.emplace_back(&p, (int32_t)uid);
		}
	};

	struct RefSkip
	{
		Blob& b;
		void operator()(sprite*&)
		{
			uint32_t uid = 0;
			b.get(uid);
		}
	};

	// One class a snapshot can serialize. `make` builds an empty instance through the class's own
	// constructor - a real object, with its real vtable and its real layout - which is what lets a
	// restore rebuild a sprite the game has already deleted. It is null for a class whose
	// constructor needs arguments the snapshot cannot invent; those stay on the byte-image path.
	struct SpriteClass
	{
		const char* name;
		const std::type_info* type;
		size_t size;
		void (*write)(StateWriter&, sprite*);
		void (*read)(StateReader&, sprite*);
		void (*skip)(StateSkip&, sprite*);
		void (*write_refs)(RefWriter&, sprite*);
		void (*read_refs)(RefReader&, sprite*);
		void (*skip_refs)(RefSkip&, sprite*);
		sprite* (*make)();
		// Some classes have to be *constructed* with data the snapshot carries - `breakable`'s
		// combo reference, which cannot be re-seated after construction. These three are set for
		// them and null for every other class; the data sits between the UID and the state.
		void (*write_build)(StateWriter&, sprite*);
		void (*skip_build)(StateSkip&);
		sprite* (*rebuild)(Blob&);
		// Debugging only: every field the class's state description covers, hashed one by one.
		void (*dump)(StateFieldHash&, sprite*);
	};

	template <typename T, bool HasRefs> const SpriteClass& sprite_class_entry(const char* name, sprite* (*make)())
	{
		static const SpriteClass entry = [name, make]
		{
			SpriteClass e{};
			e.name = name;
			e.type = &typeid(T);
			e.size = sizeof(T);
			e.write = [](StateWriter& w, sprite* s) { static_cast<T*>(s)->visit_state(w); };
			e.read = [](StateReader& r, sprite* s) { static_cast<T*>(s)->visit_state(r); };
			e.skip = [](StateSkip& k, sprite* s) { static_cast<T*>(s)->visit_state(k); };
			if constexpr (HasRefs)
			{
				e.write_refs = [](RefWriter& w, sprite* s) { static_cast<T*>(s)->visit_refs(w); };
				e.read_refs = [](RefReader& r, sprite* s) { static_cast<T*>(s)->visit_refs(r); };
				e.skip_refs = [](RefSkip& k, sprite* s) { static_cast<T*>(s)->visit_refs(k); };
			}
			e.make = make;
			e.dump = [](StateFieldHash& h, sprite* s) { static_cast<T*>(s)->visit_state(h); };
			return e;
		}();
		return entry;
	}

	// A class whose constructor needs data the snapshot carries (see SpriteClass::rebuild).
	template <typename T> const SpriteClass& sprite_class_entry_with_build(const char* name,
		void (*write_build)(StateWriter&, sprite*),
		void (*skip_build)(StateSkip&),
		sprite* (*rebuild)(Blob&))
	{
		static const SpriteClass entry = [=]
		{
			SpriteClass e{};
			e.name = name;
			e.type = &typeid(T);
			e.size = sizeof(T);
			e.write = [](StateWriter& w, sprite* s) { static_cast<T*>(s)->visit_state(w); };
			e.read = [](StateReader& r, sprite* s) { static_cast<T*>(s)->visit_state(r); };
			e.skip = [](StateSkip& k, sprite* s) { static_cast<T*>(s)->visit_state(k); };
			e.write_build = write_build;
			e.skip_build = skip_build;
			e.rebuild = rebuild;
			e.dump = [](StateFieldHash& h, sprite* s) { static_cast<T*>(s)->visit_state(h); };
			return e;
		}();
		return entry;
	}

		// The class entry for a live sprite (its vtable identifies it).
	const SpriteClass* sprite_class_of(sprite* s);

// breakable: `cmb` is bound to an entry of the combo pool when the object is constructed and a
	// reference cannot be re-seated, so the snapshot carries that entry's index and rebuilds the
	// object bound to the same one.
	void write_breakable_build(StateWriter& w, sprite* s)
	{
		w.b.put(((breakable*)s)->combo_index());
	}

	void skip_breakable_build(StateSkip& k)
	{
		k.skip_bytes(sizeof(int32_t));
	}

	sprite* rebuild_breakable(Blob& b)
	{
		int32_t combo_id = 0;
		b.get(combo_id);
		if (b.failed || combo_id < 0 || combo_id >= (int32_t)combobuf.size())
			return nullptr;
		// The constructor takes its tile/animation from the combo; the captured state is read over
		// the object straight after, so only the binding matters here.
		return new breakable((zfix)0, (zfix)0, (zfix)0, combobuf[combo_id], 0, -1, -1, 0, 0, 0, 0);
	}

	// Every sprite class that is serialized field by field. `X(name, args)`: `args` are what the
	// class's own constructor needs to build an empty instance (all real state is read over it).
	// `breakable` is registered separately, because its constructor needs carried data.
#define ZC_SERIALIZED_SPRITES(X) \
	X(sprite, ()) \
	X(item, ((zfix)0, (zfix)0, (zfix)0, 0, 0, 0)) \
	X(weapon, ((zfix)0, (zfix)0, (zfix)0, 0, 0, 0, 0, -1, -1)) \
	X(movingblock, ()) \
	X(portal, ()) \
	X(decoration, ((zfix)0, (zfix)0, 0, 0)) \
	X(comboSprite, ((zfix)0, (zfix)0, 0, 0)) \
	X(dBushLeaves, ((zfix)0, (zfix)0, 0, 0)) \
	X(dFlowerClippings, ((zfix)0, (zfix)0, 0, 0)) \
	X(dGrassClippings, ((zfix)0, (zfix)0, 0, 0)) \
	X(dHammerSmack, ((zfix)0, (zfix)0, 0, 0)) \
	X(dTallGrass, ((zfix)0, (zfix)0, 0, 0)) \
	X(dRipples, ((zfix)0, (zfix)0, 0, 0)) \
	X(dHover, ((zfix)0, (zfix)0, 0, 0)) \
	X(dDivineProtectionShield, ((zfix)0, (zfix)0, 0, 0)) \
	X(customWalkSprite, ((zfix)0, (zfix)0, 0, 0)) \
	X(enemy, ((zfix)0, (zfix)0, 0, 0)) \
	X(guy, ((zfix)0, (zfix)0, 0, 0, false)) \
	X(eFire, ((zfix)0, (zfix)0, 0, 0)) \
	X(eOther, ((zfix)0, (zfix)0, 0, 0)) \
	X(eScript, ((zfix)0, (zfix)0, 0, 0)) \
	X(eFriendly, ((zfix)0, (zfix)0, 0, 0)) \
	X(eGhini, ((zfix)0, (zfix)0, 0, 0)) \
	X(eTektite, ((zfix)0, (zfix)0, 0, 0)) \
	X(eItemFairy, ((zfix)0, (zfix)0, 0, 0)) \
	X(ePeahat, ((zfix)0, (zfix)0, 0, 0)) \
	X(eLeever, ((zfix)0, (zfix)0, 0, 0)) \
	X(eWallM, ((zfix)0, (zfix)0, 0, 0)) \
	X(eTrap, ((zfix)0, (zfix)0, 0, 0)) \
	X(eTrap2, ((zfix)0, (zfix)0, 0, 0)) \
	X(eRock, ((zfix)0, (zfix)0, 0, 0)) \
	X(eBoulder, ((zfix)0, (zfix)0, 0, 0)) \
	X(eProjectile, ((zfix)0, (zfix)0, 0, 0)) \
	X(eTrigger, ((zfix)0, (zfix)0, 0, 0)) \
	X(eNPC, ((zfix)0, (zfix)0, 0, 0)) \
	X(eSpinTile, ((zfix)0, (zfix)0, 0, 0)) \
	X(eZora, ((zfix)0, (zfix)0, 0, 0)) \
	X(eStalfos, ((zfix)0, (zfix)0, 0, 0)) \
	X(eKeese, ((zfix)0, (zfix)0, 0, 0)) \
	X(eWizzrobe, ((zfix)0, (zfix)0, 0, 0)) \
	X(eDodongo, ((zfix)0, (zfix)0, 0, 0)) \
	X(eDodongo2, ((zfix)0, (zfix)0, 0, 0)) \
	X(eAquamentus, ((zfix)0, (zfix)0, 0, 0)) \
	X(eGohma, ((zfix)0, (zfix)0, 0, 0)) \
	X(eLilDig, ((zfix)0, (zfix)0, 0, 0)) \
	X(eBigDig, ((zfix)0, (zfix)0, 0, 0)) \
	X(eGanon, ((zfix)0, (zfix)0, 0, 0)) \
	X(eMoldorm, ((zfix)0, (zfix)0, 0, 0)) \
	X(esMoldorm, ((zfix)0, (zfix)0, 0, 0)) \
	X(eBaseLanmola, ((zfix)0, (zfix)0, 0, 0)) \
	X(eLanmola, ((zfix)0, (zfix)0, 0, 0)) \
	X(esLanmola, ((zfix)0, (zfix)0, 0, 0)) \
	X(eManhandla, ((zfix)0, (zfix)0, 0, 0)) \
	X(esManhandla, ((zfix)0, (zfix)0, 0, 0)) \
	X(eGleeok, ((zfix)0, (zfix)0, 0, 0)) \
	X(ePatra, ((zfix)0, (zfix)0, 0, 0)) \
	X(ePatraBS, ((zfix)0, (zfix)0, 0, 0))

	// Classes that hold a reference to another object, which is serialized as a UID.
#define ZC_SERIALIZED_SPRITES_WITH_REFS(X) \
	X(esGleeok, ((zfix)0, (zfix)0, 0, 0, nullptr)) \
	X(esPatra, ((zfix)0, (zfix)0, 0, 0, nullptr)) \
	X(esPatraBS, ((zfix)0, (zfix)0, 0, 0, nullptr))

	const std::unordered_map<std::type_index, const SpriteClass*>& sprite_classes()
	{
		static const auto classes = []
		{
			std::unordered_map<std::type_index, const SpriteClass*> m;
#define ZC_SPRITE_CLASS_ENTRY(name, args) \
			{ \
				const SpriteClass& e = sprite_class_entry<name, false>(#name, []() -> sprite* { return new name args; }); \
				m[std::type_index(*e.type)] = &e; \
			}
			ZC_SERIALIZED_SPRITES(ZC_SPRITE_CLASS_ENTRY)
#undef ZC_SPRITE_CLASS_ENTRY
#define ZC_SPRITE_CLASS_ENTRY_REFS(name, args) \
			{ \
				const SpriteClass& e = sprite_class_entry<name, true>(#name, []() -> sprite* { return new name args; }); \
				m[std::type_index(*e.type)] = &e; \
			}
			ZC_SERIALIZED_SPRITES_WITH_REFS(ZC_SPRITE_CLASS_ENTRY_REFS)
#undef ZC_SPRITE_CLASS_ENTRY_REFS

			// Classes whose constructor needs data the snapshot carries (see SpriteClass::rebuild).
			{
				const SpriteClass& e = sprite_class_entry_with_build<breakable>("breakable",
					write_breakable_build, skip_breakable_build, rebuild_breakable);
				m[std::type_index(*e.type)] = &e;
			}

			return m;
		}();
		return classes;
	}

	const SpriteClass* sprite_class_by_name(const std::string& name)
	{
		static const std::unordered_map<std::string, const SpriteClass*> by_name = []
		{
			std::unordered_map<std::string, const SpriteClass*> m;
			for (auto& [type, entry] : sprite_classes())
				m[entry->name] = entry;
			return m;
		}();
		auto it = by_name.find(name);
		return it == by_name.end() ? nullptr : it->second;
	}

	const SpriteClass* sprite_class_of(sprite* s)
	{
		auto it = sprite_classes().find(std::type_index(typeid(*s)));
		return it == sprite_classes().end() ? nullptr : it->second;
	}

	// Write one list. Returns false when a sprite cannot be serialized at all, which makes the
	// whole snapshot unusable - better than a snapshot that silently describes half a world.
	bool put_sprite_list(Blob& b, sprite_list& list)
	{
		int32_t count = list.Count();
		b.put(count);
		for (int32_t i = 0; i < count; i++)
		{
			sprite* s = list.spr(i);
			if (!s)
				return false;
			const SpriteClass* cls = sprite_class_of(s);
			if (!cls)
				return false; // nothing describes this object, so the snapshot is not taken
			b.put_str(cls->name);
			b.put((uint32_t)s->getUID());
			if (cls->write_build)
			{
				// Data the constructor needs, written before the state it is constructed for.
				StateWriter build{b};
				cls->write_build(build, s);
			}
			StateWriter w{b};
			cls->write(w, s);
			if (cls->write_refs)
			{
				RefWriter rw{b};
				cls->write_refs(rw, s);
			}
		}
		return true;
	}

	// Sprites a restore removed from the lists: the live sprites the snapshot does not contain,
	// and the hero's weapon from before the restore. Nothing owns them once the restored state is
	// in place - every reference the snapshot carries is by UID (scripts, the hero's held weapon,
	// the sprites' own cross-references), and those are re-seated to the objects the restore puts
	// back - so they are released when the restore finishes, on every path out of it (see
	// DetachedSprites). Keeping them alive instead leaked a sprite per rewind, which is what made
	// a session that rewinds a lot eventually run out of memory.
	std::vector<sprite*> detached_sprites;

	// Releases whatever a restore detached, however that restore ends.
	struct DetachedSprites
	{
		~DetachedSprites()
		{
			for (sprite* s : detached_sprites)
				delete s;
			detached_sprites.clear();
		}
	};

	// The UID is the identity a snapshot uses for an object: it is what the game hands out and what
	// scripts see, and unlike an address it still means something after objects are rebuilt.
	std::unordered_map<int32_t, sprite*> live_sprites_by_uid()
	{
		std::unordered_map<int32_t, sprite*> by_uid;
		for (sprite_list* list : {&guys, &items, &Ewpns, &Lwpns, &chainlinks, &decorations, &portals})
			for (int32_t i = 0; i < list->Count(); i++)
				if (sprite* s = list->spr(i))
					by_uid[s->getUID()] = s;
		return by_uid;
	}

	// What one list looked like when it was captured, and what a restore made of it.
	struct SpriteEntry
	{
		// The class the object is, the UID that identifies it, and where in the blob its state
		// starts (the validating pass records that, the applying pass seeks back to it).
		const SpriteClass* cls = nullptr;
		int32_t uid = 0;
		size_t state_offset = 0;
		// What the restore puts in the list, and whether that is the captured object itself.
		sprite* object = nullptr;
		bool reused = false;
	};

	struct SpriteListImage
	{
		int32_t count = 0;
		std::vector<SpriteEntry> entries;
	};

	// First pass over a captured list: work out which object every entry is (the live sprite with
	// that UID, or a new one of the same class built for it) and walk past its state block. Nothing
	// in the world is changed here, so a snapshot that turns out to be unusable leaves the game
	// exactly as it was.
	bool read_sprite_list(Blob& b, SpriteListImage& image, const std::unordered_map<int32_t, sprite*>& live_by_uid)
	{
		int32_t count = 0;
		b.get(count);
		if (b.failed || count < 0 || count > SLMAX)
			return false;

		image.count = count;
		image.entries.assign(count, {});
		for (int32_t i = 0; i < count; i++)
		{
			SpriteEntry& e = image.entries[i];
			{
				std::string name;
				b.get_str(name);
				uint32_t uid = 0;
				b.get(uid);
				if (b.failed)
					return false;
				e.uid = (int32_t)uid;
				e.cls = sprite_class_by_name(name);
				if (!e.cls)
					return false;

				auto live = live_by_uid.find(e.uid);
				bool is_live = live != live_by_uid.end() && sprite_class_of(live->second) == e.cls;
				if (e.cls->rebuild)
				{
					// The data its constructor needs sits here: step over it for an object that is
					// still alive, read it to build one that is not.
					if (is_live)
					{
						StateSkip build{b};
						e.cls->skip_build(build);
					}
					else
					{
						e.object = e.cls->rebuild(b);
						if (!e.object)
							return false;
					}
				}
				else if (is_live)
				{
					e.object = live->second;
					e.reused = true;
				}
				else if (e.cls->make)
				{
					// The game has deleted it: build an object of the same class for the state to
					// be read into. This is the case a byte image cannot handle.
					e.object = e.cls->make();
				}
				else
				{
					return false;
				}
				if (is_live && e.cls->rebuild)
				{
					e.object = live->second;
					e.reused = true;
				}
				// Identity is state: a rebuilt object was handed a fresh UID by its constructor,
				// and everything that refers to this sprite (a script's engine slot, another
				// sprite's parent) refers to the captured one.
				e.object->setUID(e.uid);

				e.state_offset = b.pos;
				StateSkip k{b};
				e.cls->skip(k, e.object);
				if (e.cls->skip_refs)
				{
					RefSkip rk{b};
					e.cls->skip_refs(rk, e.object);
				}
				if (b.failed)
					return false;
				continue;
			}

		}
		return true;
	}

	// State that is not visited because it is a pointer into quest data rather than a value: it is
	// re-derived from the fields that are, exactly as the constructors derive it. `enemy::d` is
	// `guysbuf + (id & 0xFFF)` (guys.cpp:350).
	void fixup_sprite_state(sprite* s)
	{
		if (auto* e = dynamic_cast<enemy*>(s))
			e->d = guysbuf + (e->id & 0xFFF);
	}

	// Second pass: put the list back and write each entry's state into its object. Anything the list
	// holds that this snapshot does not is dropped from the world but kept alive - something
	// outside the lists (a script's engine data, the hero) may still point at it, and a
	// re-simulation recreates whatever the trajectory needs.
	bool restore_sprite_list(Blob& b, sprite_list& list, SpriteListImage& image, RefReader& refs)
	{
		std::vector<sprite*> objects;
		objects.reserve(image.count);
		for (auto& e : image.entries)
			objects.push_back(e.object);

		int32_t live = list.Count();
		for (int32_t i = 0; i < live; i++)
		{
			sprite* s = list.spr(i);
			if (!s)
				continue;
			if (std::find(objects.begin(), objects.end(), s) == objects.end())
				detached_sprites.push_back(s);
		}
		list.snapshot_replace(objects.data(), (int32_t)objects.size());

		for (auto& e : image.entries)
		{
			b.pos = e.state_offset;
			b.failed = false;
			StateReader r{b};
			e.cls->read(r, e.object);
			if (e.cls->read_refs)
				e.cls->read_refs(refs, e.object);
			if (b.failed)
				return false;
			fixup_sprite_state(e.object);
		}
		return true;
	}

	// ---- The hero --------------------------------------------------------------------------
	// HeroClass is serialized the way a sprite is (Hero.visit_state), plus the three things a
	// visitor cannot carry: the item use-counts (a std::map), which FFC platform the hero stands
	// on (an index into the live screen, not an address) and the weapon it is holding - while held
	// that weapon is usually in no list at all, so it travels with the hero instead of through the
	// sprite lists.

	bool put_held_weapon(Blob& b)
	{
		weapon* w = Hero.lift_wpn;
		if (!w)
		{
			b.put((uint8_t)0);
			return true;
		}
		const SpriteClass* cls = sprite_class_of(w);
		if (!cls)
			return false; // nothing describes it, so the snapshot would be incomplete
		b.put((uint8_t)1);
		b.put_str(cls->name);
		b.put((uint32_t)w->getUID());
		StateWriter sw{b};
		cls->write(sw, w);
		if (cls->write_refs)
		{
			RefWriter rw{b};
			cls->write_refs(rw, w);
		}
		return true;
	}

	bool put_hero(Blob& b)
	{
		b.put(Hero.getUID());
		{
			StateWriter w{b};
			Hero.visit_state(w);
		}
		b.put((uint32_t)Hero.usecounts.size());
		for (auto& [id, count] : Hero.usecounts)
		{
			b.put(id);
			b.put(count);
		}
		b.put(Hero.platform_ffc_index());
		b.put((uint32_t)(Hero.lift_wpn ? Hero.lift_wpn->getUID() : 0));
		b.put((uint32_t)(Hero.active_wind ? Hero.active_wind->getUID() : 0));
		return put_held_weapon(b);
	}

	// Walk the hero's section without changing anything, for the validating pass.
	bool skip_hero(Blob& b, int32_t* hero_id = nullptr)
	{
		int32_t hero_uid=0; b.get(hero_uid);
		if (b.failed || hero_uid < 0 || hero_uid >= INT32_MAX) return false;
		if (hero_id) *hero_id=hero_uid;
		{
			StateSkip k{b};
			Hero.visit_state(k);
		}
		uint32_t counts = 0;
		b.get(counts);
		if (b.failed || counts > 4096)
			return false;
		b.pos += (size_t)counts * (sizeof(int16_t) + sizeof(int32_t));
		if (b.pos > b.data.size())
			return false;

		int32_t platform = 0;
		uint32_t lift_uid = 0, wind_uid = 0;
		b.get(platform);
		b.get(lift_uid);
		b.get(wind_uid);
		uint8_t has = 0;
		b.get(has);
		if (b.failed || !has)
			return !b.failed;

		std::string name;
		b.get_str(name);
		uint32_t uid = 0;
		b.get(uid);
		const SpriteClass* cls = sprite_class_by_name(name);
		if (b.failed || !cls)
			return false;

		// Walking the weapon's state needs an object of its class: the hero's current one is
		// normally it, and otherwise a throwaway instance serves (skip writes nothing).
		sprite* scratch = (Hero.lift_wpn && sprite_class_of(Hero.lift_wpn) == cls) ? (sprite*)Hero.lift_wpn : nullptr;
		bool temporary = false;
		if (!scratch && cls->make)
		{
			scratch = cls->make();
			temporary = true;
		}
		if (!scratch)
			return false;
		StateSkip k{b};
		cls->skip(k, scratch);
		if (cls->skip_refs)
		{
			RefSkip rk{b};
			cls->skip_refs(rk, scratch);
		}
		if (temporary)
			delete scratch;
		return !b.failed;
	}

	// Read the hero back into the live object; `by_uid` resolves the references it holds.
	bool get_hero(Blob& b, std::unordered_map<int32_t, sprite*>& by_uid)
	{
		int32_t hero_uid=0; b.get(hero_uid);
		if (b.failed || hero_uid < 0 || hero_uid >= INT32_MAX) return false;
		Hero.setUID(hero_uid);
		{
			StateReader r{b};
			Hero.visit_state(r);
		}
		uint32_t counts = 0;
		b.get(counts);
		if (b.failed || counts > 4096)
			return false;
		Hero.usecounts.clear();
		for (uint32_t i = 0; i < counts; i++)
		{
			int16_t id = 0;
			int32_t count = 0;
			b.get(id);
			b.get(count);
			Hero.usecounts[id] = count;
		}

		int32_t platform = 0;
		uint32_t lift_uid = 0, wind_uid = 0;
		b.get(platform);
		b.get(lift_uid);
		b.get(wind_uid);
		if (b.failed)
			return false;
		Hero.set_platform_ffc_index(platform);

		uint8_t has = 0;
		b.get(has);
		if (b.failed)
			return false;

		weapon* held = nullptr;
		if (has)
		{
			std::string name;
			b.get_str(name);
			uint32_t uid = 0;
			b.get(uid);
			const SpriteClass* cls = sprite_class_by_name(name);
			if (b.failed || !cls)
				return false;

			// The object the hero was holding is normally still there - and, being held, it is in
			// no list, so it is not in `by_uid`: identity is checked against the hero's own
			// pointer as well.
			weapon* live = Hero.lift_wpn;
			if (live && sprite_class_of(live) == cls && (uint32_t)live->getUID() == uid)
			{
				held = live;
			}
			else
			{
				if (!cls->make)
					return false;
				held = (weapon*)cls->make();
				held->setUID((int32_t)uid);
			}

			StateReader r{b};
			cls->read(r, held);
			if (cls->read_refs)
			{
				RefReader rr{b};
				cls->read_refs(rr, held);
				for (auto& [field, ref_uid] : rr.pending)
				{
					auto it = by_uid.find(ref_uid);
					*field = it == by_uid.end() ? nullptr : it->second;
				}
			}
			if (b.failed)
				return false;
			fixup_sprite_state(held);
			by_uid[(int32_t)uid] = held;
		}

		// What the hero was holding before this restore is dropped from the world but kept alive,
		// like any other sprite the restore does not describe.
		if (Hero.lift_wpn && Hero.lift_wpn != held)
			detached_sprites.push_back(Hero.lift_wpn);

		auto resolve = [&](uint32_t uid) -> sprite*
		{
			if (!uid)
				return nullptr;
			auto it = by_uid.find((int32_t)uid);
			return it == by_uid.end() ? nullptr : it->second;
		};
		Hero.active_wind = (weapon*)resolve(wind_uid);
		Hero.lift_wpn = has ? held : nullptr;
		return !b.failed;
	}

	// ---- The live screen buffers -----------------------------------------------------------
	// A screen is copied out of the quest and then mutated as it is played: secrets triggered, pots
	// lifted, doors opened, bombs blown, FFCs run. Those edits live in `tmpscr` (the current screen
	// and the one it was entered from) and in the layer/scrolling buffers, so a restore has to put
	// them back - without them a hero who lifted a pot after the snapshot point finds nothing to
	// lift. Each buffer travels field by field, like everything else.

	// Preserve every live screen buffer and every FFC slot, including dormant
	// script-writable slots. Current validity is not proof that its data is disposable.
	const int SCREEN_BUFFER_COUNT = 2 + 6 + 6;

	mapscr& screen_buffer(int index)
	{
		if (index < 2)
			return tmpscr[index];
		if (index < 2 + 6)
			return tmpscr2[index - 2];
		return tmpscr3[index - 8];
	}

	bool screen_buffer_used(int index, mapscr& scr)
	{
		return true;
	}

	bool ffc_used(ffcdata& ffc)
	{
		return true;
	}
	struct LiveScreenCache
	{
		std::vector<uint8_t> stamp, encoded;
		std::string notes;
	};
	LiveScreenCache live_screen_caches[SCREEN_BUFFER_COUNT];
	uint64_t live_screen_cache_hits=0, live_screen_cache_rebuilds=0, live_screen_cache_skips=0;
	bool live_screen_matches(const LiveScreenCache& cache, const mapscr& scr)
	{
		return cache.stamp.size()==sizeof(scr) && !cache.encoded.empty()
			&& memcmp(cache.stamp.data(),&scr,sizeof(scr))==0 && cache.notes==scr.usr_notes;
	}
	void cache_live_screen(LiveScreenCache& cache, mapscr& scr, std::vector<uint8_t> encoded)
	{
		// Stage every allocating operation before publishing a consistent cache.
		LiveScreenCache next;
		next.stamp.resize(sizeof(scr)); memcpy(next.stamp.data(),&scr,sizeof(scr));
		next.notes=scr.usr_notes; next.encoded=std::move(encoded);
		cache=std::move(next);
	}

	void put_screen_buffers(StateWriter& w)
	{
		for (int i = 0; i < SCREEN_BUFFER_COUNT; i++)
		{
			mapscr& scr = screen_buffer(i);
			auto& cache=live_screen_caches[i];
			if(live_screen_matches(cache,scr)) {
				w.b.put(cache.encoded.data(),cache.encoded.size()); ++live_screen_cache_hits; continue;
			}
			SaveStateBlob encoded; StateWriter writer{encoded};
			encoded.put(uint8_t(1));
			scr.visit_state(writer);
			for (int32_t q = 0; q < MAXFFCS; q++)
				encoded.put(uint8_t(1));
			for (int32_t q = 0; q < MAXFFCS; q++)
				scr.ffcs[q].visit_state(writer);
			if(encoded.failed) { w.b.failed=true; return; }
			cache_live_screen(cache,scr,std::move(encoded.data)); ++live_screen_cache_rebuilds;
			w.b.put(cache.encoded.data(),cache.encoded.size());
		}
	}

	void skip_screen_buffers(StateSkip& k)
	{
		for (int i = 0; i < SCREEN_BUFFER_COUNT; i++)
		{
			uint8_t used = 0;
			k.b.get(used);
			if (used>1) { k.b.failed=true; return; }
			if (k.b.failed || !used)
				continue;
			mapscr& scr = screen_buffer(i);
			scr.visit_state(k);
			uint8_t present[MAXFFCS];
			k.b.get(present, MAXFFCS);
			for (uint8_t flag:present) if(flag>1) { k.b.failed=true; return; }
			for (int32_t q = 0; q < MAXFFCS; q++)
				if (present[q])
					scr.ffcs[q].visit_state(k);
		}
	}

	void get_screen_buffers(StateReader& r)
	{
		for (int i = 0; i < SCREEN_BUFFER_COUNT; i++)
		{
			mapscr& scr = screen_buffer(i);
			auto& cache=live_screen_caches[i];
			const size_t start=r.b.pos;
			if(!r.b.failed && r.b.pos<=r.b.data.size() && live_screen_matches(cache,scr)
				&& cache.encoded.size()<=r.b.data.size()-r.b.pos
				&& memcmp(r.b.data.data()+r.b.pos,cache.encoded.data(),cache.encoded.size())==0) {
				r.b.pos+=cache.encoded.size(); ++live_screen_cache_skips; continue;
			}
			uint8_t used = 0;
			r.b.get(used);
			if (used>1) { r.b.failed=true; return; }
			if (r.b.failed)
				return;
			if (!used)
			{
				// A buffer the snapshot does not carry is not in use: reset it, rather than leaving
				// whatever the frames after the snapshot put there.
				scr.zero_memory();
				cache=LiveScreenCache{};
				continue;
			}
			scr.visit_state(r);
			uint8_t present[MAXFFCS];
			r.b.get(present, MAXFFCS);
			for (uint8_t flag:present) if(flag>1) { r.b.failed=true; return; }
			if (r.b.failed)
				return;
			for (int32_t q = 0; q < MAXFFCS; q++)
			{
				if (present[q])
					scr.ffcs[q].visit_state(r);
				else
					scr.ffcs[q] = ffcdata();
			}
			if(r.b.failed) { cache=LiveScreenCache{}; return; }
			// Record only a completely decoded section and its resulting live stamp.
			// Noncanonical absent FFC flags must never become a future capture
			// cache: new captures always preserve all 128 slots.
			bool complete=true; for(uint8_t flag:present) if(flag!=1) complete=false;
			if(!complete) { cache=LiveScreenCache{}; continue; }
			std::vector<uint8_t> encoded(r.b.data.begin()+start,r.b.data.begin()+r.b.pos);
			cache_live_screen(cache,scr,std::move(encoded));
		}
	}

	// ---- The engine's own globals ----------------------------------------------------------
	// A snapshot is the objects it walks (the sprites, the hero, the screen buffers, the script VM)
	// plus the engine's file-scope variables. This table is the second half of that: the per-frame
	// globals that are not part of any object, each named here one at a time.
	//
	// The rule is simple and it is the reason this table exists rather than a shorter one: if a later
	// frame can read it, the snapshot carries it. A global the restore leaves at whatever the frames
	// the rewind skipped put there is a global the resumed run does not share with the recorded run,
	// and that is what "the restored state does not match the frame it came from" is made of - even
	// when the difference is a counter as dull as the clock that makes a bomb flash. It is also the
	// class of bug that shows up frames later, somewhere else, as a monster that is missing from a
	// room or a hero who walks one step out of phase.
	//
	// This table supplements the explicit object, UI, audio and definition sections.
	// Do not omit gameplay state based on a claim that it is a cache: verify that
	// it is rebuilt before any reader, otherwise preserve it (as with blockpath).
	//
	// The values travel as raw bytes through one table, so capture and restore walk the same list in
	// the same order and cannot drift apart; `restore` also puts every one of them back, which is
	// what `ZC_SNAPSHOT_ROUNDTRIP` re-captures to prove.
	struct RawGlobal
	{
		void* addr;
		size_t size;
	};

	std::vector<RawGlobal> engine_global_table()
	{
		std::vector<RawGlobal> t;
		auto add = [&t](void* addr, size_t size) { t.push_back(RawGlobal{addr, size}); };
		// A game-over menu can be restored into gameplay and vice versa.
		add(&Playing, sizeof(Playing));
		add(&Paused, sizeof(Paused));
		add(&Quit, sizeof(Quit));
		add(&GameFlags, sizeof(GameFlags));
		add(SaveScreenSettings, sizeof(SaveScreenSettings));
		add(SaveScreenText, sizeof(SaveScreenText));
		add(&show_subscreen_dmap_dots, sizeof(show_subscreen_dmap_dots));
		add(&show_subscreen_numbers, sizeof(show_subscreen_numbers));
		add(&show_subscreen_items, sizeof(show_subscreen_items));
		add(&show_subscreen_life, sizeof(show_subscreen_life));
		add(&show_ff_scripts, sizeof(show_ff_scripts));

		// Which item each button is bound to right now. These mirror gamedata's awpn..ywpn, but not
		// exactly: a script that rebinds a button sets gamedata's copy to 255 ("don't refresh") and
		// keeps its own value in the global, and the hero's item code reads the global.
		add(&Awpn, sizeof(Awpn));
		add(&Bwpn, sizeof(Bwpn));
		add(&Xwpn, sizeof(Xwpn));
		add(&Ywpn, sizeof(Ywpn));
		// The button item this frame's press is acting on, and the item a script is forcing the hero
		// to use (directWpn is whichever of them names the weapon in hand).
		add(&dowpn, sizeof(dowpn));
		add(&directItemA, sizeof(directItemA));
		add(&directItemB, sizeof(directItemB));
		add(&directItemX, sizeof(directItemX));
		add(&directItemY, sizeof(directItemY));
		add(&directWpn, sizeof(directWpn));
		// "The item script for this button has already been launched for this press": set when it is
		// launched, cleared when the button comes up. A restore that drops it relaunches the script.
		add(&did_scripta, sizeof(did_scripta));
		add(&did_scriptb, sizeof(did_scriptb));
		add(&did_scriptl, sizeof(did_scriptl));
		// The item a whirlwind came from: the wind reads it frames later, when it lands.
		add(&whistleitem, sizeof(whistleitem));

		// The subscreen's button-item animation: the ids it last saw and how long each has been on
		// its button (one frame of lag by design - that lag is how a change is noticed), the page
		// animation in flight, and the per-item clock offsets a script asked for.
		add(btnitem_ids, sizeof(btnitem_ids));
		add(btnitem_clks, sizeof(btnitem_clks));
		add(&subscr_pg_from, sizeof(subscr_pg_from));
		add(&subscr_pg_to, sizeof(subscr_pg_to));
		add(&subscr_itemless, sizeof(subscr_itemless));
		add(&subscr_pg_animating, sizeof(subscr_pg_animating));
		add(subscr_override_clkoffsets, sizeof(subscr_override_clkoffsets));

		// Entrances and shops: where the last entrance was (scripts ask), the prices on the shop
		// screen being stood on, whether something was just bought, and which side a scrolling
		// transition is entering from - the enemy loader reads that last one when it places enemies.
		add(&lastentrance, sizeof(lastentrance));
		add(&lastentrance_dmap, sizeof(lastentrance_dmap));
		add(prices, sizeof(prices));
		add(&boughtsomething, sizeof(boughtsomething));
		add(&loadside, sizeof(loadside));

		// The item guy's state on this screen: a bitfield that spans frames (an item is lying there,
		// being carried, or was taken). Set and cleared by the hero, the item and the enemy code.
		add(&hasitem, sizeof(hasitem));

		// Clocks and positions one frame leaves for a later one: the whistle's countdown, the clock
		// item's tick and its per-enemy counters, the magic drain, the conveyor animation phase, the
		// position a spell was cast from, and where the Divine Shield and net sparkles are drawn.
		add(&whistleclk, sizeof(whistleclk));
		add(&clockclk, sizeof(clockclk));
		add(clock_zoras, sizeof(clock_zoras));
		add(&magicdrainclk, sizeof(magicdrainclk));
		add(&newconveyorclk, sizeof(newconveyorclk));
		add(&castx, sizeof(castx));
		add(&casty, sizeof(casty));
		add(&df_x, sizeof(df_x));
		add(&df_y, sizeof(df_y));
		add(&nl1_x, sizeof(nl1_x));
		add(&nl1_y, sizeof(nl1_y));
		add(&nl2_x, sizeof(nl2_x));
		add(&nl2_y, sizeof(nl2_y));

		// A pit drop remembers where it started and puts the hero back there when it ends; the fairy
		// refill that is running; and the "skip the continue screen" flag a script or a death sets,
		// which the game-over path reads.
		add(&pitx, sizeof(pitx));
		add(&pity, sizeof(pity));
		add(&refill_what, sizeof(refill_what));
		add(&refill_why, sizeof(refill_why));
		add(&skipcont, sizeof(skipcont));

		// The hookshot's state across the frames of a pull: where it started and how far it has
		// travelled, the combo or object it hooked (the object itself is carried as an identity
		// below), the layers and combos it recorded, the item that pays for the switchhook, and the
		// flags that tell it to snap the hero, restart the wind, or keep the hero stunned.
		add(&hs_startx, sizeof(hs_startx));
		add(&hs_starty, sizeof(hs_starty));
		add(&hs_xdist, sizeof(hs_xdist));
		add(&hs_ydist, sizeof(hs_ydist));
		add(&hooked_combopos, sizeof(hooked_combopos));
		add(&hooked_layerbits, sizeof(hooked_layerbits));
		add(hooked_undercombos, sizeof(hooked_undercombos));
		add(&switchhook_cost_item, sizeof(switchhook_cost_item));
		add(&hookshot_used, sizeof(hookshot_used));
		add(&hookshot_frozen, sizeof(hookshot_frozen));
		add(&hs_fix, sizeof(hs_fix));
		add(&hs_switcher, sizeof(hs_switcher));
		add(&pull_hero, sizeof(pull_hero));
		add(&ewind_restart, sizeof(ewind_restart));
		add(&is_conveyor_stunned, sizeof(is_conveyor_stunned));
		add(&is_on_conveyor, sizeof(is_on_conveyor));

		// One-frame flags that the frame after the snapshot reads: a pit was entered, the low-health
		// beep is sounding, a magic cast is pending, the Divine Shield and net sparkles are owed, a
		// timed warp already fired, and the superspeed and superman cheats.
		add(&didpit, sizeof(didpit));
		add(&heart_beep, sizeof(heart_beep));
		add(&castnext, sizeof(castnext));
		add(&add_df1asparkle, sizeof(add_df1asparkle));
		add(&add_df1bsparkle, sizeof(add_df1bsparkle));
		add(&add_nl1asparkle, sizeof(add_nl1asparkle));
		add(&add_nl1bsparkle, sizeof(add_nl1bsparkle));
		add(&add_nl2asparkle, sizeof(add_nl2asparkle));
		add(&add_nl2bsparkle, sizeof(add_nl2bsparkle));
		add(&activated_timed_warp, sizeof(activated_timed_warp));
		add(&gofast, sizeof(gofast));
		add(&cheat_superman, sizeof(cheat_superman));
		add(&checkhero, sizeof(checkhero));
		add(&pausenow, sizeof(pausenow));

		// The message system. Showing a message is a multi-frame animation and these are the
		// counters it types with: which string, how far into it, and where the next character goes.
		// Scripts can read the id of the message that is on screen (Game->Message), and the reveal
		// loop reads the rest every frame, so a restore during a message has to resume exactly where
		// it was instead of letting the frames the rewind skipped finish the typing.
		add(&msgclk, sizeof(msgclk));
		add(&msgstr, sizeof(msgstr));
		add(&msgorig, sizeof(msgorig));
		add(&enqueued_str, sizeof(enqueued_str));
		add(&msgpos, sizeof(msgpos));
		add(&msgptr, sizeof(msgptr));
		add(&msgcolour, sizeof(msgcolour));
		add(&msgspeed, sizeof(msgspeed));
		add(&msg_w, sizeof(msg_w));
		add(&msg_h, sizeof(msg_h));
		add(&msg_count, sizeof(msg_count));
		add(&cursor_x, sizeof(cursor_x));
		add(&cursor_y, sizeof(cursor_y));
		add(&msg_xpos, sizeof(msg_xpos));
		add(&msg_ypos, sizeof(msg_ypos));
		add(msg_margins, sizeof(msg_margins));
		add(&msgstr_layer, sizeof(msgstr_layer));
		add(&msg_onscreen, sizeof(msg_onscreen));
		add(&msg_active, sizeof(msg_active));
		add(&msgspace, sizeof(msgspace));
		// The portrait the message draws beside it, and the box's shadow style.
		add(&prt_tile, sizeof(prt_tile));
		add(&prt_cset, sizeof(prt_cset));
		add(&prt_x, sizeof(prt_x));
		add(&prt_y, sizeof(prt_y));
		add(&prt_tw, sizeof(prt_tw));
		add(&prt_th, sizeof(prt_th));
		add(&msg_shdtype, sizeof(msg_shdtype));
		add(&msg_shdcol, sizeof(msg_shdcol));
		// The intro and dmap message clocks, the lens of truth's timer and item, the two grids
		// scripts read and write (`Game->ScreenGrid`, `Game->FFCGrid`), and the door combo set
		// counter a script can advance.
		add(&introclk, sizeof(introclk));
		add(&intropos, sizeof(intropos));
		add(&dmapmsgclk, sizeof(dmapmsgclk));
		add(&linkedmsgclk, sizeof(linkedmsgclk));
		add(&lensclk, sizeof(lensclk));
		add(&lensid, sizeof(lensid));
		add(screengrid, sizeof(screengrid));
		add(screengrid_layer, sizeof(screengrid_layer));
		add(ffcgrid, sizeof(ffcgrid));
		add(&door_combo_set_count, sizeof(door_combo_set_count));
		// Whether the game is halted, and the bomb explosion's palette: the flag and the palette it
		// names are read together, so carrying one without the other would restore half a palette.
		add(&halt, sizeof(halt));
		add(&usebombpal, sizeof(usebombpal));
		add(&tempbombpal, sizeof(tempbombpal));

		// Whether the screens' enemies and their animation are in play. `loaded_guys` and
		// `loaded_enemies` are the load bookkeeping the frame after a screen change reads: the hero's
		// code loads the screen's enemies when `loaded_guys` is false, so a restore that leaves it
		// true (or false) where the recorded run had the other value makes the re-cover load a screen
		// the recorded run did not - which spawns its monsters on top of the ones already there.
		// `freeze_guys` decides whether the enemy list is ticked at all, and the freeze flags gate
		// whole subsystems (a script can set them), so they belong to the state too.
		add(&loaded_guys, sizeof(loaded_guys));
		add(&loaded_enemies, sizeof(loaded_enemies));
		// Cave NPCs set this barrier at screen load; it is not recomputed each
		// frame. Rewinding outdoors must not retain the cave's upper-half wall.
		add(&blockpath, sizeof(blockpath));
		add(&freeze_guys, sizeof(freeze_guys));
		add(&freeze_general, sizeof(freeze_general));
		add(&freeze_message, sizeof(freeze_message));
		add(&freeze_ffc, sizeof(freeze_ffc));
		// Whether the sprites are being drawn, and whether the room is dark: both are decided when a
		// screen loads and read by later frames (scripts included), so a restore has to carry them.
		add(&drawguys, sizeof(drawguys));
		add(&darkroom, sizeof(darkroom));
		add(&naturaldark, sizeof(naturaldark));

		return t;
	}

	// `switching_object` is the one pointer among those globals: it is the object a switchhook pull
	// is acting on, read frames after the hook landed. Its address is not stable - a restore rebuilds
	// the sprite lists - so the snapshot stores *which* object it is (which list, which index, or
	// which screen buffer's FFC) and a restore looks it up again. The object's own fields, including
	// the `switch_hooked` flag that put it there, come back with the list it lives in.
	enum SwitchObjectKind
	{
		SWITCH_OBJECT_NONE = 0,
		SWITCH_OBJECT_FFC = 1,
		SWITCH_OBJECT_GUY = 2,
		SWITCH_OBJECT_ITEM = 3,
		SWITCH_OBJECT_LWEAPON = 4,
		SWITCH_OBJECT_EWEAPON = 5,
		SWITCH_OBJECT_MOVINGBLOCK = 6,
	};

	bool encode_switching_object(int32_t& kind, int32_t& index)
	{
		kind = SWITCH_OBJECT_NONE;
		index = 0;
		if (!switching_object)
			return true;
		if (switching_object == (solid_object*)&mblock2)
		{
			kind = SWITCH_OBJECT_MOVINGBLOCK;
			return true;
		}

		// The FFC case: the address of one FFC inside one of the live screen buffers.
		for (int i = 0; i < SCREEN_BUFFER_COUNT; i++)
		{
			mapscr& scr = screen_buffer(i);
			for (int32_t q = 0; q < MAXFFCS; q++)
				if ((solid_object*)&scr.ffcs[q] == switching_object)
				{
					kind = SWITCH_OBJECT_FFC;
					index = i * MAXFFCS + q;
					return true;
				}
		}

		// The sprite-list cases. A target outside these lists, the live FFCs and
		// the global moving block cannot be reproduced, so reject its capture.
		const struct { int32_t kind; sprite_list* list; } lists[] = {
			{SWITCH_OBJECT_GUY, &guys}, {SWITCH_OBJECT_ITEM, &items},
			{SWITCH_OBJECT_LWEAPON, &Lwpns}, {SWITCH_OBJECT_EWEAPON, &Ewpns},
		};
		for (const auto& l : lists)
			for (int32_t q = 0; q < l.list->Count(); q++)
				if ((solid_object*)l.list->spr(q) == switching_object)
				{
					kind = l.kind;
					index = q;
					return true;
				}
		return false;
	}

	solid_object* decode_switching_object(int32_t kind, int32_t index)
	{
		switch (kind)
		{
			case SWITCH_OBJECT_NONE:
				return nullptr;
			case SWITCH_OBJECT_MOVINGBLOCK:
				return index == 0 ? (solid_object*)&mblock2 : nullptr;
			case SWITCH_OBJECT_FFC:
			{
				int buffer = index / MAXFFCS;
				int32_t q = index % MAXFFCS;
				if (index < 0 || buffer >= SCREEN_BUFFER_COUNT || q >= MAXFFCS)
					return nullptr;
				return (solid_object*)&screen_buffer(buffer).ffcs[q];
			}
			case SWITCH_OBJECT_GUY:
				return index >= 0 && index < guys.Count() ? (solid_object*)guys.spr(index) : nullptr;
			case SWITCH_OBJECT_ITEM:
				return index >= 0 && index < items.Count() ? (solid_object*)items.spr(index) : nullptr;
			case SWITCH_OBJECT_LWEAPON:
				return index >= 0 && index < Lwpns.Count() ? (solid_object*)Lwpns.spr(index) : nullptr;
			case SWITCH_OBJECT_EWEAPON:
				return index >= 0 && index < Ewpns.Count() ? (solid_object*)Ewpns.spr(index) : nullptr;
			default:
				return nullptr;
		}
	}

	// Returns false when a snapshot cannot describe the live switchhook object, which makes the
	// capture fail rather than write a snapshot that would restore to the wrong one.
	bool put_engine_globals(Blob& b)
	{
		int32_t kind = 0, index = 0;
		if (!encode_switching_object(kind, index))
			return false;
		for (const RawGlobal& g : engine_global_table())
			b.put(g.addr, g.size);
		int32_t identity[2] = {kind, index};
		b.put(identity, sizeof(identity));
		return true;
	}

	// Returns false when the snapshot's identity does not name a live object, i.e. when the blob is
	// not the state it claims to be.
	bool get_engine_globals(Blob& b)
	{
		for (const RawGlobal& g : engine_global_table())
			b.get(g.addr, g.size);
		int32_t identity[2] = {};
		b.get(identity, sizeof(identity));
		if (b.failed)
			return false;
		if (identity[0] != SWITCH_OBJECT_NONE && !decode_switching_object(identity[0], identity[1]))
			return false;
		switching_object = decode_switching_object(identity[0], identity[1]);
		return true;
	}

	// ---- Quest progress (`gamedata`) -------------------------------------------------------
	// Everything the session has *earned or lost*: hearts, deaths, counters, the inventory and what
	// is equipped, keys and level items, the dungeon maps, which screens have been visited and what
	// was taken from them, switch states and timers, the mirror's saved portal, script-created
	// objects and portals, and the per-screen and generic script data. None of it is derivable from
	// the world state, and all of it outlives a frame - and before this section a rewound run kept
	// whatever the *future* had given it, so it could swing a sword it had not picked up yet or
	// resume with the hearts it had already lost, which is exactly what a player rewinds to undo.
	// The `game` region hash is taken over these same bytes (see snapshot_gamedata_hash), so a
	// snapshot that cannot reproduce them fails its check instead of quietly playing on.
	//
	// The container helpers below cover the four shapes gamedata uses. A container travels as the
	// size it believes it has, its set elements, and then those elements; the default value is a
	// property of the type (whatever the game constructs it with), so it does not travel, and the
	// reader's assignment normalises exactly as the game's own would.

	// What a container's own normalize() would keep is what travels, because that is what the game
	// can actually see: bounded_vec's at() hands back the default for anything at or past the size
	// it believes it has, and bounded_map drops entries equal to the default. Writing the same
	// subset also makes capture idempotent - a restore re-serialises to the same bytes, which is
	// what the region hash checks.
	template <typename Sz, typename T> void put_bvec(Blob& b, const bounded_vec<Sz, T>& v)
	{
		uint32_t size = (uint32_t)v.size();
		uint32_t count = (uint32_t)std::min<size_t>(v.capacity(), (size_t)size);
		while (count && v.data()[count - 1] == v.defval())
			count--;
		b.put(size);
		b.put(count);
		if (count)
			b.put(v.data(), (size_t)count * sizeof(T));
	}
	template <typename Sz, typename T> void get_bvec(Blob& b, bounded_vec<Sz, T>& v)
	{
		uint32_t size = 0, count = 0;
		b.get(size);
		b.get(count);
		if (b.failed || count > STATE_MAX_ELEMENTS)
		{
			b.failed = true;
			return;
		}
		std::vector<T> tmp(count);
		if (count)
			b.get(tmp.data(), (size_t)count * sizeof(T));
		if (b.failed)
			return;
		v.clear();
		v.resize((Sz)size);
		v = tmp;
	}
	// A map whose values are plain values.
	// The entries a bounded_map's normalize() would keep (see put_bvec).
	template <typename Sz, typename T, typename Map>
	uint32_t bmap_kept(const Map& m)
	{
		uint32_t kept = 0;
		for (auto& [key, value] : m.inner())
			if (value != m.defval() && (size_t)key < (size_t)m.size())
				kept++;
		return kept;
	}

	template <typename Sz, typename T> void put_bmap_pod(Blob& b, const bounded_map<Sz, T>& m)
	{
		b.put((uint32_t)m.size());
		b.put(bmap_kept<Sz, T>(m));
		for (auto& [key, value] : m.inner())
		{
			if (value == m.defval() || (size_t)key >= (size_t)m.size())
				continue;
			b.put(key);
			b.put((const void*)&value, sizeof(T));
		}
	}
	template <typename Sz, typename T> void get_bmap_pod(Blob& b, bounded_map<Sz, T>& m)
	{
		uint32_t size = 0, count = 0;
		b.get(size);
		b.get(count);
		if (b.failed || count > STATE_MAX_ELEMENTS)
		{
			b.failed = true;
			return;
		}
		m.clear();
		m.resize((Sz)size);
		auto& inner = m.mut_inner();
		inner.clear();
		for (uint32_t i = 0; i < count; i++)
		{
			Sz key{};
			b.get(key);
			if (b.failed)
				return;
			b.get((void*)&inner[key], sizeof(T));
			if (b.failed)
				return;
		}
	}
	// A map whose values are a four-byte array (the per-screen door state).
	template <typename Sz> void put_bmap_arr4(Blob& b, const bounded_map<Sz, std::array<byte, 4>>& m)
	{
		b.put((uint32_t)m.size());
		b.put(bmap_kept<Sz, std::array<byte, 4>>(m));
		for (auto& [key, value] : m.inner())
		{
			if (value == m.defval() || (size_t)key >= (size_t)m.size())
				continue;
			b.put(key);
			b.put(value.data(), value.size());
		}
	}
	template <typename Sz> void get_bmap_arr4(Blob& b, bounded_map<Sz, std::array<byte, 4>>& m)
	{
		uint32_t size = 0, count = 0;
		b.get(size);
		b.get(count);
		if (b.failed || count > STATE_MAX_ELEMENTS)
		{
			b.failed = true;
			return;
		}
		m.clear();
		m.resize((Sz)size);
		auto& inner = m.mut_inner();
		inner.clear();
		for (uint32_t i = 0; i < count; i++)
		{
			Sz key{};
			b.get(key);
			if (b.failed)
				return;
			b.get(inner[key].data(), inner[key].size());
			if (b.failed)
				return;
		}
	}
	// A map whose values are bounded vecs (the per-screen D[] banks).
	template <typename Sz, typename T> void put_bmap_vec(Blob& b, const bounded_map<Sz, bounded_vec<byte, T>>& m)
	{
		b.put((uint32_t)m.size());
		b.put(bmap_kept<Sz, bounded_vec<byte, T>>(m));
		for (auto& [key, value] : m.inner())
		{
			if (value == m.defval() || (size_t)key >= (size_t)m.size())
				continue;
			b.put(key);
			put_bvec(b, value);
		}
	}
	template <typename Sz, typename T> void get_bmap_vec(Blob& b, bounded_map<Sz, bounded_vec<byte, T>>& m)
	{
		uint32_t size = 0, count = 0;
		b.get(size);
		b.get(count);
		if (b.failed || count > STATE_MAX_ELEMENTS)
		{
			b.failed = true;
			return;
		}
		m.clear();
		m.resize((Sz)size);
		auto& inner = m.mut_inner();
		inner.clear();
		for (uint32_t i = 0; i < count; i++)
		{
			Sz key{};
			b.get(key);
			if (b.failed)
				return;
			get_bvec(b, inner[key]);
			if (b.failed)
				return;
		}
	}
	// A map whose values are maps (the generic scripts' per-key banks).
	template <typename Sz, typename T> void put_bmap_map(Blob& b, const bounded_map<Sz, bounded_map<dword, T>>& m)
	{
		b.put((uint32_t)m.size());
		b.put(bmap_kept<Sz, bounded_map<dword, T>>(m));
		for (auto& [key, value] : m.inner())
		{
			if (value == m.defval() || (size_t)key >= (size_t)m.size())
				continue;
			b.put(key);
			put_bmap_pod(b, value);
		}
	}
	template <typename Sz, typename T> void get_bmap_map(Blob& b, bounded_map<Sz, bounded_map<dword, T>>& m)
	{
		uint32_t size = 0, count = 0;
		b.get(size);
		b.get(count);
		if (b.failed || count > STATE_MAX_ELEMENTS)
		{
			b.failed = true;
			return;
		}
		m.clear();
		m.resize((Sz)size);
		auto& inner = m.mut_inner();
		inner.clear();
		for (uint32_t i = 0; i < count; i++)
		{
			Sz key{};
			b.get(key);
			if (b.failed)
				return;
			get_bmap_pod(b, inner[key]);
			if (b.failed)
				return;
		}
	}
	// A script array (ZScriptArray): whether the slot is live, and its contents.
	void put_owned_array(Blob& b, const ZScriptArray& a)
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
	void get_owned_array(Blob& b, ZScriptArray& a)
	{
		uint8_t valid = 0;
		uint32_t n = 0;
		b.get(valid);
		b.get(n);
		if (b.failed || n > STATE_MAX_ELEMENTS)
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
			if (b.failed)
				return;
			a[i] = v;
		}
	}
	// A portal: what a script created and what the mirror remembers, identified by its UID.
	void put_saved_portal(Blob& b, const savedportal& p)
	{
		int32_t scalars[] = {p.destdmap, p.srcdmap, (int32_t)p.srcscr, (int32_t)p.destscr,
			p.x, p.y, (int32_t)p.sfx, p.warpfx, p.spr, (int32_t)(p.deleting ? 1 : 0),
			p.getUID()};
		b.put(scalars, sizeof(scalars));
	}
	void get_saved_portal(Blob& b, savedportal& p)
	{
		int32_t scalars[11] = {};
		b.get(scalars, sizeof(scalars));
		if (b.failed)
			return;
		p.destdmap = (int16_t)scalars[0];
		p.srcdmap = (int16_t)scalars[1];
		p.srcscr = (byte)scalars[2];
		p.destscr = (byte)scalars[3];
		p.x = scalars[4];
		p.y = scalars[5];
		p.sfx = (byte)scalars[6];
		p.warpfx = scalars[7];
		p.spr = (int16_t)scalars[8];
		p.deleting = scalars[9] != 0;
		if (scalars[10] < 0 || scalars[10] >= INT32_MAX) { b.failed = true; return; }
		p.setUID(scalars[10]);
	}
	// A script-created object: the pool slot it belongs to, its variables and owned count, the
	// destructor it would run, and the arrays it holds.
	void put_user_object(Blob& b, const saved_user_object& o)
	{
		b.put(o.object_index);
		b.put((int32_t)o.obj.owned_type);
		b.put(o.obj.owned_i);
		b.put((uint8_t)(o.obj.reserved ? 1 : 0));
		b.put((uint64_t)o.obj.owned_vars);
		b.put((uint32_t)o.obj.data.size());
		if (!o.obj.data.empty())
			b.put(o.obj.data.data(), o.obj.data.size() * sizeof(int32_t));
		b.put(o.obj.destruct.pc);
		b.put(o.obj.destruct.thiskey);
		b.put((int32_t)o.obj.destruct.type);
		b.put(o.obj.destruct.i);
		b.put(o.obj.destruct.script);
		b.put_str(o.obj.destruct.name);
		b.put((uint32_t)o.held_arrays.size());
		for (auto& [key, arr] : o.held_arrays)
		{
			b.put(key);
			put_owned_array(b, arr);
		}
	}
	void get_user_object(Blob& b, saved_user_object& o)
	{
		b.get(o.object_index);
		int32_t owned_type = 0;
		b.get(owned_type);
		o.obj.owned_type = (ScriptType)owned_type;
		b.get(o.obj.owned_i);
		uint8_t reserved = 0;
		b.get(reserved);
		o.obj.reserved = reserved != 0;
		uint64_t owned_vars = 0;
		b.get(owned_vars);
		o.obj.owned_vars = (size_t)owned_vars;
		uint32_t count = 0;
		b.get(count);
		if (b.failed || count > STATE_MAX_ELEMENTS)
		{
			b.failed = true;
			return;
		}
		o.obj.data.resize(count);
		if (count)
			b.get(o.obj.data.data(), count * sizeof(int32_t));
		b.get(o.obj.destruct.pc);
		b.get(o.obj.destruct.thiskey);
		int32_t type = 0;
		b.get(type);
		o.obj.destruct.type = (ScriptType)type;
		b.get(o.obj.destruct.i);
		b.get(o.obj.destruct.script);
		b.get_str(o.obj.destruct.name);
		uint32_t arrays = 0;
		b.get(arrays);
		if (b.failed || arrays > STATE_MAX_ELEMENTS)
		{
			b.failed = true;
			return;
		}
		o.held_arrays.clear();
		for (uint32_t i = 0; i < arrays; i++)
		{
			int32_t key = 0;
			b.get(key);
			if (b.failed)
				return;
			get_owned_array(b, o.held_arrays[key]);
			if (b.failed)
				return;
		}
	}
	// A std::vector of records, element by element: the records are not plain values.
	template <typename Rec> void put_record_vec(Blob& b, const std::vector<Rec>& v, void (*put)(Blob&, const Rec&))
	{
		b.put((uint32_t)v.size());
		for (auto& rec : v)
			put(b, rec);
	}
	template <typename Rec> void get_record_vec(Blob& b, std::vector<Rec>& v, void (*get)(Blob&, Rec&))
	{
		uint32_t count = 0;
		b.get(count);
		if (b.failed || count > STATE_MAX_ELEMENTS)
		{
			b.failed = true;
			return;
		}
		v.clear();
		v.resize(count);
		for (uint32_t i = 0; i < count; i++)
		{
			get(b, v[i]);
			if (b.failed)
				return;
		}
	}
	// A section whose byte length is written before it, so the validating pass can step over it
	// without knowing its layout: a capture writes a placeholder, the section, then the length.
	// A section that grows a field can never desynchronise the reader this way.
	template <typename Write> void put_sized_section(Blob& b, Write write)
	{
		b.put((uint32_t)0);
		size_t at = b.data.size() - sizeof(uint32_t);
		write();
		uint32_t len = (uint32_t)(b.data.size() - at - sizeof(uint32_t));
		memcpy(b.data.data() + at, &len, sizeof(len));
	}
	void skip_sized_section(StateSkip& k)
	{
		uint32_t len = 0;
		k.b.get(len);
		k.skip_bytes(len);
	}

	// Reading a length-prefixed section: the length is consumed and remembered, so a section whose
	// layout no longer matches what the capture wrote is turned down rather than left to
	// desynchronise everything after it.
	struct SizedSection
	{
		Blob& b;
		size_t start = 0;
		size_t end = 0;
		bool ok = true;

		SizedSection(Blob& b) : b(b)
		{
			start = b.pos + sizeof(uint32_t);
			uint32_t len = 0;
			b.get(len);
			if (b.failed || len > b.data.size() - b.pos)
			{
				b.failed = true;
				ok = false;
				return;
			}
			end = b.pos + len;
		}
		bool done() const { return ok && b.pos == end; }
	};


	void put_gamedata(Blob& b)
	{
		gamedata& g = *game;
		gamedata_header& h = g.header;
		b.put(h.sversion); b.put_str(h.qstpath); b.put_str(h.replay_file);
		b.put_str(h.name); b.put_str(h.title); b.put(h.quest); b.put(h.has_played);
		b.put(h.icon); b.put(h.pal); b.put(g.version);

		int32_t scalars[] = {
			h.deaths, h.life, h.maxlife, h.hp_per_heart_container,
			(int32_t)h.time_valid, (int32_t)h.did_cheat, (int32_t)h.time,
			(int32_t)g._cheat,
			(int32_t)g._continue_scrn, (int32_t)g._continue_dmap,
			(int32_t)g.awpn, (int32_t)g.bwpn, (int32_t)g.xwpn, (int32_t)g.ywpn,
			g.abtn_itm, g.bbtn_itm, g.xbtn_itm, g.ybtn_itm,
			g.forced_awpn, g.forced_bwpn, g.forced_xwpn, g.forced_ywpn,
			(int32_t)g.swim_mult, (int32_t)g.swim_div,
		};
		b.put(scalars, sizeof(scalars));
		b.put(g.item, sizeof(g.item));
		b.put(g.items_off, sizeof(g.items_off));
		b.put(g.item_messages_played, sizeof(g.item_messages_played));
		b.put(g._counter, sizeof(g._counter));
		b.put(g._maxcounter, sizeof(g._maxcounter));
		b.put(g._dcounter, sizeof(g._dcounter));
		b.put(g.visited, sizeof(g.visited));
		b.put(g.bottleSlots, sizeof(g.bottleSlots));
		b.put(g.global_d, sizeof(g.global_d));

		put_bvec(b, g.lvlitems);
		put_bvec(b, g.lvlkeys);
		put_bvec(b, g.lvlswitches);
		put_bvec(b, g._generic);
		put_bvec(b, g.bmaps);
		put_bvec(b, g.maps);
		put_bvec(b, g.guys);
		put_bvec(b, g.gen_doscript.inner());

		put_bmap_vec(b, g.screen_d);
		put_bmap_pod(b, g.xstates);
		put_bmap_arr4(b, g.xdoors);
		put_bmap_pod(b, g.gswitch_timers);
		put_bmap_pod(b, g.OverrideItems);
		put_bmap_pod(b, g.gen_exitState);
		put_bmap_pod(b, g.gen_reloadState);
		put_bmap_vec(b, g.gen_initd);
		put_bmap_pod(b, g.gen_eventstate);
		put_bmap_map(b, g.gen_data);
		put_bmap_map(b, g.screen_data);

		put_record_vec(b, g.user_objects, put_user_object);
		put_record_vec(b, g.user_portals, put_saved_portal);
		put_saved_portal(b, g.saved_mirror_portal);
	}

	void get_gamedata(Blob& b, gamedata* target = nullptr)
	{
		gamedata& g = target ? *target : *game;
		gamedata_header& h = g.header;
		b.get(h.sversion); b.get_str(h.qstpath); b.get_str(h.replay_file);
		b.get_str(h.name); b.get_str(h.title); b.get(h.quest); b.get(h.has_played);
		b.get(h.icon); b.get(h.pal); b.get(g.version);

		int32_t scalars[24] = {};
		b.get(scalars, sizeof(scalars));
		if (b.failed)
			return;
		h.deaths = scalars[0];
		h.life = (word)scalars[1];
		h.maxlife = (word)scalars[2];
		h.hp_per_heart_container = (byte)scalars[3];
		h.time_valid = scalars[4] != 0;
		h.did_cheat = scalars[5] != 0;
		h.time = (dword)scalars[6];
		g._cheat = (byte)scalars[7];
		g._continue_scrn = (byte)scalars[8];
		g._continue_dmap = (word)scalars[9];
		g.awpn = (word)scalars[10];
		g.bwpn = (word)scalars[11];
		g.xwpn = (word)scalars[12];
		g.ywpn = (word)scalars[13];
		g.abtn_itm = (int16_t)scalars[14];
		g.bbtn_itm = (int16_t)scalars[15];
		g.xbtn_itm = (int16_t)scalars[16];
		g.ybtn_itm = (int16_t)scalars[17];
		g.forced_awpn = (int16_t)scalars[18];
		g.forced_bwpn = (int16_t)scalars[19];
		g.forced_xwpn = (int16_t)scalars[20];
		g.forced_ywpn = (int16_t)scalars[21];
		g.swim_mult = (byte)scalars[22];
		g.swim_div = (byte)scalars[23];
		b.get(g.item, sizeof(g.item));
		b.get(g.items_off, sizeof(g.items_off));
		b.get(g.item_messages_played, sizeof(g.item_messages_played));
		b.get(g._counter, sizeof(g._counter));
		b.get(g._maxcounter, sizeof(g._maxcounter));
		b.get(g._dcounter, sizeof(g._dcounter));
		b.get(g.visited, sizeof(g.visited));
		b.get(g.bottleSlots, sizeof(g.bottleSlots));
		b.get(g.global_d, sizeof(g.global_d));

		get_bvec(b, g.lvlitems);
		get_bvec(b, g.lvlkeys);
		get_bvec(b, g.lvlswitches);
		get_bvec(b, g._generic);
		get_bvec(b, g.bmaps);
		get_bvec(b, g.maps);
		get_bvec(b, g.guys);
		get_bvec(b, g.gen_doscript.inner());

		get_bmap_vec(b, g.screen_d);
		get_bmap_pod(b, g.xstates);
		get_bmap_arr4(b, g.xdoors);
		get_bmap_pod(b, g.gswitch_timers);
		get_bmap_pod(b, g.OverrideItems);
		get_bmap_pod(b, g.gen_exitState);
		get_bmap_pod(b, g.gen_reloadState);
		get_bmap_vec(b, g.gen_initd);
		get_bmap_pod(b, g.gen_eventstate);
		get_bmap_map(b, g.gen_data);
		get_bmap_map(b, g.screen_data);

		get_record_vec(b, g.user_objects, get_user_object);
		get_record_vec(b, g.user_portals, get_saved_portal);
		get_saved_portal(b, g.saved_mirror_portal);
	}

	// The region hash for quest progress is taken over exactly the bytes a snapshot stores for it,
	// so anything the capture cannot put back shows up as a mismatch instead of as a session that
	// plays on with the wrong inventory.
	static uint64_t gamedata_bytes_hash()
	{
		if (!game)
			return 0;
		SaveStateBlob scratch;
			put_gamedata(scratch);
		uint64_t h = 1469598103934665603ULL;
		for (uint8_t byte : scratch.data)
		{
			h ^= byte;
			h *= 1099511628211ULL;
		}
		return h;
	}

	bool capture_state(Snapshot& snap)
	{
// Add near start of capture_state, before any state is encoded. Requires the
// deferred-owner patch. Canonical checkpoints must have no executing owners.
sprite_list* owner_lists[]={&guys,&items,&Ewpns,&Lwpns,&chainlinks,&decorations,&portals};
for(auto* list:owner_lists) if(list->host_peek_deferred_sprite()) return false;

		const bool profiling=snapshot_profile::capture_enabled();
		std::chrono::steady_clock::time_point profile_start,profile_previous;
		std::vector<std::pair<const char*,double>> stage_times;
		if(snapshot_profile::enabled()) snapshot_profile::reset_capture();
		if(profiling) { profile_start=profile_previous=std::chrono::steady_clock::now(); }
		Blob& b = snap.blob;
		b.put(ZC255_SNAPSHOT_BUILD);

		// Names the offset each section of the blob ends at (ZC_SNAPSHOT_MARKS=1), so a byte that
		// does not survive a restore can be attributed to a section instead of hunted for. Off
		// unless asked for; the sections it names are the ones this file writes in order.
		auto mark = [&](const char* name) {
			if(profiling) {
				const auto now=std::chrono::steady_clock::now();
				stage_times.emplace_back(name,std::chrono::duration<double,std::milli>(now-profile_previous).count());
				profile_previous=now;
			}
			if (getenv("ZC_SNAPSHOT_MARKS"))
				fprintf(stderr, "snapshot mark %-20s %zu\n", name, b.data.size());
		};

		// Sprites come first because they are the one part of a snapshot a restore may have to
		// rebuild rather than write back, and doing that work before anything else means a
		// snapshot it has to turn down leaves the game untouched.
		for (sprite_list* list : {&guys, &items, &Ewpns, &Lwpns, &chainlinks, &decorations, &portals})
			if (!put_sprite_list(b, *list))
				return false;
		mark("sprites");

		// The hero, with the weapon it is holding and the two weapon references it keeps.
		if (!put_hero(b))
			return false;
		mark("hero");

		// The script VM: engine slots, the arrays scripts hold and who owns them, the per-type UID
		// counters and the per-FFC Misc[] bank (see zc/ffscript.cpp), then the drawing commands that
		// are queued for the frame (they are state until they are consumed).
		{
			StateWriter vm{b};
			FFCore.serialize_state(vm);
			script_drawing_commands.serialize_state(vm);
		}
		mark("script_vm");

		// The live screen buffers, with everything the session has done to the screens.
		{
			StateWriter scr{b};
			put_screen_buffers(scr);
		}
		mark("screen_buffers");

		// Quest progress: hearts, deaths, counters, inventory, keys, maps, switch states, the
		// script data banks, and the objects and portals scripts have created.
		if (game)
			put_sized_section(b, [&] { put_gamedata(b); });
		mark("gamedata");

		// RNGs: the two game generators and every script-visible user RNG. The user RNGs are
		// run-length encoded because they are mostly identical (256 generators), which is what
		// makes a short capture interval affordable.
		b.put(*zc_get_default_rand());
		if (zc_randgen* drunk = zc_get_drunk_rng())
			b.put(*drunk);
		put_runs(b, (const uint8_t*)script_rnggens, sizeof(script_rnggens), sizeof(zc_randgen));
		mark("rngs");

		// How many times each of those generators has been seeded this frame. It is not the
		// generators' state but the accounting that matches a seed request to the recorded step,
		// and a restore that gets it wrong desyncs the replay the next time a screen seeds an RNG.
		{
			auto counts = replay_get_rng_seed_counts();
			b.put((uint32_t)counts.size());
			for (auto& [index, count] : counts)
			{
				b.put(index);
				b.put(count);
			}
		}

		// Script arrays.
		uint32_t global_count = game ? (uint32_t)game->globalRAM.size() : 0;
		mark("global_ram_head");
		b.put(global_count);
		if (game)
			for (auto& arr : game->globalRAM)
				put_zarray(b, const_cast<ZScriptArray&>(arr));

		uint32_t object_count = (uint32_t)objectRAM.size();
		b.put(object_count);
		for (auto& [id, arr] : objectRAM)
		{
			b.put(id);
			put_zarray(b, const_cast<ZScriptArray&>(arr));
		}
		mark("object_ram");

		// Viewport, screen indices, scroll.
		int32_t vp[4] = {viewport.x, viewport.y, viewport.w, viewport.h};
		b.put(vp);
		int32_t scr[3] = {currmap, currscr, dlevel};
		b.put(scr);
		int32_t scroll[5] = {
			(int32_t)screenscrolling, scrolling_map, scrolling_scr,
			(int32_t)scrolling_using_new_region_coords, newscr_clk,
		};
		b.put(scroll);
		mark("scroll");

		// Animation clocks. The global animation counter keys combo animation and some scrolling
		// behaviour (and the fade clock decides when a fade ends), so a restored run has to carry
		// their values - otherwise everything animated would resume at whatever phase the frames
		// the rewind jumped over left behind. The sprite UID counter rides along: it is what the
		// objects the run is about to create will be identified by.
		int32_t clocks[4] = {frame, fadeclk, conveyclk, sprite::getUIDCounter()};
		b.put(clocks);

		// Engine globals the per-frame state comparison (ZC_GLOBAL_COMPARE_FRAMES) found a restore
		// was leaving behind: small pieces of the game's own state that one frame writes and a later
		// frame reads. They are the kind of thing that is easy to miss and hard to notice, because
		// the drift they cause shows up dozens of frames later in what a script or the hero does.
		{
			int32_t misc[] = {
				div_prot_item,        // the Divine Protection item the shield spell is running
				magicitem,            // the magic item being cast
				magiccastclk,         // ... and its clock
				heart_beep_timer,     // the low-health beep countdown
				quakeclk,             // the screen quake countdown
				wavy,                 // the wavy-screen phase
				new_enemy_tile_start, // the next enemy's starting tile
				nets,                 // the "nets" cheat's tile
				idle_count,           // how long the player has been idle
				active_count,         // ... and how long they have not
				hero_count,           // the hero's animation step counter
				fairy_cnt,            // the fairy following the hero
				subscr_item_clk,      // the subscreen's item animation clock
				subscr_pg_clk,        // ... and its page clock
			};
			b.put(misc, sizeof(misc));
			StateWriter portal_state{b};
			mirror_portal.visit_state(portal_state);
			b.put(mirror_portal.getUID());
			mblock2.visit_state(portal_state);
			b.put(mblock2.getUID());
		}
		mark("clocks_misc");

		// The engine's own per-frame globals, from the one table both sides walk (see the engine
		// globals section above): the item each button is bound to, the clocks a weapon or an item
		// left behind for a later frame, the hookshot's pull, and the identity of the object a
		// switchhook is acting on. A capture that cannot describe that object fails here rather than
		// write a snapshot the restore would resolve to the wrong thing.
		if (!put_engine_globals(b))
			return false;
		mark("engine_globals");

		// State two subsystems keep in their own file-scope variables: the message menu (guys.cpp)
		// and the per-combo timers (combos.cpp). Both are state rather than caches - a menu moves its
		// cursor and holds the input over several frames, and a crumbling combo or a shooter counts
		// frames in its timer between the frames that read it - so both travel, named through the
		// accessors their own module publishes.
		{
			msg_menu_state menu;
			guys_get_msg_menu_state(menu);
			b.put((uint32_t)menu.options.size());
			for (auto& [key, choice] : menu.options)
			{
				int32_t entry[8] = {key, choice.x, choice.y, choice.pos, choice.upos,
					choice.dpos, choice.lpos, choice.rpos};
				b.put(entry, sizeof(entry));
			}
			int32_t flags[4] = {menu.running, menu.wait_advance, menu.end_str, menu.name_insert};
			b.put(flags, sizeof(flags));
			b.put(menu.namebuf, sizeof(menu.namebuf));
			b.put(menu.name_offset);
			b.put(menu.tile_hei);
			b.put(menu.tile_hei_buf);
			b.put(menu.data, sizeof(menu.data));

			std::vector<uint8_t> cpos(cpos_state_size());
			cpos_get_state(cpos.data());
			b.put(cpos.data(), cpos.size());
		}
		mark("menu_cpos");

		// Which screens the session has been in most recently: a screen in that ring with no enemies
		// left is given none when it is entered again, so this is what decides whether a room the
		// player walks back into still has its enemies (see guys.cpp's side_load_enemies). Left out,
		// a room the session visited after the restore point keeps suppressing its enemies for the
		// rest of the session.
		{
			int32_t visited[6] = {};
			int32_t head = 0;
			guys_get_visited_state(visited, head);
			b.put(visited, sizeof(visited));
			b.put(head);
		}
		mark("visited");

		// Screen-load bookkeeping: the dmap/screen a change is coming from and going to, and the
		// state a load leaves behind for the next one (its entry position and counters). A screen
		// change reads these, so without them a restored run can place or time one differently.
		// `homescr`/`warpscr`/`worldscr` are part of it: they name the screen a transition is
		// coming from and going to, and a scroll reads them while it runs.
		int32_t screen_state[10] = {currdmap, opendoors, listpos, sle_x, sle_y, sle_cnt, sle_clk,
			homescr, warpscr, worldscr};
		b.put(screen_state);

		// FFC position/velocity arrays.
		b.put(ffposx, sizeof(ffposx));
		b.put(ffposy, sizeof(ffposy));
		b.put(ffprvx, sizeof(ffprvx));
		b.put(ffprvy, sizeof(ffprvy));

		// The replay cursor, so a restored snapshot continues at the same input step.
		ReplayCursorState cursor = replay_get_cursor();
		b.put(cursor.frame);
		b.put(cursor.input_index);
		b.put(cursor.assert_index);
		b.put(cursor.quit_index);
		b.put(cursor.state_index);
		b.put(cursor.loadscr_count);
		mark("tail_cursor");
		zc255_runtime_write(b);
		mark("runtime");
		zc255_cheats_write(b);
		mark("cheats");
		zc255_bridge_state_write(b);
		mark("bridge");
		b.put(savedportal::snapshot_uid_counter());
		if (!snapshot_ui::capture(b)) { fprintf(stderr,"snapshot capture failed: UI section\n"); return false; }
		mark("UI");
		snapshot_extra_definitions::put(b);
		mark("extra_definitions");
		if (!zc255_audio_state_write(b)) return false;
		mark("audio");
		if (!snapshot_definitions::put(b)) { fprintf(stderr,"snapshot capture failed: definitions section\n"); return false; }
		mark("definitions");
		if (!snapshot_collision_state::put(b)) { fprintf(stderr,"snapshot capture failed: collision section\n"); return false; }
		mark("collision");
		if(profiling) {
			const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-profile_start).count();
			const auto& compression=snapshot_profile::capture_measure();
			fprintf(stderr,"capture_profile {\"total_ms\":%.6f,\"compression_ms\":%.6f,\"compression_calls\":%llu,\"bytes\":%zu,\"stages\":{",
				elapsed,compression.compress_us/1000,(unsigned long long)compression.compress_calls,b.data.size());
			for(size_t i=0;i<stage_times.size();++i) fprintf(stderr,"%s\"%s\":%.6f",i?",":"",stage_times[i].first,stage_times[i].second);
			fprintf(stderr,"},\"definitions_stages\":{\"general\":%.6f,\"combos\":%.6f,\"maps\":%.6f,\"tiles\":%.6f}}\n",
				compression.general_us/1000,compression.combo_us/1000,compression.maps_us/1000,compression.tiles_us/1000);
		}
		return !b.failed;
	}

	const char* restore_stage="none";
    // Diagnostic only: the same restore/preflight path runs with profiling disabled.
    struct RestoreMeasure {
        bool enabled=snapshot_profile::restore_enabled();
        std::chrono::steady_clock::time_point start,previous;
        std::vector<std::pair<const char*,double>> stages;
        RestoreMeasure() { if(enabled) start=previous=std::chrono::steady_clock::now(); }
        void mark(const char* name) {
            if(!enabled) return;
            auto now=std::chrono::steady_clock::now();
            stages.emplace_back(name,std::chrono::duration<double,std::milli>(now-previous).count());
            previous=now;
        }
        void emit(bool restored) {
            if(!enabled) return;
            const auto& compression=snapshot_profile::capture_measure();
            fprintf(stderr,"restore_profile {\"total_ms\":%.6f,\"success\":%s,\"decompression_ms\":%.6f,\"decompression_calls\":%llu,\"stages\":{",
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),restored ? "true":"false",
                compression.decompress_us/1000.0,(unsigned long long)compression.decompress_calls);
            for(size_t i=0;i<stages.size();++i) fprintf(stderr,"%s\"%s\":%.6f",i ? ",":"",stages[i].first,stages[i].second);
            fprintf(stderr,"}}\n");
        }
    };
	struct PreparedTail {
		snapshot_ui::PreparedState ui;
		snapshot_extra_definitions::PreparedState extra;
		snapshot_definitions::PreparedState definitions;
		snapshot_collision_state::PreparedState collision;
		size_t ui_begin=0, ui_end=0, extra_end=0, definitions_begin=0, definitions_end=0, collision_end=0;
	};
	bool validate_tail(Blob& b, const SpriteListImage (&images)[7], PreparedTail& prepared, RestoreMeasure* timing=nullptr)
	{
		StateSkip k{b};
		auto mark=[&](const char* label){ if(timing) timing->mark(label); if(getenv("ZC_SNAPSHOT_MARKS")) fprintf(stderr,"preflight mark %s %zu failed=%d\n",label,b.pos,b.failed); };
		k.skip_bytes(sizeof(zc_randgen));
		if (zc_get_drunk_rng()) k.skip_bytes(sizeof(zc_randgen));
		std::vector<uint8_t> rng(sizeof(script_rnggens));
		get_runs(b, rng.data(), rng.size(), sizeof(zc_randgen));
		uint32_t count = 0;
		b.get(count);
		if (b.failed || count > 1024) return false;
		k.skip_bytes(size_t(count) * sizeof(int32_t) * 2);
		auto skip_arrays = [&](bool ids) {
			uint32_t n = 0; b.get(n);
			if (b.failed || n > STATE_MAX_ELEMENTS || n > (b.data.size()-b.pos)/4) { b.failed = true; return; }
			for (uint32_t i = 0; i < n && !b.failed; ++i) {
				if (ids) k.skip_bytes(sizeof(int32_t));
				uint32_t elements = 0; b.get(elements);
				if (elements > STATE_MAX_ELEMENTS) { b.failed = true; return; }
				k.skip_bytes(size_t(elements)*sizeof(int32_t));
			}
		};
		skip_arrays(false); skip_arrays(true);
		k.skip_bytes(sizeof(int32_t)*(4+3+5+4+14));
		mirror_portal.visit_state(k);
		k.skip_bytes(sizeof(int32_t));
		mblock2.visit_state(k);
		k.skip_bytes(sizeof(int32_t));
		for (const auto& g : engine_global_table()) k.skip_bytes(g.size);
		int32_t kind=0, index=0; b.get(kind); b.get(index);
		if (kind < SWITCH_OBJECT_NONE || kind > SWITCH_OBJECT_MOVINGBLOCK) return false;
		if (kind == SWITCH_OBJECT_FFC && (index < 0 || index >= SCREEN_BUFFER_COUNT*MAXFFCS)) return false;
		if (kind == SWITCH_OBJECT_MOVINGBLOCK && index != 0) return false;
		if (kind >= SWITCH_OBJECT_GUY && kind <= SWITCH_OBJECT_EWEAPON && (index < 0 || index >= images[kind-SWITCH_OBJECT_GUY].count)) return false;
		b.get(count);
		if (b.failed || count > 4096) return false;
		k.skip_bytes(size_t(count)*sizeof(int32_t)*8);
		msg_menu_state menu;
		k.skip_bytes(sizeof(int32_t)*4 + sizeof(menu.namebuf) + sizeof(menu.name_offset) +
			sizeof(menu.tile_hei) + sizeof(menu.tile_hei_buf) + sizeof(menu.data));
		k.skip_bytes(cpos_state_size());
		k.skip_bytes(sizeof(int32_t)*(6+1+10));
		k.skip_bytes(sizeof(ffposx)+sizeof(ffposy)+sizeof(ffprvx)+sizeof(ffprvy));
		ReplayCursorState cursor;
		b.get(cursor.frame); b.get(cursor.input_index); b.get(cursor.assert_index);
		b.get(cursor.quit_index); b.get(cursor.state_index); b.get(cursor.loadscr_count);
		mark("tail_cursor");
        if (b.failed || cursor.frame < 0) return false;
        restore_stage="runtime preflight";
        if(!zc255_runtime_skip(b)) return false; mark("runtime");
        restore_stage="cheats preflight";
        if(!zc255_cheats_skip(b)) return false; mark("cheats");
        restore_stage="bridge preflight";
        if(!zc255_bridge_state_read(b,false)) return false; mark("bridge");
		int32_t portal_counter=0; b.get(portal_counter);
		if (portal_counter < 1) return false;
        prepared.ui_begin=b.pos;
        restore_stage="UI preflight"; if(!snapshot_ui::prepare(b,prepared.ui)) return false; mark("UI");
        prepared.ui_end=b.pos;
        restore_stage="extra definitions preflight"; if(!snapshot_extra_definitions::prepare(b,prepared.extra)) return false; mark("extra definitions");
        prepared.extra_end=b.pos;
        restore_stage="audio preflight"; if(!zc255_audio_state_read(b,false)) return false; mark("audio");
        prepared.definitions_begin=b.pos;
        restore_stage="definitions preflight"; if(!snapshot_definitions::prepare(b,prepared.definitions)) return false; mark("definitions");
        prepared.definitions_end=b.pos;
        restore_stage="collision preflight"; if(!snapshot_collision_state::prepare(b,prepared.collision)) return false; mark("collision");
        prepared.collision_end=b.pos;
		return !b.failed && b.pos == b.data.size();
	}

	bool restore_state_unchecked(Snapshot& snap, RestoreMeasure* timing=nullptr, bool* mutated=nullptr)
	{
		const auto prepare_started=std::chrono::steady_clock::now();
		restore_stage="build identity";
		Blob& b = snap.blob;
		unsigned char build[sizeof(ZC255_SNAPSHOT_BUILD)]={}; b.get(build);
		if (b.failed || memcmp(build,ZC255_SNAPSHOT_BUILD,sizeof(build))) return false;

		// First pass: work out which object every entry belongs to (the live sprite with that UID,
		// or a new one of the same class), before changing anything - so turning a snapshot down
		// leaves the game exactly as it was. A half-applied restore would otherwise be inherited by
		// the fallback's reload.
		restore_stage="sprite metadata";
		sprite_list* lists[] = {&guys, &items, &Ewpns, &Lwpns, &chainlinks, &decorations, &portals};
		SpriteListImage images[7];
		std::unordered_set<int32_t> seen_uids;
		std::unordered_map<int32_t, sprite*> live_by_uid = live_sprites_by_uid();
		for (int i = 0; i < 7; i++)
		{
			if (!read_sprite_list(b, images[i], live_by_uid) || b.failed)
				return false;
			for (const auto& entry : images[i].entries)
				if (entry.uid <= 0 || entry.uid >= INT32_MAX || !seen_uids.insert(entry.uid).second) return false;
		}
		// Where the rest of the snapshot begins. The pass below walks the blob again entry by
		// entry, so the stream position has to come back here afterwards.
		size_t sprites_end = b.pos;
        if(timing) timing->mark("preflight_sprites");

		// Walk what follows once, without touching the game, so a stream that is truncated or not
		// ours is turned down before anything is changed. The sprite entries above were already
		// walked the same way.
		restore_stage="hero preflight";
		int32_t captured_hero_uid=0;
		if (!skip_hero(b,&captured_hero_uid) || seen_uids.count(captured_hero_uid))
			return false;
        if(timing) timing->mark("preflight_hero");
		{
			StateSkip vm{b};
			restore_stage="VM preflight";
			FFCore.skip_state(vm);
            if(timing) timing->mark("preflight_vm");
			if(b.failed) return false;
			restore_stage="draw preflight";
			script_drawing_commands.skip_state(vm);
            if(timing) timing->mark("preflight_draw");
			if(b.failed) return false;
			restore_stage="screen preflight";
			skip_screen_buffers(vm);
            if(timing) timing->mark("preflight_screens");
			if (game) {
				const int32_t portal_counter = savedportal::snapshot_uid_counter();
				auto progress = std::make_unique<gamedata>();
				SizedSection section{b};
				restore_stage="gamedata preflight";
				get_gamedata(b, progress.get());
				savedportal::snapshot_set_uid_counter(portal_counter);
				if (!section.done()) return false;
			}
			if (b.failed)
				return false;
		}
        if(timing) timing->mark("preflight_progress");
		restore_stage="tail preflight";
		auto prepared=std::make_unique<PreparedTail>();
		if (!validate_tail(b,images,*prepared,timing)) return false;
        if(timing) timing->mark("preflight_complete");
		const auto apply_started=std::chrono::steady_clock::now();

		restore_stage="apply";
        if(mutated) *mutated=true;
		// From here on the snapshot is known to be reproducible, so it is applied. Anything the
		// application detaches is released when this returns.
		DetachedSprites detached_guard;
		RefReader refs{b};
		for (int i = 0; i < 7; i++)
			if (!restore_sprite_list(b, *lists[i], images[i], refs))
				return false;

		// Every UID the snapshot mentioned now has an object: the ones that were still alive, and
		// the ones rebuilt for it.
		std::unordered_map<int32_t, sprite*> by_uid = live_by_uid;
		by_uid[captured_hero_uid] = &Hero;
		for (auto& image : images)
			for (auto& e : image.entries)
				by_uid[e.uid] = e.object;

		auto resolve = [&](uint32_t uid) -> sprite*
		{
			if (!uid)
				return nullptr;
			auto it = by_uid.find((int32_t)uid);
			return it == by_uid.end() ? nullptr : it->second;
		};
		for (auto& [field, uid] : refs.pending)
			*field = resolve(uid);

        if(timing) timing->mark("apply_sprites");
		// The hero, including the weapon it holds and the references it keeps to weapons.
		b.pos = sprites_end;
		b.failed = false;
		if (!get_hero(b, by_uid))
			return false;

        if(timing) timing->mark("apply_hero");
		// The script VM comes back before anything else is read: a script that was mid-run has to
		// find its own state, and the rest of the snapshot (RNGs, arrays, screen state) is what
		// the frames after it will read.
		{
			StateReader vm{b};
			FFCore.deserialize_state(vm);
            if(timing) timing->mark("apply_vm");
			script_drawing_commands.deserialize_state(vm);
            if(timing) timing->mark("apply_draw");
			get_screen_buffers(vm);
            if(timing) timing->mark("apply_screens");
			if (game)
			{
				SizedSection progress{vm.b};
				get_gamedata(vm.b);
				if (!progress.done())
				{
					fprintf(stderr, "snapshot restore: quest progress read %zu of %zu bytes; turning the snapshot down\n",
						vm.b.pos, progress.end);
					return false;
				}
			}
			if (b.failed)
			{
				fprintf(stderr, "snapshot restore: the stream failed after quest progress (pos %zu)\n", vm.b.pos);
				return false;
			}
		}

        if(timing) timing->mark("apply_progress");
		b.get(*zc_get_default_rand());
		if (zc_randgen* drunk = zc_get_drunk_rng())
			b.get(*drunk);
		get_runs(b, (uint8_t*)script_rnggens, sizeof(script_rnggens), sizeof(zc_randgen));

		// The seed accounting that goes with them (see capture_state).
		{
			uint32_t count = 0;
			b.get(count);
			if (b.failed || count > 1024)
				return false;
			std::vector<std::pair<int, int>> counts;
			counts.reserve(count);
			for (uint32_t i = 0; i < count; i++)
			{
				int32_t index = 0, seeded = 0;
				b.get(index);
				b.get(seeded);
				counts.emplace_back(index, seeded);
			}
			if (b.failed)
				return false;
			replay_set_rng_seed_counts(counts);
		}

		uint32_t global_count = 0;
		b.get(global_count);
		if (game)
		{
			if (global_count != game->globalRAM.size())
				game->globalRAM.resize(global_count);
			for (auto& arr : game->globalRAM)
				get_zarray(b, arr);
		}
		else
		{
			// No game: still consume the bytes so the read position stays in step.
			for (uint32_t i = 0; i < global_count; i++)
			{
				ZScriptArray scratch;
				get_zarray(b, scratch);
			}
		}

		uint32_t object_count = 0;
		b.get(object_count);
		objectRAM.clear();
		for (uint32_t i = 0; i < object_count; i++)
		{
			int32_t id = 0;
			b.get(id);
			get_zarray(b, objectRAM[id]);
		}

		int32_t vp[4] = {};
		b.get(vp);
		viewport.x = vp[0];
		viewport.y = vp[1];
		viewport.w = vp[2];
		viewport.h = vp[3];

		int32_t scr[3] = {};
		b.get(scr);
		currmap = scr[0];
		currscr = scr[1];
		dlevel = scr[2];

		int32_t scroll[5] = {};
		b.get(scroll);
		screenscrolling = scroll[0] != 0;
		scrolling_map = scroll[1];
		scrolling_scr = scroll[2];
		scrolling_using_new_region_coords = scroll[3] != 0;
		newscr_clk = scroll[4];

		int32_t clocks[4] = {};
		b.get(clocks);
		// The frame counter is captured and put back unchanged: like everything else in the blob it
		// belongs to the loop boundary the snapshot was taken at.
		frame = clocks[0];
		fadeclk = clocks[1];
		conveyclk = clocks[2];
		// Identity, not just state: every object the run has handed a UID to, and every reference
		// a script keeps to one, assumes this counter continues where the recorded run left it.
		sprite::setUIDCounter(clocks[3]);

		// The engine globals that ride with the clocks (see capture_state).
		{
			int32_t misc[14] = {};
			b.get(misc, sizeof(misc));
			div_prot_item = misc[0];
			magicitem = misc[1];
			magiccastclk = misc[2];
			heart_beep_timer = misc[3];
			quakeclk = misc[4];
			wavy = misc[5];
			new_enemy_tile_start = misc[6];
			nets = misc[7];
			idle_count = misc[8];
			active_count = misc[9];
			hero_count = misc[10];
			fairy_cnt = misc[11];
			subscr_item_clk = misc[12];
			subscr_pg_clk = misc[13];
			StateReader portal_state{b};
			mirror_portal.visit_state(portal_state);
			int32_t portal_uid=0; b.get(portal_uid);
			mirror_portal.setUID(portal_uid);
			mblock2.visit_state(portal_state);
			int32_t block_uid=0; b.get(block_uid);
			mblock2.setUID(block_uid);
		}

		// The engine's own per-frame globals (see the engine globals section above), in the same
		// order the capture wrote them. The switchhook's object is resolved against the sprite lists
		// and screen buffers that were restored above, so it points at the same object it did before
		// the rewind rather than at whatever the skipped frames left there.
		if (!get_engine_globals(b))
			return false;

		// The message menu and the per-combo timers, written by capture_state just above (see there
		// for why they are state). The menu's entries are read back into the map the menu code walks.
		{
			uint32_t count = 0;
			b.get(count);
			if (b.failed || count > 4096)
				return false;
			msg_menu_state menu;
			for (uint32_t i = 0; i < count; i++)
			{
				int32_t entry[8] = {};
				b.get(entry, sizeof(entry));
				menu.options.emplace_back(entry[0],
					menu_choice(entry[1], entry[2], entry[3], entry[4], entry[5], entry[6], entry[7]));
			}
			int32_t flags[4] = {};
			b.get(flags, sizeof(flags));
			menu.running = flags[0] != 0;
			menu.wait_advance = flags[1] != 0;
			menu.end_str = flags[2] != 0;
			menu.name_insert = flags[3] != 0;
			b.get(menu.namebuf, sizeof(menu.namebuf));
			b.get(menu.name_offset);
			b.get(menu.tile_hei);
			b.get(menu.tile_hei_buf);
			b.get(menu.data, sizeof(menu.data));

			std::vector<uint8_t> cpos(cpos_state_size());
			b.get(cpos.data(), cpos.size());
			if (b.failed)
				return false;
			guys_set_msg_menu_state(menu);
			cpos_set_state(cpos.data());
		}

		// Which screens the session has been in most recently, written by capture_state just above:
		// a screen in that ring with no enemies left is given none when it is entered again, so this
		// is what decides whether a room the player walks back into still has its enemies.
		{
			int32_t visited[6] = {};
			int32_t head = 0;
			b.get(visited, sizeof(visited));
			b.get(head);
			if (b.failed)
				return false;
			guys_set_visited_state(visited, head);
		}

		int32_t screen_state[10] = {};
		b.get(screen_state);
		currdmap = screen_state[0];
		opendoors = screen_state[1];
		listpos = screen_state[2];
		sle_x = screen_state[3];
		sle_y = screen_state[4];
		sle_cnt = screen_state[5];
		sle_clk = screen_state[6];
		homescr = screen_state[7];
		warpscr = screen_state[8];
		worldscr = screen_state[9];

		b.get(ffposx, sizeof(ffposx));
		b.get(ffposy, sizeof(ffposy));
		b.get(ffprvx, sizeof(ffprvx));
		b.get(ffprvy, sizeof(ffprvy));

		ReplayCursorState cursor;
		b.get(cursor.frame);
		b.get(cursor.input_index);
		b.get(cursor.assert_index);
		b.get(cursor.quit_index);
		b.get(cursor.state_index);
		b.get(cursor.loadscr_count);
		if (b.failed || !zc255_runtime_read(b) || !zc255_cheats_read(b) || !zc255_bridge_state_read(b,true))
			return false;
		int32_t portal_counter=0; b.get(portal_counter);
		if (portal_counter < 1 || b.failed || b.pos!=prepared->ui_begin) return false;
        if(timing) timing->mark("apply_runtime");
		if (!snapshot_ui::apply(prepared->ui)) return false;
		b.pos=prepared->ui_end;
        if(timing) timing->mark("apply_ui");
		if (!snapshot_extra_definitions::apply(prepared->extra)) return false;
		b.pos=prepared->extra_end;
        if(timing) timing->mark("apply_extra_definitions");
		if (!zc255_audio_state_read(b,true) || b.failed || b.pos!=prepared->definitions_begin) return false;
        if(timing) timing->mark("apply_audio");
		if (!snapshot_definitions::apply(prepared->definitions)) return false;
		b.pos=prepared->definitions_end;
        if(timing) timing->mark("apply_definitions");
		if (!snapshot_collision_state::apply(prepared->collision)) return false;
		b.pos=prepared->collision_end;
        if(timing) timing->mark("apply_collision");
		savedportal::snapshot_set_uid_counter(portal_counter);
		if (b.failed || b.pos != b.data.size()) return false;

		// The snapshot was taken at the main loop's frame boundary, so a restore puts the session
		// back at exactly that point: the counters, the log cursor and the held input are all the
		// ones the recorded run had there, with nothing added or taken away. (Whatever the captured
		// frame's simulation did is already in the captured state; there is no half frame to skip.)
		replay_set_cursor(cursor);
		replay_advance_cursor_to(cursor.frame);
		// A button already down has to stay "already down" rather than read as a fresh press, and a
		// button that went down on the last completed frame has to keep reading as one: the held
		// state is the one the replay's own steps describe at this point - the state before the
		// frame the session is about to simulate - and `before` is the state one frame earlier,
		// which is what tells the two apart. A live recording has no log to ask, and the keys down
		// right now are what the player was already holding.
		// Input and private edge history were restored from the snapshot, rather
		// than reconstructed from future physical input or replay approximations.

		// A restore has to be able to reproduce itself: serialize the state that is now live and
		// compare it with the blob that was just applied. Anything the restore dropped, misread or
		// wrote to the wrong place shows up as a differing byte - including fields no hash region
		// covers, which is what makes this the check that catches "bookkeeping a restore forgot".
		if (getenv("ZC_SNAPSHOT_ROUNDTRIP"))
		{
			Snapshot again;
			again.frame = snap.frame;
			uint32_t format = SNAPSHOT_FORMAT;
			again.blob.put(format);
			capture_state(again);
			const auto& was = snap.blob.data;
			const auto& now = again.blob.data;
			size_t first = 0;
			while (first < was.size() && first < now.size() && was[first] == now[first])
				first++;
			size_t differing = 0;
			for (size_t i = 0; i < was.size() && i < now.size(); i++)
				if (was[i] != now[i])
					differing++;
			if (first == was.size() && was.size() == now.size())
				fprintf(stderr, "snapshot roundtrip: frame %d reproduced byte for byte (%zu bytes)\n",
					snap.frame, was.size());
			else
				fprintf(stderr, "snapshot roundtrip: frame %d DIFFERS at offset %zu of %zu/%zu (%zu byte(s)); "
					"was %02x%02x%02x%02x now %02x%02x%02x%02x\n",
					snap.frame, first, was.size(), now.size(), differing,
					first < was.size() ? was[first] : 0, first + 1 < was.size() ? was[first + 1] : 0,
					first + 2 < was.size() ? was[first + 2] : 0, first + 3 < was.size() ? was[first + 3] : 0,
					first < now.size() ? now[first] : 0, first + 1 < now.size() ? now[first + 1] : 0,
					first + 2 < now.size() ? now[first + 2] : 0, first + 3 < now.size() ? now[first + 3] : 0);
		}

		if (getenv("ZC255_RESTORE_TIMING")) {
			const auto completed=std::chrono::steady_clock::now();
			fprintf(stderr,"snapshot restore timing: prepare=%.3f ms apply=%.3f ms\n",
				std::chrono::duration<double,std::milli>(apply_started-prepare_started).count(),
				std::chrono::duration<double,std::milli>(completed-apply_started).count());
		}
		return true;

	}

	bool restore_state(Snapshot& snap)
	{
        RestoreMeasure timing;
        // Only the encoded bytes survive calls; no pointers or prepared engine objects
        // are retained. All imports run at the main gameplay boundary, and recovery
        // uses restore_state_unchecked rather than recursively entering this function.
        static thread_local Snapshot backup;
        backup.blob.data.clear();
        backup.blob.pos=0;
        backup.blob.failed=false;
		backup.frame = replay_get_frame();
		backup.blob.put(SNAPSHOT_FORMAT);
		if (!capture_state(backup) || backup.blob.failed) { timing.mark("recovery_capture"); timing.emit(false); return false; }
        timing.mark("recovery_capture");
		bool restored = false;
        bool mutated=false;
		try { restored = restore_state_unchecked(snap,&timing,&mutated) && !snap.blob.failed; }
		catch (const std::exception& e) { fprintf(stderr,"snapshot rejected: %s\n",e.what()); }
        timing.mark("restore_complete");
		if (restored) { timing.emit(true); return true; }
		fprintf(stderr,"snapshot restore rejected at %s pos=%zu/%zu failed=%d\n",restore_stage,snap.blob.pos,snap.blob.data.size(),snap.blob.failed);
        if(!mutated) { timing.emit(false); return false; }
		backup.blob.pos = sizeof(uint32_t);
		backup.blob.failed = false;
		if (!restore_state_unchecked(backup) || backup.blob.failed) {
			fprintf(stderr,"snapshot rollback rejected at %s pos=%zu/%zu failed=%d\n",restore_stage,backup.blob.pos,backup.blob.data.size(),backup.blob.failed);
			fprintf(stderr,"snapshot rollback failed; stopping helper to prevent partial-state gameplay\n");
			std::abort();
		}
        timing.mark("rollback");
        timing.emit(false);
		return false;
	}
}

bool snapshot_rewind_catching_up()
{
	// A rewind the player asked for that has not been applied yet: the state on screen is the
	// frame they rewound to, and whatever the loop simulates before the restore or the reload
	// lands belongs to the position they just left. Drawing it would flash that position, so it
	// is suppressed exactly like the catch-up itself.
	if (replay_rewind_requested())
		return true;

	// An in-process rewind that has restored a snapshot is simulating its way to the frame the
	// player asked for.
	if (rewind_active && rewind_restored)
		return true;

	// A run anchored at a frame (a rewind that went through the reload fallback, or a restored
	// savestate) is replaying everything before that anchor to rebuild the state.
	if (replay_is_active())
	{
		int anchor = replay_get_anchor_frame();
		if (anchor > 0 && replay_get_frame() < anchor)
			return true;
	}

	return false;
}

void snapshot_forget_history()
{
	ring.clear();
	frame_hashes.clear();
	total_bytes = 0;
}

// Which session the snapshots and hashes belong to: a live session records itself, so this
// changes when the run moves to another game or replay. Snapshots from another session describe a
// timeline this one does not continue and are dropped.
static std::string rings_session;

static bool rings_belong_to_current_session()
{
	std::string session = replay_get_replay_path().string();
	if (session == rings_session)
		return true;

	ring.clear();
	frame_hashes.clear();
	total_bytes = 0;
	rings_session = session;
	return false;
}

void snapshot_set_interval(int frames)
{
	capture_interval = frames;
}

int snapshot_interval()
{
	return capture_interval;
}

bool snapshot_capture_enabled()
{
	return capture_interval > 0;
}

// What one snapshot costs: its blob (the hero is serialized into it like everything else).
static size_t snapshot_footprint(const Snapshot& snap)
{
	return snap.blob.data.size();
}

size_t snapshot_capture(int frame)
{
	Snapshot snap;
	snap.frame = frame;
	uint32_t format = SNAPSHOT_FORMAT;
	snap.blob.put(format);
	if (!capture_state(snap) || snap.blob.failed) return 0;

	// Frame numbers are the identity of a snapshot: a session that passes a frame again (a rewind
	// replays it) replaces that snapshot instead of keeping a second copy. The ring therefore
	// always describes the same window of play, however often the player rewinds, and its reach
	// grows back to the configured window as play continues instead of shrinking with use.
	size_t bytes = snapshot_footprint(snap);
	auto it = std::lower_bound(ring.begin(), ring.end(), frame,
		[](const Snapshot& s, int value) { return s.frame < value; });
	if (it != ring.end() && it->frame == frame)
	{
		total_bytes -= snapshot_footprint(*it);
		*it = std::move(snap);
	}
	else
	{
		it = ring.insert(it, std::move(snap));
	}
	total_bytes += bytes;

	// Drop the oldest until the newest fits both the byte budget and the configured window. The
	// entries before it stay: they are the player's past, and a rewind must never cost reach.
	while (!ring.empty()
		&& (total_bytes > rewind_budget_bytes()
			|| (ring.back().frame - ring.front().frame) > rewind_window_seconds() * 60))
	{
		total_bytes -= snapshot_footprint(ring.front());
		ring.pop_front();
	}
	return bytes;
}

bool snapshot_restore_at_or_before(int frame, int& restored_frame, bool* rejected)
{
	if (rejected)
		*rejected = false;

	for (auto it = ring.rbegin(); it != ring.rend(); ++it)
	{
		if (it->frame <= frame)
		{
			Snapshot& snap = *it;
			Blob& b = snap.blob;
			b.pos = 0;
			b.failed = false;
			uint32_t format = 0;
			b.get(format);
			if (format != SNAPSHOT_FORMAT)
				return false;
			if (!restore_state(snap) || b.failed)
			{
				// There was a candidate, but this run cannot reproduce it.
				if (rejected)
					*rejected = true;
				return false;
			}
			restored_frame = snap.frame;
			return true;
		}
	}
	return false;
}

bool snapshot_restore_latest(int& restored_frame)
{
	if (ring.empty())
		return false;
	Snapshot& snap = ring.back();
	Blob& b = snap.blob;
	b.pos = 0;
	b.failed = false;
	uint32_t format = 0;
	b.get(format);
	if (format != SNAPSHOT_FORMAT)
		return false;
	if (!restore_state(snap) || b.failed)
		return false;
	restored_frame = snap.frame;
	return true;
}

void snapshot_reset()
{
	ring.clear();
	total_bytes = 0;
}

// A hash over the quest-progress state a snapshot carries (hearts, items, counters, keys, maps,
// switch states, script data). Differs from one run to another exactly when a restore would not
// reproduce what the player had, which is why the state hash covers it.
uint64_t snapshot_gamedata_hash()
{
	return gamedata_bytes_hash();
}

// One line per frame: every sprite in every list, with the hash of every field its class's state
// description covers. Two runs diffed with this say exactly which field of which sprite a restore
// got wrong (see scripts/sprite_field_diff.py).
std::string snapshot_sprite_fields_line()
{
	char buf[64];
	std::string out;
	static const char* names[] = {"guys", "items", "Ewpns", "Lwpns", "chainlinks", "decorations", "portals"};
	int li = 0;
	for (sprite_list* list : {&guys, &items, &Ewpns, &Lwpns, &chainlinks, &decorations, &portals})
	{
		snprintf(buf, sizeof(buf), " %s=%d[", names[li++], list->Count());
		out += buf;
		for (int32_t i = 0; i < list->Count(); i++)
		{
			sprite* spr = list->spr(i);
			if (!spr)
				continue;
			const SpriteClass* cls = sprite_class_of(spr);
			snprintf(buf, sizeof(buf), " {uid=%d class=%s", spr->getUID(),
				cls && cls->name ? cls->name : "?");
			out += buf;
			if (cls && cls->dump)
			{
				StateFieldHash h;
				cls->dump(h, spr);
				snprintf(buf, sizeof(buf), " n=%d", (int)h.fields.size());
				out += buf;
				for (uint64_t v : h.fields)
				{
					snprintf(buf, sizeof(buf), " %016llx", (unsigned long long)v);
					out += buf;
				}
			}
			out += "}";
		}
		out += "]";
	}
	return out;
}

size_t snapshot_count()
{
	return ring.size();
}

int snapshot_oldest_frame()
{
	return ring.empty() ? -1 : ring.front().frame;
}

int snapshot_newest_frame()
{
	return ring.empty() ? -1 : ring.back().frame;
}

size_t snapshot_budget_bytes()
{
	return rewind_budget_bytes();
}

int snapshot_window_seconds()
{
	return rewind_window_seconds();
}

// How much history the ring is actually holding, in seconds of play.
int snapshot_history_seconds()
{
	if (ring.size() < 2)
		return 0;
	return (ring.back().frame - ring.front().frame) / 60;
}

size_t snapshot_total_bytes()
{
	return total_bytes;
}

bool snapshot_request_rewind(int from_frame, int target_frame)
{
	if (!replay_is_active() || target_frame >= from_frame || ring.empty())
	{
		fprintf(stderr, "snapshot rewind: cannot serve frame %d (active=%d, from=%d, ring=%zu)\n",
			target_frame, (int)replay_is_active(), from_frame, ring.size());
		return false;
	}

	// Only worth taking over from re-simulation if there is a snapshot to start from and a
	// recorded hash to verify against. A snapshot at the target itself is the best case: the
	// state at that frame's boundary, which needs no catch-up at all (the deepest rewind a
	// session can do - to the oldest frame it has - lands exactly on one).
	int restored_frame = -1;
	for (auto it = ring.rbegin(); it != ring.rend(); ++it)
	{
		if (it->frame <= target_frame)
		{
			restored_frame = it->frame;
			break;
		}
	}
	if (restored_frame < 0)
	{
		fprintf(stderr, "snapshot rewind: no snapshot before frame %d (ring holds %d..%d)\n",
			target_frame, ring.front().frame, ring.back().frame);
		return false;
	}

	// The first frame at or after the target with a recorded hash (see rewind_verify).
	int verify_frame = -1;
	for (auto& [frame, hash] : frame_hashes)
	{
		if (frame >= target_frame)
		{
			verify_frame = frame;
			break;
		}
	}
	if (verify_frame < 0)
	{
		fprintf(stderr, "snapshot rewind: no recorded hash at or after frame %d (hashes hold %d..%d)\n",
			target_frame, frame_hashes.empty() ? -1 : frame_hashes.front().first,
			frame_hashes.empty() ? -1 : frame_hashes.back().first);
		return false;
	}

	rewind_active = true;
	rewind_restored = false;
	rewind_from = from_frame;
	rewind_target = target_frame;
	rewind_verify = verify_frame;
	return true;
}

void snapshot_set_selftest(int frame, int gap)
{
	selftest_frame = frame;
	selftest_gap = gap;
	selftest_stage = 0;
}

void snapshot_frame_hook()
{
	int frame = replay_get_frame();

	// Verification runs BEFORE this frame's hash is recorded: the ring still holds what the
	// run originally produced for this frame, which is what a rewound trajectory must match.

	// A snapshot rewind that has re-covered its gap finishes here, at the same point in the
	// frame that recorded the hash it is checked against.
	if (rewind_active && rewind_restored && frame >= rewind_verify)
	{
		std::string expected = hash_for_frame(frame, StateHashKind::All);
		std::string now = state_hash_string(StateHashKind::All);
		rewind_active = false;
		rewind_restored = false;

		// The re-cover is over: a recording takes its log back. The frames it rewound past are
		// replaced in the rings as the session plays them again (see snapshot_capture), so there
		// is nothing to throw away here.
		bool recording = replay_record_replay_active() && replay_get_mode() == ReplayMode::Replay;
		replay_end_record_replay(frame);

		if (!expected.empty() && expected == now)
		{
			fprintf(stderr, "snapshot rewind: verified at frame %d (no re-simulation of the prefix)\n", frame);
			// A recording is only checked at the target frame: from there on the player's own
			// input drives the session, so a later difference from the recorded trajectory is
			// expected rather than a failed snapshot.
			if (!recording)
			{
				watch_target = frame;
				watch_until = frame + WATCH_FRAMES;
			}
		}
		else
		{
			fprintf(stderr, "snapshot rewind: state at frame %d does not match (%s vs %s); re-simulating instead\n",
				frame, now.c_str(), expected.empty() ? "no recorded hash" : expected.c_str());
			replay_request_resimulate_to(rewind_target);
		}
		rewind_from = rewind_target = -1;
		rewind_verify = -1;
	}

	// Keep checking the rewound trajectory: if it drifts from what the run originally did,
	// the snapshot did not capture everything that matters and the session is restored the
	// reliable way instead.
	if (watch_until > 0 && frame >= watch_target)
	{
		// The world alone: see StateHashes::world for why progress is not part of the watch.
		std::string expected = hash_for_frame(frame, StateHashKind::World);
		std::string now = state_hash_string(StateHashKind::World);
		if (!expected.empty() && expected != now)
		{
			fprintf(stderr, "snapshot rewind: trajectory drifted at frame %d (%d frames after the target); re-simulating instead\n",
				frame, frame - watch_target);
			replay_request_resimulate_to(watch_target);
			watch_until = -1;
			watch_target = -1;
		}
		else if (frame >= watch_until)
		{
			fprintf(stderr, "snapshot rewind: verified for %d frames past the target\n", frame - watch_target);
			watch_until = -1;
			watch_target = -1;
		}
	}

	// Remember what this frame's state hashed to, so a later snapshot rewind can be checked
	// against it (and only used when it reproduces the trajectory exactly).
	if (replay_is_active() && (capture_interval > 0 || selftest_frame >= 0))
	{
		// Same identity rule as the snapshots: a frame the session passes again replaces the hash
		// it recorded for it, so the ring keeps covering the same window of play.
		FrameHashes hashes{state_hash_string(StateHashKind::All),
			state_hash_string(StateHashKind::World)};
		auto it = std::lower_bound(frame_hashes.begin(), frame_hashes.end(), frame,
			[](const std::pair<int, FrameHashes>& e, int value) { return e.first < value; });
		if (it != frame_hashes.end() && it->first == frame)
			it->second = std::move(hashes);
		else
			frame_hashes.insert(it, std::make_pair(frame, std::move(hashes)));
		while (frame_hashes.size() > frame_hash_capacity())
			frame_hashes.pop_front();
	}

	// Interval capture (the ring the rewind path uses). The state at a frame's hash point is the
	// state at the *end* of that frame, which is the state the frame after it starts from - so a
	// snapshot is labelled with its own frame and a restore resumes at the following one (see
	// snapshot_poll and replay_advance_cursor_to). Capturing here also means every frame is seen,
	// including the ones the screen-transition loops advance. Not while a recording is being
	// re-fed: those frames are the ones the ring already holds.
	// One snapshot every `capture_interval` frames, on an absolute grid: a frame the session
	// passes again after a rewind lands on the same entry (see snapshot_capture), and the schedule
	// needs no bookkeeping to resume after a restore.
	if (capture_interval > 0 && replay_is_active() && !replay_record_replay_active()
		&& frame % capture_interval == 0
		&& rings_belong_to_current_session())
		snapshot_capture(frame);
}

// The self-test runs here - before the frame is simulated - so that a restore is in place
// before any of that frame's simulation happens. Doing it in the per-frame hash hook would
// be one frame late: game_loop() has already run by then.
void snapshot_poll()
{
	// An in-flight snapshot rewind: restore once, then let the loop re-cover the gap. The
	// verification happens in snapshot_frame_hook, at the frame's usual hash point.
	if (rewind_active && !rewind_restored)
	{
		int restored_frame = -1;
		bool rejected = false;
		// A snapshot is the state at a frame boundary, so a restore puts the session at that frame
		// and the frames up to the target are re-covered by simulating them; a snapshot at the
		// target needs no catch-up.
		if (!snapshot_restore_at_or_before(rewind_target, restored_frame, &rejected))
		{
			if (rejected)
				fprintf(stderr, "snapshot rewind: the snapshot at or before frame %d cannot be reproduced (a sprite could not be rebuilt); falling back to re-simulation\n",
					rewind_target);
			else
				fprintf(stderr, "snapshot rewind: no snapshot at or before frame %d; falling back to re-simulation\n",
					rewind_target);
			rewind_active = false;
			replay_request_rewind(rewind_from - rewind_target);
			rewind_from = rewind_target = -1;
			return;
		}
		rewind_restored = true;
		// A restore has to reproduce the state the snapshot was taken from, so it is compared with
		// the hash recorded at that frame right away: if it does not, the re-cover that follows is
		// rebuilding from the wrong state, and saying so here names the frame that lost something
		// instead of leaving it to show up as a mismatch at the target (or, worse, as drift after
		// it). A rejected restore is turned down below like any other.
		{
			std::string at_capture = hash_for_frame(restored_frame, StateHashKind::All);
			std::string now = state_hash_string(StateHashKind::All);
			if (!at_capture.empty() && at_capture != now)
				fprintf(stderr, "snapshot rewind: the restored state does not match frame %d's captured state (%s vs %s)\n",
					restored_frame, now.c_str(), at_capture.c_str());
		}
		// A recording that jumps back continues from the restored frame: the steps it recorded
		// for the frames it rewound past are a future it is no longer following, and the steps
		// it has for the gap between here and the rewound frame are played back to re-cover it
		// (see replay_begin_record_replay).
		if (replay_get_mode() == ReplayMode::Record)
			replay_begin_record_replay(rewind_target);
		int oldest = ring.empty() ? -1 : ring.front().frame;
		int newest = ring.empty() ? -1 : ring.back().frame;
		fprintf(stderr, "snapshot rewind: restored frame %d, re-covering %d frames to %d (ring holds %zu snapshots, %d..%d)\n",
			restored_frame, rewind_target - restored_frame, rewind_target, ring.size(), oldest, newest);
	}

	if (selftest_frame < 0 || !replay_is_active())
		return;

	int frame = replay_get_frame();

	if (selftest_stage == 0 && frame >= selftest_frame)
	{
		size_t bytes = snapshot_capture(frame);
		selftest_hash_at_capture = state_hash_string();
		selftest_hero_at_capture = hero_fields_string();
		selftest_sprites_at_capture = sprite_counts_string();
		fprintf(stderr, "savestate selftest: captured frame %d (%zu bytes, hash %s)\n",
			frame, bytes, selftest_hash_at_capture.c_str());
		selftest_stage = 1;
		return;
	}

	if (selftest_stage == 1 && frame >= selftest_frame + selftest_gap)
	{
		int restored_frame = -1;
		bool ok = snapshot_restore_at_or_before(selftest_frame, restored_frame);
		std::string now = state_hash_string();
		fprintf(stderr, "savestate selftest: restore at frame %d -> snapshot frame %d (%s); hash now %s, at capture %s\n",
			frame, restored_frame, ok ? "ok" : "FAILED", now.c_str(), selftest_hash_at_capture.c_str());
		// The field dumps are only interesting when the round trip is not exact.
		if (now != selftest_hash_at_capture)
		{
			fprintf(stderr, "  hero now:       %s\n  hero captured:  %s\n  sprites now:    %s\n  sprites captured: %s\n",
				hero_fields_string().c_str(), selftest_hero_at_capture.c_str(),
				sprite_counts_string().c_str(), selftest_sprites_at_capture.c_str());
		}
		selftest_stage = 2;
	}
}

// External snapshots are captured/applied only at the main gameplay loop boundary.
void zc255_screen_cache_stats(uint64_t& hits,uint64_t& rebuilds,uint64_t& skips) {
    hits=live_screen_cache_hits; rebuilds=live_screen_cache_rebuilds; skips=live_screen_cache_skips;
}
bool zc255_snapshot_export(std::vector<uint8_t>& data) {
    Snapshot snap;
    // The bridge owns one reusable output vector. Keep its allocation while
    // encoding a completely fresh state; a failed capture exposes no partial blob.
    snap.blob.data.swap(data);
    snap.blob.data.clear();
    snap.frame = replay_get_frame();
    snap.blob.put(SNAPSHOT_FORMAT);
    const bool captured=capture_state(snap) && !snap.blob.failed;
    if(!captured) snap.blob.data.clear();
    data.swap(snap.blob.data);
    return captured;
}
bool zc255_snapshot_import(const uint8_t* data, size_t size) {
    if(!data || size<sizeof(uint32_t)) return false;
    // Byte storage only: every import copies its entire incoming state and resets
    // the parser. The guard prevents any nested call overwriting the active blob.
    static thread_local Snapshot snap;
    static thread_local bool importing=false;
    if(importing) return false;
    struct ImportGuard {
        bool& active;
        explicit ImportGuard(bool& value):active(value) { active=true; }
        ~ImportGuard() { active=false; }
    } guard(importing);
    snap.blob.data.assign(data, data + size);
    snap.blob.pos=0;
    snap.blob.failed=false;
    uint32_t format = 0;
    snap.blob.get(format);
    if (format != SNAPSHOT_FORMAT || !restore_state(snap) || snap.blob.failed) return false;
    rewind_active = rewind_restored = false;
    rewind_verify = watch_target = watch_until = -1;
    replay_rewind_record_log(replay_get_frame());
    snapshot_forget_history();
    return true;
}
