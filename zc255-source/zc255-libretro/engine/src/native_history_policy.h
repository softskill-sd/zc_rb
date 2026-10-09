#pragma once
#include <cstdint>
namespace zc255_native_history {
bool enabled();
void frame_capture();
void poll_snapshot();
void snapshot_hook();
bool poll_rewind();
void note_export(bool success);
struct Stats {
    uint64_t enabled, frame_capture_calls, frame_capture_skips;
    uint64_t snapshot_poll_calls, snapshot_poll_skips;
    uint64_t snapshot_hook_calls, snapshot_hook_skips;
    uint64_t rewind_poll_calls, rewind_poll_skips;
    uint64_t external_export_attempts, external_export_successes;
    uint64_t native_state_count, native_state_bytes;
    uint64_t default_rng_hash, drunk_rng_hash, script_rng_hash;
};
}
extern "C" __declspec(dllexport) bool zc255_native_history_stats(zc255_native_history::Stats*,uint32_t bytes);
