// Included at the end of zc_sys.cpp to reach private input state safely.
#include "snapshot_runtime.h"

namespace {
struct RuntimeSnapshot {
    std::map<int32_t, int32_t> items, costs;
    bool tile_valid, tile_bunny, tile_superman;
    int tile_shield;
    int32_t tile_value;
    bool raw[ZC_CONTROL_STATES], control[ZC_CONTROL_STATES];
    bool disable[ZC_CONTROL_STATES], down[controls::btnLast];
    bool press[ZC_CONTROL_STATES], held[ZC_CONTROL_STATES];
    bool drunk[11], disabled_keys[127], input[127], key_press[127];
    bool current[127], previous[127], system[127], system_previous[127], system_press[127];
    bool control_loaded;
    bool walls;
    int32_t voices[WAV_COUNT];
    int32_t paused_midi, digi_vol, midi_vol, sfx_vol, emusic_vol, current_midi, panning;
    byte midi_suspend;
};

void put_item_cache(SaveStateBlob& b, const std::map<int32_t, int32_t>& cache) {
    b.put(uint32_t(cache.size()));
    for (const auto& [family, item] : cache) { b.put(family); b.put(item); }
}
bool get_item_cache(SaveStateBlob& b, std::map<int32_t, int32_t>& cache) {
    uint32_t count = 0; b.get(count);
    if (b.failed || count > 4096 || count > (b.data.size() - b.pos) / 8) {
        b.failed = true; return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        int32_t family = 0, item = 0; b.get(family); b.get(item);
        if (b.failed || item < -1 || item >= MAXITEMS || !cache.emplace(family, item).second) {
            b.failed = true; return false;
        }
    }
    return true;
}
#define RUNTIME_ARRAYS(X) \
    X(raw, raw_control_state) X(control, control_state) X(disable, disable_control) \
    X(down, down_control_states) X(press, button_press) X(held, button_hold) \
    X(drunk, drunk_toggle_state) X(disabled_keys, disabledKeys) X(input, KeyInput) \
    X(key_press, KeyPress) X(current, key_current_frame) X(previous, key_previous_frame) \
    X(system, key_system) X(system_previous, key_system_previous) X(system_press, key_system_press)

#define CHECK_RUNTIME_ARRAY(member, live) \
    static_assert(sizeof(((RuntimeSnapshot*)nullptr)->member) == sizeof(live), "Snapshot runtime array width mismatch: " #live);
RUNTIME_ARRAYS(CHECK_RUNTIME_ARRAY)
#undef CHECK_RUNTIME_ARRAY
static_assert(sizeof(((RuntimeSnapshot*)nullptr)->voices) == sizeof(sfx_voice), "Snapshot sound voice map width mismatch");

// Store booleans as validated bytes, rather than accepting arbitrary bool images.
template<size_t N> void put_flags(SaveStateBlob& b, const bool (&v)[N]) {
    for (bool flag : v) b.put(uint8_t(flag));
}
template<size_t N> void get_flags(SaveStateBlob& b, bool (&v)[N]) {
    for (bool& flag : v) {
        uint8_t byte = 0; b.get(byte);
        if (byte > 1) b.failed = true;
        flag = byte != 0;
    }
}
bool decode_runtime(SaveStateBlob& b, RuntimeSnapshot& state) {
    if (!get_item_cache(b, state.items) || !get_item_cache(b, state.costs)) return false;
    bool flags[5] = {}; get_flags(b, flags);
    state.tile_valid = flags[0]; state.tile_bunny = flags[1]; state.tile_superman = flags[2];
    state.control_loaded = flags[3]; state.walls = flags[4];
    b.get(state.tile_shield); b.get(state.tile_value);
    #define READ_ARRAY(member, live) get_flags(b, state.member);
    RUNTIME_ARRAYS(READ_ARRAY)
    #undef READ_ARRAY
    b.get(state.voices, sizeof(state.voices));
    b.get(state.paused_midi); b.get(state.midi_suspend);
    b.get(state.digi_vol); b.get(state.midi_vol); b.get(state.sfx_vol); b.get(state.emusic_vol);
    b.get(state.current_midi); b.get(state.panning);
    return !b.failed;
}
}

void zc255_runtime_write(SaveStateBlob& b) {
    put_item_cache(b, itemcache); put_item_cache(b, itemcache_cost);
    bool flags[5] = {tilemod_cache_state.valid, tilemod_cache_state.bunny_clock,
        tilemod_cache_state.superman, load_control_called_this_frame, bool(toogam)};
    put_flags(b, flags);
    b.put(tilemod_cache_state.shield); b.put(tilemod_cache_value);
    #define WRITE_ARRAY(member, live) put_flags(b, live);
    RUNTIME_ARRAYS(WRITE_ARRAY)
    #undef WRITE_ARRAY
    b.put(sfx_voice, sizeof(sfx_voice));
    b.put(paused_midi_pos); b.put(midi_suspended);
    b.put(digi_volume); b.put(midi_volume); b.put(sfx_volume); b.put(emusic_volume);
    b.put(currmidi); b.put(pan_style);
}

bool zc255_runtime_read(SaveStateBlob& b) {
    RuntimeSnapshot state{};
    if (!decode_runtime(b, state)) return false;
    itemcache.swap(state.items); itemcache_cost.swap(state.costs);
    tilemod_cache_state = {state.tile_valid, state.tile_bunny, state.tile_superman, state.tile_shield};
    tilemod_cache_value = state.tile_value;
    load_control_called_this_frame = state.control_loaded; toogam = state.walls;
    #define APPLY_ARRAY(member, live) std::copy(std::begin(state.member), std::end(state.member), std::begin(live));
    RUNTIME_ARRAYS(APPLY_ARRAY)
    #undef APPLY_ARRAY
    std::copy(std::begin(state.voices), std::end(state.voices), std::begin(sfx_voice));
    paused_midi_pos=state.paused_midi; midi_suspended=state.midi_suspend;
    digi_volume=state.digi_vol; midi_volume=state.midi_vol; sfx_volume=state.sfx_vol;
    emusic_volume=state.emusic_vol; currmidi=state.current_midi; pan_style=state.panning;
    return true;
}
bool zc255_runtime_skip(SaveStateBlob& b) {
    RuntimeSnapshot state{}; return decode_runtime(b, state);
}
#undef RUNTIME_ARRAYS
