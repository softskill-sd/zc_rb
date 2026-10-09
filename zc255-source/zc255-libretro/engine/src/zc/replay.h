#ifndef _REPLAY_H_
#define _REPLAY_H_

#include "base/random.h"
#include "zc/cheats.h"
#include <string>
#include <filesystem>

// Records the button input state, RNG seeds, and interesting events to a .zplay file,
// and play it back while optionally asserting that everything happens the same way
// on every frame.
//
// See docs/replays.md for an overview.
//
// See tests/run_replay_tests.py for the test runner, and instructions for the CLI.

const std::string REPLAY_EXTENSION = ".zplay";

enum ReplayMode
{
    Off,
    Replay,
    Record,
    Assert,
    Update,
    ManualTakeover,
};

std::string replay_mode_to_string(ReplayMode mode);
std::map<std::string, std::string> replay_load_meta(std::filesystem::path path);
// Returns false if a replay file could not be loaded (missing/unreadable/malformed),
// in which case replay mode is left Off.
bool replay_start(ReplayMode mode_, std::filesystem::path path, int frame);
void replay_continue(std::filesystem::path path);
void replay_poll();
bool replay_add_snapshot_frame(std::string frames_shorthand);
void replay_peek_quit();
void replay_peek_input();
void replay_do_cheats();
void replay_do_qrs();
bool replay_is_assert_done();
void replay_forget_input();
void replay_stop(bool aborted = false);
void replay_quit();
void replay_save();
void replay_save(std::filesystem::path path);
void replay_stop_manual_takeover();

void replay_step_comment(std::string comment);
void replay_step_comment_loadscr(int screen_index);
void replay_step_gfx(uint32_t gfx_hash);
void replay_step_quit(int quit_state);
void replay_step_cheat(Cheat cheat, int arg1, int arg2, std::string arg3);
void replay_step_qr(int qr, bool value);
void replay_step_meta(std::string key, std::string value);

void replay_set_meta(std::string key, std::string value);
void replay_set_meta(std::string key, int value);
void replay_set_meta_bool(std::string key, bool value);
void replay_delete_meta(std::string key);
std::string replay_get_meta_str(std::string key);
std::string replay_get_meta_str(std::string key, std::string defaultValue);
int replay_get_meta_int(std::string key);
int replay_get_meta_int(std::string key, int defaultValue);
bool replay_get_meta_bool(std::string key);
bool replay_has_meta(std::string key);

ReplayMode replay_get_mode();
int replay_get_version();
struct ZCVersionCreated
{
	bool well_formed;
	int major, minor, patch;
};
ZCVersionCreated replay_get_zc_version_created();
// Return true if replay mode is active, and the current version is between [min/max) (inclusive/exclusive).
// If `max` is -1, returns true for all versions greater than min; and when replay mode is not active.
bool replay_version_check(int min, int max = -1);
int replay_get_frame();
void replay_set_output_dir(std::filesystem::path dir);
std::filesystem::path replay_get_replay_path();
std::string replay_get_buttons_string();
bool replay_is_active();
void replay_set_debug(bool enable_debug);
bool replay_is_debug();
void replay_set_snapshot_all_frames(bool snapshot_all_frames);
bool replay_is_snapshot_all_frames();
void replay_set_sync_rng(bool enable);
bool replay_is_replaying();
bool replay_is_recording();
void replay_set_frame_arg(int frame);
void replay_enable_exit_when_done();

size_t replay_register_rng(zc_randgen *rng);
void replay_set_rng_seed(zc_randgen *rng, int seed);
void replay_sync_rng();

int replay_get_mouse(int index);
void replay_set_mouse(int index, int value);

enum ReplayStateType
{
	MusicPosition = 1,
	SfxPosition,
};

int replay_get_state(ReplayStateType state_type, int fn());

// --- Savestates and rewind --------------------------------------------------
// A savestate is a (replay, frame) anchor, so restoring one means re-simulating that
// replay up to the anchor frame. See zc/savestate.h and docs/replays.md.

// The frame a restored or rewound run begins at. Frames before it are still replayed -
// that is what reproduces the state - but their asserts and gfx comparisons are skipped,
// since a restored state can only be checked from the anchor onwards. -1 (the default)
// means "verify from frame 0".
void replay_set_anchor_frame(int frame);
int replay_get_anchor_frame();

// Rewind `frames` frames when the run next stops, then continue replaying to the end of
// the replay - or, if `stop_at_frame`, hand control back at the rewound frame instead.
// Only the first stop rewinds; a process rewinds once.
void replay_set_rewind(int frames, bool stop_at_frame);
bool replay_rewind_is_pending();

// Rewind requested by the player, via the Rewind binding in the control scheme. Applied on
// the next frame; unlike -rewind this can be requested any number of times in a session.
// In a recording, the recording is rewritten so that it continues from the rewound frame.
void replay_request_rewind(int frames);
// A rewind the player asked for that has not been applied yet (see replay.cpp).
bool replay_rewind_requested();

// How many times each registered RNG has been seeded so far this frame. A seed request is matched
// against the recorded RngReplayStep by that count, so a state restore has to put it back exactly:
// leaving the counts the frames before the rewind left behind makes the next screen load ask for a
// seed the recording has no step for, which the replay system reports as an rng desync.
std::vector<std::pair<int, int>> replay_get_rng_seed_counts();
void replay_set_rng_seed_counts(const std::vector<std::pair<int, int>>& counts);

// Re-simulate the replay up to `frame` the reliable way (reload + replay the prefix), without
// moving away from that frame: used when a restored state snapshot did not verify and the
// trajectory has to be rebuilt. In a recording, recording resumes at `frame`.
bool replay_request_resimulate_to(int frame);

// How far the Rewind binding rewinds by default, in frames (5 seconds at 60fps). Overridden
// by the `replay_rewind_frames` config, or -rewind-frames.
constexpr int DEFAULT_REWIND_FRAMES = 300;

// Instrumentation (-rewind-at): request an interactive rewind when the run reaches `frame`.
// May be called more than once; the rewinds happen in the order they were added. Lets the
// Rewind control's code path be driven from a headless test.
void replay_add_rewind_at_frame(int frame, int frames);

// Report that a restored savestate did not reproduce its saved state, and stop the run.
void replay_fail_savestate_restore(const std::string& error);

// Where the replay is in its input log, so an in-process state snapshot can be restored and
// the run continue from the same step (see zc/savestate_snapshot.cpp).
struct ReplayCursorState
{
	int frame = 0;
	size_t input_index = 0;
	size_t assert_index = 0;
	size_t quit_index = 0;
	size_t state_index = 0;
	// How many screen loads this session has been through. The assert/update modes compare a load
	// against the frame the recording expects it on, so a session that resumes from a snapshot has
	// to carry on counting from where the recorded run was.
	int loadscr_count = 0;
};
ReplayCursorState replay_get_cursor();
void replay_set_cursor(const ReplayCursorState& cursor);

// Move the frame counter and the log cursors to `frame` without touching the game state. A
// restored snapshot is the end of its own frame, so the run resumes with the frame after it.
void replay_advance_cursor_to(int frame);

// Put the held input back after a snapshot restore: the replay's steps up to `frame` decide
// which buttons are down, which keys are held, the key map and the mouse state. No-op for a
// recording (its input is read live) and while a recording is being re-fed.
void replay_restore_input_state(int frame);

// A recording that was rewound to `frame` continues from there, so the steps it recorded for
// the frames it rewound past describe a future it is no longer following and are dropped.
// No-op unless the session is a recording.
void replay_rewind_record_log(int frame);

// An in-process rewind of a recording restores a state snapshot and then has to reproduce the
// frames between the restored snapshot and the frame the player rewound to. The recording holds
// the steps for those frames itself, so they are played back exactly as replaying the file
// would (the live controls are ignored) until `until_frame` is reached, at which point the
// recording takes its log back, drops the future it rewound past, and continues from there.
void replay_begin_record_replay(int until_frame);
void replay_end_record_replay(int frame);
bool replay_record_replay_active();

#endif
