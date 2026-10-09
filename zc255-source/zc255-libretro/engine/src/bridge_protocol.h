#pragma once
#include <cstdint>
constexpr uint32_t ZC255_BRIDGE_MAGIC = 0x255C0DE;
constexpr unsigned ZC255_WIDTH = 256;
constexpr unsigned ZC255_HEIGHT = 240;
constexpr unsigned ZC255_STATE_CAPACITY = 32 * 1024 * 1024;
enum ZC255Command : uint32_t { ZC255_RUN, ZC255_GET_STATE, ZC255_SET_STATE, ZC255_SAVE };
struct ZC255Shared {
    uint32_t magic, width, height, buttons, stop, sequence;
    int32_t frame, hero_x, hero_y, cheat_level, playing;
    int32_t life, max_life, clock, no_walls;
    int32_t rupees, sfx_peak, music_peak;
    uint32_t pixels[ZC255_WIDTH * ZC255_HEIGHT];
    uint32_t command, result, state_size, audio_frames;
    int16_t audio[4096 * 2];
    int32_t state_frame;
    uint32_t state_pixels[ZC255_WIDTH * ZC255_HEIGHT];
    uint8_t state_data[ZC255_STATE_CAPACITY];
    uint32_t cheat_mask,cheat_values,cheat_events,cheat_policy_applied;
};

#include <cstddef>
static_assert(offsetof(ZC255Shared,magic)==0);
static_assert(offsetof(ZC255Shared,width)==4);
static_assert(offsetof(ZC255Shared,height)==8);
static_assert(offsetof(ZC255Shared,buttons)==12);
static_assert(offsetof(ZC255Shared,stop)==16);
static_assert(offsetof(ZC255Shared,sequence)==20);
static_assert(offsetof(ZC255Shared,frame)==24);
static_assert(offsetof(ZC255Shared,hero_x)==28);
static_assert(offsetof(ZC255Shared,hero_y)==32);
static_assert(offsetof(ZC255Shared,cheat_level)==36);
static_assert(offsetof(ZC255Shared,playing)==40);
static_assert(offsetof(ZC255Shared,life)==44);
static_assert(offsetof(ZC255Shared,max_life)==48);
static_assert(offsetof(ZC255Shared,clock)==52);
static_assert(offsetof(ZC255Shared,no_walls)==56);
static_assert(offsetof(ZC255Shared,rupees)==60);
static_assert(offsetof(ZC255Shared,sfx_peak)==64);
static_assert(offsetof(ZC255Shared,music_peak)==68);
static_assert(offsetof(ZC255Shared,pixels)==72);
static_assert(offsetof(ZC255Shared,command)==245832);
static_assert(offsetof(ZC255Shared,result)==245836);
static_assert(offsetof(ZC255Shared,state_size)==245840);
static_assert(offsetof(ZC255Shared,audio_frames)==245844);
static_assert(offsetof(ZC255Shared,audio)==245848);
static_assert(offsetof(ZC255Shared,state_frame)==262232);
static_assert(offsetof(ZC255Shared,state_pixels)==262236);
static_assert(offsetof(ZC255Shared,state_data)==507996);
static_assert(offsetof(ZC255Shared,cheat_mask)==34062428);
static_assert(sizeof(ZC255Shared)==34062444);
