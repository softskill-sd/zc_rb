#include "zc/rewind.h"

#include "base/zapp.h"
#include "zc/control_scheme.h"
#include "zc/replay.h"
#include "zc/savestate_snapshot.h"
#include "zc/zc_sys.h"
#include "zc/zelda.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

// Defined in zelda.cpp, alongside presentbuf (declared in zelda.h).
extern PALETTE* hw_palette;
extern bool update_hw_pal;

// The visible half of rewind: the frames the player just saw, kept so they can be shown again in
// reverse while the Rewind control is held.
//
// This follows the way an emulator does it (RetroArch's rewind), where the ring is a *tape*:
//
//  * one entry per `step` frames - the rewind granularity, which is also how far one step
//    of the rewind goes and therefore how fast holding the control walks back;
//  * frame numbers are the identity of an entry, so a session that passes a frame again (a rewind
//    replays it) replaces that entry instead of adding a second copy. Rewinding therefore never
//    costs history: the tape keeps everything it has, the session just moves along it, and playing
//    on fills the same slots again;
//  * the oldest entry is dropped only when the buffer is full - and the preview never offers a
//    frame older than the oldest *state* the rewind could restore, so "how far back" is always
//    something the session can actually do.
//
// The frame on screen when the control is released is exactly the frame the session is restored
// to, and a hold that runs off the end of the tape stops there (it does not wrap, and it does not
// silently reshuffle what is stored).
namespace
{
	struct StoredFrame
	{
		BITMAP* bitmap = nullptr;
		PALETTE pal = {};
		int frame = -1;
	};

	std::deque<StoredFrame> history;      // ordered by frame, oldest first
	std::vector<BITMAP*> spare_bitmaps;   // recycled, so play does not allocate per frame
	std::string history_session;          // the replay the stored frames belong to

	int capacity = 0;
	int window_seconds = 0;
	int step = 0;            // game frames between stored frames == one step of the rewind

	int shown = -1;          // index of the frame being previewed
	bool previewing = false;
	int previewed_frames = 0;

	// Set by -rewind-bind / -rewind-button; -1 for the button means "not overridden".
	int key_override = 0;
	int btn_override = -1;

	// Set by -rewind-preview-at: the frames to start a hold at, and how many game frames to hold.
	// The switch may be given more than once, which scripts several rewinds in one session.
	std::vector<std::pair<int, int>> preview_at;
	size_t preview_at_next = 0;
	int scripted_hold = 0;
	bool at_oldest_reported = false;

	void binding(int& key, int& btn)
	{
		key = key_override ? key_override : (active_control_scheme ? active_control_scheme->key_rewind : 0);
		btn = btn_override >= 0 ? btn_override : (active_control_scheme ? active_control_scheme->btn_rewind : 0);
	}

	bool feature_enabled()
	{
		int key, btn;
		binding(key, btn);
		return key || btn;
	}

	bool button_held()
	{
		int key, btn;
		binding(key, btn);
		if (!key && !btn)
			return false;
		return (key && zc_getrawkey(key, true)) || joybtn(active_control_scheme ? active_control_scheme->joystick_index : 0, btn);
	}

	void ensure_capacity()
	{
		if (capacity || !presentbuf)
			return;

		window_seconds = zc_get_config("zeldadx", "replay_rewind_seconds", 180);
		if (window_seconds <= 0)
			window_seconds = 180;

		// The rewind granularity: how many game frames one step of the rewind covers. One step is
		// shown per frame the control is held, so this is also the rewind speed - RetroArch's
		// `rewind_granularity`, "the number of frames to rewind per step", whose default is 1
		// (rewinding plays the tape back at the speed it was recorded at; raising it walks back
		// that many times faster, at the cost of landing on a coarser grid).
		step = zc_get_config("zeldadx", "replay_rewind_granularity", 1);
		if (step <= 0)
			step = 1;

		// How many entries the frame half of the window may hold: the same shape as the state
		// ring - a byte budget first (RetroArch sizes its rewind buffer in megabytes for exactly
		// this reason), with the configured window as an upper bound on top of it. Both are
		// reported, so what the settings actually buy is visible.
		int megabytes = zc_get_config("zeldadx", "replay_rewind_video_megabytes", 256);
		if (megabytes <= 0)
			megabytes = 256;
		size_t frame_bytes = (size_t)presentbuf->w * presentbuf->h + sizeof(PALETTE);
		size_t budget = (size_t)megabytes * 1024 * 1024;

		int video_frames = std::max(step, window_seconds * 60);
		capacity = (int)std::min<size_t>((size_t)(video_frames / step), std::max<size_t>(1, budget / frame_bytes));
		if (capacity < 1)
			capacity = 1;
	}

	BITMAP* take_bitmap()
	{
		if (!spare_bitmaps.empty())
		{
			BITMAP* bmp = spare_bitmaps.back();
			spare_bitmaps.pop_back();
			return bmp;
		}
		return create_bitmap_ex(8, presentbuf->w, presentbuf->h);
	}

	void recycle(StoredFrame& f)
	{
		if (!f.bitmap)
			return;
		// The pool only needs to cover the frames in flight, so it does not grow without bound.
		if (spare_bitmaps.size() < 4)
			spare_bitmaps.push_back(f.bitmap);
		else
			destroy_bitmap(f.bitmap);
		f.bitmap = nullptr;
	}

	// Where a frame belongs in the list, by frame number.
	std::deque<StoredFrame>::iterator find_frame(int frame)
	{
		return std::lower_bound(history.begin(), history.end(), frame,
			[](const StoredFrame& f, int value) { return f.frame < value; });
	}

	void present(size_t index)
	{
		StoredFrame& f = history[index];
		if (!f.bitmap || !presentbuf)
			return;

		blit(f.bitmap, presentbuf, 0, 0, 0, 0, presentbuf->w, presentbuf->h);
		memcpy(RAMpal, f.pal, sizeof(PALETTE));
		hw_palette = &RAMpal;
		update_hw_pal = true;
		shown = (int)index;
		if (getenv("ZC_REWIND_TRACE"))
			fprintf(stderr, "rewind: showing frame %d (step %d)\n", f.frame, previewed_frames);
	}

	// Store the frame that was just presented, replacing the entry for that frame if the session
	// has been here before (see the note at the top of the file).
	void remember_frame(int frame)
	{
		auto it = find_frame(frame);
		if (it != history.end() && it->frame == frame)
		{
			StoredFrame& f = *it;
			if (!f.bitmap)
				f.bitmap = take_bitmap();
			blit(presentbuf, f.bitmap, 0, 0, 0, 0, presentbuf->w, presentbuf->h);
			memcpy(f.pal, RAMpal, sizeof(PALETTE));
			return;
		}

		StoredFrame f;
		f.bitmap = take_bitmap();
		blit(presentbuf, f.bitmap, 0, 0, 0, 0, presentbuf->w, presentbuf->h);
		memcpy(f.pal, RAMpal, sizeof(PALETTE));
		f.frame = frame;
		history.insert(it, f);

		while ((int)history.size() > capacity)
		{
			recycle(history.front());
			history.pop_front();
		}
	}

	int span_seconds(int newest, int oldest)
	{
		return newest < 0 || oldest < 0 ? 0 : (newest - oldest) / 60;
	}

	void announce_history(int frame)
	{
		int oldest = history.empty() ? -1 : history.front().frame;
		int newest = history.empty() ? -1 : history.back().frame;
		size_t frame_bytes = presentbuf ? (size_t)presentbuf->w * presentbuf->h + sizeof(PALETTE) : 0;
		fprintf(stderr,
			"rewind: showing history from frame %d (%d frames back from %d to %d = %d seconds; %d states, %d MB, plus %d MB of frames)\n",
			frame, (int)history.size(), oldest, newest, span_seconds(newest, oldest),
			(int)snapshot_count(), (int)(snapshot_total_bytes() / (1024 * 1024)),
			(int)((size_t)history.size() * frame_bytes / (1024 * 1024)));
	}
}

// Which session the stored frames belong to. A live session records itself, and each game (or
// replay) has its own recording, so this changes exactly when the history stops describing the
// play the player is in the middle of.
static std::string current_session()
{
	return replay_get_replay_path().string();
}

void rewind_reset()
{
	for (auto& f : history)
		recycle(f);
	history.clear();
	for (BITMAP* bmp : spare_bitmaps)
		destroy_bitmap(bmp);
	spare_bitmaps.clear();
	history_session.clear();
	capacity = 0;
	shown = -1;
	previewing = false;
	at_oldest_reported = false;
}

void rewind_frame_capture()
{
	// Nothing to show frames with, nothing to restore state from, or history is on screen.
	if (!presentbuf || !replay_is_active() || previewing)
		return;
	if (!feature_enabled())
		return;

	// A player who can rewind gets in-process state snapshots taken as they play, so a rewind
	// restores one instead of re-simulating the replay prefix. The interval is how far a restore
	// has to re-cover by simulating (a handful of frames), and it is what the state ring's memory
	// is spent on: fewer, further-apart states reach further back in the same budget, at the cost
	// of a longer re-cover.
	// The state interval: how far back a rewind has to reach to find an in-process snapshot, and
	// therefore how long a restore spends re-covering that gap. Denser states are more exact (less
	// re-simulation) but buy less history for the same memory.
	static bool snapshots_enabled = false;
	if (!snapshots_enabled)
	{
		snapshots_enabled = true;
		int interval = zc_get_config("zeldadx", "replay_rewind_interval", 10);
		if (interval <= 0)
			interval = 10;
		snapshot_set_interval(interval);
	}

	// A history that describes another session (a new game, another replay) is not this play's past.
	std::string session = current_session();
	if (session != history_session)
	{
		rewind_reset();
		history_session = session;
	}

	ensure_capacity();

	// One entry every `step` frames, on an absolute grid: a frame the session passes again
	// after a rewind lands on the same entry instead of adding a second copy.
	int frame = replay_get_frame();
	if (frame % step == 0)
		remember_frame(frame);

	// The frame ring is clipped to what the state ring can serve: the oldest frame the preview
	// offers always has a state at or before it, so a rewind to it is a restore and not a
	// re-simulation. The oldest entries go first, exactly as the state ring drops its own.
	int oldest_state = snapshot_oldest_frame();
	while (!history.empty() && oldest_state >= 0 && history.front().frame < oldest_state)
	{
		recycle(history.front());
		history.pop_front();
		shown = -1;
	}
	while ((int)history.size() > capacity)
	{
		recycle(history.front());
		history.pop_front();
		shown = -1;
	}

	static bool announced = false;
	if (!announced)
	{
		announced = true;
		size_t frame_bytes = (size_t)presentbuf->w * presentbuf->h + sizeof(PALETTE);
		int seconds = capacity * step / 60;
		fprintf(stderr,
			"rewind: ready - a step is %d frame%s, a state every %d frames, so holding can play back up to %d seconds at %dx\n"
			"rewind: budget %d MB of states and %d MB of frames\n",
			step, step == 1 ? "" : "s", snapshot_interval(), seconds, step,
			(int)(snapshot_budget_bytes() / (1024 * 1024)),
			(int)((size_t)capacity * frame_bytes / (1024 * 1024)));
	}
}

bool rewind_is_previewing()
{
	return previewing;
}

int rewind_preview_frame()
{
	return shown < 0 || (size_t)shown >= history.size() ? -1 : history[shown].frame;
}

bool rewind_poll()
{
	// Rewinding restores the session by re-simulating it, which needs a replay to replay.
	if (!replay_is_active())
	{
		previewing = false;
		return false;
	}

	// A recording is in the middle of re-covering a rewind's gap by replaying its own steps; the
	// control is ignored until that is done (a press now is handled by a later frame).
	if (replay_record_replay_active())
	{
		previewing = false;
		return false;
	}

	ensure_capacity();

	// Instrumentation: start a hold at a given frame. The hold is given in game frames of history,
	// and a held frame walks `step` of them, so it is that many steps.
	while (preview_at_next < preview_at.size()
		&& replay_get_frame() >= preview_at[preview_at_next].first)
	{
		// The hold is given in game frames of history, and a held frame walks `step` of
		// them, so that is how many steps to take.
		scripted_hold = std::max(1, preview_at[preview_at_next].second / step);
		preview_at_next++;
	}

	// While history is being shown the game loop is frozen (`rewind_poll` returns true and the
	// caller continues the loop), so nothing would refresh the keyboard state this binding is
	// read from: the key would stay "down" forever and the release would never be seen. Poll the
	// input here so a release (and a second press) is noticed.
	if (previewing && !is_headless())
	{
		poll_keyboard();
		update_keys();
		poll_joystick();
		// The game loop is skipped while history is shown, and that is what normally pumps the
		// window's events - without this the window would stop responding.
		zc_process_display_events();
	}

	bool held = button_held();
	if (scripted_hold > 0)
		held = true;

	if (!previewing)
	{
		if (!held || history.empty())
			return false;

		// Start on the newest stored frame: a tap rewinds to it, and holding walks back from it.
		previewing = true;
		previewed_frames = 0;
		at_oldest_reported = false;
		announce_history(replay_get_frame());
	}

	if (held)
	{
		// One step per held frame: back `step` game frames, which is exactly how far the
		// step is (RetroArch's rewind granularity).
		if (shown < 0 || (size_t)shown >= history.size())
			shown = (int)history.size() - 1;
		if (shown > 0)
		{
			shown--;
			previewed_frames++;
			present((size_t)shown);
		}
		else if (!at_oldest_reported)
		{
			// The end of the tape: the session holds the oldest frame it has a state for, and
			// stays there until the control is released (emulators say the same thing rather than
			// wrapping or shuffling what they store).
			at_oldest_reported = true;
			int back = replay_get_frame() - history.front().frame;
			fprintf(stderr, "rewind: at the oldest stored frame %d (%d frames back); release to stay there\n",
				history.front().frame, back);
		}
		if (scripted_hold > 0)
			scripted_hold--;
		update_hw_screen();
		return true;
	}

	// Released: restore the session to the frame on screen, then play on from there.
	previewing = false;
	int target = rewind_preview_frame();
	shown = -1;
	if (target >= 0 && replay_is_active())
	{
		int frames_back = replay_get_frame() - target;
		if (frames_back > 0)
		{
			fprintf(stderr, "rewind: returning to frame %d (%d frames back, %d shown)\n",
				target, frames_back, previewed_frames);
			// Prefer an in-process snapshot: restore it, re-cover the gap and verify against the
			// recorded state hash. Only when that is not possible (or does not verify, which the
			// snapshot side reports) does the engine re-simulate the replay.
			//
			// The restore itself is applied at the top of the next iteration, where the loop
			// always applies one - doing it here would move it in front of this frame's input
			// poll and shift the replay cursor by a frame. What keeps the frame the player
			// rewound to on screen in the meantime is `snapshot_rewind_catching_up()`: while a
			// rewind is in flight nothing is drawn, so the position the rewind left never
			// flashes before the restored state arrives.
			// The frame after the release is still played, and only then is the state put back.
			// Skipping it looks tidier - it is a frame whose state is about to be thrown away - but
			// it also skips the frame's input poll, and the replay cursor then resumes one step
			// behind: the re-cover consumes the wrong input steps and verification fails (measured:
			// ganon and pkmn stop verifying with the frame skipped, and pass with it played).
			if (!snapshot_request_rewind(replay_get_frame(), target))
				replay_request_rewind(frames_back);
		}
	}
	return false;
}

void rewind_set_binding_override(int key, int button)
{
	key_override = key;
	btn_override = button;
}

bool rewind_control_bound()
{
	return feature_enabled();
}

void rewind_set_preview_at_frame(int frame, int hold)
{
	preview_at.emplace_back(frame, hold);
}
