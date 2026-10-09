#pragma once
#include <cstddef>
#include "sound/zcmusic.h"
struct SaveStateBlob;
#include <memory>

namespace enhanced_music {
// All calls are made by the native frame worker; no audio/device/thread callbacks.
ZCMUSIC* load(const char* full_path);
bool owns(const ZCMUSIC*);
bool play(ZCMUSIC*,int volume);
bool pause(ZCMUSIC*,int mode);
bool stop(ZCMUSIC*);
bool volume(ZCMUSIC*,int value);
void unload(ZCMUSIC*&);
void poll(); // Increments ZCMUSIC::position only; never advances the PCM cursor.
void exit();
int get_position(ZCMUSIC*); // Legacy unit: seconds *10000, despite old msecs name.
void seek(ZCMUSIC*,int position);
void speed(ZCMUSIC*,int value); // Legacy unit: ratio *10000.
int length(ZCMUSIC*);
void loop(ZCMUSIC*,double start,double end);
int tracks(ZCMUSIC*);
int change_track(ZCMUSIC*,int number);
void render(float* stereo,unsigned frames); // Adds unclipped stereo at 44100 Hz.

struct Prepared {
    struct Impl;
    std::unique_ptr<Impl> value;
    Prepared(); ~Prepared();
    Prepared(Prepared&&) noexcept;
    Prepared& operator=(Prepared&&) noexcept;
    Prepared(const Prepared&)=delete;
    Prepared& operator=(const Prepared&)=delete;
};
// Includes all loaded software tracks and zcmusic/zcmixer pointer aliases/fades.
// prepare validates/reopens/decodes fully without touching live music or bindings.
bool write(SaveStateBlob&);
bool prepare(SaveStateBlob&,Prepared&);
void apply(Prepared&) noexcept; // Preallocated commit, no I/O/decode/allocation.
const char* last_error();
struct Diagnostics { unsigned loaded_tracks; uint64_t rendered_frames; bool source_pcm_nonzero; uint64_t gme_generated_blocks; uint64_t gme_warm_restores; std::size_t gme_cached_bytes; };
Diagnostics diagnostics();
}
