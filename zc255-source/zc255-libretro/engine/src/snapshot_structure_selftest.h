#pragma once
#include "snapshot_definitions.h"
#include "snapshot_runs_selftest.h"
#include <cstdio>
#include <cstdlib>

// Direct-write regressions: these changes bypass all mutation notifications.
// Run only on request at a stable gameplay checkpoint; always restore baseline.
inline void zc255_structure_selftest() {
    static bool done=false;
    const char* report=std::getenv("ZC255_STRUCTURES_TEST");
    if(done || !report || !*report || !Playing || TheMaps.empty() || !newtilebuf) return;
    done=true;
    size_t first_tile=NEWMAXTILES, later_tile=NEWMAXTILES;
    for(size_t i=0;i<NEWMAXTILES;++i) {
        if(!newtilebuf[i].data || !is_valid_format(newtilebuf[i].format) || tilesize(newtilebuf[i].format)<=0) continue;
        if(first_tile==NEWMAXTILES) first_tile=i;
        else if(i/snapshot_definitions::TILE_BLOCK_RECORDS != first_tile/snapshot_definitions::TILE_BLOCK_RECORDS)
            later_tile=i;
    }
    const size_t later_screen=TheMaps.size()>1 ? TheMaps.size()-1 : 0;
    const bool have_tile=first_tile<NEWMAXTILES;
    const bool have_later_tile=later_tile<NEWMAXTILES;
    const bool have_later_screen=later_screen!=0;
    // numFFC() updates private cached metadata; inspect the unused data slot
    // without changing that cache, then mutate without any dirty notification.
    const bool inactive_slot=TheMaps[0].ffcs[MAXFFCS-1].data==0;
    const byte initial_pixel=have_tile ? newtilebuf[first_tile].data[0] : 0;
    const byte later_pixel=have_later_tile ? newtilebuf[later_tile].data[0] : 0;
    const word initial_combo=TheMaps[0].data[0];
    const word initial_ffc=TheMaps[0].ffcs[MAXFFCS-1].data;
    const word later_combo=TheMaps[later_screen].data[0];
    const int initial_power=itemsbuf[0].power;

    auto load=[](const SaveStateBlob& encoded) {
        SaveStateBlob copy=encoded;
        return snapshot_definitions::get(copy,true) && !copy.failed && copy.pos==copy.data.size();
    };
    auto put=[](SaveStateBlob& encoded) {
        return snapshot_definitions::put(encoded) && !encoded.failed;
    };
    SaveStateBlob original, identical;
    bool baseline=put(original);
    uint64_t tile_count=snapshot_definitions::tile_block_rebuilds();
    uint64_t map_count_before=snapshot_definitions::map_block_rebuilds();
    bool same_encoded=baseline && put(identical);
    uint64_t same_tiles=snapshot_definitions::tile_block_rebuilds()-tile_count;
    uint64_t same_maps=snapshot_definitions::map_block_rebuilds()-map_count_before;
    bool same=same_encoded && identical.data==original.data && same_tiles==0 && same_maps==0;

    bool single_tile=false, single_map=false, tile_roundtrip=false, map_roundtrip=false;
    bool later_blocks=true, later_roundtrip=true;
    uint64_t tile_delta=0, tile_map_delta=0, map_delta=0, map_tile_delta=0;
    uint64_t later_tile_delta=0, later_map_delta=0;
    if(baseline && have_tile) {
        tile_count=snapshot_definitions::tile_block_rebuilds();
        map_count_before=snapshot_definitions::map_block_rebuilds();
        newtilebuf[first_tile].data[0]=initial_pixel^0x7f;
        SaveStateBlob changed;
        bool captured=put(changed);
        tile_delta=snapshot_definitions::tile_block_rebuilds()-tile_count;
        tile_map_delta=snapshot_definitions::map_block_rebuilds()-map_count_before;
        single_tile=captured && tile_delta==1 && tile_map_delta==0 && changed.data!=original.data;
        newtilebuf[first_tile].data[0]=initial_pixel^0x3f;
        tile_roundtrip=captured && load(changed) && newtilebuf[first_tile].data[0]==byte(initial_pixel^0x7f);
        load(original);
    }
    if(baseline) {
        SaveStateBlob warm; bool warmed=put(warm);
        tile_count=snapshot_definitions::tile_block_rebuilds();
        map_count_before=snapshot_definitions::map_block_rebuilds();
        TheMaps[0].data[0]=initial_combo^1;
        TheMaps[0].ffcs[MAXFFCS-1].data=initial_ffc^1;
        SaveStateBlob changed; bool captured=warmed && put(changed);
        map_delta=snapshot_definitions::map_block_rebuilds()-map_count_before;
        map_tile_delta=snapshot_definitions::tile_block_rebuilds()-tile_count;
        single_map=captured && map_delta==1 && map_tile_delta==0 && changed.data!=original.data;
        TheMaps[0].data[0]=initial_combo^2;
        TheMaps[0].ffcs[MAXFFCS-1].data=initial_ffc^2;
        map_roundtrip=captured && load(changed) && TheMaps[0].data[0]==word(initial_combo^1)
            && TheMaps[0].ffcs[MAXFFCS-1].data==word(initial_ffc^1);
        load(original);
    }
    if(baseline && (have_later_tile || have_later_screen)) {
        SaveStateBlob warm; bool warmed=put(warm);
        tile_count=snapshot_definitions::tile_block_rebuilds();
        map_count_before=snapshot_definitions::map_block_rebuilds();
        if(have_later_tile) newtilebuf[later_tile].data[0]=later_pixel^0x55;
        if(have_later_screen) TheMaps[later_screen].data[0]=later_combo^1;
        SaveStateBlob changed; bool captured=warmed && put(changed);
        later_tile_delta=snapshot_definitions::tile_block_rebuilds()-tile_count;
        later_map_delta=snapshot_definitions::map_block_rebuilds()-map_count_before;
        later_blocks=captured && later_tile_delta==uint64_t(have_later_tile) && later_map_delta==uint64_t(have_later_screen);
        if(have_later_tile) newtilebuf[later_tile].data[0]=later_pixel;
        if(have_later_screen) TheMaps[later_screen].data[0]=later_combo;
        later_roundtrip=captured && load(changed)
            && (!have_later_tile || newtilebuf[later_tile].data[0]==byte(later_pixel^0x55))
            && (!have_later_screen || TheMaps[later_screen].data[0]==word(later_combo^1));
        load(original);
    }

    SaveStateBlob malformed=original;
    if(!malformed.data.empty()) malformed.data.pop_back();
    itemsbuf[0].power=initial_power^7;
    TheMaps[0].data[0]=initial_combo^2;
    TheMaps[0].ffcs[MAXFFCS-1].data=initial_ffc^2;
    if(have_tile) newtilebuf[first_tile].data[0]=initial_pixel^0x33;
    const int sentinel_power=itemsbuf[0].power;
    bool rejected=baseline && !snapshot_definitions::get(malformed,true);
    bool unchanged=itemsbuf[0].power==sentinel_power && TheMaps[0].data[0]==word(initial_combo^2)
        && TheMaps[0].ffcs[MAXFFCS-1].data==word(initial_ffc^2)
        && (!have_tile || newtilebuf[first_tile].data[0]==byte(initial_pixel^0x33));
    bool rollback=baseline && load(original);
    if(!rollback) {
        // Restore the exact fields this regression touched even if a decoder fails.
        itemsbuf[0].power=initial_power;
        TheMaps[0].data[0]=initial_combo; TheMaps[0].ffcs[MAXFFCS-1].data=initial_ffc;
        if(have_tile && newtilebuf[first_tile].data) newtilebuf[first_tile].data[0]=initial_pixel;
        if(have_later_tile && newtilebuf[later_tile].data) newtilebuf[later_tile].data[0]=later_pixel;
        if(have_later_screen) TheMaps[later_screen].data[0]=later_combo;
    }
    bool restored=rollback && itemsbuf[0].power==initial_power && TheMaps[0].data[0]==initial_combo
        && TheMaps[0].ffcs[MAXFFCS-1].data==initial_ffc
        && (!have_tile || newtilebuf[first_tile].data[0]==initial_pixel)
        && (!have_later_tile || newtilebuf[later_tile].data[0]==later_pixel)
        && (!have_later_screen || TheMaps[later_screen].data[0]==later_combo);
    SaveStateBlob warm;
    bool warmed=restored && put(warm);
    snapshot_definitions::clear_definition_decode_cache();
    snapshot_definitions::clear_prepared_state_cache();
    uint64_t misses=snapshot_definitions::definition_decode_cache_misses();
    SaveStateBlob cold=warm;
    bool cold_ok=warmed && snapshot_definitions::get(cold,false) && !cold.failed && cold.pos==cold.data.size();
    uint64_t hits=snapshot_definitions::definition_decode_cache_hits();
    SaveStateBlob hot=warm;
    bool hot_ok=cold_ok && snapshot_definitions::get(hot,false) && !hot.failed && hot.pos==hot.data.size();
    bool cache_hit=hot_ok && snapshot_definitions::definition_decode_cache_misses()>misses
        && snapshot_definitions::definition_decode_cache_hits()>hits;
    bool cache_bounded=snapshot_definitions::definition_decode_cache_bytes()<=128u*1024u*1024u;
    uint64_t definition_skips=snapshot_definitions::definitions_apply_skips();
    uint64_t combo_skips=snapshot_definitions::combo_apply_skips();
    bool skipped=warmed && load(warm) && snapshot_definitions::definitions_apply_skips()>definition_skips
        && snapshot_definitions::combo_apply_skips()>combo_skips;
    snapshot_definitions::PreparedState invalid_token;
    bool prepared_invalid=!snapshot_definitions::apply(invalid_token);
    bool prepared_unchanged=false,prepared_restored=false,prepared_consumed=false,prepared_shape=false;
    if(warmed && have_tile) {
        itemsbuf[0].power=initial_power^9;
        TheMaps[0].data[0]=initial_combo^3;
        newtilebuf[first_tile].data[0]=initial_pixel^0x66;
        const int sentinel=itemsbuf[0].power;
        snapshot_definitions::PreparedState prepared;
        SaveStateBlob encoded=warm;
        bool ready=snapshot_definitions::prepare(encoded,prepared) && !encoded.failed
            && encoded.pos==encoded.data.size();
        prepared_unchanged=ready && itemsbuf[0].power==sentinel
            && TheMaps[0].data[0]==word(initial_combo^3)
            && newtilebuf[first_tile].data[0]==byte(initial_pixel^0x66);
        prepared_restored=ready && snapshot_definitions::apply(prepared)
            && itemsbuf[0].power==initial_power && TheMaps[0].data[0]==initial_combo
            && newtilebuf[first_tile].data[0]==initial_pixel;
        prepared_consumed=prepared_restored && !snapshot_definitions::apply(prepared)
            && itemsbuf[0].power==initial_power && TheMaps[0].data[0]==initial_combo
            && newtilebuf[first_tile].data[0]==initial_pixel;
        snapshot_definitions::PreparedState shape_token;
        SaveStateBlob shape_encoded=warm;
        bool shape_ready=snapshot_definitions::prepare(shape_encoded,shape_token);
        // Change only the logical shape scalar, never resize or free live data.
        // The guard must reject before it traverses any buffers; restore at once.
        const auto saved_map_count=map_count;
        map_count=saved_map_count^1;
        prepared_shape=shape_ready && !snapshot_definitions::apply(shape_token);
        map_count=saved_map_count;
        prepared_shape=prepared_shape && itemsbuf[0].power==initial_power
            && TheMaps[0].data[0]==initial_combo && newtilebuf[first_tile].data[0]==initial_pixel;
        bool prepared_cleanup=load(original);
        restored=restored && prepared_cleanup;
        if(!prepared_cleanup) {
            itemsbuf[0].power=initial_power; TheMaps[0].data[0]=initial_combo;
            if(newtilebuf[first_tile].data) newtilebuf[first_tile].data[0]=initial_pixel;
        }
    }
    bool inplace_strings=false;
    if(baseline && MsgStrings && msg_count) {
        const std::string old_title=DMaps[0].title,old_message=MsgStrings[0].s;
        DMaps[0].title="__inplace_title__"; MsgStrings[0].s="__inplace_message__";
        SaveStateBlob deep_baseline; bool deep_ready=put(deep_baseline);
        const char* title_address=DMaps[0].title.data(); const char* message_address=MsgStrings[0].s.data();
        DMaps[0].title[0]^=1; MsgStrings[0].s[0]^=1;
        bool detected=deep_ready && title_address==DMaps[0].title.data() && message_address==MsgStrings[0].s.data()
            && !snapshot_definitions::definitions_live_equal(snapshot_definitions::definitions_live_stamp_cache());
        SaveStateBlob deep_changed; bool changed=put(deep_changed);
        inplace_strings=detected && changed && deep_changed.data!=deep_baseline.data
            && load(deep_baseline) && DMaps[0].title=="__inplace_title__" && MsgStrings[0].s=="__inplace_message__";
        bool deep_cleanup=load(original);
        if(!deep_cleanup) { DMaps[0].title=old_title; MsgStrings[0].s=old_message; }
        restored=restored && deep_cleanup;
    }
    bool prepared_memo=false,prepared_memo_reject=false;
    if(baseline) {
        snapshot_definitions::clear_prepared_state_cache();
        SaveStateBlob seed=original; snapshot_definitions::PreparedState seeded;
        bool seeded_ok=snapshot_definitions::prepare(seed,seeded) && snapshot_definitions::apply(seeded);
        uint64_t memo_hits=snapshot_definitions::prepared_cache_hits();
        itemsbuf[0].power=initial_power^13;
        const int sentinel=itemsbuf[0].power;
        SaveStateBlob same=original; snapshot_definitions::PreparedState memo;
        bool memo_ready=seeded_ok && snapshot_definitions::prepare(same,memo);
        prepared_memo=memo_ready && snapshot_definitions::prepared_cache_hits()>memo_hits
            && itemsbuf[0].power==sentinel && same.pos==same.data.size()
            && snapshot_definitions::apply(memo) && itemsbuf[0].power==initial_power;
        SaveStateBlob damaged=original; damaged.data[0]^=1;
        snapshot_definitions::PreparedState refused;
        bool changed_rejected=!snapshot_definitions::prepare(damaged,refused);
        SaveStateBlob short_state=original; short_state.data.pop_back();
        bool short_rejected=!snapshot_definitions::prepare(short_state,refused);
        prepared_memo_reject=changed_rejected && short_rejected && !refused.valid
            && itemsbuf[0].power==initial_power && snapshot_definitions::prepared_cache_bytes()<=128u*1024u*1024u;
        bool memo_cleanup=load(original); restored=restored && memo_cleanup;
        if(!memo_cleanup) itemsbuf[0].power=initial_power;
    }
    bool descriptor_exact=snapshot_definitions::tile_descriptor_selftest();
    const bool runs_exact=zc255_runs_selftest();
    bool passed=runs_exact && prepared_memo && prepared_memo_reject && descriptor_exact && baseline && same && have_tile && inactive_slot && single_tile && single_map
        && tile_roundtrip && map_roundtrip && later_blocks && later_roundtrip && rejected && unchanged && restored
        && cache_hit && cache_bounded && skipped && prepared_invalid && prepared_unchanged
        && prepared_restored && prepared_consumed && prepared_shape && inplace_strings;
    FILE* out=fopen(report,"w");
    if(out) {
        fprintf(out,"{\"passed\":%s,\"identical_zero_rebuilds\":%s,\"single_tile_block\":%s,\"single_map_screen\":%s,\"inactive_ffc_restored\":%s,\"later_blocks\":%s,\"malformed_rejected\":%s,\"malformed_unchanged\":%s,\"original_restored\":%s,\"same_tile_rebuilds\":%llu,\"same_map_rebuilds\":%llu,\"single_tile_rebuilds\":%llu,\"single_map_rebuilds\":%llu,\"later_tile_rebuilds\":%llu,\"later_map_rebuilds\":%llu,\"first_tile\":%zu,\"later_tile\":%zu,\"later_screen\":%zu,\"baseline_bytes\":%zu,",
            passed?"true":"false",same?"true":"false",single_tile&&tile_roundtrip?"true":"false",
            single_map&&map_roundtrip?"true":"false",inactive_slot&&map_roundtrip?"true":"false",
            later_blocks&&later_roundtrip?"true":"false",rejected?"true":"false",unchanged?"true":"false",restored?"true":"false",
            (unsigned long long)same_tiles,(unsigned long long)same_maps,(unsigned long long)tile_delta,
            (unsigned long long)map_delta,(unsigned long long)later_tile_delta,(unsigned long long)later_map_delta,
            first_tile,later_tile,later_screen,original.data.size());
        fprintf(out,"\"runs_codec_exact\":%s,",runs_exact?"true":"false");
        fprintf(out,"\"prepared_cache_exact_reuse\":%s,\"prepared_cache_changed_truncated_rejected\":%s,\"tile_descriptor_exact\":%s,\"inplace_strings_detected_restored\":%s,\"prepared_no_mutation\":%s,\"prepared_restored\":%s,\"prepared_invalid_rejected\":%s,\"prepared_consumed_rejected\":%s,\"prepared_shape_rejected\":%s,\"decode_cache_hit\":%s,\"decode_cache_bounded\":%s,\"unchanged_apply_skipped\":%s,\"decode_cache_hits\":%llu,\"decode_cache_misses\":%llu,\"decode_cache_bytes\":%zu,\"definitions_apply_skips\":%llu,\"combo_apply_skips\":%llu}\n",
            prepared_memo?"true":"false",prepared_memo_reject?"true":"false",descriptor_exact?"true":"false",inplace_strings?"true":"false",prepared_unchanged?"true":"false",prepared_restored?"true":"false",prepared_invalid?"true":"false",
            prepared_consumed?"true":"false",prepared_shape?"true":"false",cache_hit?"true":"false",
            cache_bounded?"true":"false",skipped?"true":"false",
            (unsigned long long)snapshot_definitions::definition_decode_cache_hits(),
            (unsigned long long)snapshot_definitions::definition_decode_cache_misses(),
            snapshot_definitions::definition_decode_cache_bytes(),
            (unsigned long long)snapshot_definitions::definitions_apply_skips(),
            (unsigned long long)snapshot_definitions::combo_apply_skips());
        fclose(out);
    }
    fprintf(stderr,"ZC255 structure selftest: %s\n",passed?"PASS":"FAIL");
}
