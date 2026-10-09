#pragma once
#include "snapshot_definitions.h"
#include "sprite.h"
#include <cstdio>
#include <cstdlib>

bool zc255_snapshot_export(std::vector<uint8_t>&);
bool zc255_snapshot_import(const uint8_t*,size_t);

// Call once at a stable bridge checkpoint. No gameplay is advanced by this test.
inline bool zc255_mblock_selftest() {
    static bool done=false;
    const char* report=std::getenv("ZC255_MBLOCK_TEST");
    if(done || !report || !*report || !Playing) return false;
    done=true;
    auto block_bytes=[]() {
        SaveStateBlob b; StateWriter writer{b};
        mblock2.visit_state(writer); b.put(mblock2.getUID());
        return b.data;
    };
    std::vector<uint8_t> original,before,after;
    const auto original_block=block_bytes();
    bool original_saved=zc255_snapshot_export(original);
    bool captured=false,future_captured=false,geometry=false,reverse=false,forward=false,rollback=false;
    if(original_saved) {
        mblock2.clk=0; mblock2.fallclk=0; mblock2.drownclk=0;
        mblock2.setSolid(false); mblock2.setTempNonsolid(true);
        switching_object=nullptr;
        const auto past_block=block_bytes();
        captured=zc255_snapshot_export(before);
        if(captured) {
            mblock2.x=96; mblock2.y=96; mblock2.old_x=80; mblock2.old_y=96;
            mblock2.hit_width=24; mblock2.hit_height=32;
            mblock2.hxofs=0; mblock2.hyofs=0; mblock2.sxofs=0; mblock2.syofs=0;
            mblock2.sxsz_ofs=0; mblock2.sysz_ofs=0;
            mblock2.clk=11; mblock2.endx=112; mblock2.endy=96; mblock2.step=1;
            mblock2.trigger=true; mblock2.bhole=false; mblock2.blockLayer=0;
            mblock2.blockinfo.clk=7; mblock2.blockinfo.type_clk=123;
            mblock2.blockinfo.trig_cd=9; mblock2.blockinfo.pushes[2]=5;
            mblock2.blockinfo.crumbling=true; mblock2.blockinfo.appeared=true;
            mblock2.setSolid(true); mblock2.setTempNonsolid(false);
            mblock2.switch_hooked=true; switching_object=&mblock2;
            geometry=mblock2.active() && mblock2.collide(100,100,1,1) && !mblock2.collide(130,100,1,1);
            const auto future_block=block_bytes();
            future_captured=zc255_snapshot_export(after);
            reverse=zc255_snapshot_import(before.data(),before.size())
                && block_bytes()==past_block && !mblock2.active() && !mblock2.getSolid()
                && !mblock2.collide(100,100,1,1) && switching_object==nullptr;
            forward=future_captured && zc255_snapshot_import(after.data(),after.size())
                && block_bytes()==future_block && mblock2.active() && mblock2.getSolid()
                && mblock2.collide(100,100,1,1) && switching_object==&mblock2;
        }
        rollback=zc255_snapshot_import(original.data(),original.size()) && block_bytes()==original_block;
    }
    bool passed=original_saved && captured && future_captured && geometry && reverse && forward && rollback;
    FILE* out=fopen(report,"w");
    if(out) {
        fprintf(out,"{\"passed\":%s,\"geometry\":%s,\"reverse_restore\":%s,\"forward_restore\":%s,\"original_restored\":%s}\n",
            passed?"true":"false",geometry?"true":"false",reverse?"true":"false",forward?"true":"false",rollback?"true":"false");
        fclose(out);
    }
    fprintf(stderr,"ZC255 moving-block selftest: %s\n",passed?"PASS":"FAIL");
    return passed;
}
