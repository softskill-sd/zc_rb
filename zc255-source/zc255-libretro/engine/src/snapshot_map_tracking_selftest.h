#pragma once
inline bool zc255_shared_maps_selftest(const std::vector<uint8_t>& original) {
    auto report=getenv("ZC255_SHARED_MAPS_SELFTEST");if(!report||!*report)return true;
    using namespace snapshot_definitions;
    bool shared=false,survives_rebuild=false,survives_clear=false,proof_reset=false;
    bool unknown_full=false,truncated_rejected=false,unchanged=false,bounds=false;
    bool passed=true;
    try {
        ImmutableMapBytes proof;proof.publish(std::vector<uint8_t>{1,2,3});proof.mark_validated();
        ImmutableMapBytes old=proof;
        proof.publish(std::vector<uint8_t>{4,5});
        proof_reset=old.validated()&&!proof.validated()&&old.bytes()==std::vector<uint8_t>({1,2,3});
        PreparedMapBlob invalid;invalid.data=old;invalid.pos=old.size()+1;
        BasicSkip<PreparedMapBlob> overflow{invalid};overflow.bytes(0);bounds=invalid.failed;

        SaveStateBlob maps;
        passed=put_map_blocks(maps)&&passed;
        const auto& cache=map_block_caches().front();
        SaveStateBlob record;record.put(cache.plain_size);record.put(uint32_t(cache.compressed.size()));
        record.put(cache.compressed.data(),cache.compressed.size());
        const auto bytes=cache.plain.bytes();const auto old_pointer=cache.plain.data();
        PreparedMapBlob held;
        shared=inflate_map_section(record,held,0)&&held.data.validated()
            &&held.data.data()==old_pointer&&held.data.bytes()==bytes;
        snapshot_map_tracking::access(0).data[0]^=1;
        SaveStateBlob rebuilt;
        passed=put_map_blocks(rebuilt)&&passed;
        survives_rebuild=held.data.validated()&&held.data.data()==old_pointer
            &&held.data.bytes()==bytes&&map_block_caches().front().plain.data()!=old_pointer;
        map_block_caches().clear();map_aggregate_cache().valid=false;
        survives_clear=held.data.validated()&&held.data.data()==old_pointer&&held.data.bytes()==bytes;

        // Cache-free input must decode, then run the complete structural visitor.
        record.pos=0;record.failed=false;PreparedMapBlob unknown;
        unknown_full=inflate_map_section(record,unknown,0)&&!unknown.data.validated();
        BasicSkip<PreparedMapBlob> normal{unknown};snapshot_definitions::screen(normal,TheMaps[0]);
        unknown_full=unknown_full&&!unknown.failed&&unknown.pos==unknown.data.size();
        unknown.data.mark_validated();unknown_full=unknown_full&&unknown.data.validated();

        // A checksum-valid zlib record can still be structurally truncated.
        // Remove one plain byte and recompress correctly, rather than corrupting
        // the checksum. Decode must succeed; structural preflight must reject.
        auto short_bytes=bytes;short_bytes.pop_back();
        uLongf packed_size=compressBound(uLong(short_bytes.size()));
        std::vector<uint8_t> packed(packed_size);
        int status=compress2(packed.data(),&packed_size,short_bytes.data(),uLong(short_bytes.size()),Z_BEST_SPEED);
        packed.resize(packed_size);
        SaveStateBlob short_record;short_record.put(uint32_t(short_bytes.size()));short_record.put(uint32_t(packed.size()));
        short_record.put(packed.data(),packed.size());
        PreparedMapBlob bad;
        bool decoded=status==Z_OK&&inflate_map_section(short_record,bad,0)&&!bad.data.validated();
        std::vector<PreparedMapBlob> bad_blocks(TheMaps.size());bad_blocks[0]=bad;
        std::vector<uint8_t> before,after;
        bool exported=zc255_snapshot_export(before);
        bool rejected=decoded&&!apply_map_blocks(bad_blocks);
        SaveStateBlob bad_stream;bad_stream.put(map_count);bad_stream.put(uint32_t(TheMaps.size()));
        bad_stream.put(short_record.data.data(),short_record.data.size());
        std::vector<PreparedMapBlob> rejected_blocks;
        truncated_rejected=rejected&&!get_map_blocks(bad_stream,rejected_blocks);
        unchanged=exported&&zc255_snapshot_export(after)&&before==after;
    } catch(const std::exception& error) {
        fprintf(stderr,"shared map selftest exception: %s\n",error.what());passed=false;
    }
    passed=shared&&survives_rebuild&&survives_clear&&proof_reset&&unknown_full
        &&truncated_rejected&&unchanged&&bounds&&passed;
    passed=zc255_snapshot_import(original.data(),original.size())&&passed;
    snapshot_map_tracking::invalidate_all();
    if(FILE* out=fopen(report,"w")) {
        fprintf(out,"{\"passed\":%s,\"exact_record_shared\":%s,\"survives_cache_rebuild\":%s,\"survives_cache_clear\":%s,\"publish_clears_proof\":%s,\"unknown_full_validation\":%s,\"valid_zlib_truncated_rejected\":%s,\"malformed_engine_unchanged\":%s,\"skip_position_bounds\":%s}\n",
            passed?"true":"false",shared?"true":"false",survives_rebuild?"true":"false",survives_clear?"true":"false",proof_reset?"true":"false",unknown_full?"true":"false",truncated_rejected?"true":"false",unchanged?"true":"false",bounds?"true":"false");fclose(out);
    }
    return passed;
}

inline bool zc255_tracked_maps_selftest(const std::vector<uint8_t>& original) {
    if(!zc255_shared_maps_selftest(original)) return false;
    auto report=getenv("ZC255_MAP_TRACKING_SELFTEST");if(!report||!*report)return true;
    auto& tracking=snapshot_map_tracking::state();
    const bool old_trust=tracking.trusted;const uint64_t old_missed=tracking.missed;
    bool passed=true;int cases=0;
    auto compare=[&]() {
        SaveStateBlob fast,reference;
        bool ok=snapshot_definitions::put_map_blocks(fast);
        bool trust=tracking.trusted;tracking.trusted=false;
        ok=snapshot_definitions::put_map_blocks(reference)&&ok;
        tracking.trusted=trust;++cases;return ok&&fast.data==reference.data;
    };
    passed=compare()&&passed;
    // Exact capture-owned decoding must equal ordinary decompression. A changed
    // packed record and truncated headers must never be accepted by reuse.
    {
        SaveStateBlob encoded;
        passed=snapshot_definitions::put_map_blocks(encoded)&&passed;
        auto& cache=snapshot_definitions::map_block_caches().front();
        SaveStateBlob record;
        record.put(cache.plain_size);record.put(uint32_t(cache.compressed.size()));
        record.put(cache.compressed.data(),cache.compressed.size());
        SaveStateBlob fast,reference;
        auto reused=snapshot_definitions::map_capture_decode_reuse_counter();
        auto normal=record;
        passed=snapshot_definitions::inflate_map_section(record,fast,0)
            &&snapshot_definitions::inflate_section(normal,reference)
            &&fast.data==reference.data
            &&snapshot_definitions::map_capture_decode_reuse_counter()==reused+1&&passed;
        record.pos=0;record.data.back()^=1;
        reused=snapshot_definitions::map_capture_decode_reuse_counter();
        passed=!snapshot_definitions::inflate_map_section(record,fast,0)
            &&snapshot_definitions::map_capture_decode_reuse_counter()==reused&&passed;
        record.pos=0;record.data.resize(7);record.failed=false;
        passed=!snapshot_definitions::inflate_map_section(record,fast,0)&&passed;
        ++cases;
    }
    const size_t indexes[]={0,TheMaps.size()/2,TheMaps.size()-1};
    for(size_t index:indexes) {
        auto& screen=snapshot_map_tracking::access(index);
        screen.data[0]^=1;screen.cset[175]^=1;screen.ffcs[MAXFFCS-1].data^=1;
        screen.usr_notes="__tracked_map_owned_string__";
        passed=compare()&&passed;
        snapshot_map_tracking::access(index).usr_notes[3]='X';
        passed=compare()&&passed;
    }
    // Pointer arithmetic must mark the final screen, rather than only the base.
    if(TheMaps.size()>MAPSCRS) {
        auto* screen=&TheMaps[0]+MAPSCRS;
        snapshot_map_tracking::mark_pointer(screen);screen->data[175]^=1;
        passed=compare()&&passed;
    }
    // Audit must detect a deliberately unmarked write and fall back to exact
    // comparison. This negative test is the only allowed missed-write report.
    if(snapshot_map_tracking::audit()) {
        TheMaps.back().data[17]^=1;
        passed=compare()&&!tracking.trusted&&tracking.missed==old_missed+1&&passed;
    }else passed=false;
    passed=zc255_snapshot_import(original.data(),original.size())&&passed;
    tracking.trusted=old_trust;tracking.missed=old_missed;
    snapshot_map_tracking::invalidate_all();
    if(FILE* out=fopen(report,"w")) {fprintf(out,"{\"passed\":%s,\"cases\":%d,\"intentional_missed_write\":1,\"complete_reference_equal\":%s}\n",passed?"true":"false",cases,passed?"true":"false");fclose(out);}
    return passed;
}
