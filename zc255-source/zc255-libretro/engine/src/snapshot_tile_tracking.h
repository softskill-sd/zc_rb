#pragma once
#include "base/zdefs.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <algorithm>

// Engine-thread only. Generations select complete cached encodings; they are
// never saved, and restores/quest loads invalidate every cached generation.
namespace snapshot_tile_tracking {
constexpr size_t block_records=128;
constexpr size_t blocks=(NEWMAXTILES+block_records-1)/block_records;
struct Tracker {
    std::array<uint64_t,blocks> versions{};
    uint64_t epoch=1,skipped=0,checked=0,missed=0;
    bool trusted=false;
};
inline Tracker& state() { static Tracker value;return value; }
inline bool install_engine_hooks() {state().trusted=true;return true;}
inline void invalidate_all() {
    auto& s=state();
    if(++s.epoch==0) {s.epoch=1;s.versions.fill(0);s.trusted=false;}
}
inline void mark(int32_t tile) {
    if(tile<0 || size_t(tile)>=NEWMAXTILES) {invalidate_all();return;}
    auto& value=state().versions[size_t(tile)/block_records];
    if(++value==0) invalidate_all();
}
inline bool audit() {
    static const bool enabled=[] {auto p=std::getenv("ZC255_AUDIT_TILE_TRACKING");return p && *p && *p!='0';}();
    return enabled;
}
inline void missed_write(size_t block) {
    auto& s=state();++s.missed;s.trusted=false;
    std::fprintf(stderr,"tile tracking: missed write in block %zu; full comparison retained\n",block);
}
}
