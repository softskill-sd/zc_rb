// Included inside snapshot_definitions. All blocks are complete snapshots;
// caches only avoid encoding/compressing values that compare exactly equal.
constexpr size_t TILE_BLOCK_RECORDS = 128;
constexpr size_t TILE_ZERO_MASK_WORDS = (TILE_BLOCK_RECORDS+63)/64;
static_assert(TILE_ZERO_MASK_WORDS*64>=TILE_BLOCK_RECORDS,"tile zero mask covers every block record");
constexpr size_t TILE_METADATA_BYTES = sizeof(blank_tile_table)+sizeof(used_tile_table)+sizeof(blank_tile_quarters_table);
struct TileRecordDescriptor { uint32_t offset=0; uint16_t bytes=0; byte format=0,present=0; };
struct TileBlockCache {
    uint64_t tracking_epoch=0,tracking_version=0;
    std::vector<uint8_t> plain, compressed;
    // Derived only from a complete validated encoding; never snapshot state.
    std::vector<TileRecordDescriptor> records;
    // Derived exclusively from validated cached pixels. Never trust live blank
    // metadata and never use these bits to skip reading a live payload.
    std::array<uint64_t,TILE_ZERO_MASK_WORDS> zero_expected_masks{};
};
inline uint64_t& tile_block_rebuild_counter() { static uint64_t value=0; return value; }
inline uint64_t tile_block_rebuilds() { return tile_block_rebuild_counter(); }
inline bool tile_format_supported(byte format,uint8_t present) {
    return present<=1 && (present ? format>=tf4Bit && format<=tf32Bit : format<=tf32Bit);
}
inline const std::array<byte,1024>& tile_expected_zero_bytes() {
    static const std::array<byte,1024> bytes{}; return bytes;
}
// Shared-zero equality only: every one of the validated 256 payload bytes
// is read. SSE2 is mandatory for MSVC x64; other targets and lengths use memcmp.
inline bool tile_zero_payload_equal(const byte* live,size_t bytes) {
#if defined(_MSC_VER) && defined(_M_X64)
    if(bytes==256) {
        const auto* p=reinterpret_cast<const __m128i*>(live);
        // Balanced reductions keep dependencies short. The final load starts at
        // byte240 and ends at255; loadu requires no alignment or extra padding.
        const __m128i a0=_mm_or_si128(_mm_loadu_si128(p+0),_mm_loadu_si128(p+1));
        const __m128i a1=_mm_or_si128(_mm_loadu_si128(p+2),_mm_loadu_si128(p+3));
        const __m128i a2=_mm_or_si128(_mm_loadu_si128(p+4),_mm_loadu_si128(p+5));
        const __m128i a3=_mm_or_si128(_mm_loadu_si128(p+6),_mm_loadu_si128(p+7));
        const __m128i a4=_mm_or_si128(_mm_loadu_si128(p+8),_mm_loadu_si128(p+9));
        const __m128i a5=_mm_or_si128(_mm_loadu_si128(p+10),_mm_loadu_si128(p+11));
        const __m128i a6=_mm_or_si128(_mm_loadu_si128(p+12),_mm_loadu_si128(p+13));
        const __m128i a7=_mm_or_si128(_mm_loadu_si128(p+14),_mm_loadu_si128(p+15));
        const __m128i b0=_mm_or_si128(a0,a1),b1=_mm_or_si128(a2,a3);
        const __m128i b2=_mm_or_si128(a4,a5),b3=_mm_or_si128(a6,a7);
        const __m128i combined=_mm_or_si128(_mm_or_si128(b0,b1),_mm_or_si128(b2,b3));
        return _mm_movemask_epi8(_mm_cmpeq_epi8(combined,_mm_setzero_si128()))==0xffff;
    }
#endif
    return memcmp(tile_expected_zero_bytes().data(),live,bytes)==0;
}
inline bool tile_expected_zero(const TileBlockCache& cache,size_t index) {
    return index<TILE_BLOCK_RECORDS && (cache.zero_expected_masks[index/64] & (uint64_t(1)<<(index%64)))!=0;
}
inline bool tile_cache_index(TileBlockCache& cache,size_t count) {
    if(count>TILE_BLOCK_RECORDS) return false;
    std::vector<TileRecordDescriptor> records; records.reserve(count); size_t pos=0;
    std::array<uint64_t,TILE_ZERO_MASK_WORDS> zero_expected_masks{};
    for(size_t i=0;i<count;++i) {
        if(pos>cache.plain.size() || cache.plain.size()-pos<2) return false;
        byte format=cache.plain[pos++],present=cache.plain[pos++];
        if(!tile_format_supported(format,present)) return false;
        size_t bytes=present ? tilesize(format) : 0;
        if(bytes>1024 || bytes>cache.plain.size()-pos) return false;
        // Bounds/format checks precede this exact classification. The cached
        // representation remains complete even when its baseline is all zero.
        if(present && memcmp(cache.plain.data()+pos,tile_expected_zero_bytes().data(),bytes)==0)
            zero_expected_masks[i/64]|=uint64_t(1)<<(i%64);
        records.push_back({uint32_t(pos),uint16_t(bytes),format,present}); pos+=bytes;
    }
    if(pos!=cache.plain.size()) return false;
    cache.records=std::move(records); cache.zero_expected_masks=zero_expected_masks; return true;
}
inline bool tile_records_equal_fallback(const TileBlockCache& cache,const tiledata* live,size_t count) {
    size_t pos=0;
    for(size_t i=0;i<count;++i) {
        const auto& tile=live[i];
        uint8_t present=tile.data ? 1 : 0;
        if(pos>cache.plain.size() || cache.plain.size()-pos<2
            || cache.plain[pos]!=tile.format || cache.plain[pos+1]!=present) return false;
        pos+=2;
        if(present) {
            if(!tile_format_supported(tile.format,present)) return false;
            size_t bytes=tilesize(tile.format);
            if(bytes>cache.plain.size()-pos || memcmp(cache.plain.data()+pos,tile.data,bytes)!=0) return false;
            pos+=bytes;
        }
    }
    return pos==cache.plain.size();
}
inline bool tile_records_equal(const TileBlockCache& cache,const tiledata* live,size_t count) {
    if(cache.records.size()!=count) return tile_records_equal_fallback(cache,live,count);
    for(size_t i=0;i<count;++i) {
        const auto& record=cache.records[i]; const auto& tile=live[i];
        if(tile.format!=record.format || bool(tile.data)!=bool(record.present)) return false;
        // The validated cached offset/length is independent of the live pointer.
        // Direct/script writes still compare every pixel byte on every capture.
        if(record.present) {
            if(tile_expected_zero(cache,i)) {
                if(!tile_zero_payload_equal(tile.data,record.bytes)) return false;
            } else if(memcmp(cache.plain.data()+record.offset,tile.data,record.bytes)!=0) return false;
        }
    }
    return true;
}
inline bool tile_block_equal(const TileBlockCache& cache,size_t first,size_t count) {
    return tile_records_equal(cache,newtilebuf+first,count);
}
// Targeted shared-zero regression; both edge records and direct pixel writes
// remain authoritative even when global blank metadata claims they are empty.
inline bool tile_shared_zero_selftest() {
    // Use the engine's payload size, not the packed 4-bit file representation:
    // this engine expands tf4Bit pixels to a 256-byte in-memory tile.
    const size_t pixel_bytes=tilesize(tf4Bit);
    if(pixel_bytes<128 || pixel_bytes>tile_expected_zero_bytes().size()) return false;
    std::vector<std::vector<byte>> pixels(TILE_BLOCK_RECORDS,std::vector<byte>(pixel_bytes));
    std::array<tiledata,TILE_BLOCK_RECORDS> live{};
    SaveStateBlob encoded;
    for(size_t i=0;i<live.size();++i) {
        live[i].format=tf4Bit; live[i].data=pixels[i].data();
        encoded.put(byte(tf4Bit)); encoded.put(uint8_t(1)); encoded.put(live[i].data,pixel_bytes);
    }
    TileBlockCache cache; cache.plain=std::move(encoded.data);
    if(!tile_cache_index(cache,live.size())) return false;
    for(size_t i=0;i<live.size();++i) if(!tile_expected_zero(cache,i)) return false;
    auto equal=[&]() { return tile_records_equal(cache,live.data(),live.size()); };
    auto reference=[&]() { return tile_records_equal_fallback(cache,live.data(),live.size()); };
    if(!equal() || !reference()) return false;
    struct BlankMetadataGuard {
        bool first=blank_tile_table[0],last=blank_tile_table[NEWMAXTILES-1];
        BlankMetadataGuard() { blank_tile_table[0]=blank_tile_table[NEWMAXTILES-1]=true; }
        ~BlankMetadataGuard() { blank_tile_table[0]=first; blank_tile_table[NEWMAXTILES-1]=last; }
    } guard;
    for(size_t tile:{size_t(0),live.size()-1}) {
        // Independently compare every mutable byte against the original parser
        // path, covering every SIMD lane/bit position at both record edges.
        for(size_t pixel=0;pixel<pixel_bytes;++pixel) {
            for(unsigned bit=0;bit<8;++bit) {
                pixels[tile][pixel]=byte(1u<<bit);
                if(equal() || reference()) return false;
                pixels[tile][pixel]=0;
                if(!equal() || !reference()) return false;
            }
        }
        std::vector<byte> replacement(pixel_bytes);
        live[tile].data=replacement.data();
        if(!equal() || !reference()) return false; // Pointer identity is irrelevant.
        replacement[pixel_bytes-1]=1;
        if(equal() || reference()) return false;
        live[tile].data=pixels[tile].data();
        live[tile].format=tf8Bit;
        if(equal() || reference()) return false;
        live[tile].format=tf4Bit;
        live[tile].data=nullptr;
        if(equal() || reference()) return false;
        live[tile].data=pixels[tile].data();
    }
    // A changed cached expectation must recompute its zero bit, and compare
    // against actual nonzero cached bytes until it changes back to zero.
    cache.plain[cache.records.back().offset+63]=7;
    if(!tile_cache_index(cache,live.size()) || tile_expected_zero(cache,live.size()-1) || equal()) return false;
    pixels.back()[63]=7;
    if(!equal() || !reference()) return false;
    cache.plain[cache.records.back().offset+63]=0;
    if(!tile_cache_index(cache,live.size()) || !tile_expected_zero(cache,live.size()-1) || equal()) return false;
    pixels.back()[63]=0;
    if(!equal() || !reference()) return false;
    // Null expectations do not borrow a zero baseline: presence remains state.
    const size_t last_offset=cache.records.back().offset;
    cache.plain[last_offset-1]=0; cache.plain.resize(last_offset);
    if(!tile_cache_index(cache,live.size()) || tile_expected_zero(cache,live.size()-1) || equal()) return false;
    live.back().data=nullptr;
    if(!equal() || !reference()) return false;
    live.back().data=pixels.back().data();
    if(equal() || reference()) return false;
    cache.plain[last_offset-1]=1;
    cache.plain.insert(cache.plain.end(),pixels.back().begin(),pixels.back().end());
    if(!tile_cache_index(cache,live.size()) || !tile_expected_zero(cache,live.size()-1) || !equal()) return false;
    // Missing descriptor fallback must retain its original exact behavior.
    cache.records.clear(); pixels.front()[0]=1;
    if(equal()) return false;
    pixels.front()[0]=0;
    return equal() && reference();
}
inline bool tile_descriptor_selftest() {
    std::vector<std::vector<byte>> pixels;
    std::vector<tiledata> live;
    pixels.reserve(6); live.reserve(6);
    SaveStateBlob encoded;
    for(byte format=tf4Bit;format<=tf32Bit;++format) {
        const size_t bytes=tilesize(format); pixels.emplace_back(bytes);
        for(size_t i=0;i<bytes;++i) pixels.back()[i]=byte(i*17+format);
        tiledata tile{}; tile.format=format; tile.data=pixels.back().data(); live.push_back(tile);
        encoded.put(format); encoded.put(uint8_t(1)); encoded.put(tile.data,bytes);
    }
    tiledata absent{}; absent.format=tfInvalid; absent.data=nullptr; live.push_back(absent);
    encoded.put(absent.format); encoded.put(uint8_t(0));
    TileBlockCache cache; cache.plain=std::move(encoded.data);
    if(!tile_cache_index(cache,live.size()) || !tile_records_equal(cache,live.data(),live.size())
        || !tile_records_equal_fallback(cache,live.data(),live.size())) return false;
    for(size_t i=0;i<pixels.size();++i) {
        for(size_t index=0;index<pixels[i].size();++index) {
            pixels[i][index]^=1;
            if(tile_records_equal(cache,live.data(),live.size())
                || tile_records_equal_fallback(cache,live.data(),live.size())) return false;
            pixels[i][index]^=1;
        }
        byte format=live[i].format; live[i].format=tfInvalid;
        if(tile_records_equal(cache,live.data(),live.size())) return false;
        live[i].format=format;
        byte* pointer=live[i].data; live[i].data=nullptr;
        if(tile_records_equal(cache,live.data(),live.size())) return false;
        live[i].data=pointer;
    }
    cache.records.clear();
    return tile_records_equal(cache,live.data(),live.size()) && tile_shared_zero_selftest();
}
inline bool tile_cache_encode(TileBlockCache& cache,SaveStateBlob& plain) {
    if(plain.failed || plain.data.empty() || plain.data.size()>MAX_SECTION_BYTES) return false;
    uLongf size=compressBound((uLong)plain.data.size());
    std::vector<uint8_t> packed(size);
    if(snapshot_profile::measured_compress2(packed.data(),&size,plain.data.data(),(uLong)plain.data.size(),Z_BEST_SPEED)!=Z_OK) return false;
    packed.resize(size);
    cache.plain=std::move(plain.data); cache.compressed=std::move(packed); cache.records.clear(); cache.zero_expected_masks={};
    return true;
}
inline void tile_cache_put(SaveStateBlob& out,const TileBlockCache& cache) {
    out.put((uint32_t)cache.plain.size()); out.put((uint32_t)cache.compressed.size());
    out.put(cache.compressed.data(),cache.compressed.size());
}
inline bool put_tile_blocks(SaveStateBlob& out) {
    if(!newtilebuf) return false;
    constexpr size_t BLOCKS=(NEWMAXTILES+TILE_BLOCK_RECORDS-1)/TILE_BLOCK_RECORDS;
    static std::vector<TileBlockCache> cached(BLOCKS);
    static TileBlockCache metadata;
    out.put((uint32_t)NEWMAXTILES); out.put((uint32_t)BLOCKS);
    size_t total_plain=0;
    for(size_t block=0;block<BLOCKS;++block) {
        size_t first=block*TILE_BLOCK_RECORDS;
        size_t count=std::min(TILE_BLOCK_RECORDS,size_t(NEWMAXTILES)-first);
        auto& cache=cached[block];
        auto& tracker=snapshot_tile_tracking::state();
        const bool clean=tracker.trusted && !cache.plain.empty()
            && cache.tracking_epoch==tracker.epoch
            && cache.tracking_version==tracker.versions[block];
        bool equal=clean;
        if(!clean || snapshot_tile_tracking::audit()) {
            ++tracker.checked;
            equal=!cache.plain.empty() && tile_block_equal(cache,first,count);
            if(clean && !equal) snapshot_tile_tracking::missed_write(block);
        } else ++tracker.skipped;
        if(!equal) {
            SaveStateBlob plain; plain.data.reserve(count*(2+256));
            for(size_t i=0;i<count;++i) {
                const auto& tile=newtilebuf[first+i]; uint8_t present=tile.data ? 1 : 0;
                if(!tile_format_supported(tile.format,present)) return false;
                plain.put(tile.format); plain.put(present);
                if(present) plain.put(tile.data,tilesize(tile.format));
            }
            if(!tile_cache_encode(cache,plain) || !tile_cache_index(cache,count)) return false;
            ++tile_block_rebuild_counter();
        }
        cache.tracking_epoch=tracker.epoch;
        cache.tracking_version=tracker.versions[block];
        if(cache.plain.size()>MAX_SECTION_BYTES-total_plain) return false;
        total_plain+=cache.plain.size();
        out.put((uint32_t)count); tile_cache_put(out,cache);
    }
    bool metadata_equal=metadata.plain.size()==TILE_METADATA_BYTES;
    if(metadata_equal) {
        size_t pos=0;
        metadata_equal=memcmp(metadata.plain.data(),blank_tile_table,sizeof(blank_tile_table))==0;
        pos+=sizeof(blank_tile_table);
        metadata_equal=metadata_equal && memcmp(metadata.plain.data()+pos,used_tile_table,sizeof(used_tile_table))==0;
        pos+=sizeof(used_tile_table);
        metadata_equal=metadata_equal && memcmp(metadata.plain.data()+pos,blank_tile_quarters_table,sizeof(blank_tile_quarters_table))==0;
    }
    if(!metadata_equal) {
        SaveStateBlob plain; plain.data.reserve(TILE_METADATA_BYTES);
        plain.put(blank_tile_table); plain.put(used_tile_table); plain.put(blank_tile_quarters_table);
        if(!tile_cache_encode(metadata,plain)) return false;
    }
    if(metadata.plain.size()>MAX_SECTION_BYTES-total_plain) return false;
    tile_cache_put(out,metadata);
    return !out.failed;
}
inline bool validate_tile_block(const SaveStateBlob& block,size_t count) {
    if(block.failed || block.data.size()<count*2 || block.data.size()>count*(2+1024)) return false;
    size_t pos=0;
    for(size_t i=0;i<count;++i) {
        if(pos>block.data.size() || block.data.size()-pos<2) return false;
        byte format=block.data[pos++]; uint8_t present=block.data[pos++];
        if(!tile_format_supported(format,present)) return false;
        size_t bytes=present ? tilesize(format) : 0;
        if(bytes>block.data.size()-pos) return false;
        pos+=bytes;
    }
    return pos==block.data.size();
}
inline bool validate_tile_metadata(const SaveStateBlob& metadata) {
    static_assert(sizeof(bool)==1,"snapshot Boolean table encoding requires byte-sized bool");
    if(metadata.failed || metadata.data.size()!=TILE_METADATA_BYTES) return false;
    for(uint8_t value:metadata.data) if(value>1) return false;
    return true;
}
inline bool get_tile_blocks(SaveStateBlob& in,std::vector<SaveStateBlob>& blocks,SaveStateBlob& metadata) {
    constexpr size_t EXPECTED=(NEWMAXTILES+TILE_BLOCK_RECORDS-1)/TILE_BLOCK_RECORDS;
    uint32_t tiles=0,count=0; in.get(tiles); in.get(count);
    if(in.failed || tiles!=NEWMAXTILES || count!=EXPECTED) return false;
    std::vector<SaveStateBlob> decoded(count);
    size_t total_plain=0;
    for(size_t i=0;i<count;++i) {
        size_t records=std::min(TILE_BLOCK_RECORDS,size_t(NEWMAXTILES)-i*TILE_BLOCK_RECORDS);
        uint32_t stored_records=0,plain_size=0; in.get(stored_records);
        if(in.failed || stored_records!=records || in.pos>in.data.size() || in.data.size()-in.pos<sizeof(uint32_t)*2) return false;
        memcpy(&plain_size,in.data.data()+in.pos,sizeof(plain_size));
        if(plain_size<records*2 || plain_size>records*(2+1024) || plain_size>MAX_SECTION_BYTES-total_plain) return false;
        total_plain+=plain_size;
        if(!inflate_section(in,decoded[i]) || !validate_tile_block(decoded[i],records)) return false;
    }
    uint32_t plain_size=0;
    if(in.failed || in.pos>in.data.size() || in.data.size()-in.pos<sizeof(uint32_t)*2) return false;
    memcpy(&plain_size,in.data.data()+in.pos,sizeof(plain_size));
    if(plain_size!=TILE_METADATA_BYTES || plain_size>MAX_SECTION_BYTES-total_plain) return false;
    SaveStateBlob decoded_metadata;
    if(!inflate_section(in,decoded_metadata) || !validate_tile_metadata(decoded_metadata)) return false;
    blocks=std::move(decoded); metadata=std::move(decoded_metadata);
    return true;
}
inline bool apply_tile_blocks(std::vector<SaveStateBlob>& blocks,SaveStateBlob& metadata) {
    constexpr size_t EXPECTED=(NEWMAXTILES+TILE_BLOCK_RECORDS-1)/TILE_BLOCK_RECORDS;
    if(!newtilebuf || blocks.size()!=EXPECTED || !validate_tile_metadata(metadata)) return false;
    for(size_t i=0;i<blocks.size();++i) {
        size_t count=std::min(TILE_BLOCK_RECORDS,size_t(NEWMAXTILES)-i*TILE_BLOCK_RECORDS);
        if(!validate_tile_block(blocks[i],count)) return false;
    }
    // Stage all required allocations before touching a live tile. Existing
    // allocations of the correct format keep their addresses.
    struct Replacement { size_t tile; byte* pixels; };
    std::vector<Replacement> replacements; replacements.reserve(NEWMAXTILES);
    for(size_t block=0;block<blocks.size();++block) {
        size_t count=std::min(TILE_BLOCK_RECORDS,size_t(NEWMAXTILES)-block*TILE_BLOCK_RECORDS);
        const auto& bytes=blocks[block].data; size_t pos=0;
        for(size_t i=0;i<count;++i) {
            size_t tile=block*TILE_BLOCK_RECORDS+i;
            byte format=bytes[pos++]; uint8_t present=bytes[pos++];
            size_t size=present ? tilesize(format) : 0;
            if(present && (!newtilebuf[tile].data || newtilebuf[tile].format!=format)) {
                byte* pixels=static_cast<byte*>(malloc(size));
                if(!pixels) { for(auto& old:replacements) free(old.pixels); return false; }
                memcpy(pixels,bytes.data()+pos,size); replacements.push_back({tile,pixels});
            }
            pos+=size;
        }
    }
    // Even a restore to an older value must invalidate the forward cache.
    snapshot_tile_tracking::invalidate_all();
    size_t replacement=0;
    for(size_t block=0;block<blocks.size();++block) {
        size_t count=std::min(TILE_BLOCK_RECORDS,size_t(NEWMAXTILES)-block*TILE_BLOCK_RECORDS);
        const auto& bytes=blocks[block].data; size_t pos=0;
        for(size_t i=0;i<count;++i) {
            size_t tile=block*TILE_BLOCK_RECORDS+i;
            byte format=bytes[pos++]; uint8_t present=bytes[pos++];
            size_t size=present ? tilesize(format) : 0;
            auto& target=newtilebuf[tile];
            if(!present) { free(target.data); target.data=nullptr; }
            else if(replacement<replacements.size() && replacements[replacement].tile==tile) {
                free(target.data); target.data=replacements[replacement++].pixels;
            } else if(memcmp(target.data,bytes.data()+pos,size)!=0) memcpy(target.data,bytes.data()+pos,size);
            target.format=format; pos+=size;
        }
    }
    size_t pos=0;
    memcpy(blank_tile_table,metadata.data.data()+pos,sizeof(blank_tile_table)); pos+=sizeof(blank_tile_table);
    memcpy(used_tile_table,metadata.data.data()+pos,sizeof(used_tile_table)); pos+=sizeof(used_tile_table);
    memcpy(blank_tile_quarters_table,metadata.data.data()+pos,sizeof(blank_tile_quarters_table));
    return true;
}

// Internal commit path for blocks already fully validated by prepare. Derived
// descriptors avoid repeating format parsing and tilesize calls. Every pixel
// still compares exactly, and all replacement allocations precede live writes.
inline bool apply_prepared_tile_blocks(std::vector<SaveStateBlob>& blocks,SaveStateBlob& metadata,
    const std::vector<std::vector<TileRecordDescriptor>>& descriptors) {
    constexpr size_t EXPECTED=(NEWMAXTILES+TILE_BLOCK_RECORDS-1)/TILE_BLOCK_RECORDS;
    if(!newtilebuf || blocks.size()!=EXPECTED || descriptors.size()!=EXPECTED
        || metadata.failed || metadata.data.size()!=TILE_METADATA_BYTES) return false;
    for(size_t block=0;block<EXPECTED;++block)
        if(blocks[block].failed || descriptors[block].size()!=std::min(TILE_BLOCK_RECORDS,size_t(NEWMAXTILES)-block*TILE_BLOCK_RECORDS)) return false;
    struct Replacement { size_t tile; std::unique_ptr<byte,decltype(&free)> pixels; };
    std::vector<Replacement> replacements;
    for(size_t block=0;block<EXPECTED;++block) {
        const auto& bytes=blocks[block].data;
        for(size_t i=0;i<descriptors[block].size();++i) {
            const auto& record=descriptors[block][i]; const size_t tile=block*TILE_BLOCK_RECORDS+i;
            if(record.present && (!newtilebuf[tile].data || newtilebuf[tile].format!=record.format)) {
                std::unique_ptr<byte,decltype(&free)> pixels(static_cast<byte*>(malloc(record.bytes)),&free);
                if(!pixels) return false;
                memcpy(pixels.get(),bytes.data()+record.offset,record.bytes);
                replacements.push_back({tile,std::move(pixels)});
            }
        }
    }
    snapshot_tile_tracking::invalidate_all();
    size_t replacement=0;
    for(size_t block=0;block<EXPECTED;++block) {
        const auto& bytes=blocks[block].data;
        for(size_t i=0;i<descriptors[block].size();++i) {
            const auto& record=descriptors[block][i]; const size_t tile=block*TILE_BLOCK_RECORDS+i;
            auto& target=newtilebuf[tile];
            if(!record.present) { free(target.data); target.data=nullptr; }
            else if(replacement<replacements.size() && replacements[replacement].tile==tile) {
                free(target.data); target.data=replacements[replacement++].pixels.release();
            } else if(memcmp(target.data,bytes.data()+record.offset,record.bytes)!=0)
                memcpy(target.data,bytes.data()+record.offset,record.bytes);
            target.format=record.format;
        }
    }
    size_t pos=0;
    memcpy(blank_tile_table,metadata.data.data()+pos,sizeof(blank_tile_table)); pos+=sizeof(blank_tile_table);
    memcpy(used_tile_table,metadata.data.data()+pos, sizeof(used_tile_table)); pos+=sizeof(used_tile_table);
    memcpy(blank_tile_quarters_table,metadata.data.data()+pos,sizeof(blank_tile_quarters_table));
    return true;
}
