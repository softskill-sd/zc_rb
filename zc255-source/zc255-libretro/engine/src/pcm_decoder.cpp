#include "pcm_decoder.h"
#include "sound/zcmusic.h"
#include <windows.h>
#include <bcrypt.h>
#include <minimp3_ex.h> // Reuses implementation in bundled Allegro acodec/mp3.c.
#include <dumb.h>
#include <gme/gme.h>
#define STB_VORBIS_HEADER_ONLY
#include "vendor/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <limits>

namespace enhanced_music {
namespace {
constexpr size_t MAX_ENCODED=64u*1024*1024, MAX_FLOATS=256u*1024*1024/sizeof(float);
std::map<std::string,std::weak_ptr<const PcmAsset>> cache;
bool digest(const std::vector<uint8_t>& data,std::array<uint8_t,32>& out) {
    BCRYPT_ALG_HANDLE alg=nullptr;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    ULONG length=0,received=0;BCRYPT_HASH_HANDLE hash=nullptr;bool okay=false;
    if(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&received,0)>=0 && received==sizeof(length) && length && length<1048576) {
        // Stack owns the provider even if allocation fails; close on every path.
        try {
            std::vector<uint8_t> object(length);
            if(BCryptCreateHash(alg,&hash,object.data(),length,nullptr,0,0)>=0) {
                okay=BCryptHashData(hash,const_cast<PUCHAR>(data.data()),ULONG(data.size()),0)>=0
                    && BCryptFinishHash(hash,out.data(),ULONG(out.size()),0)>=0;
                BCryptDestroyHash(hash);hash=nullptr;
            }
        } catch(...) { okay=false; }
    }
    BCryptCloseAlgorithmProvider(alg,0);return okay;
}
bool append(PcmAsset& asset,const int16_t* data,size_t frames,unsigned channels) {
    if(frames>(MAX_FLOATS-asset.stereo.size())/2) return false;
    for(size_t i=0;i<frames;++i) {
        asset.stereo.push_back(data[i*channels]/32768.0f);
        asset.stereo.push_back(data[i*channels+(channels==2?1:0)]/32768.0f);
    }
    return true;
}
bool mp3(const std::vector<uint8_t>& raw,PcmAsset& asset) {
    static_assert(sizeof(mp3d_sample_t)==sizeof(int16_t),"Bundled minimp3 must use int16 PCM ABI");
    mp3dec_ex_t decoder{};
    if(mp3dec_ex_open_buf(&decoder,raw.data(),raw.size(),MP3D_SEEK_TO_SAMPLE)) return false;
    struct Mp3Cleanup { mp3dec_ex_t* p; ~Mp3Cleanup(){mp3dec_ex_close(p);} } cleanup{&decoder};
    bool okay=decoder.info.channels==1 || decoder.info.channels==2;
    unsigned channels=unsigned(decoder.info.channels);
    asset.rate=decoder.info.hz;asset.type=ZCMF_MP3;
    mp3d_sample_t chunk[8192];
    while(okay) {
        size_t count=mp3dec_ex_read(&decoder,chunk,8192);
        if(!count) break;
        okay=decoder.info.channels==int(channels) && decoder.info.hz==int(asset.rate)
            && count%channels==0 && append(asset,chunk,count/channels,channels);
    }
    okay=okay && !decoder.last_error;
    return okay;
}
bool tracker(const std::vector<uint8_t>& raw,PcmAsset& asset) {
    DUMBFILE* file=dumbfile_open_memory(reinterpret_cast<const char*>(raw.data()),raw.size());
    if(!file) return false;
    DUH* duh=dumb_read_any(file,0,0);dumbfile_close(file);
    if(!duh) return false;
    std::unique_ptr<DUH,decltype(&unload_duh)> duh_owner(duh,&unload_duh);
    asset.rate=44100;asset.type=ZCMF_DUH;
    auto duration=duh_get_length(duh);
    if(duration<=0 || uint64_t(duration)*44100/65536>MAX_FLOATS/2) return false;
    DUH_SIGRENDERER* renderer=duh_start_sigrenderer(duh,0,2,0);
    if(!renderer)return false;
    std::unique_ptr<DUH_SIGRENDERER,decltype(&duh_end_sigrenderer)> renderer_owner(renderer,&duh_end_sigrenderer);
    if(auto* it=duh_get_it_sigrenderer(renderer)) {
        dumb_it_set_loop_callback(it,dumb_it_callback_terminate,nullptr);
        dumb_it_set_xm_speed_zero_callback(it,dumb_it_callback_terminate,nullptr);
    }
    sample_t** temporary=nullptr;long temporary_size=0;int16_t chunk[4096*2];bool okay=true;
    struct SampleCleanup { sample_t*** p; ~SampleCleanup(){if(*p)destroy_sample_buffer(*p);} } samples_owner{&temporary};
    for(;;) {
        long count=duh_render_int(renderer,&temporary,&temporary_size,16,0,1.0f,65536.0f/44100,4096,chunk);
        if(count<0) { okay=false; break; }
        if(!count) break;
        if(!append(asset,chunk,size_t(count),2)) { okay=false;break; }
    }
    return okay;
}
bool stb_ogg(const std::vector<uint8_t>& raw,PcmAsset& asset) {
    int code=0;stb_vorbis* decoder=stb_vorbis_open_memory(raw.data(),int(raw.size()),&code,nullptr);
    if(!decoder)return false;
    std::unique_ptr<stb_vorbis,decltype(&stb_vorbis_close)> owner(decoder,&stb_vorbis_close);
    auto info=stb_vorbis_get_info(decoder);
    bool okay=(info.channels==1 || info.channels==2) && info.sample_rate>=8000 && info.sample_rate<=192000;
    asset.rate=info.sample_rate;asset.type=ZCMF_OGG;
    float chunk[4096*2];
    while(okay) {
        // Return value is frames per channel; num_floats is total output capacity.
        // Preserve native channels: stb's float API zero-fills extra channels,
        // so mono must be duplicated explicitly into our canonical stereo PCM.
        int frames=stb_vorbis_get_samples_float_interleaved(decoder,info.channels,chunk,4096*info.channels);
        if(frames<=0)break;
        size_t count=size_t(frames)*2;
        if(count>MAX_FLOATS-asset.stereo.size()){okay=false;break;}
        for(size_t i=0;i<size_t(frames)*info.channels;++i)if(!std::isfinite(chunk[i])){okay=false;break;}
        if(!okay)break;
        if(info.channels==2) asset.stereo.insert(asset.stereo.end(),chunk,chunk+count);
        else for(int i=0;i<frames;++i) {
            asset.stereo.push_back(chunk[i]);asset.stereo.push_back(chunk[i]);
        }
    }
    okay=okay && stb_vorbis_get_error(decoder)==VORBIS__no_error;
    return okay;
}
bool wav(const std::vector<uint8_t>& raw,PcmAsset& asset) {
    auto u16=[&](size_t i){return unsigned(raw[i])|(unsigned(raw[i+1])<<8);};
    auto u32=[&](size_t i){return uint32_t(u16(i))|(uint32_t(u16(i+2))<<16);};
    if(raw.size()<12 || memcmp(raw.data(),"RIFF",4) || memcmp(raw.data()+8,"WAVE",4))return false;
    uint64_t limit=uint64_t(u32(4))+8;if(limit>raw.size() || limit<12)return false;
    unsigned format=0,channels=0,bits=0,alignment=0;size_t data=0,length=0;
    bool have_format=false,have_data=false;
    for(size_t pos=12;pos+8<=limit;) {
        uint32_t size=u32(pos+4);size_t body=pos+8;
        if(size>limit-body)return false;
        if(!memcmp(raw.data()+pos,"fmt ",4)) {
            if(have_format || size<16)return false;
            format=u16(body);channels=u16(body+2);asset.rate=u32(body+4);alignment=u16(body+12);bits=u16(body+14);
            if(format==0xfffe) {
                const uint8_t guid_tail[]={0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if(size<40 || u16(body+16)<22 || u16(body+18)!=bits || memcmp(raw.data()+body+28,guid_tail,12))return false;
                format=u32(body+24);
            }
            have_format=true;
        } else if(!memcmp(raw.data()+pos,"data",4)) {
            if(have_data)return false;data=body;length=size;have_data=true;
        }
        pos=body+size+(size&1);if(pos>limit)return false;
    }
    if(!have_format || !have_data || !length || (channels!=1 && channels!=2) || (format!=1 && format!=3)
       || (format==1 && bits!=8 && bits!=16 && bits!=24 && bits!=32) || (format==3 && bits!=32)
       || alignment!=channels*(bits/8) || length%alignment || length/alignment>MAX_FLOATS/2)return false;
    asset.type=0x10;asset.stereo.reserve((length/alignment)*2);
    for(size_t i=data;i<data+length;i+=alignment) {
        float frame[2]{};
        for(unsigned ch=0;ch<channels;++ch) {
            size_t at=i+ch*(bits/8);float value=0;
            if(format==3){uint32_t v=u32(at);memcpy(&value,&v,4);if(!std::isfinite(value))return false;}
            else if(bits==8)value=(int(raw[at])-128)/128.0f;
            else if(bits==16)value=int16_t(u16(at))/32768.0f;
            else if(bits==24){int32_t v=int32_t(u16(at)|(unsigned(raw[at+2])<<16));if(v&0x800000)v-=0x1000000;value=v/8388608.0f;}
            else value=int32_t(u32(at))/2147483648.0f;
            frame[ch]=value;
        }
        asset.stereo.push_back(frame[0]);asset.stereo.push_back(channels==1?frame[0]:frame[1]);
    }
    return true;
}
}
std::shared_ptr<const PcmAsset> decode(const std::string& path,int track,
    const std::array<uint8_t,32>* expected,std::string& error) {
    try {
        if(track!=0) { error="Only first track is implemented in finite PCM backend"; return {}; }
        auto file=std::filesystem::u8path(path);std::error_code ec;
        if(!file.is_absolute() || !std::filesystem::is_regular_file(file,ec) || ec) { error="Music source is missing or not absolute";return {}; }
        auto size=std::filesystem::file_size(file,ec);
        if(ec || !size || size>MAX_ENCODED) { error="Music source exceeds encoded-size bound";return {}; }
        std::vector<uint8_t> raw(size);std::ifstream in(file,std::ios::binary);
        if(!in.read(reinterpret_cast<char*>(raw.data()),std::streamsize(size))) { error="Music source could not be read exactly";return {}; }
        std::array<uint8_t,32> hash{};
        if(!digest(raw,hash) || (expected && hash!=*expected)) { error="Music source hash changed or hashing failed";return {}; }
        auto found=cache.find(path);
        if(found!=cache.end()) if(auto prior=found->second.lock();prior && prior->hash==hash) return prior;
        auto asset=std::make_shared<PcmAsset>();asset->path=path;asset->hash=hash;
        auto extension=file.extension().string();for(char& value:extension)value=char(std::tolower(static_cast<unsigned char>(value)));
        bool okay=false;
        if(extension==".mp3") okay=mp3(raw,*asset);
        else if(extension==".it" || extension==".xm" || extension==".s3m" || extension==".mod") okay=tracker(raw,*asset);
        else if(extension==".ogg") okay=stb_ogg(raw,*asset);
        else if(extension==".wav") okay=wav(raw,*asset);
        else if(extension==".spc" || extension==".nsf" || extension==".nsfe" || extension==".gbs" || extension==".gym" || extension==".vgm" || extension==".vgz") {
            Music_Emu* emu=nullptr;
            if(!gme_open_data(raw.data(),long(raw.size()),&emu,44100) && emu) {
                std::unique_ptr<Music_Emu,decltype(&gme_delete)> owner(emu,&gme_delete);
                asset->track_count=gme_track_count(emu);
                if(asset->track_count>0) {
                    asset->encoded=std::move(raw);asset->rate=44100;asset->type=ZCMF_GME;okay=true;
                }
            } else if(emu)gme_delete(emu);
        }
        if(!okay || (asset->type!=ZCMF_GME && !asset->frames()) || asset->rate<8000 || asset->rate>192000) {
            error="Unsupported/invalid decoder, unbounded duration, or decoded-size limit";return {};
        }
        cache[path]=asset;return asset;
    } catch(...) { error="Music decode allocation/I/O failure";return {}; }
}
void clear_decode_cache() { cache.clear(); }
}

