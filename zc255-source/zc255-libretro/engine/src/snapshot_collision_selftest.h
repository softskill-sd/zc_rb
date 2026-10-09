#pragma once
#include "zc/zelda.h"
#include "zc/hero.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

extern HeroClass Hero;

bool zc255_snapshot_export(std::vector<uint8_t>&);
bool zc255_snapshot_import(const uint8_t*, size_t);

// Opt-in, synchronous regression at a canonical gameplay checkpoint. Query
// Hero's real collision path without executing the returned movement actions.
inline void zc255_collision_selftest() {
    static bool done=false;
    const char* report=std::getenv("ZC255_COLLISION_TEST");
    if(done || !report || !*report || !Playing) return;
    done=true;
    const bool initial_blockpath=blockpath, initial_toogam=toogam;
    const auto initial_x=Hero.getX(), initial_y=Hero.getY();
    const auto initial_ffc_data=tmpscr[0].ffcs[MAXFFCS-1].data;
    const auto initial_ffc_initd=tmpscr[0].ffcs[MAXFFCS-1].initd[0];
    const auto initial_ffc_x=tmpscr[0].ffcs[MAXFFCS-1].x;
    const auto initial_screen_valid=tmpscr2[5].valid;
    const auto initial_screen_data=tmpscr2[5].data[175];
    const auto initial_screen_cset=tmpscr2[5].cset[175];
    const auto initial_screen_initd=tmpscr2[5].ffcs[MAXFFCS-1].initd[0];
    std::vector<uint8_t> original, open_state, blocked_state;
    bool exported=zc255_snapshot_export(original);
    int px=-1, py=-1;
    auto blocked=[&]() { return Hero.walkflag(px,py,1,byte(up)).isUnwalkable(); };
    bool found=false, baseline=false, future_blocked=false;
    bool open_imported=false, open_restored=false;
    bool blocked_exported=false, future_open=false, blocked_imported=false, blocked_restored=false;
    bool rejected=false, unchanged=false;
    bool dormant_ffc_restored=false, invalid_screen_restored=false;
    if(exported) {
        // Cheats otherwise bypass blockpath before Hero checks the barrier.
        toogam=false; blockpath=false;
        for(int y=32; y<=72 && !found; y+=8) {
            for(int x=16; x<=240; x+=8) {
                if(!Hero.walkflag(x,y,1,byte(up)).isUnwalkable()) {
                    px=x; py=y; found=true; break;
                }
            }
        }
        // Dormant live data must survive even when normal gameplay ignores it.
        auto& dormant=tmpscr[0].ffcs[MAXFFCS-1];
        const bool initial_loaded=dormant.getLoaded();
        const auto initial_flags=dormant.flags;
        const auto initial_script=dormant.script;
        dormant.data=17; dormant.initd[0]=0x13572468; dormant.x=zfix(37);
        tmpscr2[5].valid=0;
        tmpscr2[5].data[175]=23; tmpscr2[5].cset[175]=7;
        tmpscr2[5].ffcs[MAXFFCS-1].initd[0]=0x24681357;
        std::vector<uint8_t> dormant_state;
        bool dormant_captured=zc255_snapshot_export(dormant_state);
        dormant.data=31; dormant.initd[0]=-54321; dormant.x=zfix(93);
        tmpscr2[5].data[175]=47; tmpscr2[5].cset[175]=3;
        tmpscr2[5].ffcs[MAXFFCS-1].initd[0]=-12345;
        bool dormant_imported=dormant_captured && zc255_snapshot_import(dormant_state.data(),dormant_state.size());
        dormant_ffc_restored=dormant_imported && dormant.data==17 && dormant.initd[0]==0x13572468
            && dormant.x==zfix(37) && dormant.getLoaded()==initial_loaded
            && dormant.flags==initial_flags && dormant.script==initial_script;
        invalid_screen_restored=dormant_imported && tmpscr2[5].valid==0
            && tmpscr2[5].data[175]==23 && tmpscr2[5].cset[175]==7
            && tmpscr2[5].ffcs[MAXFFCS-1].initd[0]==0x24681357;
        if(found) {
            baseline=zc255_snapshot_export(open_state);
            blockpath=true;
            future_blocked=blocked();
            open_imported=baseline && zc255_snapshot_import(open_state.data(),open_state.size());
            open_restored=open_imported && !blockpath && !toogam && !blocked();

            blockpath=true; toogam=false;
            blocked_exported=zc255_snapshot_export(blocked_state);
            blockpath=false;
            future_open=!blocked();
            blocked_imported=blocked_exported && zc255_snapshot_import(blocked_state.data(),blocked_state.size());
            blocked_restored=blocked_imported && blockpath && !toogam && blocked();

            // Opposite collision state makes an accidental partial apply visible.
            blockpath=false; toogam=false;
            const auto sentinel_x=Hero.getX(), sentinel_y=Hero.getY();
            const bool sentinel_collision=blocked();
            auto malformed=blocked_state;
            if(!malformed.empty()) malformed.pop_back();
            rejected=blocked_exported && !zc255_snapshot_import(malformed.data(),malformed.size());
            unchanged=!blockpath && !toogam && blocked()==sentinel_collision
                && Hero.getX()==sentinel_x && Hero.getY()==sentinel_y;
        }
    }
    bool cleanup=exported && zc255_snapshot_import(original.data(),original.size());
    cleanup=cleanup && blockpath==initial_blockpath && toogam==initial_toogam
        && Hero.getX()==initial_x && Hero.getY()==initial_y
        && tmpscr[0].ffcs[MAXFFCS-1].data==initial_ffc_data
        && tmpscr[0].ffcs[MAXFFCS-1].initd[0]==initial_ffc_initd
        && tmpscr[0].ffcs[MAXFFCS-1].x==initial_ffc_x
        && tmpscr2[5].valid==initial_screen_valid && tmpscr2[5].data[175]==initial_screen_data
        && tmpscr2[5].cset[175]==initial_screen_cset
        && tmpscr2[5].ffcs[MAXFFCS-1].initd[0]==initial_screen_initd;
    if(!cleanup) {
        blockpath=initial_blockpath; toogam=initial_toogam;
        tmpscr[0].ffcs[MAXFFCS-1].data=initial_ffc_data;
        tmpscr[0].ffcs[MAXFFCS-1].initd[0]=initial_ffc_initd;
        tmpscr[0].ffcs[MAXFFCS-1].x=initial_ffc_x;
        tmpscr2[5].valid=initial_screen_valid; tmpscr2[5].data[175]=initial_screen_data;
        tmpscr2[5].cset[175]=initial_screen_cset;
        tmpscr2[5].ffcs[MAXFFCS-1].initd[0]=initial_screen_initd;
    }
    const bool passed=exported && found && baseline && future_blocked && open_restored
        && blocked_exported && future_open && blocked_restored && rejected && unchanged && cleanup
        && dormant_ffc_restored && invalid_screen_restored;
    if(FILE* out=std::fopen(report,"w")) {
        std::fprintf(out,"{\"passed\":%s,\"exported\":%s,\"passable_point_found\":%s,\"point_x\":%d,\"point_y\":%d,\"future_barrier_blocks_movement\":%s,\"rewind_restores_passability\":%s,\"future_barrier_removed\":%s,\"rewind_preserves_barrier\":%s,\"malformed_rejected\":%s,\"malformed_left_collision_unchanged\":%s,\"original_state_restored\":%s,\"dormant_live_ffc_restored\":%s,\"invalid_live_screen_restored\":%s}\n",
            passed?"true":"false",exported?"true":"false",found?"true":"false",px,py,
            future_blocked?"true":"false",open_restored?"true":"false",future_open?"true":"false",
            blocked_restored?"true":"false",rejected?"true":"false",unchanged?"true":"false",cleanup?"true":"false",
            dormant_ffc_restored?"true":"false",invalid_screen_restored?"true":"false");
        std::fclose(out);
    }
    std::fprintf(stderr,"ZC255 collision selftest: %s, actual Hero collision point (%d,%d)\n",passed?"PASS":"FAIL",px,py);
}
