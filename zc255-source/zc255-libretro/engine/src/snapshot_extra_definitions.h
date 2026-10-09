#pragma once
#include "base/initdata.h"
#include "base/misctypes.h"
#include "zc/savestate_serialize.h"
#include <memory>
#include <limits>

// Initial-data gravity/movement and QMisc shops, bottles, sprite/SFX IDs and
// quest-misc values are script writable. Capture the entire definitions.
namespace snapshot_extra_definitions {
template<class T> struct Codec {
    static void put(SaveStateBlob& b, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>); b.put(value);
    }
    static void get(SaveStateBlob& b, T& value) {
        static_assert(std::is_trivially_copyable_v<T>); b.get(value);
    }
};
template<> struct Codec<std::string> {
    static void put(SaveStateBlob& b, const std::string& v) { b.put_str(v); }
    static void get(SaveStateBlob& b, std::string& v) { b.get_str(v); }
};
template<> struct Codec<zfix> {
    static void put(SaveStateBlob& b, const zfix& v) { b.put(v.getZLong()); }
    static void get(SaveStateBlob& b, zfix& v) { int32_t n=0; b.get(n); v=zslongToFix(n); }
};
template<class S, class T> struct Codec<bounded_vec<S,T>> {
    using V = bounded_vec<S,T>;
    static void put(SaveStateBlob& b, const V& v) {
        b.put(v.size()); Codec<T>::put(b,v.defval()); b.put(uint32_t(v.inner().size()));
        for(const auto& value:v.inner()) Codec<T>::put(b,value);
    }
    static void get(SaveStateBlob& b, V& v) {
        S size{}; uint32_t count=0;
        b.get(size); Codec<T>::get(b,const_cast<T&>(v.defval())); b.get(count);
        if(b.failed || count > uint64_t(size) || count > STATE_MAX_ELEMENTS || count > b.data.size()-b.pos) {
            b.failed=true; return;
        }
        v.resize(size); v.clear();
        for(uint32_t index=0;index<count;++index) {
            Codec<T>::get(b,v[static_cast<S>(index)]); if(b.failed) return;
        }
    }
};
template<class S, class T> struct Codec<bounded_map<S,T>> {
    using V = bounded_map<S,T>;
    static void put(SaveStateBlob& b, const V& v) {
        b.put(v.size()); Codec<T>::put(b,v.defval()); b.put(uint32_t(v.inner().size()));
        for(const auto& [key,value]:v.inner()) { b.put(key); Codec<T>::put(b,value); }
    }
    static void get(SaveStateBlob& b, V& v) {
        S size{}; uint32_t count=0;
        b.get(size); Codec<T>::get(b,const_cast<T&>(v.defval())); b.get(count);
        if(b.failed || count > uint64_t(size) || count > STATE_MAX_ELEMENTS || count > b.data.size()-b.pos) {
            b.failed=true; return;
        }
        v.resize(size); v.clear();
        for(uint32_t n=0;n<count;++n) {
            S key{}; b.get(key);
            if(b.failed || uint64_t(key)>=uint64_t(size)) {
                b.failed=true; return;
            }
            auto [entry,inserted]=v.mut_inner().try_emplace(key);
            if(!inserted) { b.failed=true; return; }
            Codec<T>::get(b,entry->second); if(b.failed) return;
        }
    }
};
template<> struct Codec<bitstring> {
    static void put(SaveStateBlob& b,const bitstring& v) { Codec<bounded_vec<word,byte>>::put(b,v.inner()); }
    static void get(SaveStateBlob& b,bitstring& v) { Codec<bounded_vec<word,byte>>::get(b,v.inner()); }
};
struct Writer {
    SaveStateBlob& b;
    template<class... T> void operator()(const T&... v) { (Codec<T>::put(b,v),...); }
};
struct Reader {
    SaveStateBlob& b;
    template<class... T> void operator()(T&... v) { (Codec<T>::get(b,v),...); }
};
template<class S> void init_fields(S& s,zinitdata& v) {
    s(v.items,v.map,v.compass,v.boss_key,v.mcguffin,v.level_keys,v.counter,v.mcounter);
    s(v.bomb_ratio,v.hcp,v.hcp_per_hc,v.cont_heart,v.hp_per_heart,v.magic_per_block,v.hero_damage_multiplier,v.ene_damage_multiplier);
    s(v.dither_type,v.dither_arg,v.dither_percent,v.def_lightrad,v.transdark_percent,v.darkcol);
    s(v.ss_grid_x,v.ss_grid_y,v.ss_grid_xofs,v.ss_grid_yofs,v.ss_grid_color,v.ss_bbox_1_color,v.ss_bbox_2_color,v.ss_flags);
    s(v.flags,v.last_map,v.last_screen,v.msg_more_x,v.msg_more_y,v.msg_more_is_offset,v.msg_speed);
    s(v.gravity,v.swimgravity,v.terminalv,v.hero_swim_speed,v.hero_swim_mult,v.hero_swim_div);
    s(v.heroSideswimUpStep,v.heroSideswimSideStep,v.heroSideswimDownStep,v.exitWaterJump,v.heroStep,v.heroAnimationStyle);
    s(v.jump_hero_layer_threshold,v.bunny_ltm,v.start_dmap,v.subscrSpeed,v.switchhookstyle,v.magicdrainrate);
    s(v.spriteflickerspeed,v.spriteflickercolor,v.spriteflickertransp,v.shove_offset);
    s(v.gen_doscript,v.gen_exitState,v.gen_reloadState,v.gen_initd,v.gen_eventstate,v.gen_data,v.screen_data);
}
template<class S> void misc_fields(S& s,miscQdata& v) {
    for(auto& shop:v.shop) s(shop.name,shop.item,shop.hasitem,shop.price,shop.str);
    for(auto& info:v.info) s(info.name,info.str,info.price);
    for(auto& warp:v.warp) s(warp.dmap,warp.scr,warp.size);
    for(auto& cycles:v.cycles) for(auto& cycle:cycles) s(cycle.first,cycle.count,cycle.speed);
    s(v.triforce);
    auto& c=v.colors;
    s(c.text,c.caption,c.overw_bg,c.dngn_bg,c.dngn_fg,c.cave_fg,c.bs_dk,c.bs_goal,c.compass_lt,c.compass_dk);
    s(c.subscr_bg,c.subscr_shadow,c.triframe_color,c.bmap_bg,c.bmap_fg,c.hero_dot);
    s(c.triforce_cset,c.triframe_cset,c.overworld_map_cset,c.dungeon_map_cset,c.blueframe_cset);
    s(c.triforce_tile,c.triframe_tile,c.overworld_map_tile,c.dungeon_map_tile,c.blueframe_tile,c.HCpieces_tile,c.HCpieces_cset,c.msgtext,c.foo,c.foo2);
    s(v.icons,v.endstring,v.questmisc,v.questmisc_strings,v.zscript_last_compiled_version,v.sprites);
    for(auto& bottle:v.bottle_types) s(bottle.name,bottle.counter,bottle.amount,bottle.flags,bottle.next_type);
    for(auto& shop:v.bottle_shop_types) s(shop.name,shop.fill,shop.comb,shop.cset,shop.price,shop.str);
    s(v.miscsfx);
}
template<class S> void door_fields(S& s,DoorComboSet& v) {
    s(v.doorcombo_u,v.doorcset_u,v.doorcombo_d,v.doorcset_d,v.doorcombo_l,v.doorcset_l,v.doorcombo_r,v.doorcset_r);
    s(v.bombdoorcombo_u,v.bombdoorcset_u,v.bombdoorcombo_d,v.bombdoorcset_d,v.bombdoorcombo_l,v.bombdoorcset_l,v.bombdoorcombo_r,v.bombdoorcset_r);
    s(v.walkthroughcombo,v.walkthroughcset,v.flags,v.expansion);
}
inline void put(SaveStateBlob& b) {
    b.put(uint32_t(1)); Writer w{b};
    init_fields(w,zinit); misc_fields(w,QMisc);
    for(size_t i=0;i<DoorComboSets.size();++i) { door_fields(w,DoorComboSets[i]); w(DoorComboSetNames[i]); }
}
struct Decoded {
    zinitdata init;
    miscQdata misc;
    std::array<DoorComboSet,MAXDOORCOMBOSETS> doors;
    std::array<std::string,MAXDOORCOMBOSETS> names;
};
struct PreparedState {
    // Own the exact validated field encoding, rather than assigning Decoded:
    // bounded container assignment can normalize allocated default entries.
    SaveStateBlob fields;
    size_t doors=0,names=0;
    bool valid=false;
};
inline bool prepare(SaveStateBlob& b,PreparedState& prepared) {
    prepared.valid=false;
    uint32_t version=0; b.get(version);
    if(b.failed || version!=1) { b.failed=true; return false; }
    const size_t fields_start=b.pos;
    auto state=std::make_unique<Decoded>(); Reader r{b};
    init_fields(r,state->init); misc_fields(r,state->misc);
    for(size_t i=0;i<state->doors.size() && !b.failed;++i) { door_fields(r,state->doors[i]); r(state->names[i]); }
    if(b.failed) return false;
    PreparedState next;
    next.fields.data.assign(b.data.begin()+fields_start,b.data.begin()+b.pos);
    next.doors=DoorComboSets.size(); next.names=DoorComboSetNames.size();
    next.valid=true; prepared=std::move(next);
    return true;
}
inline bool apply(PreparedState& prepared) {
    if(!prepared.valid || prepared.doors!=DoorComboSets.size() || prepared.names!=DoorComboSetNames.size()) return false;
    prepared.valid=false;
    auto& b=prepared.fields; b.pos=0; b.failed=false;
    Reader r{b};
    init_fields(r,zinit); misc_fields(r,QMisc);
    for(size_t i=0;i<DoorComboSets.size() && !b.failed;++i) { door_fields(r,DoorComboSets[i]); r(DoorComboSetNames[i]); }
    return !b.failed && b.pos==b.data.size();
}
inline bool get(SaveStateBlob& b,bool commit) {
    PreparedState prepared;
    if(!prepare(b,prepared)) return false;
    return !commit || apply(prepared);
}
}
