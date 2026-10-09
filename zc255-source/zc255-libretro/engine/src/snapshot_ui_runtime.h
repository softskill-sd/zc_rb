#pragma once
#include <allegro.h>
// Every value surviving a modal frame is explicit. Phase: closed, requested,
// opening, interactive, closing. Bridge-only modal execution occurs between
// complete game_loop calls, so it has no interrupted gameplay C++ stack.
struct ZC255UiRuntime {
    int phase=0,y=0,distance=0,offy=0;
    bool showtime=false,use_a=false,use_x=false,use_y=false,b_only=false;
    bool compat=false,noverify=false,done=false,legacy_btn_press_peek=false;
    bool opening_bottom=false,saved_skipdraws=false,saved_bombpal=false;
    PALETTE temppal{};
};
extern ZC255UiRuntime zc255_ui_runtime;
void zc255_ui_request();
bool zc255_ui_active();
void zc255_ui_step();
struct ZC255MapRuntime {
    int phase=0,px=0,py=0,lx=0,ly=0,sc=6,delay=0,showmode=3;
    bool done=false,redraw=true;
};
extern ZC255MapRuntime zc255_map_runtime;
extern BITMAP* zc255_map_bitmap;
bool zc255_map_active();
void zc255_map_step();
void zc255_map_reset();
// Hosted game-over UI runs only at complete gameplay boundaries. Phase 3 is
// consumed immediately by the main loop, before another frontend frame.
struct ZC255DeathMenuRuntime {
    int phase=0,type=0,pos=0,flash=-1;
    bool waiting_release=true;
};
extern ZC255DeathMenuRuntime zc255_death_menu_runtime;
bool zc255_death_menu_active();
void zc255_death_menu_begin(int type);
void zc255_death_menu_step();
void zc255_death_menu_finish();
void zc255_death_menu_reset();
void zc255_death_menu_trace(const char* event);
