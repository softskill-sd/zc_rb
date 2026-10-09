#pragma once
#include "base/zdefs.h"
#include "base/combo.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Candidate only: capture cache integration and restore invalidations required
// before enabling clean-block skips. Never serialize these process-local values.
namespace snapshot_combo_tracking {
constexpr size_t block_records = 256;
constexpr size_t blocks = (MAXCOMBOS + block_records - 1) / block_records;
struct Tracker {
    std::array<uint64_t, blocks> versions{};
    uint64_t epoch=1, skipped=0, checked=0, missed=0;
    bool trusted=false;
};
inline Tracker& state() { static Tracker value; return value; }
inline bool install_engine_hooks() { state().trusted=true; return true; }
inline void invalidate_all() {
    auto& s=state();
    if (++s.epoch==0) { s.epoch=1; s.versions.fill(0); s.trusted=false; }
}
inline void mark(size_t index) {
    if (index>=MAXCOMBOS) { invalidate_all(); return; }
    if (++state().versions[index/block_records]==0) invalidate_all();
}
inline newcombo& mutable_record(size_t index) {
    mark(index);
    return combobuf[index];
}
inline void mark_reference(newcombo const& value) {
    // Integer bounds avoid relational comparisons between unrelated pointers.
    auto begin=reinterpret_cast<uintptr_t>(combobuf.data());
    auto address=reinterpret_cast<uintptr_t>(&value);
    auto bytes=combobuf.size()*sizeof(newcombo);
    if (address>=begin && address-begin<bytes && (address-begin)%sizeof(newcombo)==0)
        mark((address-begin)/sizeof(newcombo));
}
inline bool audit() {
    static bool enabled=[] { auto p=std::getenv("ZC255_AUDIT_COMBO_TRACKING"); return p && *p && *p!='0'; }();
    return enabled;
}
inline void missed_write(size_t block) {
    auto& s=state(); ++s.missed; s.trusted=false;
    std::fprintf(stderr,"combo tracking: missed write in block %zu; full comparison retained\n",block);
}
}
