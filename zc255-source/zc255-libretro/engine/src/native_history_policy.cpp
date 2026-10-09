#include "native_history_policy.h"
#include "engine_host.h"
#include "zc/rewind.h"
#include "zc/savestate_snapshot.h"
#include "zc/zelda.h"
#include "zc/ffscript.h"
#include "base/random.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
extern zc_randgen* zc_get_drunk_rng();
extern zc_randgen script_rnggens[MAX_USER_RNGS];
namespace zc255_native_history {
namespace {
std::atomic<uint64_t> frame_calls{0},frame_skips{0},poll_calls{0},poll_skips{0};
std::atomic<uint64_t> hook_calls{0},hook_skips{0},rewind_calls{0},rewind_skips{0};
std::atomic<uint64_t> export_calls{0},export_successes{0};
void count(std::atomic<uint64_t>& value) { value.fetch_add(1,std::memory_order_relaxed); }
uint64_t read(const std::atomic<uint64_t>& value) { return value.load(std::memory_order_relaxed); }
uint64_t hash(const void* data,size_t bytes) {
    auto ptr=static_cast<const uint8_t*>(data); uint64_t result=1469598103934665603ULL;
    for(size_t i=0;i<bytes;++i) { result^=ptr[i]; result*=1099511628211ULL; }
    return result;
}
}
bool enabled() {
    return zc255_host::frontend_rewind_only();
}
void frame_capture() { count(frame_calls); if(enabled()) count(frame_skips); else ::rewind_frame_capture(); }
void poll_snapshot() { count(poll_calls); if(enabled()) count(poll_skips); else ::snapshot_poll(); }
void snapshot_hook() { count(hook_calls); if(enabled()) count(hook_skips); else ::snapshot_frame_hook(); }
bool poll_rewind() { count(rewind_calls); if(enabled()) { count(rewind_skips); return false; } return ::rewind_poll(); }
void note_export(bool success) { count(export_calls); if(success) count(export_successes); }
Stats statistics() {
    // Native ring values are read only. Query this export at a paused command
    // boundary, never concurrently with engine simulation/restore.
    return {uint64_t(enabled()),read(frame_calls),read(frame_skips),read(poll_calls),read(poll_skips),
        read(hook_calls),read(hook_skips),read(rewind_calls),read(rewind_skips),
        read(export_calls),read(export_successes),uint64_t(::snapshot_count()),uint64_t(::snapshot_total_bytes()),
        hash(zc_get_default_rand(),sizeof(zc_randgen)),zc_get_drunk_rng()?hash(zc_get_drunk_rng(),sizeof(zc_randgen)):0,
        hash(script_rnggens,sizeof(script_rnggens))};
}
}
extern "C" __declspec(dllexport) bool zc255_native_history_stats(zc255_native_history::Stats* out,uint32_t bytes) {
    if(!out || bytes!=sizeof(*out)) return false;
    *out=zc255_native_history::statistics(); return true;
}
