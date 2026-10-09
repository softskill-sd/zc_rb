#if defined(ALLEGRO_LEGACY_STATICLINK)
#define ZC255_LEGACY_AUDIO_EXPORT
#else
#define ZC255_LEGACY_AUDIO_EXPORT __declspec(dllexport)
#endif
#define ALLEGRO_LEGACY_SRC
/*         ______   ___    ___
 *        /\  _  \ /\_ \  /\_ \
 *        \ \ \L\ \\//\ \ \//\ \      __     __   _ __   ___
 *         \ \  __ \ \ \ \  \ \ \   /'__`\ /'_ `\/\`'__\/ __`\
 *          \ \ \/\ \ \_\ \_ \_\ \_/\  __//\ \L\ \ \ \//\ \L\ \
 *           \ \_\ \_\/\____\/\____\ \____\ \____ \ \_\\ \____/
 *            \/_/\/_/\/____/\/____/\/____/\/___L\ \/_/ \/___/
 *                                           /\____/
 *                                           \_/__/
 *
 *      Stuff for BeOS.
 *
 *      By Jason Wilkins.
 *
 *      See readme.txt for copyright information.
 */

#include "allegro.h"
#include "allegro5/internal/aintern_audio.h"
extern int _zc255_sfx_try_lock(void);
extern void _zc255_sfx_unlock(void);
#include "allegro/internal/aintern.h"
#include "allegro/platform/ainta5.h"
#include "allegro/platform/ala5.h"

#define _A5_SOUND_BUFFERS        3
#define _A5_SOUND_BUFFER_SIZE 1024
#ifdef __EMSCRIPTEN__
    #define _A5_SOUND_FREQUENCY  22050
#else
    #define _A5_SOUND_FREQUENCY  44100
#endif
#define _A5_SOUND_DEPTH ALLEGRO_AUDIO_DEPTH_INT16
#define _A5_SOUND_CHANNELS ALLEGRO_CHANNEL_CONF_2


extern int _zc255_frame_audio_enabled(void);
static int zc255_frame_volume=255;
ZC255_LEGACY_AUDIO_EXPORT int zc255_audio_frame_volume(void) { return zc255_frame_volume; }
static ALLEGRO_THREAD * a5_sound_thread = NULL;

static ALLEGRO_AUDIO_STREAM * a5_sound_stream = NULL;
static ALLEGRO_MUTEX * a5_sound_mutex = NULL;

static void * a5_sound_thread_proc(ALLEGRO_THREAD * thread, void * data)
{
    ALLEGRO_EVENT_QUEUE * queue;
    ALLEGRO_EVENT event;
    ALLEGRO_TIMEOUT timeout;
    void * fragment;
    bool fragments_done = false;

    a5_sound_stream = al_create_audio_stream(_A5_SOUND_BUFFERS, _A5_SOUND_BUFFER_SIZE, _A5_SOUND_FREQUENCY, _A5_SOUND_DEPTH, _A5_SOUND_CHANNELS);
    if(!a5_sound_stream)
    {
        return NULL;
    }
    // local edit - Avoids "Out of buffer" warning in logs.
    while ((fragment = al_get_audio_stream_fragment(a5_sound_stream)))
    {
        int bytes_per_sample = al_get_channel_count(_A5_SOUND_CHANNELS) * al_get_audio_depth_size(_A5_SOUND_DEPTH);
        int samples = _A5_SOUND_BUFFER_SIZE / bytes_per_sample / _A5_SOUND_CHANNELS;
        al_fill_silence(fragment, samples, _A5_SOUND_DEPTH, _A5_SOUND_CHANNELS);
        al_set_audio_stream_fragment(a5_sound_stream, fragment);
    }
    if(!al_attach_audio_stream_to_mixer(a5_sound_stream, al_get_default_mixer()))
    {
        al_destroy_audio_stream(a5_sound_stream);
        return NULL;
    }
    queue = al_create_event_queue();
    if(!queue)
    {
        al_destroy_audio_stream(a5_sound_stream);
        return NULL;
    }
    al_set_audio_stream_playing(a5_sound_stream, true);
    al_register_event_source(queue, al_get_audio_stream_event_source(a5_sound_stream));
    while(!al_get_thread_should_stop(thread))
    {
#ifdef ALLEGRO_LEGACY_CLOSE_THREADS
        if (al_wait_for_event_until(queue, &event, &timeout))
#else
        al_wait_for_event(queue, &event);
#endif
        {
            switch(event.type)
            {
                case ALLEGRO_EVENT_AUDIO_STREAM_FRAGMENT:
                {
                    fragments_done = false;
                    while(!fragments_done)
                    {
                        if(!_zc255_sfx_try_lock()) { fragments_done=true; continue; }
                        fragment = al_get_audio_stream_fragment(a5_sound_stream);
                        if(fragment)
                        {
                            // local edit
                            _mix_some_samples((uintptr_t)fragment, 0, TRUE);
                            al_set_audio_stream_fragment(a5_sound_stream, fragment);
                        }
                        else
                        {
                            fragments_done = true;
                        }
                        _zc255_sfx_unlock();
                    }
                    break;
                }
            }
        }
    }
    al_destroy_event_queue(queue);
    al_destroy_audio_stream(a5_sound_stream);
    return NULL;
}

static int a5_sound_detect(int input)
{
    return TRUE;
}

static int a5_sound_init(int input, int voices)
{
    digi_allegro_5.voices = voices;
    _sound_stereo = 1;
    _sound_freq = _A5_SOUND_FREQUENCY;
    _sound_bits = 16;

    if(_mixer_init(_A5_SOUND_BUFFER_SIZE * (_sound_bits / 8), _sound_freq, _sound_stereo, ((_sound_bits == 16) ? 1 : 0), &digi_allegro_5.voices) != 0)
    {
        return -1;
    }
    if(_zc255_frame_audio_enabled()) {
        a5_sound_mutex=al_create_mutex();
        if(!a5_sound_mutex) { _mixer_exit(); return -1; }
        return 0;
    }
    if(!al_install_audio())
    {
        return -1;
    }
    if(!al_reserve_samples(4))
    {
        al_uninstall_audio();
        return -1;
    }
    a5_sound_mutex = al_create_mutex();
    if(!a5_sound_mutex)
    {
        al_uninstall_audio();
        return -1;
    }
    a5_sound_thread = al_create_thread(a5_sound_thread_proc, NULL);
    if(!a5_sound_thread)
    {
        al_uninstall_audio();
        return -1;
    }
    al_start_thread(a5_sound_thread);
    return 0;
}

static void a5_sound_exit(int input)
{
    if(_zc255_frame_audio_enabled()) {
        if(a5_sound_mutex) { al_destroy_mutex(a5_sound_mutex); a5_sound_mutex=NULL; }
        _mixer_exit();
        return;
    }
#ifdef ALLEGRO_LEGACY_CLOSE_THREADS
    if(a5_sound_thread) al_destroy_thread(a5_sound_thread);
    a5_sound_thread = NULL;
#endif
    al_uninstall_audio();
}

static int a5_sound_set_mixer_volume(int volume)
{
    if(_zc255_frame_audio_enabled()) { zc255_frame_volume=volume; return 0; }
    if(al_set_audio_stream_gain(a5_sound_stream, (float)volume / 255.0))
    {
        return 0;
    }
    return -1;
}

static int a5_sound_get_mixer_volume(void)
{
    if(_zc255_frame_audio_enabled()) return zc255_frame_volume;
    return al_get_audio_stream_gain(a5_sound_stream) * 255.0;
}

static int a5_sound_buffer_size(void)
{
    return _A5_SOUND_BUFFER_SIZE * al_get_audio_depth_size(_A5_SOUND_DEPTH) * 2;
}

static void * a5_sound_lock_voice(int voice, int start, int end)
{
    al_lock_mutex(a5_sound_mutex);
    return NULL;
}

static void a5_sound_unlock_voice(int voice)
{
    al_unlock_mutex(a5_sound_mutex);
}

DIGI_DRIVER digi_allegro_5 =
{
   DIGI_ALLEGRO_5,
   empty_string,
   empty_string,
   "Allegro 5 Sound",
   0,
   0,
   MIXER_MAX_SFX,
   MIXER_DEF_SFX,

   a5_sound_detect,
   a5_sound_init,
   a5_sound_exit,
   a5_sound_set_mixer_volume,
   a5_sound_get_mixer_volume,

   a5_sound_lock_voice,
   a5_sound_unlock_voice,
   a5_sound_buffer_size,
   _mixer_init_voice,
   _mixer_release_voice,
   _mixer_start_voice,
   _mixer_stop_voice,
   _mixer_loop_voice,

   _mixer_get_position,
   _mixer_set_position,

   _mixer_get_volume,
   _mixer_set_volume,
   _mixer_ramp_volume,
   _mixer_stop_volume_ramp,

   _mixer_get_frequency,
   _mixer_set_frequency,
   _mixer_sweep_frequency,
   _mixer_stop_frequency_sweep,

   _mixer_get_pan,
   _mixer_set_pan,
   _mixer_sweep_pan,
   _mixer_stop_pan_sweep,

   _mixer_set_echo,
   _mixer_set_tremolo,
   _mixer_set_vibrato,
   0, 0,
   0,
   0,
   0,
   0,
   0,
   0
};

/* Capture the outstanding PCM fragments and resampler position too. */
#define ZC255_STREAM_BYTES ((_A5_SOUND_BUFFER_SIZE+3)*_A5_SOUND_BUFFERS*4)
typedef struct SFX_STREAM_STATE {
    int frame_mode,frame_volume;
    int version,present,playing,draining,loop,pos,error,loop_start,loop_end,step,denom;
    float speed,gain,pan,matrix[4];
    int matrix_present;
    long current,pending[_A5_SOUND_BUFFERS],used[_A5_SOUND_BUFFERS];
    uint64_t consumed;
    unsigned char buffer[ZC255_STREAM_BYTES];
} SFX_STREAM_STATE;
static long stream_offset(void* p) {
    return p ? (long)((char*)p-(char*)a5_sound_stream->main_buffer) : -1;
}
ZC255_LEGACY_AUDIO_EXPORT size_t zc255_stream_snapshot_size(void) { return sizeof(SFX_STREAM_STATE); }
ZC255_LEGACY_AUDIO_EXPORT int zc255_stream_snapshot_write(void* bytes,size_t count) {
    SFX_STREAM_STATE s; int i;
    if(count!=sizeof(s)) return 0;
    memset(&s,0,sizeof(s)); s.version=2; s.frame_mode=_zc255_frame_audio_enabled(); s.frame_volume=zc255_frame_volume; s.present=a5_sound_stream!=NULL;
    if(a5_sound_stream) {
        ALLEGRO_SAMPLE_INSTANCE* spl=&a5_sound_stream->spl;
        if(a5_sound_stream->buf_count!=_A5_SOUND_BUFFERS || spl->spl_data.len!=_A5_SOUND_BUFFER_SIZE || spl->spl_data.depth!=_A5_SOUND_DEPTH || spl->spl_data.chan_conf!=_A5_SOUND_CHANNELS) return 0;
        if(spl->mutex) al_lock_mutex(spl->mutex);
        s.playing=spl->is_playing; s.draining=a5_sound_stream->is_draining; s.loop=spl->loop;
        s.pos=spl->pos; s.error=spl->pos_bresenham_error; s.loop_start=spl->loop_start; s.loop_end=spl->loop_end;
        s.step=spl->step; s.denom=spl->step_denom; s.speed=spl->speed; s.gain=spl->gain; s.pan=spl->pan;
        s.matrix_present=spl->matrix!=NULL; if(spl->matrix) memcpy(s.matrix,spl->matrix,sizeof(s.matrix));
        s.current=stream_offset(spl->spl_data.buffer.ptr); s.consumed=a5_sound_stream->consumed_fragments;
        for(i=0;i<_A5_SOUND_BUFFERS;i++) { s.pending[i]=stream_offset(a5_sound_stream->pending_bufs[i]); s.used[i]=stream_offset(a5_sound_stream->used_bufs[i]); }
        memcpy(s.buffer,a5_sound_stream->main_buffer,sizeof(s.buffer));
        if(spl->mutex) al_unlock_mutex(spl->mutex);
    }
    memcpy(bytes,&s,sizeof(s)); return 1;
}
ZC255_LEGACY_AUDIO_EXPORT int zc255_stream_snapshot_read(const void* bytes,size_t count,int apply) {
    SFX_STREAM_STATE s; int i;
    if(count!=sizeof(s)) return 0;
    memcpy(&s,bytes,sizeof(s)); if(s.version!=2 || s.frame_mode!=_zc255_frame_audio_enabled() || s.frame_volume<0 || s.frame_volume>255 || s.present!=(a5_sound_stream!=NULL)) return 0;
    if(!s.present) { if(apply) zc255_frame_volume=s.frame_volume; return 1; }
    if(s.current < -1 || s.current>=ZC255_STREAM_BYTES || s.pos<0 || s.pos>_A5_SOUND_BUFFER_SIZE*4 || s.step<0 || s.denom<0) return 0;
    if(s.matrix_present!=(a5_sound_stream->spl.matrix!=NULL)) return 0;
    for(i=0;i<_A5_SOUND_BUFFERS;i++) if(s.pending[i]<-1 || s.pending[i]>=ZC255_STREAM_BYTES || s.used[i]<-1 || s.used[i]>=ZC255_STREAM_BYTES) return 0;
    if(!apply) return 1;
    {
        ALLEGRO_SAMPLE_INSTANCE* spl=&a5_sound_stream->spl;
        char* base=(char*)a5_sound_stream->main_buffer;
        if(spl->mutex) al_lock_mutex(spl->mutex);
        zc255_frame_volume=s.frame_volume;
        spl->is_playing=s.playing!=0; a5_sound_stream->is_draining=s.draining!=0; spl->loop=s.loop;
        spl->pos=s.pos; spl->pos_bresenham_error=s.error; spl->loop_start=s.loop_start; spl->loop_end=s.loop_end;
        spl->step=s.step; spl->step_denom=s.denom; spl->speed=s.speed; spl->gain=s.gain; spl->pan=s.pan;
        if(spl->matrix) memcpy(spl->matrix,s.matrix,sizeof(s.matrix));
        spl->spl_data.buffer.ptr=s.current>=0 ? base+s.current : NULL;
        a5_sound_stream->consumed_fragments=s.consumed;
        for(i=0;i<_A5_SOUND_BUFFERS;i++) {
            a5_sound_stream->pending_bufs[i]=s.pending[i]>=0 ? base+s.pending[i] : NULL;
            a5_sound_stream->used_bufs[i]=s.used[i]>=0 ? base+s.used[i] : NULL;
        }
        memcpy(base,s.buffer,sizeof(s.buffer));
        if(spl->mutex) al_unlock_mutex(spl->mutex);
    }
    return 1;
}
