#pragma once
inline bool zc255_writer_batch_selftest() {
    auto report=getenv("ZC255_WRITER_BATCH_SELFTEST");if(!report||!*report)return true;
    bool passed=true;unsigned cases=0;
    auto compare=[&](const auto&... values) {
        SaveStateBlob candidate,reference;
        uint32_t prefix=0xAABBCCDD;candidate.put(prefix);reference.put(prefix);
        candidate.pos=reference.pos=2;
        StateWriter{candidate}(values...);(reference.put(values),...);
        passed=candidate.data==reference.data&&candidate.pos==reference.pos
            &&candidate.failed==reference.failed&&passed;++cases;
        // Existing writers append even with the parser failure flag set.
        candidate.failed=reference.failed=true;
        StateWriter{candidate}(values...);(reference.put(values),...);
        passed=candidate.data==reference.data&&candidate.pos==reference.pos
            &&candidate.failed==reference.failed&&passed;++cases;
    };
    uint8_t byte=0xFF;int16_t small_value=-32768;int32_t integer=INT32_MIN;
    uint64_t large=UINT64_MAX;bool flag=true;float f=-0.0f;double d=1.25;
    compare();compare(integer);compare(byte,small_value,integer,large,flag,f,d);
    uint8_t exact_a[256]{},exact_b[256]{};
    for(size_t i=0;i<256;++i){exact_a[i]=uint8_t(i);exact_b[i]=uint8_t(~i);}
    compare(exact_a,exact_b); // Exactly the 512-byte batching boundary.
    compare(exact_a,exact_b,byte); // Above the boundary uses legacy writes.
    int32_t fields[8]={0,1,-1,INT32_MIN,INT32_MAX,11,22,33};
    compare(byte,fields,flag);compare(zslongToFix(-12345),integer);
    // A non-trivially-copyable type must retain the legacy raw writer path.
    struct LegacyValue {uint8_t bytes[3]{1,2,3};~LegacyValue(){}};
    LegacyValue legacy;compare(legacy,byte);
    if(FILE* out=fopen(report,"w")) {
        fprintf(out,"{\"passed\":%s,\"cases\":%u,\"complete_bytes_equal\":%s,\"failure_flag_preserved\":%s}\n",
            passed?"true":"false",cases,passed?"true":"false",passed?"true":"false");fclose(out);
    }
    return passed;
}
inline bool zc255_combo_scratch_selftest(const std::vector<uint8_t>& original) {
    auto report=getenv("ZC255_COMBO_SCRATCH_SELFTEST");if(!report||!*report)return true;
    using namespace snapshot_definitions;
    bool passed=true,packed_exact=true,bounded=true;unsigned cases=0;
    const bool old_stored=stored_combo_encoding();
    try {
        const size_t indexes[]={0,std::min(COMBO_CHUNK_RECORDS,combobuf.size()-1),combobuf.size()-1};
        for(unsigned cycle=0;cycle<3;++cycle) for(size_t index:indexes) {
            auto& value=snapshot_combo_tracking::mutable_record(index);
            value.tile^=1;value.aclk+=1;
            value.label=std::string(cycle==0?0:cycle==1?31:4096,char('A'+cycle));
            // Exercise both stored and compressed output with changed labels.
            stored_combo_encoding()=cycle!=1;
            bool full=zc255_combo_cache_matches_full_encoding();++cases;
            const auto& cache=combo_chunk_caches()[index/COMBO_CHUNK_RECORDS];
            std::vector<uint8_t> decoded(cache.plain_size);uLongf size=decoded.size();
            bool packed=uncompress(decoded.data(),&size,cache.compressed.data(),uLong(cache.compressed.size()))==Z_OK
                &&size==decoded.size()&&decoded==cache.plain;
            packed_exact=packed_exact&&packed;
            bounded=bounded&&combo_encoding_scratch().retained_bytes()<=ComboEncodingScratch::LIMIT;
            passed=full&&packed&&bounded&&passed;
        }
        auto& scratch=combo_encoding_scratch();
        scratch.raw.data.reserve(ComboEncodingScratch::LIMIT+1);scratch.trim();
        bounded=bounded&&scratch.retained_bytes()<=ComboEncodingScratch::LIMIT;
    } catch(const std::exception& error) {
        fprintf(stderr,"combo scratch selftest exception: %s\n",error.what());passed=false;
    }
    stored_combo_encoding()=old_stored;
    passed=zc255_snapshot_import(original.data(),original.size())&&bounded&&passed;
    snapshot_combo_tracking::invalidate_all();
    if(FILE* out=fopen(report,"w")) {
        fprintf(out,"{\"passed\":%s,\"cases\":%u,\"packed_exact\":%s,\"scratch_bounded\":%s}\n",
            passed?"true":"false",cases,packed_exact?"true":"false",bounded?"true":"false");fclose(out);
    }
    return passed;
}
inline bool zc255_tracked_combos_selftest(const std::vector<uint8_t>& original) {
    if(!zc255_writer_batch_selftest()) return false;
    if(!zc255_combo_scratch_selftest(original)) return false;
    auto report=getenv("ZC255_COMBO_TRACKING_SELFTEST"); if(!report||!*report)return true;
    auto& tracking=snapshot_combo_tracking::state();
    const bool old_trust=tracking.trusted;const uint64_t old_missed=tracking.missed;
    bool passed=old_trust&&snapshot_combo_tracking::audit();unsigned cases=0;
    auto compare=[&]() {++cases;return zc255_combo_cache_matches_full_encoding();};
    passed=compare()&&passed;
    passed=compare()&&passed;
    const auto& warm=snapshot_definitions::combo_chunk_caches().front();
    passed=warm.tracking_epoch==tracking.epoch&&warm.tracking_version==tracking.versions[0]&&passed;
    const size_t indexes[]={0,combobuf.size()/2,combobuf.size()-1};
    for(size_t index:indexes) {
        auto& value=snapshot_combo_tracking::mutable_record(index);
        value.tile^=1;value.walk^=1;value.attributes[3]^=123;
        value.triggerflags[5]^=1;value.speed_add=zslongToFix(value.speed_add.getZLong()+123);
        value.label="__tracked_combo_owned_string__";
        passed=compare()&&passed;
        snapshot_combo_tracking::mutable_record(index).label[3]='X';
        passed=compare()&&passed;
    }
    auto& animation=snapshot_combo_tracking::mutable_record(0);
    animation.o_tile=0;animation.tile=1;animation.frames=2;
    animation.speed=1;animation.aclk=1;animation.cur_frame=0;
    passed=compare()&&passed;
    animate(animation,true);passed=compare()&&passed;
    animate(animation,false);passed=compare()&&passed;
    reset_combo_animation(animation);passed=compare()&&passed;
    // SRAM reload invalidates the cache before output-pointer writes.
    snapshot_combo_tracking::invalidate_all();combobuf.back().label="__sram_reload__";
    passed=compare()&&passed;
    if(snapshot_combo_tracking::audit()) {
        // Cold capture after invalidation must be followed by a warm clean one.
        passed=compare()&&passed;
        combobuf.back().tile^=1;
        passed=compare()&&!tracking.trusted&&tracking.missed==old_missed+1&&passed;
    } else passed=false;
    auto epoch=tracking.epoch;
    passed=zc255_snapshot_import(original.data(),original.size())&&tracking.epoch!=epoch&&passed;
    passed=compare()&&passed;
    // Ordinary import and memoized prepared import converge on the same commit.
    snapshot_combo_tracking::mutable_record(0).tile^=1;epoch=tracking.epoch;
    passed=zc255_snapshot_import(original.data(),original.size())&&tracking.epoch!=epoch&&passed;
    passed=compare()&&passed;
    if(FILE* out=fopen(report,"w")) {
        fprintf(out,"{\"passed\":%s,\"cases\":%u,\"intentional_missed_write\":1,\"complete_reference_equal\":%s}\n",passed?"true":"false",cases,passed?"true":"false");fclose(out);
    }
    tracking.trusted=old_trust;tracking.missed=old_missed;
    snapshot_combo_tracking::invalidate_all();return passed;
}

