#pragma once
// Complete value snapshots of quest definition data. Each section is self-contained;
// no mutation-hook coverage assumptions or process-local baseline are required.
#include "base/combo.h"
#include "base/dmap.h"
#include "base/mapscr.h"
#include "base/msgstr.h"
#include "base/qrs.h"
#include "items.h"
#include "tiles.h"
#include "zc/zelda.h"
#include "zc/savestate_serialize.h"
#include <zlib.h>
#include "xxhash.h"
#include "snapshot_profile.h"
#include "snapshot_tile_tracking.h"
#include "snapshot_map_tracking.h"
#include "snapshot_combo_tracking.h"
#include <limits>
#include <array>
#include <list>
#include <unordered_map>
#include <iterator>
#include <memory>
#include <thread>
// Intrinsic declarations must be global: tile blocks are included inside the
// snapshot_definitions namespace. Restrict this optional fast path to MSVC x64.
#if defined(_MSC_VER) && defined(_M_X64)
#include <emmintrin.h>
#endif

namespace snapshot_definitions {
constexpr uint32_t VERSION = 3;
constexpr uint32_t MAX_SECTION_BYTES = 512u * 1024u * 1024u;
struct Writer {
    SaveStateBlob& b;
    template<class T> void one(const T& v) { static_assert(std::is_trivially_copyable_v<T>); b.put(v); }
    void one(const std::string& v) { b.put_str(v); }
    void one(const zfix& v) { b.put(v.getZLong()); }
    template<class... T> void operator()(const T&... v) { (one(v), ...); }
};
template<class Blob> struct BasicReader {
    Blob& b;
    template<class T> void one(T& v) { static_assert(std::is_trivially_copyable_v<T>); b.get(v); }
    void one(std::string& v) { b.get_str(v); }
    void one(zfix& v) { int32_t n=0; b.get(n); v=zslongToFix(n); }
    template<class... T> void operator()(T&... v) { (one(v), ...); }
};
using Reader=BasicReader<SaveStateBlob>;
template<class Blob> struct BasicSkip {
    Blob& b;
    void bytes(size_t n) { if (b.failed || b.pos>b.data.size() || n > b.data.size()-b.pos) b.failed=true; else b.pos += n; }
    template<class T> void one(const T&) { static_assert(std::is_trivially_copyable_v<T>); bytes(sizeof(T)); }
    void one(const std::string&) { uint32_t n=0; b.get(n); bytes(n); }
    void one(const zfix&) { bytes(sizeof(int32_t)); }
    template<class... T> void operator()(const T&... v) { (one(v), ...); }
};
using Skip=BasicSkip<SaveStateBlob>;
template<class S> inline void fields(S& s, itemdata& v) {
    s(v.tile, v.misc_flags, v.csets, v.frames, v.speed, v.delay, v.ltm);
    s(v.family, v.fam_type, v.power, v.flags, v.script, v.count, v.amount);
    s(v.setmax, v.max, v.playsound, v.collect_script, v.initiald, v.initiala, v.wpn);
    s(v.wpn2, v.wpn3, v.wpn4, v.wpn5, v.wpn6, v.wpn7, v.wpn8);
    s(v.wpn9, v.wpn10, v.pickup_hearts, v.misc1, v.misc2, v.misc3, v.misc4);
    s(v.misc5, v.misc6, v.misc7, v.misc8, v.misc9, v.misc10, v.cost_amount);
    s(v.usesound, v.usesound2, v.burnsprs, v.light_rads, v.useweapon, v.usedefence, v.weap_pattern);
    s(v.weaprange, v.weapduration, v.duplicates, v.wpn_misc_d, v.weap_initiald, v.weap_initiala, v.drawlayer);
    s(v.collectflags, v.hxofs, v.hyofs, v.hxsz, v.hysz, v.hzsz, v.xofs);
    s(v.yofs, v.weap_hxofs, v.weap_hyofs, v.weap_hxsz, v.weap_hysz, v.weap_hzsz, v.weap_xofs);
    s(v.weap_yofs, v.tilew, v.tileh, v.weap_tilew, v.weap_tileh, v.pickup, v.pickupflag);
    s(v.pstring, v.pickup_string_flags, v.overrideFLAGS, v.weapoverrideFLAGS, v.weaponscript, v.wpnsprite, v.magiccosttimer);
    s(v.cost_counter, v.initD_label, v.weapon_initD_label, v.sprite_initD_label, v.sprite_initiald, v.sprite_initiala, v.sprite_script);
    s(v.display_name);
}

template<class S> inline void fields(S& s, newcombo& v) {
    s(v.tile, v.flip, v.walk, v.type, v.csets, v.frames, v.speed);
    s(v.nextcombo, v.nextcset, v.flag, v.skipanim, v.nexttimer, v.skipanimy, v.animflags);
    s(v.expansion, v.attributes, v.usrflags, v.genflags, v.triggerflags, v.triggerlevel, v.triggerbtn);
    s(v.triggeritem, v.trigtimer, v.trigsfx, v.trigchange, v.trigprox, v.trigctr, v.trigctramnt);
    s(v.triglbeam, v.trigcschange, v.spawnitem, v.spawnenemy, v.exstate, v.exdoor_dir, v.exdoor_ind);
    s(v.spawnip, v.trigcopycat, v.trigcooldown, v.trig_lstate, v.trig_gstate, v.trig_statetime, v.trig_genscr);
    s(v.trig_group, v.trig_group_val, v.liftflags, v.liftlvl, v.liftsfx, v.liftitm, v.liftgfx);
    s(v.liftcmb, v.liftundercmb, v.liftcs, v.liftundercs, v.liftsprite, v.liftdmg, v.liftbreaksprite);
    s(v.liftbreaksfx, v.lifthei, v.lifttime, v.lift_parent_item, v.prompt_cid, v.prompt_cs, v.prompt_x);
    s(v.prompt_y, v.label, v.attribytes, v.attrishorts, v.script, v.initd, v.o_tile);
    s(v.cur_frame, v.aclk, v.speed_mult, v.speed_div, v.speed_add, v.sfx_appear, v.sfx_disappear);
    s(v.sfx_loop, v.sfx_walking, v.sfx_standing, v.sfx_tap, v.spr_appear, v.spr_disappear, v.spr_walking);
    s(v.spr_standing);
}

template<class S> inline void fields(S& s, dmap& v) {
    s(v.map, v.level, v.xoff, v.compass, v.color, v.midi, v.cont);
    s(v.type, v.grid, v.name, v.title, v.intro, v.minimap_1_tile, v.minimap_1_cset);
    s(v.minimap_2_tile, v.minimap_2_cset, v.largemap_1_tile, v.largemap_1_cset, v.largemap_2_tile, v.largemap_2_cset, v.tmusic);
    s(v.tmusictrack, v.active_subscreen, v.passive_subscreen, v.overlay_subscreen, v.disableditems, v.flags, v.sideview);
    s(v.script, v.initD, v.initD_label, v.active_sub_script, v.passive_sub_script, v.sub_initD, v.sub_initD_label);
    s(v.onmap_script, v.onmap_initD, v.onmap_initD_label, v.mirrorDMap, v.tmusic_loop_start, v.tmusic_loop_end, v.tmusic_xfade_in);
    s(v.tmusic_xfade_out, v.intro_string_id);
}

template<class S> inline void fields(S& s, comboclass& v) {
    s(v.name, v.block_enemies, v.block_hole, v.block_trigger, v.block_weapon, v.conveyor_x_speed, v.conveyor_y_speed);
    s(v.create_enemy, v.create_enemy_when, v.create_enemy_change, v.directional_change_type, v.distance_change_tiles, v.dive_item, v.dock);
    s(v.fairy, v.ff_combo_attr_change, v.foot_decorations_tile, v.foot_decorations_type, v.hookshot_grab_point, v.ladder_pass, v.lock_block_type);
    s(v.lock_block_change, v.magic_mirror_type, v.modify_hp_amount, v.modify_hp_delay, v.modify_hp_type, v.modify_mp_amount, v.modify_mp_delay);
    s(v.modify_mp_type, v.no_push_blocks, v.overhead, v.place_enemy, v.push_direction, v.push_weight, v.push_wait);
    s(v.pushed, v.raft, v.reset_room, v.save_point_type, v.screen_freeze_type, v.secret_combo, v.singular);
    s(v.slow_movement, v.statue_type, v.step_type, v.step_change_to, v.strike_weapons, v.strike_remnants, v.strike_remnants_type);
    s(v.strike_change, v.strike_item, v.touch_item, v.touch_stairs, v.trigger_type, v.trigger_sensitive, v.warp_type);
    s(v.warp_sensitive, v.warp_direct, v.warp_location, v.water, v.whistle, v.win_game, v.block_weapon_lvl);
    s(v.pit);
}

template<class S> inline void fields(S& s, guydata& v) {
    s(v.flags, v.flags2, v.tile, v.width, v.height, v.s_tile, v.s_width);
    s(v.s_height, v.e_tile, v.e_width, v.e_height, v.hp, v.family, v.cset);
    s(v.anim, v.e_anim, v.frate, v.e_frate, v.dp, v.wdp, v.weapon);
    s(v.rate, v.hrate, v.step, v.homing, v.grumble, v.item_set, v.misc1);
    s(v.misc2, v.misc3, v.misc4, v.misc5, v.misc6, v.misc7, v.misc8);
    s(v.misc9, v.misc10, v.misc11, v.misc12, v.misc13, v.misc14, v.misc15);
    s(v.bgsfx, v.bosspal, v.extend, v.defense, v.hitsfx, v.deadsfx, v.xofs);
    s(v.yofs, v.zofs, v.hxofs, v.hyofs, v.hxsz, v.hysz, v.hzsz);
    s(v.txsz, v.tysz, v.scriptdefense, v.wpnsprite, v.SIZEflags, v.frozentile, v.frozencset);
    s(v.frozenclock, v.frozenmisc, v.firesfx, v.misc16, v.misc17, v.misc18, v.misc19);
    s(v.misc20, v.misc21, v.misc22, v.misc23, v.misc24, v.misc25, v.misc26);
    s(v.misc27, v.misc28, v.misc29, v.misc30, v.misc31, v.misc32, v.movement);
    s(v.new_weapon, v.initD, v.initA, v.script, v.editorflags, v.moveflags, v.initD_label);
    s(v.weapon_initD_label, v.weaponscript, v.weap_initiald, v.weap_initiala, v.spr_shadow, v.spr_death, v.spr_spawn);
}

template<class S> inline void fields(S& s, wpndata& v) {
    s(v.tile, v.misc, v.csets, v.frames, v.speed, v.type, v.script);
}

template<class S> inline void fields(S& s, MsgStr& v) {
    s(v.s, v.nextstring, v.tile, v.cset, v.trans, v.font, v.x);
    s(v.y, v.w, v.h, v.sfx, v.listpos, v.vspace, v.hspace);
    s(v.stringflags, v.margins, v.portrait_tile, v.portrait_cset, v.portrait_x, v.portrait_y, v.portrait_tw);
    s(v.portrait_th, v.shadow_type, v.shadow_color, v.drawlayer);
}

template<class S> inline void fields(S& s, item_drop_object& v) {
    s(v.name, v.item, v.chance);
}

// Cached *complete* encoding only avoids recompression; memcmp examines every byte
// each capture. Equality is exact rather than a probabilistic hash match.
struct Cache { std::vector<uint8_t> plain, compressed; };
inline std::array<Cache, 4>& caches() { static std::array<Cache,4> c; return c; }
inline void put_cached(SaveStateBlob& out, size_t section) {
    auto& c=caches()[section]; out.put((uint32_t)c.plain.size()); out.put((uint32_t)c.compressed.size());
    out.put(c.compressed.data(),c.compressed.size());
}
// Object representation is used ONLY as an exact change detector, never as
// snapshot data. Every string's contents are compared too (in-place edits can
// preserve its address/size/capacity). Extra padding differences merely cause
// unnecessary recapture; they can never hide a changed value.
inline std::vector<uint8_t> definitions_stamp() {
    SaveStateBlob b;
    b.put(itemsbuf,MAXITEMS*sizeof(itemdata)); b.put(wpnsbuf,MAXWPNS*sizeof(wpndata));
    b.put(guysbuf,MAXGUYS*sizeof(guydata)); b.put(combo_class_buf,cMAX*sizeof(comboclass));
    b.put(DMaps,MAXDMAPS*sizeof(dmap)); b.put(item_drop_sets,sizeof(item_drop_sets));
    b.put(MsgStrings,msg_count*sizeof(MsgStr)); b.put(quest_rules); b.put(extra_rules);
    for(size_t i=0;i<MAXDMAPS;++i) b.put_str(DMaps[i].title);
    for(size_t i=0;i<msg_count;++i) b.put_str(MsgStrings[i].s);
    return std::move(b.data);
}
// Compare live spans/deep strings directly against the exact capture stamp.
// The common unchanged case allocates and copies no replacement stamp.
inline bool definitions_live_equal(const std::vector<uint8_t>& stamp) {
    size_t pos=0;
    auto span=[&](const void* data,size_t bytes) {
        if(pos>stamp.size() || bytes>stamp.size()-pos) return false;
        if(bytes && memcmp(stamp.data()+pos,data,bytes)!=0) return false;
        pos+=bytes; return true;
    };
    if(!span(itemsbuf,MAXITEMS*sizeof(itemdata)) || !span(wpnsbuf,MAXWPNS*sizeof(wpndata))
        || !span(guysbuf,MAXGUYS*sizeof(guydata)) || !span(combo_class_buf,cMAX*sizeof(comboclass))
        || !span(DMaps,MAXDMAPS*sizeof(dmap)) || !span(item_drop_sets,sizeof(item_drop_sets))
        || !span(MsgStrings,msg_count*sizeof(MsgStr)) || !span(quest_rules,sizeof(quest_rules))
        || !span(extra_rules,sizeof(extra_rules))) return false;
    auto string=[&](const std::string& value) {
        if(value.size()>UINT32_MAX) return false;
        const uint32_t bytes=uint32_t(value.size());
        return span(&bytes,sizeof(bytes)) && span(value.data(),value.size());
    };
    for(size_t i=0;i<MAXDMAPS;++i) if(!string(DMaps[i].title)) return false;
    for(size_t i=0;i<msg_count;++i) if(!string(MsgStrings[i].s)) return false;
    return pos==stamp.size();
}
constexpr size_t COMBO_CHUNK_RECORDS = 256;
struct ComboEncodingSpan { size_t object_offset,encoded_offset,bytes; bool after_label; };
struct ComboEncodingFix { size_t object_offset,encoded_offset; bool after_label; };
struct ComboEncodingLayout {
    const newcombo& base;
    std::vector<ComboEncodingSpan> spans;
    std::vector<ComboEncodingFix> fixes;
    size_t cursor=0,prefix=0;
    bool after_label=false,valid=true;
    template<class T> void one(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto origin=reinterpret_cast<uintptr_t>(&base);
        const auto address=reinterpret_cast<uintptr_t>(&value);
        if(address<origin || address-origin>sizeof(newcombo)
            || sizeof(T)>sizeof(newcombo)-(address-origin)) { valid=false;return; }
        const size_t offset=address-origin;
        if(!spans.empty() && spans.back().after_label==after_label
            && spans.back().object_offset+spans.back().bytes==offset
            && spans.back().encoded_offset+spans.back().bytes==cursor)
            spans.back().bytes+=sizeof(T);
        else spans.push_back({offset,cursor,sizeof(T),after_label});
        cursor+=sizeof(T);
    }
    void one(const std::string& value) {
        if(&value!=&base.label || after_label) valid=false;
        prefix=cursor;cursor=0;after_label=true;
    }
    // Keep fixed-point conversion identical to Writer; never copy its object.
    void one(const zfix& value) {
        const auto origin=reinterpret_cast<uintptr_t>(&base);
        const auto address=reinterpret_cast<uintptr_t>(&value);
        if(address<origin || address-origin>sizeof(newcombo)
            || sizeof(zfix)>sizeof(newcombo)-(address-origin)) { valid=false;return; }
        fixes.push_back({address-origin,cursor,after_label});cursor+=sizeof(int32_t);
    }
    template<class... T> void operator()(const T&... value) { (one(value),...); }
};
inline const ComboEncodingLayout& combo_encoding_layout() {
    static const ComboEncodingLayout layout=[] {
        ComboEncodingLayout result{combobuf[0]};fields(result,combobuf[0]);
        result.valid=result.valid && result.after_label;return result;
    }();
    return layout;
}
inline bool& stored_combo_encoding() { static bool enabled=true;return enabled; }
struct ComboChunkCache {
    // These process-local bytes only detect changes. Snapshot values still
    // come exclusively from fields(), with labels stored as strings.
    std::vector<uint8_t> object_stamp, compressed, plain;
    std::vector<std::string> labels;
    // Boundaries in plain let changed chunks reuse exact encoded records.
    // This cache never appears on disk and never replaces live comparisons.
    std::vector<size_t> record_offsets;
    uint32_t plain_size = 0;
    bool stored=false;
    uint64_t tracking_epoch=0,tracking_version=0;
};
inline std::vector<ComboChunkCache>& combo_chunk_caches() { static std::vector<ComboChunkCache> value; return value; }
inline bool combo_chunk_live_equal(const ComboChunkCache& cache,size_t first,size_t count) {
    size_t bytes=count*sizeof(newcombo);
    if(!cache.plain_size || cache.object_stamp.size()!=bytes || cache.labels.size()!=count
        || memcmp(cache.object_stamp.data(),combobuf.data()+first,bytes)!=0) return false;
    for(size_t i=0;i<count;++i) if(cache.labels[i]!=combobuf[first+i].label) return false;
    return true;
}
static_assert(COMBO_CHUNK_RECORDS==snapshot_combo_tracking::block_records,"combo tracking shape");
// Temporary only: swaps receive the previous chunk allocations after complete
// encoding succeeds. Bound retained scratch on every return/exception; oversized
// valid records still encode normally, then relinquish temporary storage.
struct ComboEncodingScratch {
    static constexpr size_t LIMIT=2u*1024u*1024u;
    SaveStateBlob raw;
    std::vector<uint8_t> compressed;
    std::vector<size_t> offsets;
    size_t retained_bytes() const {
        size_t total=raw.data.capacity();
        if(total>LIMIT || compressed.capacity()>LIMIT-total) return SIZE_MAX;
        total+=compressed.capacity();
        if(offsets.capacity()>(LIMIT-total)/sizeof(size_t)) return SIZE_MAX;
        return total+offsets.capacity()*sizeof(size_t);
    }
    void reset() {
        raw.data.clear();raw.pos=0;raw.failed=false;offsets.clear();
        // Keep compressed.size(): the compressor overwrites every published
        // output byte; resize need not zero the entire buffer on warm calls.
    }
    void trim() noexcept {
        if(retained_bytes()>LIMIT) {
            std::vector<uint8_t>().swap(raw.data);
            std::vector<uint8_t>().swap(compressed);
            std::vector<size_t>().swap(offsets);
        }
    }
};
inline ComboEncodingScratch& combo_encoding_scratch() {
    static thread_local ComboEncodingScratch scratch;return scratch;
}
inline bool put_combo_chunks(SaveStateBlob& out) {
    auto& scratch=combo_encoding_scratch();
    struct TrimOnExit {ComboEncodingScratch& s;~TrimOnExit(){s.trim();}} trim{scratch};
    auto& cached=combo_chunk_caches();
    if(combobuf.size()>MAXCOMBOS) return false;
    const size_t chunks=(combobuf.size()+COMBO_CHUNK_RECORDS-1)/COMBO_CHUNK_RECORDS;
    if(cached.size()!=chunks) cached.resize(chunks);
    out.put((uint32_t)chunks);
    size_t total_plain=0;
    for(size_t index=0;index<chunks;++index) {
        const size_t first=index*COMBO_CHUNK_RECORDS;
        const size_t count=std::min(COMBO_CHUNK_RECORDS,combobuf.size()-first);
        auto& cache=cached[index];
        const size_t bytes=count*sizeof(newcombo);
        auto& tracking=snapshot_combo_tracking::state();
        const uint64_t epoch=tracking.epoch,version=tracking.versions[index];
        const bool clean=tracking.trusted&&cache.plain_size&&!cache.compressed.empty()
            &&cache.object_stamp.size()==bytes&&cache.labels.size()==count
            &&cache.tracking_epoch==epoch&&cache.tracking_version==version;
        bool unchanged=clean;
        if(!clean||snapshot_combo_tracking::audit()) {
            unchanged=combo_chunk_live_equal(cache,first,count);
            ++tracking.checked;
            if(clean&&!unchanged)snapshot_combo_tracking::missed_write(index);
        }else ++tracking.skipped;
        if(!unchanged || (cache.stored && !stored_combo_encoding())) {
            // Cold/static blocks stay compressed. Only repeatedly changing
            // records use stored streams while the transport budget permits.
            const bool stored=stored_combo_encoding() && cache.plain_size!=0;
            scratch.reset();
            auto& raw=scratch.raw;
            raw.data.reserve(cache.plain_size);
            Writer writer{raw};
            const bool can_reuse=cache.object_stamp.size()==bytes
                && cache.labels.size()==count && cache.record_offsets.size()==count+1
                && cache.record_offsets.front()==0 && cache.record_offsets.back()==cache.plain.size();
            auto& offsets=scratch.offsets;
            offsets.reserve(count+1);
            const auto& layout=combo_encoding_layout();
            for(size_t i=0;i<count;++i) {
                offsets.push_back(raw.data.size());
                const size_t begin=can_reuse ? cache.record_offsets[i] : 0;
                const size_t end=can_reuse ? cache.record_offsets[i+1] : 0;
                if(can_reuse && begin<=end && end<=cache.plain.size()
                    && memcmp(cache.object_stamp.data()+i*sizeof(newcombo),
                              &combobuf[first+i],sizeof(newcombo))==0
                    && cache.labels[i]==combobuf[first+i].label) {
                    raw.put(cache.plain.data()+begin,end-begin);
                } else if(can_reuse && layout.valid && begin<=end && end<=cache.plain.size()
                    && cache.labels[i]==combobuf[first+i].label
                    && end-begin==layout.prefix+sizeof(uint32_t)+cache.labels[i].size()+layout.cursor) {
                    // Copy the exact previous value record, then replace every
                    // POD field from the live object. Spans come from fields()
                    // itself; object padding and pointers are never substituted
                    // for the explicitly encoded string or converted values.
                    const size_t output=raw.data.size();
                    raw.put(cache.plain.data()+begin,end-begin);
                    const auto object=reinterpret_cast<const uint8_t*>(&combobuf[first+i]);
                    for(const auto& span:layout.spans) {
                        const size_t tail=span.after_label?layout.prefix+sizeof(uint32_t)+cache.labels[i].size():0;
                        memcpy(raw.data.data()+output+tail+span.encoded_offset,object+span.object_offset,span.bytes);
                    }
                    for(const auto& fix:layout.fixes) {
                        const size_t tail=fix.after_label?layout.prefix+sizeof(uint32_t)+cache.labels[i].size():0;
                        const int32_t value=reinterpret_cast<const zfix*>(object+fix.object_offset)->getZLong();
                        memcpy(raw.data.data()+output+tail+fix.encoded_offset,&value,sizeof(value));
                    }
                } else fields(writer,combobuf[first+i]);
                if(raw.failed) return false;
            }
            offsets.push_back(raw.data.size());
            if(raw.failed || raw.data.empty() || raw.data.size()>MAX_SECTION_BYTES) return false;
            uLongf length=compressBound((uLong)raw.data.size());
            auto& compressed=scratch.compressed;compressed.resize(length);
            const int status=stored
                ? snapshot_profile::measured_store_zlib(compressed.data(),&length,raw.data.data(),uLong(raw.data.size()))
                : snapshot_profile::measured_compress2(compressed.data(),&length,raw.data.data(),uLong(raw.data.size()),Z_BEST_SPEED);
            if(status!=Z_OK) return false;
            compressed.resize(length);
            std::vector<uint8_t> stamp;
            const bool reuse_stamp=cache.object_stamp.size()==bytes;
            if(!reuse_stamp) {stamp.resize(bytes);memcpy(stamp.data(),combobuf.data()+first,bytes);}
            bool same_labels=cache.labels.size()==count;
            if(same_labels) for(size_t i=0;i<count;++i) if(cache.labels[i]!=combobuf[first+i].label) {same_labels=false;break;}
            std::vector<std::string> labels;
            if(!same_labels) {
                labels.reserve(count);
                for(size_t i=0;i<count;++i) labels.push_back(combobuf[first+i].label);
            }
            // Publish only a fully built cache; failed encoding never exposes
            // a mixture of old values and a new change stamp.
            if(reuse_stamp) memcpy(cache.object_stamp.data(),combobuf.data()+first,bytes);
            else cache.object_stamp=std::move(stamp);
            if(!same_labels) cache.labels=std::move(labels);
            cache.plain_size=(uint32_t)raw.data.size();
            cache.compressed.swap(compressed);
            cache.plain.swap(raw.data);
            cache.record_offsets.swap(offsets);
            cache.stored=stored;
        }
        if(cache.plain_size>MAX_SECTION_BYTES-total_plain) return false;
        total_plain+=cache.plain_size;
        cache.tracking_epoch=epoch;cache.tracking_version=version;
        out.put((uint32_t)count); out.put(cache.plain_size);
        out.put((uint32_t)cache.compressed.size());
        out.put(cache.compressed.data(),cache.compressed.size());
    }
    return !out.failed;
}
inline bool compress_section(SaveStateBlob& out, SaveStateBlob& raw, size_t section) {
    if(raw.failed || raw.data.size()>MAX_SECTION_BYTES) return false;
    auto& c=caches()[section];
    if(c.plain != raw.data) {
        uLongf len=compressBound((uLong)raw.data.size());
        std::vector<uint8_t> packed(len);
        if(snapshot_profile::measured_compress2(packed.data(), &len, raw.data.data(), (uLong)raw.data.size(), Z_BEST_SPEED)!=Z_OK) return false;
        packed.resize(len); c.plain=std::move(raw.data); c.compressed=std::move(packed);
    }
    put_cached(out,section); return true;
}
// Cached decoded bytes remain subject to every caller's normal shape/value
// validation. Hashes only select candidates: exact packed bytes and unpacked
// length must also match. The LRU has bounded storage and bounded entry count.
struct DecodedSectionEntry {
    uint64_t hash=0;
    std::vector<uint8_t> packed,plain;
    size_t cost() const { return packed.size()+plain.size()+192; }
};
struct DecodedSectionCache {
    std::list<DecodedSectionEntry> entries;
    std::unordered_multimap<uint64_t,std::list<DecodedSectionEntry>::iterator> index;
    size_t bytes=0;
    uint64_t hits=0,misses=0;
};
inline DecodedSectionCache& decoded_section_cache() { static DecodedSectionCache cache; return cache; }
inline uint64_t definition_decode_cache_hits() { return decoded_section_cache().hits; }
inline uint64_t definition_decode_cache_misses() { return decoded_section_cache().misses; }
inline size_t definition_decode_cache_bytes() { return decoded_section_cache().bytes; }
inline void clear_definition_decode_cache() {
    auto& cache=decoded_section_cache(); cache.index.clear(); cache.entries.clear(); cache.bytes=0;
}
inline uint64_t definition_decode_key(const uint8_t* bytes,size_t count,uint32_t unpacked) {
    // Bucket selection only; full byte equality is still mandatory on a hit.
    return XXH64(bytes,count,uint64_t(unpacked));
}
inline bool inflate_section(SaveStateBlob& in, SaveStateBlob& raw) {
    uint32_t unpacked=0, packed=0; in.get(unpacked); in.get(packed);
    if(in.failed || !unpacked || unpacked>MAX_SECTION_BYTES || !packed || packed>in.data.size()-in.pos) return false;
    const uint8_t* source=in.data.data()+in.pos;
    auto& cache=decoded_section_cache();
    uint64_t hash=definition_decode_key(source,packed,unpacked);
    auto candidates=cache.index.equal_range(hash);
    for(auto it=candidates.first;it!=candidates.second;++it) {
        auto entry=it->second;
        if(entry->plain.size()==unpacked && entry->packed.size()==packed
            && memcmp(entry->packed.data(),source,packed)==0) {
            raw.data=entry->plain; raw.pos=0; raw.failed=false;
            cache.entries.splice(cache.entries.end(),cache.entries,entry);
            ++cache.hits; in.pos+=packed; return true;
        }
    }
    ++cache.misses;
    raw.data.resize(unpacked); raw.pos=0; raw.failed=false; uLongf len=unpacked;
    if(snapshot_profile::measured_uncompress(raw.data.data(), &len, source, packed)!=Z_OK || len!=unpacked) return false;
    // Keep 2 MiB of the 128 MiB ceiling for retained hash buckets/allocator
    // bookkeeping; charge another 192 bytes per record conservatively.
    constexpr size_t BUDGET=126u*1024u*1024u,MAX_ENTRIES=16384;
    size_t cost=size_t(unpacked)+packed+192;
    if(cost<=BUDGET) {
        while(!cache.entries.empty() && (cache.bytes>BUDGET-cost || cache.entries.size()>=MAX_ENTRIES)) {
            auto oldest=cache.entries.begin();
            auto range=cache.index.equal_range(oldest->hash);
            for(auto it=range.first;it!=range.second;++it) if(it->second==oldest) { cache.index.erase(it); break; }
            cache.bytes-=oldest->cost(); cache.entries.erase(oldest);
        }
        DecodedSectionEntry entry;
        entry.hash=hash; entry.packed.assign(source,source+packed); entry.plain=raw.data;
        cache.entries.push_back(std::move(entry));
        auto last=std::prev(cache.entries.end());
        try { cache.index.emplace(hash,last); }
        catch(...) { cache.entries.pop_back(); throw; }
        cache.bytes+=cost;
    }
    in.pos+=packed; return true;
}
template<class S> inline void definitions(S& s) {
    for(size_t i=0;i<MAXITEMS;++i) fields(s,itemsbuf[i]);
    for(size_t i=0;i<MAXWPNS;++i) fields(s,wpnsbuf[i]);
    for(size_t i=0;i<MAXGUYS;++i) fields(s,guysbuf[i]);
    for(size_t i=0;i<cMAX;++i) fields(s,combo_class_buf[i]);
    for(size_t i=0;i<MAXDMAPS;++i) fields(s,DMaps[i]);
    for(size_t i=0;i<MAXITEMDROPSETS;++i) fields(s,item_drop_sets[i]);
    for(size_t i=0;i<msg_count;++i) fields(s,MsgStrings[i]);
    s(quest_rules,extra_rules);
}
template<class S> inline void combos(S& s) {
    for(auto& combo:combobuf) fields(s,combo);
}
template<class S> inline void screen(S& s, mapscr& sc) {
    sc.visit_state(s);
    // Inactive slots can later be activated by a script. Preserve those too.
    for(auto& f:sc.ffcs) f.visit_state(s);
    s(sc.usr_notes);
}
#include "snapshot_map_blocks.inc"
#include "snapshot_tile_blocks.inc"
inline std::vector<uint8_t>& definitions_live_stamp_cache() { static std::vector<uint8_t> stamp; return stamp; }
inline uint64_t& definitions_apply_skip_counter() { static uint64_t value=0; return value; }
inline uint64_t& combo_apply_skip_counter() { static uint64_t value=0; return value; }
inline uint64_t definitions_apply_skips() { return definitions_apply_skip_counter(); }
inline uint64_t combo_apply_skips() { return combo_apply_skip_counter(); }
inline bool put(SaveStateBlob& out) {
    if(!itemsbuf || !wpnsbuf || !guysbuf || !combo_class_buf || !DMaps || combobuf.empty() || !newtilebuf || !MsgStrings) return false;
    out.put(VERSION); out.put(msg_count); out.put((uint32_t)combobuf.size());
    { snapshot_profile::ScopedMeasure timer(snapshot_profile::capture_measure().general_us);
      auto& previous_stamp=definitions_live_stamp_cache();
      if(definitions_live_equal(previous_stamp) && !caches()[0].plain.empty()) put_cached(out,0);
      else { SaveStateBlob raw; raw.data.reserve(caches()[0].plain.size()); Writer s{raw}; definitions(s);
        if(!compress_section(out,raw,0)) return false; previous_stamp=definitions_stamp(); }
    }
    { snapshot_profile::ScopedMeasure timer(snapshot_profile::capture_measure().combo_us);
      if(!put_combo_chunks(out)) return false; }
    { snapshot_profile::ScopedMeasure timer(snapshot_profile::capture_measure().maps_us);
      if(!put_map_blocks(out)) return false; }
    { snapshot_profile::ScopedMeasure timer(snapshot_profile::capture_measure().tiles_us);
      if(!put_tile_blocks(out)) return false; }
    return !out.failed;
}
// Prepared state owns decoded, fully validated sections. Preparation does not
// mutate live quest data; commit requires the same fixed allocation shapes.
struct PreparedState {
    std::array<SaveStateBlob,4> raw;
    std::vector<SaveStateBlob> combo_chunks,tile_blocks;
    std::vector<PreparedMapBlob> map_blocks;
    SaveStateBlob tile_metadata;
    std::vector<std::vector<TileRecordDescriptor>> tile_records;
    std::vector<uint8_t> encoded;
    word messages=0;
    size_t combos=0,maps=0;
    decltype(map_count) map_shape=0;
    bool valid=false;
};
struct PreparedStateCache { PreparedState state; uint64_t hits=0; };
inline PreparedStateCache& prepared_state_cache() { static PreparedStateCache cache; return cache; }
inline uint64_t prepared_cache_hits() { return prepared_state_cache().hits; }
inline void clear_prepared_state_cache() { prepared_state_cache().state=PreparedState{}; }
inline bool prepared_shapes_match(const PreparedState& state) {
    return state.messages==msg_count && state.combos==combobuf.size()
        && state.maps==TheMaps.size() && state.map_shape==map_count;
}
inline size_t prepared_storage_bytes(const PreparedState& state) {
    constexpr size_t LIMIT=128u*1024u*1024u;
    size_t bytes=sizeof(PreparedState);
    auto add=[&](size_t amount) { if(amount>LIMIT-bytes) return false; bytes+=amount; return true; };
    if(!add(state.encoded.capacity()) || !add(state.tile_metadata.data.capacity())) return SIZE_MAX;
    for(const auto& blob:state.raw) if(!add(blob.data.capacity())) return SIZE_MAX;
    for(const auto* group:{&state.combo_chunks,&state.tile_blocks}) {
        if(!add(group->capacity()*sizeof(SaveStateBlob))) return SIZE_MAX;
        for(const auto& blob:*group) if(!add(blob.data.capacity())) return SIZE_MAX;
    }
    // Charge full owner capacity, even when capture shares it. This keeps the
    // existing retained-prepared budget conservative across future captures.
    if(!add(state.map_blocks.capacity()*sizeof(PreparedMapBlob))) return SIZE_MAX;
    for(const auto& block:state.map_blocks)
        if(!add(block.data.capacity()) || !add(64)) return SIZE_MAX;
    if(!add(state.tile_records.capacity()*sizeof(std::vector<TileRecordDescriptor>))) return SIZE_MAX;
    for(const auto& records:state.tile_records) if(!add(records.capacity()*sizeof(TileRecordDescriptor))) return SIZE_MAX;
    return bytes;
}
inline size_t prepared_cache_bytes() { return prepared_storage_bytes(prepared_state_cache().state); }
inline void reset_prepared_positions(PreparedState& state) {
    for(auto& b:state.raw) { b.pos=0; b.failed=false; }
    for(auto* group:{&state.combo_chunks,&state.tile_blocks})
        for(auto& b:*group) { b.pos=0; b.failed=false; }
    for(auto& b:state.map_blocks) {b.pos=0;b.failed=false;}
    state.tile_metadata.pos=0; state.tile_metadata.failed=false;
}
inline bool prepare(SaveStateBlob& in, PreparedState& prepared) {
    prepared.valid=false;
    const size_t begin=in.pos;
    auto& memo=prepared_state_cache();
    // Reuse successful structural validation only for the identical complete
    // encoded section and identical allocation shapes. No hash is involved.
    if(!in.failed && in.pos<=in.data.size() && memo.state.valid && prepared_shapes_match(memo.state)
        && !memo.state.encoded.empty() && memo.state.encoded.size()<=in.data.size()-in.pos
        && memcmp(memo.state.encoded.data(),in.data.data()+in.pos,memo.state.encoded.size())==0) {
        in.pos+=memo.state.encoded.size(); prepared=std::move(memo.state); memo.state=PreparedState{};
        reset_prepared_positions(prepared); ++memo.hits; return true;
    }
    PreparedState next;
    uint32_t version=0, combo_count=0; word messages=0; in.get(version); in.get(messages); in.get(combo_count);
    if(in.failed || version!=VERSION || messages!=msg_count || combo_count!=combobuf.size()) return false;
    auto& raw=next.raw;
    if(!inflate_section(in,raw[0])) return false;
    uint32_t chunk_count=0; in.get(chunk_count);
    const size_t expected_chunks=(combobuf.size()+COMBO_CHUNK_RECORDS-1)/COMBO_CHUNK_RECORDS;
    if(in.failed || combo_count>MAXCOMBOS || chunk_count!=expected_chunks) return false;
    auto& combo_chunks=next.combo_chunks; combo_chunks.resize(chunk_count);
    size_t total_combo_plain=0;
    for(size_t index=0;index<chunk_count;++index) {
        const size_t first=index*COMBO_CHUNK_RECORDS;
        const size_t expected_count=std::min(COMBO_CHUNK_RECORDS,combobuf.size()-first);
        uint32_t count=0,plain_size=0;
        in.get(count);
        if(in.failed || count!=expected_count || in.pos>in.data.size() || in.data.size()-in.pos<sizeof(uint32_t)*2) return false;
        memcpy(&plain_size,in.data.data()+in.pos,sizeof(plain_size));
        if(plain_size>MAX_SECTION_BYTES-total_combo_plain) return false;
        total_combo_plain+=plain_size;
        if(!inflate_section(in,combo_chunks[index])) return false;
        Skip skip{combo_chunks[index]};
        for(size_t i=0;i<expected_count;++i) fields(skip,combobuf[first+i]);
        if(combo_chunks[index].failed || combo_chunks[index].pos!=combo_chunks[index].data.size()) return false;
    }
    auto& map_blocks=next.map_blocks; auto& tile_blocks=next.tile_blocks;
    auto& tile_metadata=next.tile_metadata;
    if(!get_map_blocks(in,map_blocks) || !get_tile_blocks(in,tile_blocks,tile_metadata)) return false;
    { Skip s{raw[0]}; definitions(s); if(raw[0].failed || raw[0].pos!=raw[0].data.size()) return false; }
    next.messages=msg_count; next.combos=combobuf.size();
    next.maps=TheMaps.size(); next.map_shape=map_count;
    next.tile_records.resize(next.tile_blocks.size());
    for(size_t index=0;index<next.tile_blocks.size();++index) {
        auto& records=next.tile_records[index]; const auto& bytes=next.tile_blocks[index].data;
        const size_t count=std::min(TILE_BLOCK_RECORDS,size_t(NEWMAXTILES)-index*TILE_BLOCK_RECORDS);
        records.reserve(count); size_t pos=0;
        for(size_t i=0;i<count;++i) {
            byte format=bytes[pos++],present=bytes[pos++];
            const size_t size=present?tilesize(format):0;
            records.push_back({uint32_t(pos),uint16_t(size),format,present}); pos+=size;
        }
    }
    constexpr size_t LIMIT=128u*1024u*1024u;
    size_t storage=prepared_storage_bytes(next);
    if(storage<=LIMIT && in.pos-begin<=LIMIT-storage)
        next.encoded.assign(in.data.begin()+begin,in.data.begin()+in.pos);
    next.valid=true; prepared=std::move(next); return true;
}
inline bool apply(PreparedState& prepared) {
    if(!prepared.valid || !prepared_shapes_match(prepared)) return false;
    // Both ordinary get(commit=true) and prepared restores use this commit.
    // Invalidate before any definition/combo mutation or skip decision.
    snapshot_combo_tracking::invalidate_all();
    // Consume the token: callers must prepare again before another commit.
    prepared.valid=false;
    auto& raw=prepared.raw; auto& combo_chunks=prepared.combo_chunks;
    auto& map_blocks=prepared.map_blocks; auto& tile_blocks=prepared.tile_blocks;
    auto& tile_metadata=prepared.tile_metadata;
    for(auto& b:raw) b.pos=0;
    if(definitions_live_equal(definitions_live_stamp_cache()) && caches()[0].plain==raw[0].data) {
        ++definitions_apply_skip_counter(); raw[0].pos=raw[0].data.size();
    } else {
        Reader s{raw[0]}; definitions(s);
        definitions_live_stamp_cache().clear();
        if(raw[0].failed || raw[0].pos!=raw[0].data.size()) return false;
    }
    for(size_t index=0;index<combo_chunks.size();++index) {
        const size_t first=index*COMBO_CHUNK_RECORDS;
        const size_t count=std::min(COMBO_CHUNK_RECORDS,combobuf.size()-first);
        auto& entries=combo_chunk_caches();
        ComboChunkCache* cache=index<entries.size() ? &entries[index] : nullptr;
        if(cache && combo_chunk_live_equal(*cache,first,count) && cache->plain==combo_chunks[index].data) {
            ++combo_apply_skip_counter(); combo_chunks[index].pos=combo_chunks[index].data.size(); continue;
        }
        combo_chunks[index].pos=0;
        Reader reader{combo_chunks[index]};
        for(size_t i=0;i<count;++i) fields(reader,combobuf[first+i]);
        if(cache) { cache->object_stamp.clear(); cache->labels.clear(); cache->compressed.clear(); cache->plain.clear(); cache->record_offsets.clear(); cache->plain_size=0; }
        if(combo_chunks[index].failed || combo_chunks[index].pos!=combo_chunks[index].data.size()) return false;
    }
    // Maps were fully traversed by prepare. Only compare live values and apply
    // their already-validated encodings here, without another Skip traversal.
    const auto map_skips=map_restore_skips(map_blocks);
    bool maps_invalidated=false;
    for(size_t index=0;index<map_blocks.size();++index) {
        auto& b=map_blocks[index]; auto& sc=TheMaps[index];
        auto& entries=map_block_caches();
        MapBlockCache* cache=index<entries.size()?&entries[index]:nullptr;
        if(map_skips[index]) { b.pos=b.data.size(); continue; }
        if(!maps_invalidated) {snapshot_map_tracking::invalidate_all();maps_invalidated=true;}
        b.pos=0; BasicReader<PreparedMapBlob> reader{b}; screen(reader,sc);
        if(cache) cache->compressed.clear();
        if(b.failed || b.pos!=b.data.size()) return false;
    }
    if(!apply_prepared_tile_blocks(tile_blocks,tile_metadata,prepared.tile_records)) return false;
    // Preserve decoded buffers after success instead of repeatedly copying and
    // freeing them. Changed/malformed sequences always use normal preparation.
    if(!prepared.encoded.empty() && prepared_storage_bytes(prepared)<=128u*1024u*1024u) {
        prepared.valid=true; prepared_state_cache().state=std::move(prepared); prepared=PreparedState{};
    }
    return true;
}
inline bool get(SaveStateBlob& in, bool commit) {
    PreparedState prepared;
    if(!prepare(in,prepared)) return false;
    return !commit || apply(prepared);
}
} // namespace snapshot_definitions
