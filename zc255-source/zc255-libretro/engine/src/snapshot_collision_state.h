#pragma once
#include "slopes.h"
#include "snapshot_map_tracking.h"
#include "base/mapscr.h"
#include "zc/zelda.h"
#include "zc/savestate_serialize.h"
#include <map>
#include <set>
#include <cstdint>
#include <cstring>
#include <vector>

bool zc255_snapshot_export(std::vector<uint8_t>&);
bool zc255_snapshot_import(const uint8_t*,size_t);

namespace snapshot_collision_state {
constexpr uint32_t VERSION=1,MAX_SLOPES=65536;
enum : uint8_t { NONE=0,SCREEN_DATA=1,SCREEN_FFC_DATA=2,MAP_DATA=3,MAP_FFC_DATA=4,OWNED_WORD=5 };
struct Identity { uint8_t kind=NONE; uint32_t screen=0,index=0; };
struct Entry { int32_t id=0; Identity combo,ffc; word value=0; int32_t ox1=0,oy1=0,ox2=0,oy2=0,xoffs=0,yoffs=0; };
struct PreparedState { std::vector<Entry> entries; size_t maps=0; bool valid=false; };
inline mapscr& buffer(size_t i) { return i<2 ? tmpscr[i] : i<8 ? tmpscr2[i-2] : tmpscr3[i-8]; }
inline std::map<int32_t,word>& owned_words() { static std::map<int32_t,word> value; return value; }
inline bool pointer_index(const word* p,const word* base,size_t count,uint32_t& index) {
    uintptr_t address=reinterpret_cast<uintptr_t>(p),start=reinterpret_cast<uintptr_t>(base);
    if(address<start || address-start>=count*sizeof(word) || (address-start)%sizeof(word)) return false;
    index=uint32_t((address-start)/sizeof(word)); return true;
}
inline bool combo_identity(const word* pointer,Identity& result) {
    if(!pointer) return true;
    for(size_t i=0;i<14+TheMaps.size();++i) {
        mapscr& sc=i<14 ? buffer(i) : TheMaps[i-14];
        if(pointer_index(pointer,sc.data,176,result.index)) {
            result.kind=i<14 ? SCREEN_DATA : MAP_DATA; result.screen=uint32_t(i<14?i:i-14); return true;
        }
        for(size_t j=0;j<MAXFFCS;++j) if(pointer==&sc.ffcs[j].data) {
            result.kind=i<14 ? SCREEN_FFC_DATA : MAP_FFC_DATA; result.screen=uint32_t(i<14?i:i-14); result.index=uint32_t(j); return true;
        }
    }
    // Bordering-screen slopes refer to engine-private static combo words.
    // Preserve their value with stable owned nodes rather than an address.
    result.kind=OWNED_WORD; return true;
}
inline bool ffc_identity(const ffcdata* pointer,Identity& result) {
    if(!pointer) return true;
    for(size_t i=0;i<14+TheMaps.size();++i) {
        mapscr& sc=i<14 ? buffer(i) : TheMaps[i-14];
        for(size_t j=0;j<MAXFFCS;++j) if(pointer==&sc.ffcs[j]) {
            result.kind=i<14 ? SCREEN_FFC_DATA : MAP_FFC_DATA; result.screen=uint32_t(i<14?i:i-14); result.index=uint32_t(j); return true;
        }
    }
    return false;
}
inline bool valid_identity(const Identity& v,bool ffc) {
    if(v.kind==NONE) return !v.screen && !v.index;
    if(v.kind==OWNED_WORD) return !ffc && !v.screen && !v.index;
    if(v.kind<SCREEN_DATA || v.kind>MAP_FFC_DATA) return false;
    if(ffc && v.kind!=SCREEN_FFC_DATA && v.kind!=MAP_FFC_DATA) return false;
    bool screen=v.kind==SCREEN_DATA || v.kind==SCREEN_FFC_DATA;
    bool slot=v.kind==SCREEN_FFC_DATA || v.kind==MAP_FFC_DATA;
    return v.screen<(screen?14:TheMaps.size()) && v.index<(slot?MAXFFCS:176);
}
inline mapscr& identity_screen(const Identity& v) {
    return v.kind==SCREEN_DATA || v.kind==SCREEN_FFC_DATA ? buffer(v.screen) : TheMaps[v.screen];
}
inline const ffcdata* resolve_ffc(const Identity& v) { return v.kind==NONE ? nullptr : &identity_screen(v).ffcs[v.index]; }
inline const word* resolve_combo(const Identity& v) {
    if(v.kind==NONE) return nullptr;
    mapscr& sc=identity_screen(v);
    return v.kind==SCREEN_DATA || v.kind==MAP_DATA ? &sc.data[v.index] : &sc.ffcs[v.index].data;
}
inline void put_identity(SaveStateBlob& b,const Identity& v) { b.put(v.kind); b.put(v.screen); b.put(v.index); }
inline void get_identity(SaveStateBlob& b,Identity& v) { b.get(v.kind); b.get(v.screen); b.get(v.index); }
inline bool put(SaveStateBlob& out) {
    if(slopes.size()>MAX_SLOPES || TheMaps.size()>UINT32_MAX) return false;
    SaveStateBlob encoded; encoded.put(VERSION); encoded.put(uint32_t(TheMaps.size())); encoded.put(uint32_t(slopes.size()));
    for(const auto& pair:slopes) {
        const auto& slope=pair.second; Identity combo,ffc;
        if(!combo_identity(slope.cmbid,combo) || !ffc_identity(slope.ffc,ffc)) return false;
        encoded.put(pair.first); put_identity(encoded,combo); put_identity(encoded,ffc);
        encoded.put(word(slope.cmbid?*slope.cmbid:0));
        encoded.put(slope.ox1.getZLong()); encoded.put(slope.oy1.getZLong());
        encoded.put(slope.ox2.getZLong()); encoded.put(slope.oy2.getZLong());
        encoded.put(slope.xoffs.getZLong()); encoded.put(slope.yoffs.getZLong());
    }
    if(encoded.failed) return false;
    out.put(encoded.data.data(),encoded.data.size()); return !out.failed;
}
inline bool prepare(SaveStateBlob& in,PreparedState& prepared) {
    prepared.valid=false; PreparedState next;
    uint32_t version=0,maps=0,count=0; in.get(version); in.get(maps); in.get(count);
    constexpr size_t RECORD_BYTES=sizeof(int32_t)+2*(sizeof(uint8_t)+2*sizeof(uint32_t))+sizeof(word)+6*sizeof(int32_t);
    if(in.failed || version!=VERSION || maps!=TheMaps.size() || count>MAX_SLOPES
        || in.pos>in.data.size() || size_t(count)>(in.data.size()-in.pos)/RECORD_BYTES) return false;
    next.entries.reserve(count); std::set<int32_t> ids;
    for(size_t i=0;i<count;++i) {
        Entry entry; in.get(entry.id); get_identity(in,entry.combo); get_identity(in,entry.ffc); in.get(entry.value);
        in.get(entry.ox1); in.get(entry.oy1); in.get(entry.ox2); in.get(entry.oy2); in.get(entry.xoffs); in.get(entry.yoffs);
        if(in.failed || !ids.insert(entry.id).second || !valid_identity(entry.combo,false) || !valid_identity(entry.ffc,true)) return false;
        next.entries.push_back(entry);
    }
    next.maps=maps; next.valid=true; prepared=std::move(next); return true;
}
inline bool apply(PreparedState& prepared) {
    if(!prepared.valid || prepared.maps!=TheMaps.size()) return false;
    prepared.valid=false;
    // Node ownership and every map allocation are staged before replacing live
    // slopes. Swapping preserves the addresses stored in the staged entries.
    std::map<int32_t,word> words;
    std::map<int32_t,slope_object> staged;
    for(const auto& entry:prepared.entries) {
        const word* combo=nullptr;
        if(entry.combo.kind==OWNED_WORD) combo=&words.emplace(entry.id,entry.value).first->second;
        else combo=resolve_combo(entry.combo);
        const ffcdata* ffc=resolve_ffc(entry.ffc);
        auto result=staged.try_emplace(entry.id);
        auto& slope=result.first->second;
        slope.cmbid=combo; slope.ffc=ffc;
        slope.ox1=zslongToFix(entry.ox1); slope.oy1=zslongToFix(entry.oy1);
        slope.ox2=zslongToFix(entry.ox2); slope.oy2=zslongToFix(entry.oy2);
        slope.xoffs=zslongToFix(entry.xoffs); slope.yoffs=zslongToFix(entry.yoffs);
    }
    slopes.swap(staged); owned_words().swap(words); return true;
}
inline bool get(SaveStateBlob& in,bool commit) { PreparedState state; return prepare(in,state) && (!commit || apply(state)); }
inline bool& full_engine_selftest_passed() { static bool value=false; return value; }
// Called only by an explicitly enabled stable-checkpoint regression hook.
inline bool semantic_selftest() {
    full_engine_selftest_passed()=false;
    SaveStateBlob baseline; if(!put(baseline)) return false;
    std::vector<uint8_t> engine_baseline;
    if(!zc255_snapshot_export(engine_baseline)) return false;
    bool passed=true; slopes.clear();
    word external=37;
    auto& a=slopes.try_emplace(1232,nullptr,&tmpscr[0].ffcs[0]).first->second;
    a.ox1=3; a.oy1=4; a.ox2=5; a.oy2=6; a.xoffs=7; a.yoffs=8;
    auto& b=slopes.try_emplace(5000,&external,nullptr,9,10).first->second; b.ox1=11; b.oy2=12;
    slopes.try_emplace(42,&tmpscr[0].data[1],nullptr,13,14);
    const size_t map_index=TheMaps.size()-1;
    slopes.try_emplace(40000,&TheMaps[map_index].data[175],nullptr,15,16);
    slopes.try_emplace(40001,nullptr,&TheMaps[map_index].ffcs[MAXFFCS-1],17,18);
    const auto map_data=TheMaps[map_index].data[175];
    const auto map_ffc_data=TheMaps[map_index].ffcs[MAXFFCS-1].data;
    SaveStateBlob expected; passed=put(expected);
    a.ox1=99; slopes.erase(5000); slopes.erase(42);
    SaveStateBlob input=expected; PreparedState prepared;
    bool ready=prepare(input,prepared);
    passed=passed && ready && slopes.size()==3 && slopes.at(1232).ox1==99;
    bool restored=ready && apply(prepared);
    passed=passed && restored && slopes.size()==5 && slopes.at(42).cmbid==&tmpscr[0].data[1] && slopes.at(1232).ffc==&tmpscr[0].ffcs[0]
        && slopes.at(1232).cmbid==&tmpscr[0].ffcs[0].data && slopes.at(1232).ox1==3
        && slopes.at(1232).yoffs==8 && slopes.at(5000).cmbid && *slopes.at(5000).cmbid==37
        && slopes.at(5000).ox1==11 && slopes.at(5000).oy2==12
        && slopes.at(40000).cmbid==&TheMaps[map_index].data[175]
        && slopes.at(40001).ffc==&TheMaps[map_index].ffcs[MAXFFCS-1]
        && slopes.at(40001).cmbid==&TheMaps[map_index].ffcs[MAXFFCS-1].data && !apply(prepared);
    SaveStateBlob before; put(before);
    SaveStateBlob malformed=expected; malformed.data.pop_back(); PreparedState bad;
    bool rejected=!prepare(malformed,bad); SaveStateBlob after; put(after);
    passed=passed && rejected && before.data==after.data;
    SaveStateBlob duplicate=expected;
    constexpr size_t RECORD_BYTES=sizeof(int32_t)+2*(sizeof(uint8_t)+2*sizeof(uint32_t))+sizeof(word)+6*sizeof(int32_t);
    memcpy(duplicate.data.data()+12+RECORD_BYTES,duplicate.data.data()+12,sizeof(int32_t));
    PreparedState duplicate_state; passed=passed && !prepare(duplicate,duplicate_state);
    ffcdata unknown; slopes.try_emplace(6000,nullptr,&unknown);
    SaveStateBlob refused; passed=passed && !put(refused); slopes.erase(6000);
    // Exercise the actual native snapshot pipeline with nonempty live slopes,
    // not only the collision-section decoder. Re-export the collision section
    // to catch omitted history or a binding which changed its identity kind.
    std::vector<uint8_t> full_state;
    bool full_export=zc255_snapshot_export(full_state);
    snapshot_map_tracking::access(map_index).data[175]^=1;
    snapshot_map_tracking::access(map_index).ffcs[MAXFFCS-1].data^=1;
    slopes.clear();
    bool full_import=full_export && zc255_snapshot_import(full_state.data(),full_state.size());
    SaveStateBlob full_section;
    bool full_exact=full_import && put(full_section) && full_section.data==expected.data
        && TheMaps[map_index].data[175]==map_data
        && TheMaps[map_index].ffcs[MAXFFCS-1].data==map_ffc_data;
    full_engine_selftest_passed()=full_exact && slopes.size()==5
        && slopes.at(42).cmbid==&tmpscr[0].data[1]
        && slopes.at(1232).ffc==&tmpscr[0].ffcs[0]
        && slopes.at(1232).cmbid==&tmpscr[0].ffcs[0].data
        && slopes.at(1232).ox1==3 && slopes.at(1232).oy1==4
        && slopes.at(1232).ox2==5 && slopes.at(1232).oy2==6
        && slopes.at(1232).xoffs==7 && slopes.at(1232).yoffs==8
        && slopes.at(5000).cmbid && *slopes.at(5000).cmbid==37
        && slopes.at(5000).ox1==11 && slopes.at(5000).oy2==12
        && slopes.at(40000).cmbid==&TheMaps[map_index].data[175]
        && slopes.at(40001).ffc==&TheMaps[map_index].ffcs[MAXFFCS-1]
        && slopes.at(40001).cmbid==&TheMaps[map_index].ffcs[MAXFFCS-1].data;
    bool full_cleanup=zc255_snapshot_import(engine_baseline.data(),engine_baseline.size());
    PreparedState original; baseline.pos=0;
    bool cleanup=prepare(baseline,original) && apply(original);
    return passed && full_engine_selftest_passed() && full_cleanup && cleanup;
}
} // namespace snapshot_collision_state