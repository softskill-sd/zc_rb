#pragma once
#include <zlib.h>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <cstring>

// Opt-in main-thread profiling. Native snapshot bytes and zlib behavior are
// unchanged; cached blocks never count as a compression/decompression call.
namespace snapshot_profile {
struct CaptureMeasure {
    double compress_us=0,decompress_us=0;
    double general_us=0,combo_us=0,maps_us=0,tiles_us=0;
    uint64_t compress_calls=0,decompress_calls=0;
    uint64_t compress_input_bytes=0,compress_output_bytes=0;
    uint64_t decompress_input_bytes=0,decompress_output_bytes=0;
};
inline bool capture_enabled() {
    static const bool value=[] { const char* setting=std::getenv("ZC255_PROFILE_CAPTURE"); return setting && *setting && *setting!='0'; }();
    return value;
}
inline bool restore_enabled() {
    static const bool value=[] { const char* setting=std::getenv("ZC255_PROFILE_RESTORE"); return setting && *setting && *setting!='0'; }();
    return value;
}
inline bool enabled() { return capture_enabled() || restore_enabled(); }
inline CaptureMeasure& capture_measure() { static CaptureMeasure value; return value; }
inline void reset_capture() { capture_measure()=CaptureMeasure{}; }
struct ScopedMeasure {
    double* target=nullptr;
    std::chrono::steady_clock::time_point start;
    explicit ScopedMeasure(double& field) {
        if(enabled()) { target=&field; start=std::chrono::steady_clock::now(); }
    }
    ~ScopedMeasure() {
        if(target) *target+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
    }
    ScopedMeasure(const ScopedMeasure&)=delete;
    ScopedMeasure& operator=(const ScopedMeasure&)=delete;
};
inline int measured_compress2(Bytef* destination,uLongf* destination_length,
    const Bytef* source,uLong source_length,int level) {
    if(!enabled()) return ::compress2(destination,destination_length,source,source_length,level);
    auto start=std::chrono::steady_clock::now();
    int status=::compress2(destination,destination_length,source,source_length,level);
    auto elapsed=std::chrono::steady_clock::now()-start;
    auto& measure=capture_measure();
    measure.compress_us+=std::chrono::duration<double,std::micro>(elapsed).count();
    ++measure.compress_calls; measure.compress_input_bytes+=source_length;
    if(status==Z_OK) measure.compress_output_bytes+=*destination_length;
    return status;
}
// RFC1950/1951 stored blocks: standard zlib streams without allocating a
// compressor/window for each changing chunk. Existing uncompress validates it.
inline int measured_store_zlib(Bytef* destination,uLongf* destination_length,
    const Bytef* source,uLong source_length) {
    const uLong blocks=source_length?1+(source_length-1)/65535:1;
    const uint64_t required=uint64_t(source_length)+uint64_t(blocks)*5+6;
    if(required>*destination_length || source_length>std::numeric_limits<uInt>::max()) return Z_BUF_ERROR;
    const bool profiling=enabled();
    const auto start=profiling?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    Bytef* out=destination;*out++=0x78;*out++=0x01;
    uLong remaining=source_length;const Bytef* input=source;
    do {
        const uint16_t count=uint16_t(remaining>65535?65535:remaining);
        const uint16_t inverse=uint16_t(~count);
        *out++=remaining<=65535?1:0;
        *out++=Bytef(count);*out++=Bytef(count>>8);
        *out++=Bytef(inverse);*out++=Bytef(inverse>>8);
        if(count) {std::memcpy(out,input,count);out+=count;input+=count;}
        remaining-=count;
    } while(remaining);
    const uLong checksum=adler32(adler32(0,Z_NULL,0),source,uInt(source_length));
    *out++=Bytef(checksum>>24);*out++=Bytef(checksum>>16);*out++=Bytef(checksum>>8);*out++=Bytef(checksum);
    *destination_length=uLongf(out-destination);
    if(profiling) {
        auto& measure=capture_measure();
        measure.compress_us+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
        ++measure.compress_calls;measure.compress_input_bytes+=source_length;measure.compress_output_bytes+=*destination_length;
    }
    return Z_OK;
}
inline int measured_uncompress(Bytef* destination,uLongf* destination_length,
    const Bytef* source,uLong source_length) {
    if(!enabled()) return ::uncompress(destination,destination_length,source,source_length);
    auto start=std::chrono::steady_clock::now();
    int status=::uncompress(destination,destination_length,source,source_length);
    auto elapsed=std::chrono::steady_clock::now()-start;
    auto& measure=capture_measure();
    measure.decompress_us+=std::chrono::duration<double,std::micro>(elapsed).count();
    ++measure.decompress_calls; measure.decompress_input_bytes+=source_length;
    if(status==Z_OK) measure.decompress_output_bytes+=*destination_length;
    return status;
}
} // namespace snapshot_profile
