#pragma once
#include "base/mapscr.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
// Engine-thread generations. Groups follow the quest's MAPSCRS screen layout.
// Script map resolution marks its concrete map group. Temporary collision
// screens retain their complete encoding and never share this tracker.
namespace snapshot_map_tracking {
struct Tracker {
    std::array<uint64_t,MAXMAPS> versions{};
    uint64_t epoch=1,skipped=0,checked=0,missed=0;
    bool trusted=false;
};
inline Tracker& state() {static Tracker value;return value;}
inline bool install_engine_hooks() {state().trusted=true;return true;}
inline void invalidate_all() {auto& s=state();if(++s.epoch==0){s.epoch=1;s.versions.fill(0);s.trusted=false;}}
inline void mark(size_t index) {auto& s=state();if(index/MAPSCRS>=s.versions.size()){invalidate_all();return;}if(++s.versions[index/MAPSCRS]==0)invalidate_all();}
inline uint64_t version(size_t index) {return index/MAPSCRS<state().versions.size()?state().versions[index/MAPSCRS]:0;}
inline mapscr& access(size_t index) {mark(index);return TheMaps[index];}
inline void mark_pointer(const mapscr* screen) {
    auto first=reinterpret_cast<uintptr_t>(TheMaps.data()),ptr=reinterpret_cast<uintptr_t>(screen);
    if(ptr>=first&&ptr-first<TheMaps.size()*sizeof(mapscr)&&(ptr-first)%sizeof(mapscr)==0)mark((ptr-first)/sizeof(mapscr));
}
// A diagnostic fallback can retain the original whole-quest invalidation.
// Mapdata getter/setter resolution marks its concrete screen/map group.
inline bool conservative_script_invalidation() {
    static bool value=[] {auto p=getenv("ZC255_PRECISE_SCRIPT_MAP_TRACKING");return p&&*p=='0';}();
    return value;
}
inline bool audit() {static bool value=[] {auto p=getenv("ZC255_AUDIT_MAP_TRACKING");return p&&*p&&*p!='0';}();return value;}
inline void missed_write(size_t index) {++state().missed;state().trusted=false;fprintf(stderr,"map tracking: missed write at screen %zu; full comparison retained\n",index);}
}
