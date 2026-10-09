#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif
// Read only complete elements within [data,data+count). Equality is exact object
// representation equality, preserving the legacy memcmp-defined run boundaries.
template<class T> inline size_t zc255_run_length(const T* data,size_t count) {
    if(!count) return 0;
    size_t run=1;
    // Singleton/short runs keep the old inexpensive scalar path.
    while(run<count && run<4 && !memcmp(data,data+run,sizeof(T))) ++run;
    if(run==count || run<4) return run;
#if defined(_M_X64) || defined(__SSE2__)
    if constexpr(sizeof(T)<=8 && 16%sizeof(T)==0) {
        constexpr size_t GROUP=16/sizeof(T);
        uint8_t pattern[16];
        for(size_t offset=0;offset<16;offset+=sizeof(T)) memcpy(pattern+offset,data,sizeof(T));
        const __m128i expected=_mm_loadu_si128(reinterpret_cast<const __m128i*>(pattern));
        while(count-run>=GROUP) {
            const __m128i next=_mm_loadu_si128(reinterpret_cast<const __m128i*>(data+run));
            if(_mm_movemask_epi8(_mm_cmpeq_epi8(expected,next))!=0xFFFF) break;
            run+=GROUP;
        }
    }
#endif
    while(run<count && !memcmp(data,data+run,sizeof(T))) ++run;
    return run;
}
