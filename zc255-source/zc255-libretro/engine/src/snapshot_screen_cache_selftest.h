#pragma once
#include "snapshot_definitions.h"
#include <cstdio>
#include <cstdlib>

bool zc255_snapshot_export(std::vector<uint8_t>&);
bool zc255_snapshot_import(const uint8_t*,size_t);
void zc255_screen_cache_stats(uint64_t&,uint64_t&,uint64_t&);

inline void zc255_screen_cache_selftest() {
    static bool done=false;
    const char* report=std::getenv("ZC255_SCREEN_CACHE_TEST");
    if(done || !report || !*report || !Playing) return;
    done=true;
    auto screen=[](size_t i)->mapscr& { return i<2?tmpscr[i]:i<8?tmpscr2[i-2]:tmpscr3[i-8]; };
    auto values=[&]() {
        SaveStateBlob b; StateWriter writer{b};
        for(size_t i=0;i<14;++i) {
            auto& sc=screen(i); sc.visit_state(writer);
            for(auto& f:sc.ffcs) f.visit_state(writer);
        }
        return b.data;
    };
    auto& dormant=screen(13); const auto notes=dormant.usr_notes;
    const auto original_values=values();
    std::vector<uint8_t> original,same,changed,warm;
    bool baseline=zc255_snapshot_export(original);
    uint64_t h0=0,r0=0,s0=0,h1=0,r1=0,s1=0;
    zc255_screen_cache_stats(h0,r0,s0);
    bool same_saved=baseline && zc255_snapshot_export(same);
    zc255_screen_cache_stats(h1,r1,s1);
    const uint64_t identical_hits=h1-h0,identical_rebuilds=r1-r0;
    bool identical=same_saved && identical_hits==14 && identical_rebuilds==0;
    bool direct=false,restored=false,skipped=false,deep_notes=false,malformed=false,unchanged=false,rollback=false;
    uint64_t changed_hits=0,changed_rebuilds=0,restore_skips=0,notes_rebuilds=0;
    if(baseline) {
        dormant.valid=0; dormant.data[0]^=1;
        auto& f=dormant.ffcs[MAXFFCS-1]; f.data^=3; f.initd[7]^=123;
        f.hit_width+=9; f.info.type_clk^=17;
        const auto changed_values=values();
        zc255_screen_cache_stats(h0,r0,s0);
        bool saved=zc255_snapshot_export(changed);
        zc255_screen_cache_stats(h1,r1,s1);
        changed_hits=h1-h0; changed_rebuilds=r1-r0;
        direct=saved && changed_values!=original_values && changed_hits==13 && changed_rebuilds==1;
        restored=zc255_snapshot_import(original.data(),original.size()) && values()==original_values;
        restored=restored && saved && zc255_snapshot_import(changed.data(),changed.size()) && values()==changed_values;
        zc255_screen_cache_stats(h0,r0,s0);
        bool same_restore=saved && zc255_snapshot_import(changed.data(),changed.size());
        zc255_screen_cache_stats(h1,r1,s1);
        restore_skips=s1-s0;
        skipped=same_restore && restore_skips==14 && values()==changed_values;

        // Heap-backed notes have unchanged pointer/size/capacity on this write.
        // Their contents are not snapshot fields, but must be checked deeply.
        dormant.usr_notes.assign(64,'a');
        bool warmed=zc255_snapshot_export(warm);
        zc255_screen_cache_stats(h0,r0,s0);
        dormant.usr_notes[32]='b';
        std::vector<uint8_t> notes_state;
        bool notes_saved=warmed && zc255_snapshot_export(notes_state);
        zc255_screen_cache_stats(h1,r1,s1); notes_rebuilds=r1-r0;
        deep_notes=notes_saved && notes_rebuilds==1 && h1-h0==13;
        dormant.usr_notes=notes;
        std::vector<uint8_t> truncated=original; if(!truncated.empty()) truncated.pop_back();
        const auto sentinels=values();
        malformed=!zc255_snapshot_import(truncated.data(),truncated.size());
        unchanged=values()==sentinels;
        rollback=zc255_snapshot_import(original.data(),original.size()) && values()==original_values;
    }
    dormant.usr_notes=notes;
    const bool passed=baseline && identical && direct && restored && skipped && deep_notes && malformed && unchanged && rollback;
    FILE* out=fopen(report,"w");
    if(out) {
        fprintf(out,"{\"passed\":%s,\"identical\":%s,\"dormant_direct_write\":%s,\"reverse_forward_restore\":%s,\"restore_skip\":%s,\"deep_notes\":%s,\"malformed_rejected\":%s,\"malformed_unchanged\":%s,\"original_restored\":%s,\"identical_hits\":%llu,\"identical_rebuilds\":%llu,\"changed_hits\":%llu,\"changed_rebuilds\":%llu,\"restore_skips\":%llu,\"notes_rebuilds\":%llu}\n",
            passed?"true":"false",identical?"true":"false",direct?"true":"false",restored?"true":"false",skipped?"true":"false",deep_notes?"true":"false",malformed?"true":"false",unchanged?"true":"false",rollback?"true":"false",
            (unsigned long long)identical_hits,(unsigned long long)identical_rebuilds,(unsigned long long)changed_hits,(unsigned long long)changed_rebuilds,(unsigned long long)restore_skips,(unsigned long long)notes_rebuilds);
        fclose(out);
    }
    fprintf(stderr,"ZC255 live-screen cache selftest: %s\n",passed?"PASS":"FAIL");
}
