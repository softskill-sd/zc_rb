#pragma once
#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

// Frame output only: preserve the existing indexed-palette conversion exactly.
inline void zc255_convert_frame(BITMAP* image,const uint32_t* palette,uint32_t* dest) {
    const unsigned width=image ? std::min(ZC255_WIDTH,unsigned(std::max(0,image->w))) : 0;
    const unsigned height=image ? std::min(ZC255_HEIGHT,unsigned(std::max(0,image->h))) : 0;
    if(width!=ZC255_WIDTH || height!=ZC255_HEIGHT)
        std::memset(dest,0,ZC255_WIDTH*ZC255_HEIGHT*sizeof(uint32_t));
    if(!image) return;
    if(bitmap_color_depth(image)==8 && is_memory_bitmap(image)) {
        for(unsigned y=0;y<height;++y) {
            const auto* row=image->line[y]; auto* out=dest+y*ZC255_WIDTH;
            for(unsigned x=0;x<width;++x) out[x]=palette[row[x]];
        }
    } else {
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
            dest[y*ZC255_WIDTH+x]=palette[getpixel(image,x,y)&255];
    }
}
inline void zc255_convert_frame_reference(BITMAP* image,const uint32_t* palette,uint32_t* dest) {
    std::memset(dest,0,ZC255_WIDTH*ZC255_HEIGHT*sizeof(uint32_t));
    if(image)for(unsigned y=0;y<ZC255_HEIGHT && y<unsigned(image->h);++y)
        for(unsigned x=0;x<ZC255_WIDTH && x<unsigned(image->w);++x)
            dest[y*ZC255_WIDTH+x]=palette[getpixel(image,x,y)&255];
}
inline void zc255_frame_conversion_selftest() {
    static bool done=false; const char* path=std::getenv("ZC255_FRAME_TEST");
    if(done || !path || !*path) return; done=true;
    uint32_t palette[256]; for(unsigned i=0;i<256;++i) palette[i]=i*65793u;
    std::vector<uint32_t> actual(ZC255_WIDTH*ZC255_HEIGHT),expected(actual.size());
    bool passed=true; double before=0,after=0; uint32_t benchmark_check=0;
    for(int depth:{8,32})for(auto shape:{std::pair<int,int>{256,240},{73,61},{300,260}}) {
        BITMAP* image=create_bitmap_ex(depth,shape.first,shape.second);
        if(!image) { passed=false; continue; }
        for(int y=0;y<image->h;++y)for(int x=0;x<image->w;++x)putpixel(image,x,y,(x*37+y*11)&255);
        zc255_convert_frame_reference(image,palette,expected.data());
        zc255_convert_frame(image,palette,actual.data()); passed=passed && actual==expected;
        if(depth==8 && shape.first==256) {
            auto measure=[&](bool optimized) {
                auto start=std::chrono::steady_clock::now();
                for(int i=0;i<200;++i) {
                    if(optimized) zc255_convert_frame(image,palette,actual.data());
                    else zc255_convert_frame_reference(image,palette,actual.data());
                    benchmark_check^=actual[size_t(i)%actual.size()];
                }
                return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;
            };
            before=measure(false); after=measure(true);
        }
        destroy_bitmap(image);
    }
    zc255_convert_frame_reference(nullptr,palette,expected.data());
    zc255_convert_frame(nullptr,palette,actual.data()); passed=passed && actual==expected;
    if(FILE* out=fopen(path,"w")) {
        fprintf(out,"{\"passed\":%s,\"reference_ms\":%.6f,\"optimized_ms\":%.6f,\"benchmark_check\":%u}\n",passed?"true":"false",before,after,benchmark_check); fclose(out);
    }
}
