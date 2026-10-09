#pragma once
// Optional libretro SET_VARIABLE (70); unknown frontends return false.
// https://github.com/libretro/RetroArch/blob/master/libretro-common/include/libretro.h
#ifndef RETRO_ENVIRONMENT_SET_VARIABLE
#define RETRO_ENVIRONMENT_SET_VARIABLE 70
#endif
// Frontend action only. A persisted request never runs when content loads.
struct ProgressClearPrompt {
    std::string last;
    bool visible=false, first_frame=false;
    unsigned previous=0;
    uint32_t pixels[256*240]{};
    static std::string option(retro_environment_t env) {
        retro_variable value{"zc255_clear_progress",nullptr};
        return env && env(RETRO_ENVIRONMENT_GET_VARIABLE,&value) && value.value?value.value:"keep";
    }
    void rearm(retro_environment_t env) {
        retro_variable value{"zc255_clear_progress","keep"};
        last=env && env(RETRO_ENVIRONMENT_SET_VARIABLE,&value)?"keep":option(env);
    }
    void reset(retro_environment_t env) {rearm(env);visible=false;first_frame=false;previous=0;}
    bool changed(retro_environment_t env) {
        auto value=option(env);bool request=value!=last && value=="request clear";
        last=std::move(value);return request;
    }
    void open(const uint32_t* picture) {
        memcpy(pixels,picture,sizeof(pixels));visible=true;first_frame=true;
        for(int y=73;y<167;++y)for(int x=9;x<247;++x)pixels[y*256+x]=0x181824;
        text("CLEAR SAVED PROGRESS?",88);text("A: CLEAR AND RESTART",117);text("B: CANCEL",142);
    }
    void text(const char* value,int y) {
        struct Glyph {char c;uint8_t rows[7];};
        static constexpr Glyph glyphs[]={
            {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},
            {'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},
            {'E',{31,16,16,30,16,16,31}},{'G',{14,17,16,23,17,17,14}},
            {'L',{16,16,16,16,16,16,31}},{'N',{17,25,25,21,19,19,17}},
            {'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},
            {'R',{30,17,17,30,20,18,17}},{'S',{15,16,16,14,1,1,30}},
            {'T',{31,4,4,4,4,4,4}},{'V',{17,17,17,17,17,10,4}},
            {'?',{14,17,1,2,4,0,4}},{':',{0,4,4,0,4,4,0}}};
        int x=(256-int(strlen(value))*6)/2;
        for(;*value;++value,x+=6)for(const auto& glyph:glyphs)if(glyph.c==*value)
            for(int r=0;r<7;++r)for(int c=0;c<5;++c)if(glyph.rows[r]&(16>>c))pixels[(y+r)*256+x+c]=0xFFFFFF;
    }
    // Ignore held buttons on entry. B always wins when both are pressed.
    int update(retro_input_state_t input) {
        unsigned buttons=0;
        if(input) {if(input(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A))buttons|=1;
            if(input(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B))buttons|=2;}
        unsigned pressed=buttons&~previous;previous=buttons;
        if(first_frame){first_frame=false;return 0;}
        if(pressed&2){visible=false;return -1;}
        if(pressed&1){visible=false;return 1;}
        return 0;
    }
};
