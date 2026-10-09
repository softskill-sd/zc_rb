#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class sprite;

// In-process state snapshots: the foundation for instant rewind.
//
// A snapshot is a blob describing the game state, restored into the live objects without
// re-simulating the replay prefix.
//
// Sprites are captured by value - the concrete class, the UID, and their whole object's bytes -
// so a restore can put the state back into a sprite that is still alive or rebuild one the game
// has since deleted. Nothing about a snapshot depends on the game having kept an object around,
// which is what keeps a rewind instant after something in the world has died. The bytes are the
// object as it is in *this* process, so the class's vtable and anything it points at in quest data
// come back with them; the few pointers a sprite can hold to another sprite (and the hero's two)
// are rebuilt from the addresses the capture recorded.
//
// Captured exactly today: hero position/direction, the script and default RNGs, globalRAM and
// objectRAM, the viewport, the FFC position arrays, screen indices, scroll globals, and the
// replay cursor, plus every sprite in the seven world lists. NOT captured yet: the ZScript VM
// (engines, stacks, object registries) - a scripted sprite that died has lost its engine data, so
// a snapshot that would need it is verified and, if it diverges, taken down in favour of
// re-simulation - and combos/palette/subscreen/messages and audio position.

// Capture a snapshot every `frames` frames (0 turns it off). Used by the rewind path and by
// the self-test.
void snapshot_set_interval(int frames);

// The interval captures are taken at (0 when they are off).
int snapshot_interval();
bool snapshot_capture_enabled();

// Capture the current state as a snapshot anchored at `frame`. Returns the blob size.
size_t snapshot_capture(int frame);

// Restore the newest snapshot at or before `frame`. Returns false when there is none;
// `restored_frame` is set to the snapshot's frame on success. `rejected` (optional) is set when
// a snapshot was there but this run cannot reproduce it - nothing was changed in that case.
bool snapshot_restore_at_or_before(int frame, int& restored_frame, bool* rejected = nullptr);

// Restore the newest snapshot there is, whatever frame it is for.
bool snapshot_restore_latest(int& restored_frame);

// Debugging: every sprite in every list, with the hash of every field its class's state
// description covers. Written once per frame to the file named by ZC_SPRITE_FIELD_LOG (frames
// limited by ZC_SPRITE_FIELD_FRAMES); diffing a baseline run against a rewound one names the field
// that failed to restore.
std::string snapshot_sprite_fields_line();

// A hash over the quest-progress state a snapshot carries (hearts, items, counters, keys, maps,
// switch states, script data): the part of the state that says what the player has earned or
// lost. It is hashed as the exact bytes a snapshot stores, so a restore that cannot reproduce
// them fails its check instead of playing on with the wrong inventory.
uint64_t snapshot_gamedata_hash();

void snapshot_reset();
size_t snapshot_count();
size_t snapshot_total_bytes();

// What the ring is holding, and what it is allowed to hold: the newest and oldest frames it can
// restore, the byte budget, the configured window (seconds) and the history actually held
// (seconds). The preview clips itself to the oldest frame - a rewind can only go as far back as
// there is a state for.
int snapshot_oldest_frame();
int snapshot_newest_frame();
size_t snapshot_budget_bytes();
int snapshot_window_seconds();
int snapshot_history_seconds();

// Forget every snapshot and per-frame hash. The hashes are what a rewind verifies against, so
// they are only good for a trajectory the session actually followed: after a rewind that
// rebuilds the session (the reload fallback), the ones from before it describe a pass whose
// state may no longer be the one the session will produce.
void snapshot_forget_history();

// Instrumentation (-savestate-selftest <frame> <gap>): capture at `frame`, continue, restore
// at `frame + gap`, then let the run continue over the same frames again. With
// -state-hash-log the two passes can be compared frame by frame to see exactly which regions
// a snapshot reproduces.
void snapshot_set_selftest(int frame, int gap);

// Called once per frame by the player, next to dump_state_hashes(): the per-frame hash ring, the
// interval snapshots, and the verification of a rewind that has finished re-covering its gap.
void snapshot_frame_hook();

// Called once per iteration of the game loop, before the frame is simulated, so a restore
// takes effect before any of that frame's simulation runs (and so that the interval captures
// are the state a frame *starts* from).
void snapshot_poll();

// True while the engine is re-simulating to reach a frame a rewind or a restored savestate asked
// for. Those frames are the same trajectory being reproduced, not something to watch, so the
// player keeps the frame that is already on screen instead (see update_hw_screen in zelda.cpp).
bool snapshot_rewind_catching_up();

// Ask for a rewind from `from_frame` back to `target_frame` using an in-process snapshot:
// the newest snapshot at or before the target is restored, the gap is re-covered by
// simulating, and the state hash at the target is compared against the hash recorded when the
// run first passed it. Returns true if the snapshot path took over; if it later fails to
// verify, the caller's fallback (a full re-simulation rewind) is requested automatically.
bool snapshot_request_rewind(int from_frame, int target_frame);
