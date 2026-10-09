#include "enhanced_music.h"
#include "music_playback.h"
#include <cstddef>
#if defined(ALLEGRO_LEGACY_STATICLINK)
#define ZC255_LEGACY_AUDIO_API
#else
#define ZC255_LEGACY_AUDIO_API __declspec(dllimport)
#endif
#include "engine_host.h"
#include <allegro.h>
extern "C" {
#include <a5alleg.h>
}
#include "base/zc_alleg.h"
#include "allegro5/allegro_audio.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <array>
#include <deque>
#include <vector>
#include <cmath>
#include <string>
#include "zc/zelda.h"
#include "zc/zc_sys.h"
#include "zc/savestate_serialize.h"
#include "bridge_protocol.h"
#define TSF_IMPLEMENTATION
#include "tsf.h"

ZC255Shared* zc255_audio_shared;
static CRITICAL_SECTION audio_lock;
static tsf* synth;
static CRITICAL_SECTION queue_lock;
// Fixed capacity matches the existing 100 ms queue bound. Logical order is the
// wire representation; ring indices are implementation details, never state.
class SampleQueue {
    std::array<int16_t,8820> values{};
    size_t head=0,count=0;
public:
    size_t size() const { return count; }
    void clear() { head=count=0; }
    void push_back(int16_t v) {
        if(count==values.size()) { head=(head+1)%values.size(); --count; }
        values[(head+count)%values.size()]=v; ++count;
    }
    size_t read(int16_t* target,size_t wanted) {
        const size_t n=std::min(wanted,count),first=std::min(n,values.size()-head);
        if(first) memcpy(target,values.data()+head,first*sizeof(int16_t));
        if(n>first) memcpy(target+first,values.data(),(n-first)*sizeof(int16_t));
        head=(head+n)%values.size(); count-=n; return n;
    }
    void write(SaveStateBlob& b) const {
        const size_t first=std::min(count,values.size()-head);
        if(first) b.put(values.data()+head,first*sizeof(int16_t));
        if(count>first) b.put(values.data(),(count-first)*sizeof(int16_t));
    }
    template<class I> void assign(I first,I last) { clear(); for(;first!=last;++first) push_back(*first); }
};
static SampleQueue samples;
static enhanced_music::Prepared music_preflight;
static std::vector<uint8_t> music_preflight_bytes;
static unsigned midi_status, midi_count, midi_bytes[2];
static int bridge_midi_volume = 255;
static uint64_t soundfont_identity;
static void audio_selftest();
static void enhanced_music_selftest();
static bool frame_audio;
static int16_t frame_sfx_carry[16*2];
static unsigned frame_sfx_position=16;
static bool render_frame_audio();
// Diagnostic counters describe transport timing, never gameplay or audio state.
// Only the read thread writes the report; the real-time mixer never touches disk.
static std::string audio_profile_path;
static LARGE_INTEGER audio_profile_frequency;
static volatile LONG64 audio_profile_lock_skips;
static volatile LONG64 audio_profile_owner_skips[7];
static volatile LONG audio_profile_owner;
static thread_local unsigned audio_profile_lock_depth;
static struct AudioProfile {
    uint64_t reads=0, produced=0, consumed=0, missing=0, dropped=0;
    size_t queue_min=SIZE_MAX,queue_max=0;
    int64_t last_mix=0,last_read=0,mix_gap=0,read_gap=0,mix_duration=0,read_lock_wait=0;
} audio_profile;
static int64_t audio_ticks() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static void audio_profile_report() {
    if(audio_profile_path.empty()) return;
    EnterCriticalSection(&queue_lock);
    if(audio_profile.reads<60) { LeaveCriticalSection(&queue_lock); return; }
    AudioProfile row=audio_profile;
    LONG64 skipped=InterlockedExchange64(&audio_profile_lock_skips,0);
    LONG64 owner_skips[7];
    for(unsigned i=0;i<7;++i) owner_skips[i]=InterlockedExchange64(&audio_profile_owner_skips[i],0);
    audio_profile=AudioProfile{};
    audio_profile.last_mix=row.last_mix; audio_profile.last_read=row.last_read;
    LeaveCriticalSection(&queue_lock);
    const double ms=1000.0/double(audio_profile_frequency.QuadPart);
    FILE* f=fopen(audio_profile_path.c_str(),"a");
    if(f) {
        fprintf(f,"{\"reads\":%llu,\"produced_frames\":%llu,\"consumed_frames\":%llu,\"missing_frames\":%llu,\"dropped_frames\":%llu,\"mixer_lock_skips\":%lld,\"queue_min_frames\":%zu,\"queue_max_frames\":%zu,\"mix_gap_max_ms\":%.6f,\"read_gap_max_ms\":%.6f,\"mix_duration_max_ms\":%.6f,\"read_lock_wait_max_ms\":%.6f,\"lock_skip_unknown\":%lld,\"lock_skip_general\":%lld,\"lock_skip_midi\":%lld,\"lock_skip_sfx\":%lld,\"lock_skip_snapshot\":%lld,\"lock_skip_raw_midi\":%lld,\"lock_skip_mixer\":%lld}\n",
            (unsigned long long)row.reads,(unsigned long long)row.produced,(unsigned long long)row.consumed,
            (unsigned long long)row.missing,(unsigned long long)row.dropped,(long long)skipped,
            row.queue_min==SIZE_MAX?0:row.queue_min,row.queue_max,row.mix_gap*ms,row.read_gap*ms,row.mix_duration*ms,row.read_lock_wait*ms,
            (long long)owner_skips[0],(long long)owner_skips[1],(long long)owner_skips[2],(long long)owner_skips[3],
            (long long)owner_skips[4],(long long)owner_skips[5],(long long)owner_skips[6]);
        fclose(f);
    }
}
extern int32_t sfx_voice[WAV_COUNT];
extern "C" ZC255_LEGACY_AUDIO_API void zc255_audio_set_frame_mode(int);
extern "C" ZC255_LEGACY_AUDIO_API unsigned zc255_midi_frames_until_tick();
extern "C" ZC255_LEGACY_AUDIO_API unsigned zc255_sfx_frames_until_tick();
extern "C" ZC255_LEGACY_AUDIO_API void zc255_midi_advance_samples(unsigned);
extern "C" ZC255_LEGACY_AUDIO_API void zc255_sfx_advance_samples(unsigned);
extern "C" ZC255_LEGACY_AUDIO_API int zc255_sfx_render_frames(int16_t*,unsigned);
extern "C" ZC255_LEGACY_AUDIO_API int zc255_audio_frame_volume();

extern "C" ZC255_LEGACY_AUDIO_API size_t zc255_midi_snapshot_size();
extern "C" ZC255_LEGACY_AUDIO_API int zc255_midi_snapshot_write(void*,size_t);
extern "C" ZC255_LEGACY_AUDIO_API int zc255_midi_snapshot_read(const void*,size_t,int);
extern "C" ZC255_LEGACY_AUDIO_API void zc255_midi_set_hooks(int(*)(),void(*)(),uint64_t(*)(MIDI*),MIDI*(*)(uint64_t));
extern "C" ZC255_LEGACY_AUDIO_API void zc255_sfx_set_hooks(int(*)(),void(*)(),int(*)(const SAMPLE*),const SAMPLE*(*)(int));
extern "C" ZC255_LEGACY_AUDIO_API size_t zc255_sound_snapshot_size();
extern "C" ZC255_LEGACY_AUDIO_API int zc255_sound_snapshot_write(void*,size_t);
extern "C" ZC255_LEGACY_AUDIO_API int zc255_sound_snapshot_read(const void*,size_t,int);
extern "C" ZC255_LEGACY_AUDIO_API size_t zc255_mixer_snapshot_size();
extern "C" ZC255_LEGACY_AUDIO_API int zc255_mixer_snapshot_write(void*,size_t);
extern "C" ZC255_LEGACY_AUDIO_API int zc255_mixer_snapshot_read(const void*,size_t,int);
extern "C" ZC255_LEGACY_AUDIO_API size_t zc255_stream_snapshot_size();
extern "C" ZC255_LEGACY_AUDIO_API int zc255_stream_snapshot_write(void*,size_t);
extern "C" ZC255_LEGACY_AUDIO_API int zc255_stream_snapshot_read(const void*,size_t,int);
static void audio_locked(unsigned owner) {
    if(!audio_profile_path.empty() && !audio_profile_lock_depth++) InterlockedExchange(&audio_profile_owner,LONG(owner));
}
static void audio_enter(unsigned owner) { EnterCriticalSection(&audio_lock); audio_locked(owner); }
static int audio_try_enter(unsigned owner) {
    if(!TryEnterCriticalSection(&audio_lock)) return 0;
    audio_locked(owner); return 1;
}
extern "C" void zc255_audio_lock() { audio_enter(1); }
extern "C" int zc255_audio_try_lock() { return audio_try_enter(1); }
extern "C" void zc255_audio_unlock() {
    if(!audio_profile_path.empty() && !--audio_profile_lock_depth) InterlockedExchange(&audio_profile_owner,0);
    LeaveCriticalSection(&audio_lock);
}
static int midi_try_lock() { return audio_try_enter(2); }
static int sfx_try_lock() { return audio_try_enter(3); }
extern "C" uint64_t zc255_midi_identity(MIDI* track) {
    if(!track) return 0;
    uint64_t h=1469598103934665603ull;
    auto bytes=[&](const void* p,size_t n) { auto v=static_cast<const uint8_t*>(p); for(size_t i=0;i<n;++i) { h^=v[i]; h*=1099511628211ull; } };
    bytes(&track->divisions,sizeof(track->divisions));
    for(auto& t:track->track) { bytes(&t.len,sizeof(t.len)); if(t.len>0 && t.data) bytes(t.data,t.len); }
    return h ? h : 1;
}
extern "C" MIDI* zc255_midi_resolve(uint64_t identity) {
    for(auto& tune:tunes)
        if(tune.data && tune.format==MFORMAT_MIDI && zc255_midi_identity(static_cast<MIDI*>(tune.data))==identity)
            return static_cast<MIDI*>(tune.data);
    if(!mididata) return nullptr;
    for(size_t i=0;i<4096 && mididata[i].type!=DAT_END;++i)
        if(mididata[i].type==DAT_MIDI && zc255_midi_identity(static_cast<MIDI*>(mididata[i].dat))==identity)
            return static_cast<MIDI*>(mididata[i].dat);
    return nullptr;
}
struct AudioGuard { explicit AudioGuard(unsigned owner=1) { audio_enter(owner); } ~AudioGuard() { zc255_audio_unlock(); } };
static int sample_identity(const SAMPLE* sample) {
    for(int i=0;i<WAV_COUNT;++i) if(sfx_get_sample(i)==sample) return i+1;
    return -1;
}
static const SAMPLE* sample_resolve(int identity) {
    return identity>0 && identity<=WAV_COUNT ? sfx_get_sample(identity-1) : nullptr;
}

static int detect(int input) { return input ? 0 : 1; }
static int init(int input, int) {
    if (input) return -1;
    synth = tsf_load_filename(zc255_host::asset_path("default.sf2").c_str());
    fprintf(stderr,"bridge MIDI: SoundFont %s\n",synth ? "loaded" : "missing");
    if (!synth) return -1;
    FILE* font=fopen(zc255_host::asset_path("default.sf2").c_str(),"rb");
    soundfont_identity=1469598103934665603ull;
    if(font) { int c; while((c=fgetc(font))!=EOF) { soundfont_identity^=uint8_t(c); soundfont_identity*=1099511628211ull; } fclose(font); }
    tsf_set_output(synth, TSF_STEREO_INTERLEAVED, 44100, -8.0f);
    for (int ch = 0; ch < 16; ++ch) tsf_channel_set_presetnumber(synth,ch,0,ch == 9);
    return 0;
}
static void finish(int) {}
static int set_volume(int v) { AudioGuard guard; bridge_midi_volume = v; return 0; }
static int get_volume() { AudioGuard guard; return bridge_midi_volume; }
static void midi(int byte) {
    AudioGuard guard(5);
    if (byte & 0x80) {
        if (byte < 0xF0) { midi_status = unsigned(byte); midi_count = 0; }
    } else if (synth && midi_status) {
        midi_bytes[midi_count++] = unsigned(byte);
        unsigned type = midi_status & 0xF0, ch = midi_status & 15;
        unsigned needed = type == 0xC0 || type == 0xD0 ? 1 : 2;
        if (midi_count == needed) {
            const unsigned a = midi_bytes[0], b = midi_bytes[1];
            switch (type) {
            case 0x80: tsf_channel_note_off(synth,ch,a); break;
            case 0x90: if (b) tsf_channel_note_on(synth,ch,a,b / 127.0f * bridge_midi_volume / 255.0f); else tsf_channel_note_off(synth,ch,a); break;
            case 0xB0: tsf_channel_midi_control(synth,ch,a,b); break;
            case 0xC0: tsf_channel_set_presetnumber(synth,ch,a,ch == 9); break;
            case 0xE0: tsf_channel_set_pitchwheel(synth,ch,a | (b << 7)); break;
            }
            midi_count = 0;
        }
    }
}

static void mixed(void* buffer, unsigned frames, void*) {
    float* pcm = static_cast<float*>(buffer);
    const bool profiling=!audio_profile_path.empty();
    const int64_t started=profiling?audio_ticks():0;
    if(!audio_try_enter(6)) {
        if(profiling) {
            InterlockedIncrement64(&audio_profile_lock_skips);
            const LONG owner=InterlockedCompareExchange(&audio_profile_owner,0,0);
            InterlockedIncrement64(&audio_profile_owner_skips[owner>=0 && owner<7?owner:0]);
        }
        std::memset(buffer,0,frames*2*sizeof(float)); return;
    }
    float effect_peak = 0;
    for (unsigned i = 0; i < frames*2; ++i) effect_peak = std::max(effect_peak,std::abs(pcm[i]));
    if (synth) tsf_render_float(synth,pcm,frames,1);
    if (zc255_audio_shared) {
        zc255_audio_shared->sfx_peak = std::max(zc255_audio_shared->sfx_peak,int(effect_peak*32767));
        float total_peak = 0;
        for (unsigned i = 0; i < frames*2; ++i) total_peak = std::max(total_peak,std::abs(pcm[i]));
        zc255_audio_shared->music_peak = std::max(zc255_audio_shared->music_peak,int(total_peak*32767));
    }
    EnterCriticalSection(&queue_lock);
    if(profiling) {
        if(audio_profile.last_mix) audio_profile.mix_gap=std::max(audio_profile.mix_gap,started-audio_profile.last_mix);
        audio_profile.last_mix=started;
        audio_profile.produced+=frames;
    }
    if(profiling) {
        const size_t incoming=samples.size()+frames*2;
        audio_profile.queue_max=std::max(audio_profile.queue_max,incoming/2);
        if(incoming>8820) audio_profile.dropped+=(incoming-8820)/2;
    }
    for (unsigned i = 0; i < frames * 2; ++i)
        samples.push_back(int16_t(std::clamp(pcm[i],-1.0f,1.0f) * 32767));
    // SampleQueue retains the same 100 ms cap during frontend stalls.
    if(profiling) audio_profile.mix_duration=std::max(audio_profile.mix_duration,audio_ticks()-started);
    LeaveCriticalSection(&queue_lock);
    zc255_audio_unlock();
    // Only the frontend plays the captured mix.
    std::memset(buffer,0,frames * 2 * sizeof(float));
}

// Generate exactly one frontend frame. Hardware callbacks and timer workers
// are disabled in this mode. Event boundaries retain fractional timer ticks.
static bool render_frame_audio() {
    AudioGuard guard;
    float pcm[735*2];
    unsigned offset=0;
    int effect_peak=0,total_peak=0;
    while(offset<735) {
        const unsigned frames=std::min({64u,735-offset,zc255_midi_frames_until_tick(),zc255_sfx_frames_until_tick()});
        if(!frames) return false;
        const float gain=zc255_audio_frame_volume()/255.0f;
        float* target=pcm+offset*2;
        // Fixed SFX blocks preserve the legacy mixer's per-block volume cache.
        // The tiny carry is timeline state and is fully serialized below.
        for(unsigned i=0;i<frames;++i) {
            if(frame_sfx_position==16) {
                if(!zc255_sfx_render_frames(frame_sfx_carry,16)) return false;
                frame_sfx_position=0;
            }
            for(unsigned ch=0;ch<2;++ch) {
                target[i*2+ch]=frame_sfx_carry[frame_sfx_position*2+ch]/32768.0f*gain;
                effect_peak=std::max(effect_peak,int(std::abs(target[i*2+ch])*32767));
            }
            ++frame_sfx_position;
        }
        if(synth) tsf_render_float(synth,target,frames,1);
        enhanced_music::render(target,frames);
        offset+=frames;
        zc255_midi_advance_samples(frames);
        zc255_sfx_advance_samples(frames);
    }
    for(float value:pcm) total_peak=std::max(total_peak,int(std::abs(value)*32767));
    if(zc255_audio_shared) {
        zc255_audio_shared->sfx_peak=std::max(zc255_audio_shared->sfx_peak,effect_peak);
        zc255_audio_shared->music_peak=std::max(zc255_audio_shared->music_peak,total_peak);
    }
    EnterCriticalSection(&queue_lock);
    if(!audio_profile_path.empty()) {
        audio_profile.produced+=735;
        audio_profile.queue_max=std::max(audio_profile.queue_max,(samples.size()+1470)/2);
        if(samples.size()+1470>8820) audio_profile.dropped+=(samples.size()+1470-8820)/2;
    }
    for(float value:pcm) samples.push_back(int16_t(std::clamp(value,-1.0f,1.0f)*32767));
    LeaveCriticalSection(&queue_lock);
    return true;
}

static bool host_audio_prepared=false;
static MIDI_DRIVER* host_midi_driver=nullptr;
static MIDI_DRIVER host_midi_original{};
void zc255_audio_prepare() {
    if(host_audio_prepared) return;
    InitializeCriticalSection(&audio_lock);
    InitializeCriticalSection(&queue_lock);
    host_audio_prepared=true;
    frame_audio=zc255_host::frame_audio_requested();
    zc255_audio_set_frame_mode(frame_audio?1:0);
    const char* profile=getenv("ZC255_PROFILE_AUDIO");
    if(profile && *profile) {
        audio_profile_path=profile;
        QueryPerformanceFrequency(&audio_profile_frequency);
    }
    zc255_midi_set_hooks(midi_try_lock,zc255_audio_unlock,zc255_midi_identity,zc255_midi_resolve);
    zc255_sfx_set_hooks(sfx_try_lock,zc255_audio_unlock,sample_identity,sample_resolve);
    for (auto entry = _midi_driver_list; entry->id; ++entry) {
        if (entry->id != MIDI_ALLEGRO_5) continue;
        auto driver = static_cast<MIDI_DRIVER*>(entry->driver);
        host_midi_driver=driver; host_midi_original=*driver;
        driver->detect = detect;
        driver->init = init;
        driver->exit = finish;
        driver->set_mixer_volume = set_volume;
        driver->get_mixer_volume = get_volume;
        driver->raw_midi = midi;
    }
}
bool zc255_audio_has_frame_clock() { return frame_audio; }
void zc255_audio_start() {
    if(zc255_host::frame_audio_requested() && !host_audio_prepared)
        zc255_host::fatal("Software audio was requested but engine sound initialization was skipped");
    if(frame_audio) {
        const bool installed=al_is_audio_installed();
        const bool has_default_mixer=al_get_default_mixer()!=nullptr;
        const bool ready=host_audio_prepared && synth && host_midi_driver
            && midi_driver==host_midi_driver && midi_driver->id==MIDI_ALLEGRO_5
            && digi_driver && digi_driver->id==DIGI_ALLEGRO_5;
        fprintf(stderr,"bridge audio readiness: ready=%d digi=%d midi=%d synth=%d physical=%d mixer=%d error=%s\n",
            ready?1:0,digi_driver?digi_driver->id:0,midi_driver?midi_driver->id:0,
            synth?1:0,installed?1:0,has_default_mixer?1:0,allegro_error);
        if(!ready || installed || has_default_mixer)
            zc255_host::fatal("Software audio startup failed its driver/device readiness check");
        fprintf(stderr,"bridge audio: deterministic software mode, physical driver=%s mixer=%s\n",
            installed?"present":"absent",has_default_mixer?"present":"absent");
        const char* path=getenv("ZC255_AUDIO_DEVICE_REPORT");
        if(path && *path) {
            FILE* report=fopen(path,"w");
            if(report) {
                fprintf(report,"{\"frame_mode\":true,\"audio_driver_installed\":%s,\"default_mixer_present\":%s,\"software_only\":%s,\"ready\":true}\n",
                    installed?"true":"false",has_default_mixer?"true":"false",(!installed&&!has_default_mixer)?"true":"false");
                fclose(report);
            }
        }
        return;
    }
    auto mixer = al_get_default_mixer();
    fprintf(stderr,"bridge audio: mixer %p depth=%d rate=%u channels=%d\n",mixer,mixer ? al_get_mixer_depth(mixer) : -1,mixer ? al_get_mixer_frequency(mixer) : 0,mixer ? al_get_mixer_channels(mixer) : -1);
    if (mixer && al_get_mixer_depth(mixer) == ALLEGRO_AUDIO_DEPTH_FLOAT32 &&
        al_get_mixer_frequency(mixer) == 44100 && al_get_mixer_channels(mixer) == ALLEGRO_CHANNEL_CONF_2) {
        al_set_mixer_postprocess_callback(mixer,mixed,nullptr);
    }
}
void zc255_audio_read() {
    if (!zc255_audio_shared) return;
    enhanced_music_selftest();
    audio_selftest();
    if(frame_audio && !render_frame_audio()) {
        fprintf(stderr,"bridge audio: deterministic software render failed\n");
        std::memset(zc255_audio_shared->audio,0,735*2*sizeof(int16_t));
        zc255_audio_shared->audio_frames=735;
        return;
    }
    const bool profiling=!audio_profile_path.empty();
    const int64_t requested=profiling?audio_ticks():0;
    EnterCriticalSection(&queue_lock);
    if(profiling) {
        audio_profile.read_lock_wait=std::max(audio_profile.read_lock_wait,audio_ticks()-requested);
        if(audio_profile.last_read) audio_profile.read_gap=std::max(audio_profile.read_gap,requested-audio_profile.last_read);
        audio_profile.last_read=requested;
        audio_profile.queue_min=std::min(audio_profile.queue_min,samples.size()/2);
    }
    unsigned frames = unsigned(std::min<size_t>(samples.size()/2,735));
    samples.read(zc255_audio_shared->audio,frames*2);
    if(profiling) {
        ++audio_profile.reads; audio_profile.consumed+=frames; audio_profile.missing+=735-frames;

    }
    LeaveCriticalSection(&queue_lock);
    if(profiling) audio_profile_report();
    // A fast frontend may consume frames before the asynchronous mixer has produced them.
    std::memset(zc255_audio_shared->audio + frames*2,0,(735-frames)*2*sizeof(int16_t));
    zc255_audio_shared->audio_frames = 735;
}

namespace {
template<class S> void voice_values(S& s, tsf_voice& v) {
    s(v.playingPreset,v.playingKey,v.playingChannel);
    s(v.pitchInputTimecents,v.pitchOutputFactor,v.sourceSamplePosition);
    s(v.noteGainDB,v.panFactorLeft,v.panFactorRight,v.playIndex,v.loopStart,v.loopEnd);
    s(v.ampenv,v.modenv,v.lowpass,v.modlfo,v.viblfo);
}
struct AudioState {
    enhanced_music::Prepared enhanced;
    unsigned status=0,count=0,bytes[2]={}; int volume=0;
    std::vector<int16_t> pcm;
    unsigned sfx_position=16; int16_t sfx_carry[32]={};
    int32_t sfx_bindings[WAV_COUNT]={};
    std::vector<uint8_t> scheduler;
    std::vector<uint8_t> sound,mixer,stream;
    int voices=0,maxvoices=0,channels=0,activechannel=0;
    unsigned playindex=0; int mode=0; float rate=44100,gain=0;
    std::vector<tsf_voice> voice;
    std::vector<int32_t> regions;
    std::vector<tsf_channel> channel;
};
struct SnapshotAudioGuard {
    AudioGuard guard{4};
    struct QueueGuard {
        QueueGuard() { EnterCriticalSection(&queue_lock); }
        ~QueueGuard() { LeaveCriticalSection(&queue_lock); }
    } queue_guard;
    ALLEGRO_MIXER* mixer=frame_audio?nullptr:al_get_default_mixer();
    bool playing=mixer && al_get_mixer_playing(mixer);
    SnapshotAudioGuard() { if(playing) al_set_mixer_playing(mixer,false); }
    ~SnapshotAudioGuard() { if(playing) al_set_mixer_playing(mixer,true); }
};
}

bool zc255_audio_state_write(SaveStateBlob& b) {
    // Enhanced decoders keep private buffered state that seeking alone cannot
    // restore exactly. Refuse capture here until those decoders gain visitors.
    if(!frame_audio && (zcmusic || (zcmixer && (zcmixer->newtrack || zcmixer->oldtrack)))) return false;
    SnapshotAudioGuard guard;
    b.put(uint32_t(3)); b.put(soundfont_identity); b.put(frame_audio);
    b.put(frame_sfx_position); b.put(frame_sfx_carry);
    b.put(midi_status); b.put(midi_count); b.put(midi_bytes); b.put(bridge_midi_volume);
    b.put(sfx_voice);
    b.put((uint32_t)samples.size()); samples.write(b);
    std::vector<uint8_t> scheduler(zc255_midi_snapshot_size());
    if(!zc255_midi_snapshot_write(scheduler.data(),scheduler.size())) return false;
    b.put((uint32_t)scheduler.size()); b.put(scheduler.data(),scheduler.size());
    std::vector<uint8_t> sound(zc255_sound_snapshot_size()), mixer(zc255_mixer_snapshot_size());
    if(!zc255_sound_snapshot_write(sound.data(),sound.size()) || !zc255_mixer_snapshot_write(mixer.data(),mixer.size())) return false;
    b.put((uint32_t)sound.size()); b.put(sound.data(),sound.size());
    b.put((uint32_t)mixer.size()); b.put(mixer.data(),mixer.size());
    std::vector<uint8_t> stream(zc255_stream_snapshot_size());
    if(!zc255_stream_snapshot_write(stream.data(),stream.size())) return false;
    b.put((uint32_t)stream.size()); b.put(stream.data(),stream.size());
    bool present=synth!=nullptr; b.put(present); if(!present) return enhanced_music::write(b);
    if(synth->voiceNum<0 || synth->voiceNum>4096 || (synth->channels && synth->channels->channelNum>256)) return false;
    b.put(synth->voiceNum); b.put(synth->maxVoiceNum); b.put(synth->voicePlayIndex);
    b.put((int)synth->outputmode); b.put(synth->outSampleRate); b.put(synth->globalGainDB);
    StateWriter s{b};
    for(int i=0;i<synth->voiceNum;++i) {
        auto& v=synth->voices[i]; b.put(v.playingPreset);
        if(v.playingPreset<0) continue;
        if(v.playingPreset>=synth->presetNum || !v.region) return false;
        const auto& preset=synth->presets[v.playingPreset];
        int32_t region=-1;
        for(int r=0;r<preset.regionNum;++r) if(v.region==&preset.regions[r]) { region=r; break; }
        if(region<0) return false;
        b.put(region); voice_values(s,v);
    }
    int channels=synth->channels ? synth->channels->channelNum : 0;
    b.put(channels); b.put(synth->channels ? synth->channels->activeChannel : 0);
    if(channels) b.put(synth->channels->channels,channels*sizeof(tsf_channel));
    return enhanced_music::write(b) && !b.failed;
}

bool zc255_audio_state_read(SaveStateBlob& b, bool apply) {
    uint32_t version=0; uint64_t font=0; b.get(version); b.get(font);
    uint8_t mode=0; b.get(mode);
    if(b.failed || version!=3 || font!=soundfont_identity || mode>1 || bool(mode)!=frame_audio) return false;
    AudioState state;
    b.get(state.sfx_position); b.get(state.sfx_carry);
    if(b.failed || state.sfx_position>16 || (!frame_audio && state.sfx_position!=16)) return false;
    b.get(state.status); b.get(state.count); b.get(state.bytes); b.get(state.volume);
    b.get(state.sfx_bindings);
    for(auto voice:state.sfx_bindings) if(voice < -1 || voice>=256) return false;
    if(state.count>1 || state.status>255 || state.volume<0 || state.volume>255) return false;
    uint32_t n=0; b.get(n);
    if(b.failed || n>8820 || (frame_audio && (n&1)) || n>(b.data.size()-b.pos)/sizeof(int16_t)) return false;
    state.pcm.resize(n); if(n) b.get(state.pcm.data(),n*sizeof(int16_t));
    b.get(n); if(b.failed || n!=zc255_midi_snapshot_size() || n>b.data.size()-b.pos) return false;
    state.scheduler.resize(n); b.get(state.scheduler.data(),n);
    if(!zc255_midi_snapshot_read(state.scheduler.data(),n,0)) return false;
    b.get(n); if(b.failed || n!=zc255_sound_snapshot_size() || n>b.data.size()-b.pos) return false;
    state.sound.resize(n); b.get(state.sound.data(),n);
    if(!zc255_sound_snapshot_read(state.sound.data(),n,0)) return false;
    b.get(n); if(b.failed || n!=zc255_mixer_snapshot_size() || n>b.data.size()-b.pos) return false;
    state.mixer.resize(n); b.get(state.mixer.data(),n);
    if(!zc255_mixer_snapshot_read(state.mixer.data(),n,0)) return false;
    b.get(n); if(b.failed || n!=zc255_stream_snapshot_size() || n>b.data.size()-b.pos) return false;
    state.stream.resize(n); b.get(state.stream.data(),n);
    if(!zc255_stream_snapshot_read(state.stream.data(),n,0)) return false;
    uint8_t present=0; b.get(present); if(present>1 || bool(present)!=(synth!=nullptr)) return false;
    if(present) {
        b.get(state.voices); b.get(state.maxvoices); b.get(state.playindex);
        b.get(state.mode); b.get(state.rate); b.get(state.gain);
        if(state.voices<0 || state.voices>4096 || state.maxvoices<0 || state.maxvoices>4096 || state.mode<0 || state.mode>2
            || !std::isfinite(state.rate) || state.rate<8000 || state.rate>192000 || !std::isfinite(state.gain)) return false;
        state.voice.resize(state.voices); state.regions.resize(state.voices,-1);
        StateReader s{b};
        for(int i=0;i<state.voices;++i) {
            auto& v=state.voice[i]; memset(&v,0,sizeof(v)); b.get(v.playingPreset);
            if(v.playingPreset==-1) continue;
            if(v.playingPreset<0 || v.playingPreset>=synth->presetNum) return false;
            b.get(state.regions[i]); voice_values(s,v);
            if(b.failed || v.playingPreset<0 || v.playingPreset>=synth->presetNum || state.regions[i]<0
                || state.regions[i]>=synth->presets[v.playingPreset].regionNum || v.playingKey<0 || v.playingKey>127
                || !std::isfinite(v.sourceSamplePosition) || !std::isfinite(v.pitchOutputFactor)) return false;
            v.region=&synth->presets[v.playingPreset].regions[state.regions[i]];
        }
        b.get(state.channels); b.get(state.activechannel);
        if(b.failed || state.channels<0 || state.channels>256 || state.activechannel<0 || state.activechannel>=std::max(1,state.channels)
            || size_t(state.channels)*sizeof(tsf_channel)>b.data.size()-b.pos) return false;
        state.channel.resize(state.channels);
        if(state.channels) b.get(state.channel.data(),state.channels*sizeof(tsf_channel));
        for(auto& ch:state.channel) if(ch.presetIndex>=synth->presetNum) return false;
    }
    if(b.failed) return false;
    const size_t music_begin=b.pos;
    if(apply && music_preflight.value && !music_preflight_bytes.empty()
        && music_preflight_bytes.size()<=b.data.size()-b.pos
        && !memcmp(music_preflight_bytes.data(),b.data.data()+b.pos,music_preflight_bytes.size())) {
        b.pos+=music_preflight_bytes.size();
        state.enhanced=std::move(music_preflight);music_preflight_bytes.clear();
    } else {
        music_preflight=enhanced_music::Prepared{};music_preflight_bytes.clear();
        if(!enhanced_music::prepare(b,state.enhanced)) return false;
    }
    if(!apply) {
        music_preflight_bytes.assign(b.data.begin()+music_begin,b.data.begin()+b.pos);
        music_preflight=std::move(state.enhanced);return true;
    }
    // Allocate before touching current synth or scheduler.
    tsf_voice* voices=nullptr; tsf_channel* channels=nullptr;
    if(state.voices) { voices=static_cast<tsf_voice*>(malloc(state.voices*sizeof(tsf_voice))); if(!voices) return false; }
    if(state.channels) { channels=static_cast<tsf_channel*>(malloc(state.channels*sizeof(tsf_channel))); if(!channels) { free(voices); return false; } }
    if(voices) memcpy(voices,state.voice.data(),state.voices*sizeof(tsf_voice));
    if(channels) memcpy(channels,state.channel.data(),state.channels*sizeof(tsf_channel));
    tsf_channels* new_container=nullptr;
    if(synth && !synth->channels) {
        new_container=static_cast<tsf_channels*>(calloc(1,sizeof(tsf_channels)));
        if(!new_container) { free(voices); free(channels); return false; }
        new_container->setupVoice=&tsf_channel_setup_voice;
    }
    SnapshotAudioGuard guard;
    if(!zc255_midi_snapshot_read(state.scheduler.data(),state.scheduler.size(),1)) { free(new_container); free(voices); free(channels); return false; }
    if(!zc255_sound_snapshot_read(state.sound.data(),state.sound.size(),1) || !zc255_mixer_snapshot_read(state.mixer.data(),state.mixer.size(),1)) { free(new_container); free(voices); free(channels); return false; }
    if(!zc255_stream_snapshot_read(state.stream.data(),state.stream.size(),1)) { free(new_container); free(voices); free(channels); return false; }
    midi_status=state.status; midi_count=state.count; memcpy(midi_bytes,state.bytes,sizeof(midi_bytes)); bridge_midi_volume=state.volume;
    memcpy(sfx_voice,state.sfx_bindings,sizeof(sfx_voice));
    samples.assign(state.pcm.begin(),state.pcm.end());
    enhanced_music::apply(state.enhanced);
    frame_sfx_position=state.sfx_position; memcpy(frame_sfx_carry,state.sfx_carry,sizeof(frame_sfx_carry));
    if(synth) {
        if(new_container) synth->channels=new_container;
        free(synth->voices); free(synth->channels->channels);
        synth->voices=voices; synth->voiceNum=state.voices; synth->maxVoiceNum=state.maxvoices;
        synth->voicePlayIndex=state.playindex; synth->outputmode=(TSFOutputMode)state.mode;
        synth->outSampleRate=state.rate; synth->globalGainDB=state.gain;
        synth->channels->channels=channels; synth->channels->channelNum=state.channels; synth->channels->activeChannel=state.activechannel;
    }
    return true;
}

static bool sample_queue_selftest() {
    SampleQueue ring;
    std::deque<int16_t> reference;
    for(unsigned i=0;i<20000;++i) {
        ring.push_back(int16_t(i)); reference.push_back(int16_t(i));
        if(reference.size()>8820) reference.pop_front();
    }
    // Overflow, wraparound, empty reads and odd sample counts must retain the
    // same sequence as the old deque implementation, including wire bytes.
    for(unsigned turn=0;turn<100;++turn) {
        for(unsigned i=0;i<317+(turn%19)*37;++i) {
            const int16_t value=int16_t(turn*997+i);
            ring.push_back(value); reference.push_back(value);
            if(reference.size()>8820) reference.pop_front();
        }
        const size_t requested=(turn%7)*311;
        std::vector<int16_t> actual(requested);
        const size_t read=ring.read(actual.data(),requested);
        if(read!=std::min(requested,reference.size())) return false;
        for(size_t i=0;i<read;++i) {
            if(actual[i]!=reference.front()) return false;
            reference.pop_front();
        }
        SaveStateBlob bytes;
        ring.write(bytes);
        std::vector<int16_t> expected(reference.begin(),reference.end());
        if(bytes.data.size()!=expected.size()*sizeof(int16_t)
            || (!expected.empty() && memcmp(bytes.data.data(),expected.data(),bytes.data.size()))) return false;
        SampleQueue restored;
        restored.assign(expected.begin(),expected.end());
        SaveStateBlob roundtrip;
        restored.write(roundtrip);
        if(roundtrip.data!=bytes.data || ring.size()!=reference.size()) return false;
    }
    ring.clear();
    int16_t value=1;
    return ring.size()==0 && ring.read(&value,1)==0 && value==1;
}

// Opt-in selftest failure diagnostics only. No values are omitted or normalized.
struct AudioDiffSpan { const char* name; size_t offset=0,size=0; };
static bool audio_diff_spans(const SaveStateBlob& blob,std::vector<AudioDiffSpan>& spans) {
    size_t cursor=sizeof(uint32_t)+sizeof(uint64_t)+sizeof(uint8_t)+sizeof(frame_sfx_position)
        +sizeof(frame_sfx_carry)+sizeof(midi_status)+sizeof(midi_count)+sizeof(midi_bytes)
        +sizeof(bridge_midi_volume)+sizeof(sfx_voice);
    if(cursor>blob.data.size()) return false;
    spans.push_back({"prefix",0,cursor});
    auto section=[&](const char* name,size_t unit) {
        if(cursor>blob.data.size() || blob.data.size()-cursor<sizeof(uint32_t)) return false;
        uint32_t count=0; memcpy(&count,blob.data.data()+cursor,sizeof(count)); cursor+=sizeof(count);
        if(count>(blob.data.size()-cursor)/unit) return false;
        spans.push_back({name,cursor,size_t(count)*unit}); cursor+=size_t(count)*unit; return true;
    };
    if(!section("pcm_queue",sizeof(int16_t)) || !section("midi",1) || !section("sound",1)
        || !section("mixer",1) || !section("stream",1)) return false;
    spans.push_back({"synth",cursor,blob.data.size()-cursor}); return true;
}
static void audio_frame_diff_report(const char* path,const SaveStateBlob& first,const SaveStateBlob& second,
    bool rendered,bool restarted,bool rendered_again,bool second_written,int first_voices,int second_voices) {
    std::vector<AudioDiffSpan> a,b;
    const bool parsed=audio_diff_spans(first,a) && audio_diff_spans(second,b) && a.size()==b.size();
    const std::string base(path);
    for(const auto& entry:{std::pair<const char*,const SaveStateBlob*>{".frame-first.bin",&first},
                          std::pair<const char*,const SaveStateBlob*>{".frame-second.bin",&second}}) {
        FILE* output=fopen((base+entry.first).c_str(),"wb");
        if(output) { fwrite(entry.second->data.data(),1,entry.second->data.size(),output); fclose(output); }
    }
    FILE* report=fopen((base+".frame-diff.json").c_str(),"w");
    if(!report) return;
    fprintf(report,"{\"rendered\":%d,\"restarted\":%d,\"rendered_again\":%d,\"second_written\":%d,\"parsed\":%d,\"first_bytes\":%zu,\"second_bytes\":%zu,\"first_voices\":%d,\"second_voices\":%d,\"tsf_layout\":{\"voice\":%zu,\"envelope\":%zu,\"lowpass\":%zu,\"lowpass_active_offset\":%zu,\"envelope_last_bool_offset\":%zu},\"sections\":[",
        rendered,restarted,rendered_again,second_written,parsed,first.data.size(),second.data.size(),first_voices,second_voices,
        sizeof(tsf_voice),sizeof(tsf_voice_envelope),sizeof(tsf_voice_lowpass),offsetof(tsf_voice_lowpass,active),offsetof(tsf_voice_envelope,isAmpEnv));
    if(parsed) for(size_t i=0;i<a.size();++i) {
        size_t differing=0,first_difference=SIZE_MAX;
        const size_t common=std::min(a[i].size,b[i].size);
        for(size_t j=0;j<common;++j) if(first.data[a[i].offset+j]!=second.data[b[i].offset+j]) {
            ++differing; if(first_difference==SIZE_MAX) first_difference=j;
        }
        fprintf(report,"%s{\"name\":\"%s\",\"first_offset\":%zu,\"second_offset\":%zu,\"first_size\":%zu,\"second_size\":%zu,\"differing_bytes\":%zu,\"first_difference\":%lld}",
            i?",":"",a[i].name,a[i].offset,b[i].offset,a[i].size,b[i].size,differing,first_difference==SIZE_MAX?-1LL:(long long)first_difference);
        if(differing || a[i].size!=b[i].size) fprintf(stderr,"audio frame selftest diff: %s first=%lld differing=%zu sizes=%zu/%zu\n",
            a[i].name,first_difference==SIZE_MAX?-1LL:(long long)first_difference,differing,a[i].size,b[i].size);
    }
    fprintf(report,"]}\n"); fclose(report);
}

static void audio_selftest() {
    static bool done=false;
    const char* path=getenv("ZC255_AUDIO_SELFTEST");
    if(done || !path || !*path || !Playing || !synth) return;
    done=true;
    for(const char* suffix:{".frame-first.bin",".frame-second.bin",".frame-diff.json"})
        remove((std::string(path)+suffix).c_str());
    SnapshotAudioGuard guard;
    SaveStateBlob initial,captured;
    bool frame_pcm_exact=!frame_audio;
    bool ring_exact=sample_queue_selftest();
    const bool software_only=frame_audio && !al_is_audio_installed() && al_get_default_mixer()==nullptr;
    bool initial_saved=zc255_audio_state_write(initial);
    tsf_channel_note_on(synth,0,60,1.0f);
    std::vector<float> warmup(128*2);
    tsf_render_float(synth,warmup.data(),128,0);
    bool active_voice=tsf_active_voice_count(synth)>0;
    bool active_effect=!frame_audio;
    if(frame_audio) {
        sfx(WAV_SECRET);
        const int effect_voice=sfx_voice[WAV_SECRET];
        active_effect=effect_voice>=0 && voice_check(effect_voice)!=nullptr;
        if(active_effect) {
            voice_ramp_volume(effect_voice,120,0);
            voice_sweep_pan(effect_voice,120,220);
        }
    }
    bool written=initial_saved && zc255_audio_state_write(captured);
    auto restore_captured=[&]() { SaveStateBlob copy=captured; copy.pos=0; return zc255_audio_state_read(copy,true); };
    std::vector<float> expected(735*2),actual(735*2);
    if(written) {
        tsf_render_float(synth,expected.data(),735,0);
        midi_status=0x90; midi_count=1; midi_bytes[0]=60;
        bridge_midi_volume=17;
        samples.clear(); samples.push_back(1234);
        if(frame_audio) { frame_sfx_position=7; frame_sfx_carry[3]^=0x7777; }
        tsf_channel_note_on(synth,0,61,1);
    }
    bool restored=written && restore_captured();
    SaveStateBlob recaptured;
    bool exact=restored && zc255_audio_state_write(recaptured) && recaptured.data==captured.data;
    if(restored) tsf_render_float(synth,actual.data(),735,0);
    bool pcm_equal=restored && expected==actual;
    if(frame_audio && written && restore_captured()) {
        SaveStateBlob first,second;
        bool rendered=render_frame_audio() && render_frame_audio() && zc255_audio_state_write(first);
        const int first_voices=synth?synth->voiceNum:0;
        bool restarted=restore_captured();
        bool rendered_again=rendered && restarted && render_frame_audio() && render_frame_audio();
        bool second_written=rendered_again && zc255_audio_state_write(second);
        const int second_voices=synth?synth->voiceNum:0;
        frame_pcm_exact=rendered && restarted && rendered_again && second_written && first.data==second.data;
        if(!frame_pcm_exact) audio_frame_diff_report(path,first,second,rendered,restarted,rendered_again,second_written,first_voices,second_voices);
    }
    bool clock_rejected=false;
    if(written && restore_captured()) {
        SaveStateBlob bad_mode=captured,bad_position=captured,unchanged;
        bad_mode.pos=bad_position.pos=0;
        // Format2 prefix: uint32 version, uint64 font, uint8 mode, uint32 carry position.
        bad_mode.data[12]^=1;
        const unsigned impossible_position=17;
        memcpy(bad_position.data.data()+13,&impossible_position,sizeof(impossible_position));
        clock_rejected=!zc255_audio_state_read(bad_mode,true) && !zc255_audio_state_read(bad_position,true)
            && zc255_audio_state_write(unchanged) && unchanged.data==captured.data;
    }
    captured.pos=0;
    bool cleanup=initial_saved && zc255_audio_state_read(initial,true);
    SaveStateBlob malformed=captured;
    malformed.pos=0; if(!malformed.data.empty()) malformed.data.pop_back();
    bool rejected=written && !zc255_audio_state_read(malformed,true);
    FILE* report=fopen(path,"w");
    if(report) {
        fprintf(report,"{\"captured\":%s,\"restored\":%s,\"backend_values_exact\":%s,\"synth_pcm_exact\":%s,\"active_voice_tested\":%s,\"cleanup_restored\":%s,\"malformed_rejected\":%s,\"queue_ring_exact\":%s,\"frame_pcm_exact\":%s,\"frame_mode\":%s,\"clock_fields_rejected_unchanged\":%s,\"active_effect_ramps_tested\":%s,\"software_only\":%s,\"bytes\":%zu}\n",
            written?"true":"false",restored?"true":"false",exact?"true":"false",pcm_equal?"true":"false",active_voice?"true":"false",cleanup?"true":"false",rejected?"true":"false",ring_exact?"true":"false",frame_pcm_exact?"true":"false",frame_audio?"true":"false",clock_rejected?"true":"false",active_effect?"true":"false",software_only?"true":"false",captured.data.size());
        fclose(report);
    }
    fprintf(stderr,"audio snapshot selftest: %s (%zu bytes)\n",exact&&pcm_equal&&cleanup&&rejected&&ring_exact&&frame_pcm_exact&&clock_rejected&&active_effect?"PASS":"FAIL",captured.data.size());
}


void zc255_audio_content_reset() {
    if(!host_audio_prepared) return;
    EnterCriticalSection(&queue_lock); samples.clear(); LeaveCriticalSection(&queue_lock);
}
// Only after mixer callbacks and MIDI/SFX timers are stopped and sound removed.
void zc255_audio_host_release() {
    if(!host_audio_prepared) return;
    if(al_is_audio_installed()) { auto mixer=al_get_default_mixer(); if(mixer) al_set_mixer_postprocess_callback(mixer,nullptr,nullptr); }
    remove_sound(); // Calls our installed driver exit before restoring its original callbacks.
    zc255_audio_set_frame_mode(0);
    zc255_midi_set_hooks(nullptr,nullptr,nullptr,nullptr);
    zc255_sfx_set_hooks(nullptr,nullptr,nullptr,nullptr);
    if(host_midi_driver) { *host_midi_driver=host_midi_original; host_midi_driver=nullptr; }
    if(synth) { tsf_close(synth); synth=nullptr; }
    samples.clear(); midi_status=midi_count=0; bridge_midi_volume=255; soundfont_identity=0;
    audio_profile_path.clear();
    DeleteCriticalSection(&queue_lock); DeleteCriticalSection(&audio_lock);
    music_preflight=enhanced_music::Prepared{};music_preflight_bytes.clear();
    host_audio_prepared=false; zc255_audio_shared=nullptr;
}

// No destruction here. Worker has stopped producing PCM; backend is software-only.
bool zc255_audio_host_quiesce_for_world() {
    zc255_audio_shared=nullptr;
    if(!host_audio_prepared) return !al_is_audio_installed() && al_get_default_mixer()==nullptr;
    return frame_audio && !al_is_audio_installed() && al_get_default_mixer()==nullptr;
}

// Explicit opt-in integration probe; ordinary quest playback never enters it.
static void enhanced_music_selftest() {
    static bool done=false;
    const char* name=getenv("ZC255_ENHANCED_TEST_TRACK");
    if(done || !name || !*name || !Playing || !frame_audio) return;
    done=true;
    const char* probe_quest=getenv("ZC255_ENHANCED_TEST_QUEST");
    bool loaded=try_zcmusic(name,probe_quest?const_cast<char*>(probe_quest):qstpath,0,-1000,255);
    SaveStateBlob captured;
    bool written=loaded && zc255_audio_state_write(captured);
    std::vector<float> expected(735*2),actual(735*2);
    if(written) enhanced_music::render(expected.data(),735);
    bool audible=std::any_of(expected.begin(),expected.end(),[](float v){return v!=0;});
    bool restored=false,exact=false,rejected=false,pause_exact=false;
    if(written) {
        SaveStateBlob copy=captured;copy.pos=0;
        restored=zc255_audio_state_read(copy,true);
        if(restored) enhanced_music::render(actual.data(),735);
        exact=restored && expected==actual;
        SaveStateBlob before,after,bad;
        if(zc255_audio_state_write(before)) {
            bad=before;bad.pos=0;bad.data.pop_back();
            rejected=!zc255_audio_state_read(bad,true) && zc255_audio_state_write(after) && before.data==after.data;
        }
        enhanced_music::pause(zcmusic,ZCM_PAUSE);
        std::vector<float> silence(735*2);
        SaveStateBlob paused,paused_after;
        bool saved=zc255_audio_state_write(paused);
        enhanced_music::render(silence.data(),735);
        pause_exact=saved && zc255_audio_state_write(paused_after) && paused.data==paused_after.data
            && std::all_of(silence.begin(),silence.end(),[](float v){return v==0;});
        copy=captured;copy.pos=0;restored=zc255_audio_state_read(copy,true) && restored;
    }
    const char* path=getenv("ZC255_ENHANCED_TEST_REPORT");
    if(path && *path) if(FILE* f=fopen(path,"w")) {
        fprintf(f,"{\"loaded\":%s,\"audible\":%s,\"restored\":%s,\"pcm_exact\":%s,\"malformed_unchanged\":%s,\"paused_unchanged\":%s,\"bytes\":%zu}\n",
          loaded?"true":"false",audible?"true":"false",restored?"true":"false",exact?"true":"false",rejected?"true":"false",pause_exact?"true":"false",captured.data.size());
        fclose(f);
    }
    fprintf(stderr,"Enhanced music probe: %s (%s)\n",loaded&&audible&&restored&&exact&&rejected&&pause_exact?"PASS":"FAIL",enhanced_music::last_error());
}
