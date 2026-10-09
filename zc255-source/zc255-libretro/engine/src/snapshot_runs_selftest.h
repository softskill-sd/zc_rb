#pragma once
#include "zc/savestate_serialize.h"
#include <array>

// Compare the optimized decoder with a deliberately elementary reference. Test
// every value byte, mixed runs, non-power-of-two lengths and guard boundaries.
template<class T> inline bool zc255_test_runs_values(const std::vector<T>& values) {
    SaveStateBlob encoded;
    StateWriter{encoded}.runs(values.data(),values.size());
    SaveStateBlob expected;
    expected.put(uint32_t(values.size()));
    for(size_t first=0;first<values.size();) {
        size_t run=1;
        while(first+run<values.size() && !memcmp(&values[first],&values[first+run],sizeof(T))) ++run;
        expected.put(uint32_t(run)); expected.put(&values[first],sizeof(T)); first+=run;
    }
    if(encoded.data!=expected.data) return false;
    std::vector<T> actual(values.size()+2);
    memset(actual.data(),0xD3,actual.size()*sizeof(T));
    StateReader{encoded}.runs(actual.data()+1,values.size());
    if(encoded.failed || encoded.pos!=encoded.data.size()
        || (!values.empty() && memcmp(actual.data()+1,values.data(),values.size()*sizeof(T)))) return false;
    const auto* begin=reinterpret_cast<const uint8_t*>(actual.data());
    const auto* end=reinterpret_cast<const uint8_t*>(&actual.back());
    for(size_t i=0;i<sizeof(T);++i) if(begin[i]!=0xD3 || end[i]!=0xD3) return false;
    return true;
}
inline bool zc255_runs_selftest() {
    for(size_t count:std::array<size_t,8>{0,1,2,3,17,64,513,4096}) {
        std::vector<uint32_t> values(count,0);
        if(!zc255_test_runs_values(values)) return false;
        std::fill(values.begin(),values.end(),0xA5A5A5A5u);
        if(!zc255_test_runs_values(values)) return false;
        std::fill(values.begin(),values.end(),0x12345678u);
        if(!zc255_test_runs_values(values)) return false;
        for(size_t i=0;i<count;++i) values[i]=uint32_t(i/7);
        if(!zc255_test_runs_values(values)) return false;
        std::vector<std::array<uint8_t,7>> records(count);
        for(auto& record:records) for(size_t i=0;i<record.size();++i) record[i]=uint8_t(i*29);
        if(!zc255_test_runs_values(records)) return false;
    }
    for(unsigned kind=0;kind<4;++kind) {
        SaveStateBlob malformed;
        malformed.put(uint32_t(kind==0 ? 3:4));
        malformed.put(uint32_t(kind==1 ? 0:kind==2 ? 5:4));
        if(kind!=3) malformed.put(uint32_t(0));
        uint32_t guarded[6]; memset(guarded,0xD3,sizeof(guarded));
        StateReader{malformed}.runs(guarded+1,4);
        if(!malformed.failed || guarded[0]!=0xD3D3D3D3u || guarded[5]!=0xD3D3D3D3u) return false;
    }
    return true;
}
