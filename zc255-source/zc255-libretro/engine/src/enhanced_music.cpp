#include "zc/zelda.h"
#include "zc/savestate_serialize.h"
#include "enhanced_music.h"
#include "pcm_decoder.h"
#include "sound/zcmixer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>
#include <chrono>
#include <deque>
#include <gme/gme.h>

extern ZCMUSIC* zcmusic;
extern ZCMIXER* zcmixer;
namespace enhanced_music {
namespace {
constexpr size_t GME_PCM_BLOCK_LIMIT=64u*1024*1024/(4096*sizeof(short));
size_t gme_pcm_blocks=0;
uint64_t gme_block_age=0;
uint64_t gme_generated_blocks=0,gme_warm_restores=0;
struct GmeBlock { std::array<short,4096> samples{};uint64_t age=0; };
struct GmeOutput {
    std::shared_ptr<const PcmAsset> source;
    std::unique_ptr<Music_Emu,decltype(&gme_delete)> emu{nullptr,&gme_delete};
    std::deque<GmeBlock> blocks;
    uint64_t first=0,frontier=0;
    int subsong=0;
    bool broken=false;
    ~GmeOutput(){gme_pcm_blocks-=blocks.size();}
};
std::vector<std::weak_ptr<GmeOutput>> gme_owners;
struct Track:ZCMUSIC {
    std::shared_ptr<const PcmAsset> pcm;
    std::shared_ptr<GmeOutput> output;
    std::array<short,4096> carry{};
    unsigned carry_offset=2048;
    uint64_t cursor=0,loop_begin=0,loop_end=0; // Finite: Q32; GME: delivered frames.
    int rate_speed=10000,volume=255;
    Track() { memset(static_cast<ZCMUSIC*>(this),0,sizeof(ZCMUSIC));fadevolume=10000; }
};
// Decoder state belongs only to its forward frontier. Past decoded blocks are
// immutable; independent/restored tracks only move their own delivered cursor.
void gme_evict() {
    while(gme_pcm_blocks>=GME_PCM_BLOCK_LIMIT) {
        std::shared_ptr<GmeOutput> oldest;
        for(auto& weak:gme_owners)if(auto owner=weak.lock();owner && !owner->blocks.empty()) {
            if(!oldest || owner->blocks.front().age<oldest->blocks.front().age)oldest=std::move(owner);
        }
        if(!oldest)break;
        oldest->blocks.pop_front();++oldest->first;--gme_pcm_blocks;
    }
}
bool gme_generate(GmeOutput& owner) {
    if(owner.broken)return false;
    try {
        gme_evict();
        owner.blocks.emplace_back(); // Allocate before advancing opaque decoder.
        auto& block=owner.blocks.back();
        if(gme_play(owner.emu.get(),4096,block.samples.data())) {
            owner.blocks.pop_back();owner.broken=true;return false;
        }
        block.age=++gme_block_age;++owner.frontier;++gme_pcm_blocks;++gme_generated_blocks;return true;
    }catch(...){return false;}
}
std::shared_ptr<GmeOutput> gme_new(const Track& track) try {
    auto owner=std::make_shared<GmeOutput>();owner->source=track.pcm;owner->subsong=track.track;
    Music_Emu* value=nullptr;
    auto failure=gme_open_data(track.pcm->encoded.data(),long(track.pcm->encoded.size()),&value,44100);
    owner->emu.reset(value);if(failure || !value)return {};
    gme_set_autoload_playback_limit(value,false);
    if(gme_start_track(value,track.track))return {};
    gme_owners.erase(std::remove_if(gme_owners.begin(),gme_owners.end(),[](auto& weak){return weak.expired();}),gme_owners.end());
    gme_owners.push_back(owner);return owner;
}catch(...){return {};}
std::shared_ptr<GmeOutput> gme_cached(const Track& track,uint64_t cursor) {
    uint64_t block=cursor/2048;
    for(auto& weak:gme_owners)if(auto owner=weak.lock();owner && !owner->broken
        && owner->subsong==track.track && owner->source->path==track.pcm->path
        && owner->source->hash==track.pcm->hash && owner->source->encoded==track.pcm->encoded
        && block>=owner->first && (block<owner->frontier || (block==owner->frontier && cursor%2048==0)))return owner;
    return {};
}
bool gme_reset(Track& track) {
    track.output=gme_cached(track,0);if(!track.output)track.output=gme_new(track);
    track.carry_offset=2048;track.cursor=0;return bool(track.output);
}
bool gme_replay(Track&,uint64_t,std::chrono::steady_clock::time_point);
bool gme_fill(Track& track) {
    uint64_t block=track.cursor/2048;
    if(!track.output || track.output->broken || block<track.output->first || block>track.output->frontier) {
        if(!gme_replay(track,track.cursor,std::chrono::steady_clock::now()+std::chrono::seconds(25)))return false;
        block=track.cursor/2048;
    }
    if(block==track.output->frontier && !gme_generate(*track.output))return false;
    track.carry=track.output->blocks[size_t(block-track.output->first)].samples;
    track.carry_offset=unsigned(track.cursor%2048);return true;
}
bool gme_replay(Track& track,uint64_t cursor,std::chrono::steady_clock::time_point deadline) {
    // Warm restore needs only immutable samples. No opaque replay or state copy.
    track.output=gme_cached(track,cursor);
    if(track.output)++gme_warm_restores;
    if(!track.output) {
        if(std::chrono::steady_clock::now()>=deadline)return false;
        track.output=gme_new(track);if(!track.output)return false;
        uint64_t required=cursor/2048+(cursor%2048!=0);
        while(track.output->frontier<required) {
            if(std::chrono::steady_clock::now()>=deadline || !gme_generate(*track.output))return false;
        }
        if(std::chrono::steady_clock::now()>=deadline)return false;
    }
    track.cursor=cursor;track.carry_offset=2048;
    if(cursor%2048 && !gme_fill(track))return false;
    return true;
}
std::vector<std::unique_ptr<Track>> live;
std::string error;
uint64_t rendered=0;
bool nonzero=false;
Track* lookup(const ZCMUSIC* value) {
    for(auto& track:live) if(track.get()==value) return track.get();return nullptr;
}
uint64_t end(const Track& track) { return track.loop_end?track.loop_end:track.pcm->frames()<<32; }
uint64_t step(const Track& track) {
    constexpr uint64_t denominator=44100ull*10000;
    uint64_t numerator=uint64_t(track.pcm->rate)*unsigned(track.rate_speed);
    return ((numerator/denominator)<<32)+((numerator%denominator)<<32)/denominator;
}
std::unique_ptr<Track> create(std::shared_ptr<const PcmAsset> asset) {
    auto track=std::make_unique<Track>();track->pcm=std::move(asset);
    track->type=track->pcm->type;track->track=track->pcm->track;
    if(track->type==ZCMF_GME && !gme_reset(*track))return {};
    auto name=std::filesystem::u8path(track->pcm->path).filename().u8string();
    std::string bytes(name.begin(),name.end());
    memcpy(track->filename,bytes.data(),std::min(bytes.size(),sizeof(track->filename)-1));return track;
}
uint32_t index(const ZCMUSIC* value) {
    if(!value)return 0;
    for(uint32_t i=0;i<live.size();++i)if(live[i].get()==value)return i+1;
    return UINT32_MAX;
}
bool valid_fade(int frames,int maximum,int delay) {
    return frames>=0 && (!frames || (maximum>0 && frames<=maximum && delay>=0));
}
}
struct Prepared::Impl {
    std::vector<std::unique_ptr<Track>> tracks;
    std::unique_ptr<ZCMIXER,decltype(&free)> mixer{nullptr,&free};
    uint32_t current=0,newtrack=0,oldtrack=0;
};
Prepared::Prepared()=default;Prepared::~Prepared()=default;
Prepared::Prepared(Prepared&&) noexcept=default;
Prepared& Prepared::operator=(Prepared&&) noexcept=default;
const char* last_error(){return error.c_str();}
bool owns(const ZCMUSIC* value){return lookup(value)!=nullptr;}
ZCMUSIC* load(const char* filename) {
    try {
        if(!filename || live.size()>=8)return nullptr;
        auto path=std::filesystem::absolute(std::filesystem::u8path(filename)).lexically_normal().u8string();
        auto pcm=decode(std::string(path.begin(),path.end()),0,nullptr,error);if(!pcm)return nullptr;
        size_t total=pcm->stereo.size()*sizeof(float)+pcm->encoded.size();
        for(auto& current:live)total+=current->pcm->stereo.size()*sizeof(float)+current->pcm->encoded.size();
        if(total>256u*1024*1024){error="Loaded music exceeds aggregate decoded PCM bound";return nullptr;}
        auto track=create(std::move(pcm));if(!track){error="GME initialization failed";return nullptr;}auto* result=track.get();live.push_back(std::move(track));return result;
    }catch(...){error="Music load allocation failed";return nullptr;}
}
bool volume(ZCMUSIC* value,int amount){auto* track=lookup(value);if(!track)return false;track->volume=std::clamp(amount,0,255);return true;}
bool play(ZCMUSIC* value,int amount) {
    auto* track=lookup(value);if(!track)return false;
    volume(value,amount);
    if(track->playing==ZCM_STOPPED){if(track->type==ZCMF_GME && !gme_reset(*track))return false;track->cursor=0;track->position=0;track->playing=ZCM_PLAYING;}
    return true;
}
bool pause(ZCMUSIC* value,int mode) {
    auto* track=lookup(value);if(!track || track->playing==ZCM_STOPPED)return false;
    if(mode==ZCM_TOGGLE) mode=track->playing==ZCM_PAUSED?ZCM_RESUME:ZCM_PAUSE;
    if(mode!=ZCM_PAUSE && mode!=ZCM_RESUME)return false;
    track->playing=mode==ZCM_PAUSE?ZCM_PAUSED:ZCM_PLAYING;return true;
}
bool stop(ZCMUSIC* value){auto* track=lookup(value);if(!track)return false;track->playing=ZCM_STOPPED;track->position=0;track->cursor=0;return true;}
void unload(ZCMUSIC*& value) {
    ZCMUSIC* target=value;
    auto found=std::find_if(live.begin(),live.end(),[&](auto& track){return track.get()==target;});
    if(found==live.end())return;
    if(zcmusic==target)zcmusic=nullptr;
    if(zcmixer){if(zcmixer->newtrack==target)zcmixer->newtrack=nullptr;if(zcmixer->oldtrack==target)zcmixer->oldtrack=nullptr;}
    live.erase(found);value=nullptr;
}
void poll(){for(auto& track:live)if(track->playing==ZCM_PLAYING && track->position<INT32_MAX)++track->position;}
void exit(){zcmusic=nullptr;if(zcmixer){zcmixer->newtrack=nullptr;zcmixer->oldtrack=nullptr;}live.clear();clear_decode_cache();gme_owners.clear();gme_block_age=0;gme_generated_blocks=0;gme_warm_restores=0;rendered=0;nonzero=false;}
int get_position(ZCMUSIC* value){auto* track=lookup(value);if(!track || track->type==ZCMF_GME)return -10000;return int(((track->cursor>>32)*10000+((uint64_t(uint32_t(track->cursor))*10000)>>32))/track->pcm->rate);}
void seek(ZCMUSIC* value,int position) {
    auto* track=lookup(value);if(!track || track->type==ZCMF_GME || position<0)return;
    uint64_t numerator=uint64_t(unsigned(position))*track->pcm->rate;
    uint64_t cursor=((numerator/10000)<<32)+((numerator%10000)<<32)/10000;
    if(cursor>=track->pcm->frames()<<32)return;track->cursor=cursor;
}
void speed(ZCMUSIC* value,int amount){if(auto* track=lookup(value);track && track->type!=ZCMF_GME && amount>0 && amount<=160000)track->rate_speed=amount;}
int length(ZCMUSIC* value){auto* track=lookup(value);if(!track || track->type==ZCMF_GME)return -10000;uint64_t result=track->pcm->frames()*10000/track->pcm->rate;return int(std::min<uint64_t>(INT32_MAX,result));}
void loop(ZCMUSIC* value,double begin,double finish) {
    auto* track=lookup(value);if(!track || track->type==ZCMF_GME || !std::isfinite(begin) || !std::isfinite(finish) || begin<0 || (begin==0 && finish==0))return;
    const double duration=double(track->pcm->frames())/track->pcm->rate;
    if(finish<begin || finish<=0)finish=duration;
    if(finish>duration || begin>=finish)return;
    uint64_t first=uint64_t(begin*track->pcm->rate*4294967296.0),last=uint64_t(finish*track->pcm->rate*4294967296.0);
    last=std::min(last,track->pcm->frames()<<32);
    if(last<=first)return;
    track->loop_begin=first;track->loop_end=last;
    if(track->cursor>=track->loop_end)track->cursor=track->loop_begin+(track->cursor-track->loop_begin)%(track->loop_end-track->loop_begin);
}
int tracks(ZCMUSIC* value){auto* track=lookup(value);return track && track->type==ZCMF_GME && track->pcm->track_count>1?track->pcm->track_count:0;}
int change_track(ZCMUSIC* value,int selected){auto* track=lookup(value);if(!track || track->type!=ZCMF_GME)return -1;if(selected<0 || selected>=track->pcm->track_count)selected=0;int old=track->track;track->track=selected;if(!gme_reset(*track)){track->track=old;return -1;}return selected;}
void render(float* output,unsigned frames) {
    for(auto& track:live) {
        if(track->playing!=ZCM_PLAYING)continue;
        if(track->type==ZCMF_GME) {
            for(unsigned i=0;i<frames;++i) {
                if(track->carry_offset==2048 && !gme_fill(*track)) {
                    error="GME render failed";track->playing=ZCM_STOPPED;track->cursor=0;track->position=0;track->carry_offset=2048;break;
                }
                for(unsigned channel=0;channel<2;++channel) {
                    int sample=std::clamp(int(track->carry[track->carry_offset*2+channel])*2,-32768,32767);
                    output[i*2+channel]+=(sample/32768.0f)*(track->volume/255.0f);nonzero=nonzero || sample!=0;
                }
                ++track->carry_offset;++track->cursor;
            }
            continue;
        }
        const uint64_t finish=end(*track),begin=track->loop_begin,increment=step(*track);
        for(unsigned i=0;i<frames;++i) {
            if(track->cursor>=finish)track->cursor=begin+(track->cursor-begin)%(finish-begin);
            size_t first=size_t(track->cursor>>32),second=first+1;
            if((uint64_t(second)<<32)>=finish)second=size_t(begin>>32);
            float fraction=float(uint32_t(track->cursor))*(1.0f/4294967296.0f),gain=track->volume/255.0f;
            for(unsigned channel=0;channel<2;++channel) {
                float a=track->pcm->stereo[first*2+channel],b=track->pcm->stereo[second*2+channel];
                float value=(a+(b-a)*fraction)*gain;output[i*2+channel]+=value;nonzero=nonzero || a!=0 || b!=0;
            }
            track->cursor+=increment;
        }
    }
    rendered+=frames;
}
bool write(SaveStateBlob& blob) {
    uint32_t current=index(zcmusic),fresh=index(zcmixer?zcmixer->newtrack:nullptr),old=index(zcmixer?zcmixer->oldtrack:nullptr);
    if(current==UINT32_MAX || fresh==UINT32_MAX || old==UINT32_MAX)return false;
    blob.put(uint32_t(2));blob.put(uint32_t(live.size()));
    for(auto& track:live) {
        blob.put_str(track->pcm->path);blob.put(track->pcm->hash);blob.put(track->pcm->rate);blob.put(track->pcm->frames());
        blob.put(track->type);blob.put(track->playing);blob.put(track->position);blob.put(track->track);
        blob.put(track->fadevolume);blob.put(track->fadeoutframes);blob.put(track->filename);
        blob.put(track->cursor);blob.put(track->loop_begin);blob.put(track->loop_end);blob.put(track->rate_speed);blob.put(track->volume);
    }
    blob.put(current);blob.put(uint8_t(zcmixer!=nullptr));blob.put(fresh);blob.put(old);
    if(zcmixer){blob.put(zcmixer->fadeinframes);blob.put(zcmixer->fadeinmaxframes);blob.put(zcmixer->fadeindelay);blob.put(zcmixer->fadeoutframes);blob.put(zcmixer->fadeoutmaxframes);blob.put(zcmixer->fadeoutdelay);}
    return true;
}
bool prepare(SaveStateBlob& blob,Prepared& output) {
    try {
        auto result=std::make_unique<Prepared::Impl>();uint32_t version=0,count=0;blob.get(version);blob.get(count);
        if(blob.failed || version!=2 || count>8)return false;
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(25);
        size_t decoded_bytes=0;
        for(uint32_t i=0;i<count;++i) {
            uint32_t path_size=0;blob.get(path_size);
            if(blob.failed || !path_size || path_size>32768 || path_size>blob.data.size()-blob.pos)return false;
            std::string path(path_size,'\0');blob.get(path.data(),path_size);
            if(path.find('\0')!=std::string::npos)return false;
            std::array<uint8_t,32> hash{};unsigned rate=0;uint64_t frames=0;
            blob.get(hash);blob.get(rate);blob.get(frames);
            auto track=std::make_unique<Track>();
            blob.get(track->type);blob.get(track->playing);blob.get(track->position);blob.get(track->track);
            blob.get(track->fadevolume);blob.get(track->fadeoutframes);blob.get(track->filename);
            blob.get(track->cursor);blob.get(track->loop_begin);blob.get(track->loop_end);blob.get(track->rate_speed);blob.get(track->volume);
            if(blob.failed || track->track<0 || track->playing < -1 || track->playing>1 || track->position<0 || track->fadevolume<0 || track->fadevolume>10000 || track->fadeoutframes<0 || !memchr(track->filename,0,sizeof(track->filename)) || track->rate_speed<=0 || track->rate_speed>160000 || track->volume<0 || track->volume>255)return false;
            bool streaming=track->type==ZCMF_GME;
            if(streaming) {
                if(frames || rate!=44100 || track->loop_begin || track->loop_end || track->rate_speed!=10000 || (track->playing==ZCM_STOPPED && track->cursor))return false;
            } else {
            if(!frames || frames>UINT32_MAX || track->track!=0)return false;
            uint64_t finish=track->loop_end?track->loop_end:frames<<32;
            // Cursor may overshoot a boundary by one speed step until next render.
            if(track->loop_begin>=finish || finish>frames<<32 || (track->loop_begin && !track->loop_end) || track->cursor>finish+(uint64_t(128)<<32))return false;
            }
            auto asset=decode(path,0,&hash,error);
            if(!asset || asset->rate!=rate || asset->frames()!=frames || asset->type!=track->type)return false;
            decoded_bytes+=asset->stereo.size()*sizeof(float)+asset->encoded.size();if(decoded_bytes>256u*1024*1024)return false;
            track->pcm=std::move(asset);
            if(streaming && (track->track>=track->pcm->track_count || !gme_replay(*track,track->cursor,deadline))) {error="GME restore rejected: decoder failure or 25-second cold preflight budget";return false;}
            result->tracks.push_back(std::move(track));
        }
        uint8_t present=0;blob.get(result->current);blob.get(present);blob.get(result->newtrack);blob.get(result->oldtrack);
        if(blob.failed || present>1 || result->current>count || result->newtrack>count || result->oldtrack>count || (!present && (result->newtrack || result->oldtrack)))return false;
        if(present) {
            result->mixer.reset(static_cast<ZCMIXER*>(calloc(1,sizeof(ZCMIXER))));if(!result->mixer)return false;
            auto& mix=*result->mixer;blob.get(mix.fadeinframes);blob.get(mix.fadeinmaxframes);blob.get(mix.fadeindelay);blob.get(mix.fadeoutframes);blob.get(mix.fadeoutmaxframes);blob.get(mix.fadeoutdelay);
            if(blob.failed || !valid_fade(mix.fadeinframes,mix.fadeinmaxframes,mix.fadeindelay) || !(valid_fade(mix.fadeoutframes,mix.fadeoutmaxframes,mix.fadeoutdelay) || (mix.fadeoutframes==1 && mix.fadeoutmaxframes==0 && mix.fadeoutdelay>=0)))return false;
        }
        output.value=std::move(result);return true;
    }catch(...){error="Music snapshot preflight allocation/decode failure";return false;}
}
void apply(Prepared& prepared) noexcept {
    if(!prepared.value)return;auto& data=*prepared.value;
    live.swap(data.tracks);zcmusic=data.current?live[data.current-1].get():nullptr;
    free(zcmixer);zcmixer=data.mixer.release();
    if(zcmixer){zcmixer->newtrack=data.newtrack?live[data.newtrack-1].get():nullptr;zcmixer->oldtrack=data.oldtrack?live[data.oldtrack-1].get():nullptr;}
    prepared.value.reset();
}
Diagnostics diagnostics(){return {unsigned(live.size()),rendered,nonzero,gme_generated_blocks,gme_warm_restores,gme_pcm_blocks*4096*sizeof(short)};}
}
