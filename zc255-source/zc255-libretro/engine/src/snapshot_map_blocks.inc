// Published plain maps are immutable; prepared records keep their storage alive.
// Every replacement publishes a new owner. No mutable vector reference escapes.
struct ImmutableMapBytes {
private:
    std::shared_ptr<const std::vector<uint8_t>> owner;
    bool proof=false;
public:
    bool validated() const {return proof && bool(owner);}
    void mark_validated() {proof=bool(owner);}
    const std::vector<uint8_t>& bytes() const {
        static const std::vector<uint8_t> empty;
        return owner?*owner:empty;
    }
    size_t size() const {return bytes().size();}
    size_t capacity() const {return bytes().capacity();}
    const uint8_t* data() const {return bytes().data();}
    bool operator==(const ImmutableMapBytes& other) const {
        return owner==other.owner || bytes()==other.bytes();
    }
    bool operator==(const std::vector<uint8_t>& other) const {return bytes()==other;}
    void publish(std::vector<uint8_t>&& values) {
        auto next=std::make_shared<const std::vector<uint8_t>>(std::move(values));
        owner=std::move(next);proof=false;
    }
};
struct PreparedMapBlob {
    ImmutableMapBytes data;
    size_t pos=0;
    bool failed=false;
    void get(void* target,size_t count) {
        if(failed || pos>data.size() || count>data.size()-pos) {
            failed=true;memset(target,0,count);return;
        }
        memcpy(target,data.data()+pos,count);pos+=count;
    }
    template<class T> void get(T& value) {get(&value,sizeof(value));}
    void get_str(std::string& value) {
        uint32_t count=0;get(count);
        if(failed || pos>data.size() || count>data.size()-pos) {
            failed=true;value.clear();return;
        }
        value.assign(reinterpret_cast<const char*>(data.data()+pos),count);pos+=count;
    }
};
// Included inside snapshot_definitions after screen(). Each block contains the
// complete value encoding of one screen, independent of process-local baselines.
struct MapBlockCache {
    std::vector<uint8_t> object_stamp, compressed;
    ImmutableMapBytes plain;
    std::string notes;
    uint32_t plain_size=0;
    uint64_t tracking_epoch=0,tracking_version=0;
};
inline std::vector<MapBlockCache>& map_block_caches() {
    static std::vector<MapBlockCache> entries;
    return entries;
}
inline uint64_t& map_block_rebuilds() {
    static uint64_t count=0;
    return count;
}
// Derived encoded bytes only: every capture still validates all live screens.
// The entire aggregate is emitted into every state, so restoration never needs
// this cache or any previous state. Invalidate before publishing changed blocks
// so an allocation exception cannot leave stale aggregate bytes reusable.
struct MapAggregateCache {
    SaveStateBlob encoded;
    std::vector<uint8_t> equal;
    decltype(map_count) shape=0;
    size_t screens=0;
    bool valid=false;
};
inline MapAggregateCache& map_aggregate_cache() {
    static MapAggregateCache cache;
    return cache;
}
inline bool put_map_blocks(SaveStateBlob& out) {
    if(TheMaps.size()>UINT32_MAX) return false;
    auto& aggregate_cache=map_aggregate_cache();
    if(aggregate_cache.shape!=map_count || aggregate_cache.screens!=TheMaps.size())
        aggregate_cache.valid=false;
    auto& entries=map_block_caches();entries.resize(TheMaps.size());
    // The engine is paused at its canonical snapshot boundary. Workers only
    // compare disjoint immutable screens; all encoding and cache writes remain
    // on the caller. Join every worker before returning from this DLL call.
    auto& equal=aggregate_cache.equal;equal.resize(TheMaps.size());
    const bool trusted=snapshot_map_tracking::state().trusted;
    const bool audit=snapshot_map_tracking::audit();
    const uint64_t epoch=snapshot_map_tracking::state().epoch;
    std::vector<uint8_t> missed(audit?TheMaps.size():0);
    auto compare=[&](size_t begin,size_t end) {
        for(size_t index=begin;index<end;++index) {
            const auto& sc=TheMaps[index];const auto& cache=entries[index];
            const bool clean=trusted&&cache.plain_size&&cache.tracking_epoch==epoch
                &&cache.tracking_version==snapshot_map_tracking::version(index);
            if(clean&&!audit) {equal[index]=1;continue;}
            equal[index]=cache.object_stamp.size()==sizeof(mapscr)
                && memcmp(cache.object_stamp.data(),&sc,sizeof(mapscr))==0
                && cache.notes==sc.usr_notes;
            if(clean&&audit&&!equal[index])missed[index]=1;
        }
    };
    // Script execution can invalidate the shared epoch while hooks remain trusted.
    // In that case every screen still needs the same complete comparison.
    const bool globally_dirty=!entries.empty() && entries[0].tracking_epoch!=epoch;
    if((!trusted||audit||globally_dirty)&&TheMaps.size()>=2048 && entries[0].object_stamp.size()==sizeof(mapscr)) {
        std::array<std::thread,3> workers;
        bool launched=true;
        try {
            for(size_t i=0;i<workers.size();++i)
                workers[i]=std::thread(compare,TheMaps.size()*i/4,TheMaps.size()*(i+1)/4);
        } catch(...) { launched=false; }
        if(launched) compare(TheMaps.size()*3/4,TheMaps.size());
        for(auto& worker:workers) if(worker.joinable()) worker.join();
        if(!launched) compare(0,TheMaps.size());
    } else compare(0,TheMaps.size());
    if(audit)for(size_t index=0;index<missed.size();++index)if(missed[index])snapshot_map_tracking::missed_write(index);
    size_t aggregate=0;
    for(size_t index=0;index<TheMaps.size();++index) {
        auto& sc=TheMaps[index];auto& cache=entries[index];
        const bool same=equal[index]!=0;
        if(!same || cache.compressed.empty()) {
            aggregate_cache.valid=false;
            SaveStateBlob raw;Writer writer{raw};screen(writer,sc);
            if(raw.failed || raw.data.empty() || raw.data.size()>MAX_SECTION_BYTES-aggregate) return false;
            // Validate a new encoding once before issuing its immutable proof.
            // screen() uses a fixed field schema and all MAXFFCS slots.
            Skip validation{raw};screen(validation,sc);
            if(raw.failed || raw.pos!=raw.data.size()) return false;
            raw.pos=0;
            uLongf packed_size=compressBound(uLong(raw.data.size()));
            std::vector<uint8_t> compressed(packed_size);
            if(snapshot_profile::measured_compress2(compressed.data(),&packed_size,raw.data.data(),uLong(raw.data.size()),Z_BEST_SPEED)!=Z_OK) return false;
            compressed.resize(packed_size);
            // Publish only after encoding and compression both succeed. Stamps
            // are change detectors only; no object representation is serialized.
            std::vector<uint8_t> stamp(sizeof(mapscr));memcpy(stamp.data(),&sc,sizeof(mapscr));
            std::string notes=sc.usr_notes;
            // Allocate the immutable owner before publishing either encoding:
            // an allocation failure must retain the previous matching pair.
            ImmutableMapBytes next_plain;next_plain.publish(std::move(raw.data));
            next_plain.mark_validated();
            cache.object_stamp=std::move(stamp);cache.notes=std::move(notes);
            cache.compressed=std::move(compressed);cache.plain_size=uint32_t(next_plain.size());
            cache.plain=std::move(next_plain);
            ++map_block_rebuilds();
        }
        if(!cache.plain_size || cache.plain_size>MAX_SECTION_BYTES-aggregate || cache.compressed.size()>UINT32_MAX) return false;
        aggregate+=cache.plain_size;
        const bool clean=trusted&&cache.tracking_epoch==epoch&&cache.tracking_version==snapshot_map_tracking::version(index);
        if(clean)++snapshot_map_tracking::state().skipped;else ++snapshot_map_tracking::state().checked;
        cache.tracking_epoch=epoch;cache.tracking_version=snapshot_map_tracking::version(index);
    }
    if(!aggregate_cache.valid) {
        auto& encoded=aggregate_cache.encoded;
        encoded.data.clear();encoded.pos=0;encoded.failed=false;
        encoded.put(map_count);encoded.put(uint32_t(TheMaps.size()));
        for(const auto& cache:entries) {
            encoded.put(cache.plain_size);encoded.put(uint32_t(cache.compressed.size()));
            encoded.put(cache.compressed.data(),cache.compressed.size());
        }
        if(encoded.failed) return false;
        aggregate_cache.shape=map_count;aggregate_cache.screens=TheMaps.size();
        aggregate_cache.valid=true;
    }
    out.put(aggregate_cache.encoded.data.data(),aggregate_cache.encoded.data.size());
    return !out.failed;
}
// The capture cache already owns both encodings of every screen. Prefer
// it to the bounded global decode LRU when incoming packed bytes match exactly.
// This is byte decoding only: get_map_blocks still runs its complete structural
// Skip validation before any live value changes. No live stamp or dirty bit is
// trusted here, and the returned blob owns its bytes independently of the cache.
inline uint64_t& map_capture_decode_reuse_counter() { static uint64_t value=0;return value; }
inline bool inflate_map_section(SaveStateBlob& in,SaveStateBlob& raw,size_t index) {
    if(in.failed || in.pos>in.data.size() || in.data.size()-in.pos<sizeof(uint32_t)*2) return false;
    uint32_t unpacked=0,packed=0;
    memcpy(&unpacked,in.data.data()+in.pos,sizeof(unpacked));
    memcpy(&packed,in.data.data()+in.pos+sizeof(unpacked),sizeof(packed));
    const size_t header=sizeof(uint32_t)*2;
    if(!unpacked || unpacked>MAX_SECTION_BYTES || !packed || packed>in.data.size()-in.pos-header) return false;
    const auto& entries=map_block_caches();
    if(index<entries.size()) {
        const auto& cache=entries[index];
        if(cache.plain_size==unpacked && cache.plain.size()==unpacked
            && cache.compressed.size()==packed
            && memcmp(cache.compressed.data(),in.data.data()+in.pos+header,packed)==0) {
            raw.data=cache.plain.bytes();raw.pos=0;raw.failed=false;
            in.pos+=header+packed;++map_capture_decode_reuse_counter();return true;
        }
    }
    return inflate_section(in,raw);
}
// Only the map prepared path uses immutable sharing. The SaveStateBlob
// overload above remains available for direct decoding callers/selftests.
inline bool inflate_map_section(SaveStateBlob& in,PreparedMapBlob& raw,size_t index) {
    if(in.failed || in.pos>in.data.size() || in.data.size()-in.pos<sizeof(uint32_t)*2) return false;
    uint32_t unpacked=0,packed=0;
    memcpy(&unpacked,in.data.data()+in.pos,sizeof(unpacked));
    memcpy(&packed,in.data.data()+in.pos+sizeof(unpacked),sizeof(packed));
    const size_t header=sizeof(uint32_t)*2;
    if(!unpacked || unpacked>MAX_SECTION_BYTES || !packed || packed>in.data.size()-in.pos-header) return false;
    const auto& entries=map_block_caches();
    if(index<entries.size()) {
        const auto& cache=entries[index];
        if(cache.plain_size==unpacked && cache.plain.size()==unpacked
            && cache.compressed.size()==packed
            && memcmp(cache.compressed.data(),in.data.data()+in.pos+header,packed)==0) {
            raw.data=cache.plain;raw.pos=0;raw.failed=false;
            in.pos+=header+packed;++map_capture_decode_reuse_counter();return true;
        }
    }
    SaveStateBlob decoded;
    if(!inflate_section(in,decoded)) return false;
    raw.data.publish(std::move(decoded.data));raw.pos=0;raw.failed=false;
    return true;
}
inline bool get_map_blocks(SaveStateBlob& in,std::vector<PreparedMapBlob>& blocks) {
    decltype(map_count) maps=0;uint32_t count=0;in.get(maps);in.get(count);
    if(in.failed || maps!=map_count || count!=TheMaps.size()) return false;
    std::vector<PreparedMapBlob> decoded(count);size_t aggregate=0;
    for(size_t index=0;index<count;++index) {
        if(in.failed || in.pos>in.data.size() || in.data.size()-in.pos<sizeof(uint32_t)*2) return false;
        uint32_t plain_size=0;memcpy(&plain_size,in.data.data()+in.pos,sizeof(plain_size));
        if(!plain_size || plain_size>MAX_SECTION_BYTES-aggregate) return false;
        aggregate+=plain_size;
        if(!inflate_map_section(in,decoded[index],index)) return false;
        if(!decoded[index].data.validated()) {
            BasicSkip<PreparedMapBlob> skip{decoded[index]};screen(skip,TheMaps[index]);
            if(decoded[index].failed || decoded[index].pos!=decoded[index].data.size()) return false;
            decoded[index].data.mark_validated();
        }
        decoded[index].pos=0;
    }
    blocks.swap(decoded);return !in.failed;
}
// Preflight has validated each complete record. Decide skips before mutations
// invalidate generations, and still require exact requested value equality.
inline std::vector<uint8_t> map_restore_skips(const std::vector<PreparedMapBlob>& blocks) {
    std::vector<uint8_t> skips(blocks.size(),0);
    auto& entries=map_block_caches();
    const bool audit=snapshot_map_tracking::audit();
    for(size_t index=0;index<blocks.size();++index) {
        if(index>=entries.size()) continue;
        const auto& cache=entries[index];const auto& sc=TheMaps[index];
        const bool clean=snapshot_map_tracking::state().trusted && cache.plain_size
            && cache.tracking_epoch==snapshot_map_tracking::state().epoch
            && cache.tracking_version==snapshot_map_tracking::version(index);
        bool equal=clean&&!audit;
        if(!equal) {
            equal=cache.object_stamp.size()==sizeof(mapscr)
                && memcmp(cache.object_stamp.data(),&sc,sizeof(mapscr))==0
                && cache.notes==sc.usr_notes;
            if(clean&&audit&&!equal) snapshot_map_tracking::missed_write(index);
        }
        skips[index]=equal && cache.plain==blocks[index].data;
    }
    return skips;
}
inline bool apply_map_blocks(std::vector<PreparedMapBlob>& blocks) {
    if(blocks.size()!=TheMaps.size()) return false;
    // Validate all records again before the first mutation, so direct callers
    // receive the same complete preflight guarantee as get_map_blocks callers.
    size_t aggregate=0;
    for(size_t index=0;index<blocks.size();++index) {
        auto& b=blocks[index];
        if(b.failed || b.data.size()>MAX_SECTION_BYTES-aggregate) return false;
        aggregate+=b.data.size();b.pos=0;
        if(!b.data.validated()) {
            BasicSkip<PreparedMapBlob> skip{b};screen(skip,TheMaps[index]);
            if(b.failed || b.pos!=b.data.size()) return false;
            b.data.mark_validated();
        }
    }
    const auto skips=map_restore_skips(blocks);
    bool invalidated=false;
    for(size_t index=0;index<blocks.size();++index) {
        auto& b=blocks[index];auto& sc=TheMaps[index];
        auto& entries=map_block_caches();
        MapBlockCache* cache=index<entries.size()?&entries[index]:nullptr;
        if(skips[index]) {b.pos=b.data.size();continue;}
        if(!invalidated) {snapshot_map_tracking::invalidate_all();invalidated=true;}
        b.pos=0;BasicReader<PreparedMapBlob> reader{b};screen(reader,sc);
        // The old encoding describes a different screen. Force the next capture
        // to encode its actual restored values before publishing another block.
        if(cache) cache->compressed.clear();
        if(b.failed || b.pos!=b.data.size()) return false;
    }
    return true;
}
