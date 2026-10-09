#pragma once
#include "snapshot_definitions.h"
#include <chrono>
#include <cstdlib>
#include <cstdio>

bool zc255_snapshot_export(std::vector<uint8_t>&);
bool zc255_snapshot_import(const uint8_t*, size_t);
#include "snapshot_map_tracking_selftest.h"
#include "snapshot_script_map_selftest.h"

// Compare the optimized output to complete field-by-field encoding, including
// reused neighbours when variable-length labels change record boundaries.
inline bool zc255_combo_cache_matches_full_encoding() {
    SaveStateBlob encoded;
    if(!snapshot_definitions::put_combo_chunks(encoded)) return false;
    const auto& chunks=snapshot_definitions::combo_chunk_caches();
    for(size_t index=0;index<chunks.size();++index) {
        const size_t first=index*snapshot_definitions::COMBO_CHUNK_RECORDS;
        const size_t count=std::min(snapshot_definitions::COMBO_CHUNK_RECORDS,combobuf.size()-first);
        SaveStateBlob full;
        snapshot_definitions::Writer writer{full};
        for(size_t i=0;i<count;++i) snapshot_definitions::fields(writer,combobuf[first+i]);
        if(full.failed || full.data!=chunks[index].plain) return false;
    }
    return true;
}

#include "snapshot_combo_tracking_selftest.h"

inline bool zc255_tracked_tiles_selftest(const std::vector<uint8_t>& original) {
    const char* report=std::getenv("ZC255_TILE_TRACKING_SELFTEST");
    if(!report || !*report) return true;
    auto& tracker=snapshot_tile_tracking::state();
    bool passed=tracker.trusted && snapshot_tile_tracking::audit();
    unsigned cases=0;
    auto compare=[&] {
        SaveStateBlob fast,reference;
        bool ok=snapshot_definitions::put_tile_blocks(fast);
        const bool trusted=tracker.trusted;tracker.trusted=false;
        ok=snapshot_definitions::put_tile_blocks(reference) && ok;
        tracker.trusted=trusted;
        passed=ok && fast.data==reference.data && passed;++cases;
    };
    compare();
    byte pixels[256];for(size_t i=0;i<256;++i) pixels[i]=byte(i%15+1);
    for(byte format=tf4Bit;format<=tf32Bit;++format) {
        reset_tile(newtilebuf,0,format);compare();
        // pack_tile supports the engine's 4/8-bit unpacked 256-byte input.
        if(format<=tf8Bit) {pack_tile(newtilebuf,pixels,0);compare();}
    }
    reset_tile(newtilebuf,0,tf8Bit);pack_tile(newtilebuf,pixels,0);
    copy_tile(newtilebuf,0,NEWMAXTILES-1,false);compare();
    copy_tile(newtilebuf,0,NEWMAXTILES-1,true);compare();
    overlay_tile(newtilebuf,NEWMAXTILES-1,1,0,false);compare();
    pack_tiledata(newtilebuf[NEWMAXTILES-1].data,pixels,tf8Bit);compare();
    BITMAP* bitmap=create_bitmap_ex(8,16,16);
    if(bitmap) {clear_to_color(bitmap,5);write_tile(newtilebuf,bitmap,0,0,0,true,false);compare();destroy_bitmap(bitmap);}
    else passed=false;
    passed=tracker.missed==0 && passed;
    // Deliberately bypass every hook: audit must detect it, capture the actual
    // pixel, and permanently fall back. This is a diagnostic test only.
    newtilebuf[0].data[255]^=1;
    compare();const bool caught=tracker.missed==1 && !tracker.trusted;
    bool restored=zc255_snapshot_import(original.data(),original.size());
    passed=passed && caught && restored;
    FILE* file=fopen(report,"w");
    if(file) {fprintf(file,"{\"passed\":%s,\"cases\":%u,\"unmarked_write_detected\":%s,\"original_restored\":%s}\n",passed?"true":"false",cases,caught?"true":"false",restored?"true":"false");fclose(file);}
    // Reset only in this explicit test after restoring the original snapshot.
    tracker.missed=0;tracker.trusted=passed;snapshot_tile_tracking::invalidate_all();
    return passed;
}

// Opt-in regression at a real gameplay checkpoint, exercising the complete
// external capture/import path rather than only the definition serializer.
inline void zc255_definitions_selftest() {
    static bool done=false;
    const char* report=getenv("ZC255_DEFINITIONS_SELFTEST");
    if(done || !report || !*report || !Playing || TheMaps.empty() || combobuf.empty() || !msg_count) return;
    done=true;
    std::vector<uint8_t> original;
    auto start=std::chrono::steady_clock::now();
    bool exported=zc255_snapshot_export(original);
    exported=exported && zc255_tracked_tiles_selftest(original);
    exported=exported && zc255_tracked_maps_selftest(original);
    exported=exported && zc255_script_maps_selftest(original);
    exported=exported && zc255_tracked_combos_selftest(original);
    auto capture_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    int item=itemsbuf[0].power, enemy=guysbuf[0].hp, weapon=wpnsbuf[0].tile;
    byte classname=combo_class_buf[0].water, combo_frame=combobuf[0].cur_frame;
    int combo_tile=combobuf[0].tile;
    auto label=combobuf[0].label, title=DMaps[0].title, message=MsgStrings[0].s;
    word screen_combo=TheMaps[0].data[0], inactive_ffc=TheMaps[0].ffcs[MAXFFCS-1].data;
    byte rule=quest_rules[QUESTRULES_NEW_SIZE-1];
    bool tile_present=newtilebuf[0].data != nullptr;
    byte pixel=tile_present ? newtilebuf[0].data[0] : 0;
    bool blank=blank_tile_table[0];
    word drop=item_drop_sets[0].item[0];
    if(exported) {
        itemsbuf[0].power=item+7; guysbuf[0].hp=enemy+1; wpnsbuf[0].tile=weapon+1;
        combo_class_buf[0].water=classname^1;
        snapshot_combo_tracking::mark(0);
        combobuf[0].cur_frame=combo_frame^1; combobuf[0].tile=combo_tile+1;
        combobuf[0].label="__snapshot_test_future_combo__";
        DMaps[0].title="__snapshot_test_future_dmap__";
        MsgStrings[0].s="__snapshot_test_future_message__";
        snapshot_map_tracking::mark(0);
        TheMaps[0].data[0]=screen_combo^1;
        TheMaps[0].ffcs[MAXFFCS-1].data=inactive_ffc^1;
        quest_rules[QUESTRULES_NEW_SIZE-1]=rule^1;
        if(tile_present) {snapshot_tile_tracking::mark(0);newtilebuf[0].data[0]=pixel^0x7f;}
        blank_tile_table[0]=!blank; item_drop_sets[0].item[0]=drop^1;
    }
    bool records_exact=exported && snapshot_definitions::combo_encoding_layout().valid && zc255_combo_cache_matches_full_encoding();
    if(exported) {
        snapshot_combo_tracking::mark(0);
        combobuf[0].label="x";
        records_exact=zc255_combo_cache_matches_full_encoding() && records_exact;
        // Unchanged label exercises fast POD-span patching, before and after it.
        snapshot_combo_tracking::mark(0);
        combobuf[0].cur_frame=combo_frame^2;combobuf[0].tile=combo_tile+3;
        combobuf[0].speed_add=zslongToFix(combobuf[0].speed_add.getZLong()+123);
        records_exact=zc255_combo_cache_matches_full_encoding() && records_exact;
    }
    start=std::chrono::steady_clock::now();
    bool imported=exported && zc255_snapshot_import(original.data(),original.size());
    auto restore_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    bool restored=imported && itemsbuf[0].power==item && guysbuf[0].hp==enemy && wpnsbuf[0].tile==weapon
        && combo_class_buf[0].water==classname && combobuf[0].cur_frame==combo_frame && combobuf[0].tile==combo_tile
        && combobuf[0].label==label && DMaps[0].title==title && MsgStrings[0].s==message
        && TheMaps[0].data[0]==screen_combo && TheMaps[0].ffcs[MAXFFCS-1].data==inactive_ffc
        && quest_rules[QUESTRULES_NEW_SIZE-1]==rule && blank_tile_table[0]==blank
        && item_drop_sets[0].item[0]==drop && (!tile_present || newtilebuf[0].data[0]==pixel);
    SaveStateBlob good;
    bool encoded=snapshot_definitions::put(good);
    SaveStateBlob malformed=good;
    // A truncated compressed final section must be rejected before any section applies.
    if(!malformed.data.empty()) malformed.data.pop_back();
    int sentinel=itemsbuf[0].power;
    bool rejected=encoded && !snapshot_definitions::get(malformed,true);
    bool unchanged=itemsbuf[0].power==sentinel && DMaps[0].title==title && MsgStrings[0].s==message;
    FILE* out=fopen(report,"w");
    if(out) {
        fprintf(out,"{\"exported\":%s,\"imported\":%s,\"independent_values_restored\":%s,\"malformed_rejected\":%s,\"malformed_left_state_unchanged\":%s,\"changed_combo_records_match_full_encoding\":%s,\"snapshot_bytes\":%zu,\"capture_ms\":%.3f,\"restore_ms\":%.3f}\n",
            exported?"true":"false",imported?"true":"false",restored?"true":"false",rejected?"true":"false",unchanged?"true":"false",records_exact?"true":"false",original.size(),capture_ms,restore_ms);
        fclose(out);
    }
    fprintf(stderr,"ZC255 definition selftest: %s, %zu bytes, capture %.3f ms, restore %.3f ms\n",restored&&rejected&&unchanged&&records_exact?"PASS":"FAIL",original.size(),capture_ms,restore_ms);
}
