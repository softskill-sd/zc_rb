#include "zc/savestate.h"

#include "base/version.h"
#include "zc/replay.h"
#include "zc/zelda.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

static std::string now_string()
{
	std::time_t t = std::time(nullptr);
	std::tm tm{};
#ifdef _WIN32
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
	return buf;
}

static void trim(std::string& s)
{
	size_t start = s.find_first_not_of(" \t\r\n");
	size_t end = s.find_last_not_of(" \t\r\n");
	s = start == std::string::npos ? "" : s.substr(start, end - start + 1);
}

std::filesystem::path SaveState::resolved_replay_path(const std::filesystem::path& savestate_path) const
{
	std::filesystem::path replay_path(replay);
	if (replay_path.is_absolute() || std::filesystem::exists(replay_path))
		return replay_path;

	// Savestates are usually written next to (or near) the replay they point into.
	std::filesystem::path sibling = savestate_path.parent_path() / replay_path;
	if (std::filesystem::exists(sibling))
		return sibling;

	return replay_path;
}

bool savestate_write(const std::filesystem::path& path, const SaveState& state, std::string& err)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open())
	{
		err = "could not open file for writing";
		return false;
	}

	out << "zsav " << state.version << "\n";
	out << "zc_version " << state.zc_version << "\n";
	out << "replay " << state.replay << "\n";
	out << "quest " << state.quest << "\n";
	out << "frame " << state.frame << "\n";
	out << "state_hash " << state.state_hash << "\n";
	out << "created " << state.created << "\n";

	if (!out.good())
	{
		err = "failed while writing";
		return false;
	}
	return true;
}

bool savestate_read(const std::filesystem::path& path, SaveState& state, std::string& err)
{
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
	{
		err = "could not open file";
		return false;
	}

	bool saw_header = false;
	bool saw_frame = false;
	std::string line;
	int line_number = 0;
	while (std::getline(in, line))
	{
		line_number++;
		trim(line);
		if (line.empty() || line[0] == '#')
			continue;

		std::istringstream ss(line);
		std::string key;
		ss >> key;

		if (!saw_header)
		{
			int version = 0;
			if (key != "zsav" || !(ss >> version))
			{
				err = "not a savestate file (missing 'zsav' header)";
				return false;
			}
			if (version > SAVESTATE_VERSION)
			{
				err = "savestate version " + std::to_string(version) + " is newer than this build supports";
				return false;
			}
			state.version = version;
			saw_header = true;
			continue;
		}

		std::string value;
		std::getline(ss, value);
		trim(value);

		if (key == "replay")
			state.replay = value;
		else if (key == "quest")
			state.quest = value;
		else if (key == "state_hash")
			state.state_hash = value;
		else if (key == "zc_version")
			state.zc_version = value;
		else if (key == "created")
			state.created = value;
		else if (key == "frame")
		{
			try
			{
				state.frame = std::stoi(value);
			}
			catch (...)
			{
				err = "invalid frame on line " + std::to_string(line_number);
				return false;
			}
			if (state.frame < 0)
			{
				err = "frame must not be negative";
				return false;
			}
			saw_frame = true;
		}
		// Unknown keys are ignored, so newer savestates stay loadable by older builds.
	}

	if (!saw_header)
	{
		err = "empty or malformed savestate file";
		return false;
	}
	if (!saw_frame)
	{
		err = "savestate has no frame";
		return false;
	}
	if (state.replay.empty())
	{
		err = "savestate has no replay";
		return false;
	}
	return true;
}

std::string savestate_describe(const SaveState& state)
{
	std::string out = "savestate v" + std::to_string(state.version);
	out += "\n  replay:   " + state.replay;
	if (!state.quest.empty())
		out += "\n  quest:    " + state.quest;
	out += "\n  frame:    " + std::to_string(state.frame);
	if (!state.state_hash.empty())
		out += "\n  hash:     " + state.state_hash;
	if (!state.zc_version.empty())
		out += "\n  zc:       " + state.zc_version;
	if (!state.created.empty())
		out += "\n  created:  " + state.created;
	return out;
}

static bool savestate_write_for_run(const std::filesystem::path& path, int frame,
	const std::string& state_hash, std::string& err)
{
	SaveState state;
	state.zc_version = getVersionString();
	state.replay = replay_get_replay_path().string();
	state.quest = replay_get_meta_str("qst");
	state.frame = frame;
	state.state_hash = state_hash;
	state.created = now_string();

	if (state.replay.empty())
	{
		err = "the replay has no file path yet";
		return false;
	}

	return savestate_write(path, state, err);
}

// --- Wiring into the player -------------------------------------------------

struct PendingSave
{
	std::filesystem::path path;
	int frame = -1;
	bool pending = false;
	bool done = false;
	// The most recent frame and state hash seen by savestate_frame_hook(), used when a
	// savestate is requested for "the frame the run is on when it stops".
	int last_frame = -1;
	std::string last_hash;
};

static PendingSave g_save;
static bool g_verify_pending = false;
static bool g_verify_done = false;
static int g_verify_frame = -1;
static std::string g_verify_hash;

void savestate_request_save(const std::filesystem::path& path, int frame)
{
	g_save.path = path;
	g_save.frame = frame;
	g_save.pending = true;
	g_save.done = false;
}

void savestate_request_verify(int frame, const std::string& expected_hash)
{
	g_verify_pending = true;
	g_verify_done = false;
	g_verify_frame = frame;
	g_verify_hash = expected_hash;
}

static void do_save(int frame, const std::string& state_hash)
{
	g_save.done = true;
	std::string err;
	if (!savestate_write_for_run(g_save.path, frame, state_hash, err))
		fprintf(stderr, "failed to write savestate %s: %s\n", g_save.path.string().c_str(), err.c_str());
	else
		fprintf(stderr, "savestate written: %s (frame %d, hash %s)\n",
			g_save.path.string().c_str(), frame, state_hash.empty() ? "-" : state_hash.c_str());
}

void savestate_frame_hook()
{
	if (!replay_is_active())
		return;

	// Nothing to do unless a save or a verification is in flight; hashing the game state
	// every frame is not free, so it is only done when it is actually needed.
	bool saving_now = g_save.pending && !g_save.done;
	bool verifying = g_verify_pending && !g_verify_done;
	if (!saving_now && !verifying)
		return;

	int frame = replay_get_frame();
	std::string hash = state_hash_string();

	g_save.last_frame = frame;
	g_save.last_hash = hash;

	if (saving_now && g_save.frame != -1 && frame == g_save.frame)
		do_save(frame, hash);

	if (verifying && frame == g_verify_frame)
	{
		g_verify_done = true;
		if (!g_verify_hash.empty() && hash != g_verify_hash)
			replay_fail_savestate_restore(
				"savestate restore mismatch at frame " + std::to_string(frame) +
				": expected state hash " + g_verify_hash + " but got " + (hash.empty() ? "-" : hash));
		else
			fprintf(stderr, "savestate restored: frame %d matches saved state (hash %s)\n",
				frame, hash.empty() ? "-" : hash.c_str());
	}
}

void savestate_on_replay_stop()
{
	if (!g_save.pending || g_save.done)
		return;

	g_save.done = true;

	// Nothing to anchor at.
	if (g_save.last_frame == -1)
	{
		fprintf(stderr, "savestate not written: no frame was observed to anchor it at\n");
		return;
	}

	// A savestate for a specific frame is usually written by savestate_frame_hook() when the
	// run reaches it. A run can stop just short of that frame (the stop is handled before
	// that frame's hook), so fall back to the last frame seen - the hash stored is the hash
	// of that same point in a frame, which is what a restore is checked against.
	if (g_save.frame != -1 && g_save.last_frame != g_save.frame)
		fprintf(stderr, "requested savestate frame %d; anchoring at the last frame reached (%d)\n",
			g_save.frame, g_save.last_frame);

	do_save(g_save.last_frame, g_save.last_hash);
}
