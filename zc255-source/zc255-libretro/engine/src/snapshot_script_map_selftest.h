#pragma once
#include "zc/ffscript.h"
extern refInfo* ri;
void set_register(int32_t arg,int32_t value);
int32_t get_register(int32_t arg);
mapscr* GetMapscr(int32_t mapref);

// Opt-in calls the real VM getter/setter dispatch; it does not synthesize marks.
inline bool zc255_script_maps_selftest(const std::vector<uint8_t>& original) {
    const char* report=getenv("ZC255_SCRIPT_MAP_SELFTEST");if(!report||!*report)return true;
    auto& tracking=snapshot_map_tracking::state();
    const uint64_t old_missed=tracking.missed;
    bool passed=tracking.trusted&&snapshot_map_tracking::audit()&&TheMaps.size()>MAPSCRS;
    unsigned cases=0;bool restored=false;
    auto compare=[&]() {
        SaveStateBlob fast,reference;
        bool ok=snapshot_definitions::put_map_blocks(fast);
        const bool trust=tracking.trusted;tracking.trusted=false;
        ok=snapshot_definitions::put_map_blocks(reference)&&ok;tracking.trusted=trust;
        return ok&&fast.data==reference.data&&tracking.missed==old_missed;
    };
    if(passed) {
        refInfo local{};
        struct Guard {
            refInfo* previous;
            explicit Guard(refInfo* test):previous(ri){ri=test;}
            ~Guard(){ri=previous;}
        } guard(&local);
        passed=compare()&&passed;
        const size_t indices[]={size_t(MAPSCRS),TheMaps.size()/2,TheMaps.size()-1};
        for(size_t index:indices) {
            // Retain the SAME integer mapdata ID across independent captures.
            local.mapsref=int32_t(index);
            auto mutate=[&](int32_t code,int32_t argument,int32_t value,auto verify) {
                local.d[rINDEX]=argument;
                const auto epoch=tracking.epoch,version=snapshot_map_tracking::version(index);
                set_register(code,value);
                ++cases;
                passed=ri==&local&&local.mapsref==int32_t(index)&&tracking.epoch==epoch
                    &&snapshot_map_tracking::version(index)!=version&&verify()&&compare()&&passed;
            };
            const word combo=TheMaps[index].data[175]^1;
            mutate(MAPDATACOMBODD,1750000,int32_t(combo)*10000,
                   [&]{return TheMaps[index].data[175]==combo;});
            const byte cset=byte((TheMaps[index].cset[175]+1)%15);
            mutate(MAPDATACOMBOCD,1750000,int32_t(cset)*10000,
                   [&]{return TheMaps[index].cset[175]==cset;});
            const word ffc=TheMaps[index].ffcs[MAXFFCS-1].data^1;
            mutate(MAPDATAFFDATA,MAXFFCS*10000,int32_t(ffc)*10000,
                   [&]{return TheMaps[index].ffcs[MAXFFCS-1].data==ffc;});
            const byte ffcset=TheMaps[index].ffcs[MAXFFCS-1].cset^1;
            mutate(MAPDATAFFCSET,MAXFFCS*10000,int32_t(ffcset)*10000,
                   [&]{return TheMaps[index].ffcs[MAXFFCS-1].cset==ffcset;});
            mutate(MAPDATAFFX,MAXFFCS*10000,123456,
                   [&]{return TheMaps[index].ffcs[MAXFFCS-1].x.getZLong()==123456;});
            const byte flag=TheMaps[index].flags11^1;
            mutate(MAPDATAFLAGS,100000,int32_t(flag)*10000,
                   [&]{return TheMaps[index].flags11==flag;});
            // Layer arrays use older/newer quest-format index conventions.
            const int32_t layer_arg=FFCore.quest_format[vFFScript]<11?50000:60000;
            const byte opacity=TheMaps[index].layeropacity[5]^1;
            mutate(MAPDATALAYEROPACITY,layer_arg,int32_t(opacity)*10000,
                   [&]{return TheMaps[index].layeropacity[5]==opacity;});
            const word secret=TheMaps[index].secretcombo[127]^1;
            mutate(MAPDATASECRETCOMBO,1270000,int32_t(secret)*10000,
                   [&]{return TheMaps[index].secretcombo[127]==secret;});
            // Real getter resolves the retained ID again, including JIT's shared
            // get_register dispatch. Getters conservatively mark as well.
            local.d[rINDEX]=1750000;
            passed=get_register(MAPDATACOMBODD)==int32_t(combo)*10000&&compare()&&passed;++cases;
            // Notes have no VM setter. Test the real native resolver and deep
            // in-place string writes without pretending they are VM commands.
            auto epoch=tracking.epoch,version=snapshot_map_tracking::version(index);
            auto* screen=GetMapscr(local.mapsref);screen->usr_notes="__script_map_notes__";
            passed=screen==&TheMaps[index]&&tracking.epoch==epoch
                &&snapshot_map_tracking::version(index)!=version&&compare()&&passed;++cases;
            screen=GetMapscr(local.mapsref);screen->usr_notes[3]='X';
            passed=compare()&&passed;++cases;
        }
    }
    // The guard has restored interpreter context before importing the original.
    restored=zc255_snapshot_import(original.data(),original.size());
    passed=restored&&tracking.missed==old_missed&&passed;
    if(FILE* out=fopen(report,"w")) {
        fprintf(out,"{\"passed\":%s,\"cases\":%u,\"actual_vm_setters\":true,\"retained_map_ids\":true,\"all_maxffcs\":true,\"zero_missed_writes\":%s,\"original_restored\":%s}\n",passed?"true":"false",cases,tracking.missed==old_missed?"true":"false",restored?"true":"false");fclose(out);
    }
    return passed;
}
