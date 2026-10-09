#ifndef ZC_SAVESTATE_H_
#define ZC_SAVESTATE_H_

#include <filesystem>
#include <string>

// Savestates for the player (`zplayer`).
//
// ZC replays are deterministic: a whole session is reproduced frame-by-frame from the
// recorded inputs and RNG seeds. A savestate therefore does not need to serialize the
// game's memory - it is an *anchor* into that deterministic timeline:
//
//     savestate = (replay file, frame)
//
// Saving writes that anchor, plus a hash of the game state at that frame. Loading
// re-simulates the replay from the beginning up to the anchor frame, which restores the
// state, and then checks that the restored state hashes the same as when it was saved.
// Rewinding is the same operation with an earlier frame (see -rewind).
//
// The tradeoff is that restoring costs a re-simulation of the replay prefix rather than
// a memory copy - the state is recovered, not stored. This requires the session to be
// recorded or replayed: a savestate can only be taken while a replay is active.
//
// See docs/replays.md.

constexpr int SAVESTATE_VERSION = 1;

struct SaveState
{
	int version = SAVESTATE_VERSION;
	// Replay file this anchor points into. Either absolute, or relative to the
	// savestate file's directory.
	std::string replay;
	// Quest file named by the replay's metadata (informational only).
	std::string quest;
	// The frame to re-simulate to in order to restore this state.
	int frame = 0;
	// Hash of the game state at `frame` when this savestate was written, or "" if the
	// savestate was written without one (then loading it cannot check the state).
	std::string state_hash;
	std::string zc_version;
	std::string created;

	// Resolve `replay` against `savestate_path`'s directory, returning the first
	// candidate that exists (or the path as-is when none do).
	std::filesystem::path resolved_replay_path(const std::filesystem::path& savestate_path) const;
};

bool savestate_write(const std::filesystem::path& path, const SaveState& state, std::string& err);
bool savestate_read(const std::filesystem::path& path, SaveState& state, std::string& err);
std::string savestate_describe(const SaveState& state);

// --- Wiring into the player -------------------------------------------------

// Request a savestate for this run. It is written at `frame`, or -1 for "at the frame the
// run is on when it stops". Does nothing (and reports it) if the run ends first.
void savestate_request_save(const std::filesystem::path& path, int frame = -1);

// Ask for a restored savestate to be checked: when the run reaches `frame`, the state hash
// there is compared against `expected_hash` (skipped when empty). A mismatch means the
// restored state is not the state that was saved, and fails the run.
void savestate_request_verify(int frame, const std::string& expected_hash);

// Called once per frame by the player, next to dump_state_hashes().
void savestate_frame_hook();

// Called by the replay system when a run stops, to finish a requested save.
void savestate_on_replay_stop();

#endif
