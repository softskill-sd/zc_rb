#include "native_history_policy.h"
#include "engine_host.h"
#include "zc/savestate_snapshot.h"
extern int32_t skipcont;
#include "base/zc_alleg.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "bridge_protocol.h"
#include "player_bridge.h"
#include "zc/zelda.h"
#include "zc/zc_sys.h"
#include "zc/control_scheme.h"
#include "zc/rewind.h"
#include "zc/replay.h"
#include "zc/hero.h"
#include "zc/cheats.h"
#include "zc/saves.h"
#include "zc/title.h"



#include "zc/ffscript.h"
#include "snapshot_runtime.h"
#include "snapshot_ui_runtime.h"
#include "snapshot_ui.h"
#include "snapshot_definitions_selftest.h"
#include "snapshot_structure_selftest.h"
#include "snapshot_collision_selftest.h"
#include "snapshot_collision_state.h"
#include "snapshot_mblock_selftest.h"
#include "snapshot_screen_cache_selftest.h"
#include "frame_conversion.h"

extern HeroClass Hero;
extern int32_t cheat, maxcheat;
extern bool key_current_frame[127];
static ZC255Shared* shared = nullptr;
static HANDLE ready, advance;
static uint32_t previous_buttons;
static uint32_t pending_policy_mask,pending_policy_values,pending_policy_events;
static bool policy_pending;
static void apply_pending_policy();
static std::vector<uint8_t> cached_state;
static bool restore_pending;
static bool capture_pending;
static unsigned modal_depth;
static unsigned death_depth;
void zc255_bridge_modal_enter() { ++modal_depth; }
void zc255_bridge_modal_leave() { --modal_depth; }
void zc255_bridge_death_enter() { ++death_depth; }
void zc255_bridge_death_leave() { --death_depth; }
static int last_auto_save = -1;
static int cached_frame = -1;
static uint32_t cached_pixels[ZC255_WIDTH * ZC255_HEIGHT];
extern bool subscreen_open;
bool zc255_snapshot_export(std::vector<uint8_t>&);
bool zc255_snapshot_import(const uint8_t*, size_t);
extern ZC255Shared* zc255_audio_shared;

void zc255_bridge_state_write(SaveStateBlob& b) {
    b.put(previous_buttons);
    b.put(last_auto_save);
}
bool zc255_bridge_state_read(SaveStateBlob& b, bool apply) {
    uint32_t buttons = 0;
    int autosave = -1;
    b.get(buttons); b.get(autosave);
    if (b.failed || buttons & ~0xFFFFu || autosave < -1) { b.failed = true; return false; }
    if (apply) { previous_buttons = buttons; last_auto_save = autosave; }
    return true;
}

static void copy_frame_pixels(uint32_t* dest) {
    BITMAP* image = presentbuf ? presentbuf : latest_screen_image();
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i) {
        auto expand = [](unsigned c) { return (c << 2) | (c >> 4); };
        palette[i] = (expand(RAMpal[i].r) << 16) | (expand(RAMpal[i].g) << 8) | expand(RAMpal[i].b);
    }
    zc255_convert_frame(image,palette,dest);
}

const char* zc255_bridge_save_path() { return zc255_host::save_path(); }

bool zc255_bridge_active() { return shared != nullptr; }

void zc255_bridge_init() {
    shared=zc255_host::exchange();
    ready=static_cast<HANDLE>(zc255_host::ready_event());
    advance=static_cast<HANDLE>(zc255_host::advance_event());
    if(!shared || !ready || !advance) zc255_host::fatal("Host bridge is not initialized");
    zc255_audio_shared=shared;
}

void zc255_bridge_input() {
    if (!shared) return;
    std::memset((void*)key, 0, 127);
    auto down = [](unsigned id) { return (shared->buttons & (1u << id)) != 0; };
    // RetroPad: up/down/left/right, A/B, Start, X/Y, L/R.
    const unsigned ids[] = {4,5,6,7,8,0,3,9,1,10,11,2};
    const unsigned btns[] = {btnUp,btnDown,btnLeft,btnRight,btnA,btnB,btnS,btnEx1,btnEx2,btnL,btnR,btnP};
    if (active_control_scheme) {
        for (unsigned i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i) {
            int k = active_control_scheme->keys[btns[i]];
            if (k > 0 && k < 127 && down(ids[i])) key[k] = 1;
        }
    }
    key[KEY_R] = down(12) && !zc255_external_io_blocked(); // Disk writes cannot be rewound.
    if (rewind_is_previewing()) key_current_frame[KEY_R] = key[KEY_R] != 0;
    // Unlock all four cheat levels without changing the game's test/debug mode.
    cheat = maxcheat = (shared->cheat_mask&0x80000000u)?0:4;
    if (game && Playing && !rewind_is_previewing()) {
        uint32_t presses = shared->buttons & ~previous_buttons;
        if (presses & (1u << 13)) cheats_apply_frontend_policy(shared->cheat_mask,shared->cheat_values,4);
    }
    previous_buttons = shared->buttons;
}

static void publish_diagnostics() {
    shared->frame = replay_get_frame();
    shared->cheat_level = cheat;
    shared->playing = Playing;
    shared->hero_x = int32_t(Hero.getX().getZLong());
    shared->hero_y = int32_t(Hero.getY().getZLong());
    shared->life = game->get_life();
    shared->max_life = game->get_maxlife();
    shared->rupees = game->get_rupies();
    shared->clock = getClock();
    shared->no_walls = toogam;
}

static void capture_response() {
    if (Playing && game && game->get_life()<=0 && !zc255_death_menu_active() &&
        !Quit && !modal_depth && !zc255_external_io_blocked() &&
        !(GameFlags & (GAMEFLAG_SCRIPTMENU_ACTIVE | GAMEFLAG_F6SCRIPT_ACTIVE))) {
        // RetroArch runs a core frame after each rewind restore. A snapshot
        // just before the fatal animation would immediately re-enter that
        // non-serializable stack. Retain the preceding complete state instead.
        shared->result=!cached_state.empty() && cached_frame>=0;
        shared->state_size=shared->result?uint32_t(cached_state.size()):0;
        shared->state_frame=cached_frame;
        if(shared->result) {
            std::memcpy(shared->state_data,cached_state.data(),cached_state.size());
            std::memcpy(shared->state_pixels,cached_pixels,sizeof(cached_pixels));
        }
        return;
    }
    shared->result = (Playing || zc255_death_menu_active()) && !Quit && !modal_depth && !zc255_external_io_blocked() &&
        !(GameFlags & (GAMEFLAG_SCRIPTMENU_ACTIVE | GAMEFLAG_F6SCRIPT_ACTIVE)) &&
        zc255_snapshot_export(cached_state);
    // Faster stored combo streams must never make a previously fitting quest
    // lose states. On overflow, rebuild those caches with the original codec.
    if(shared->result && cached_state.size()>ZC255_STATE_CAPACITY && snapshot_definitions::stored_combo_encoding()) {
        snapshot_definitions::stored_combo_encoding()=false;
        shared->result=zc255_snapshot_export(cached_state);
    }
    if(cached_state.size()>ZC255_STATE_CAPACITY) shared->result=0;
    zc255_native_history::note_export(shared->result!=0);
    if (!shared->result) { cached_state.clear(); cached_frame = -2; }
    else { cached_frame = replay_get_frame(); copy_frame_pixels(cached_pixels); publish_diagnostics(); }
    shared->state_size = shared->result ? uint32_t(cached_state.size()) : 0;
    shared->state_frame = cached_frame;
    if (shared->result) {
        std::memcpy(shared->state_data,cached_state.data(),cached_state.size());
        std::memcpy(shared->state_pixels,cached_pixels,sizeof(cached_pixels));
    }
}

static bool command_loop(bool canonical = false) {
    for (;;) {
        if(shared->stop) { Quit=qEXIT; skipcont=1; return false; }
        if(zc255_host::unloading()) { Quit=qQUIT; skipcont=1; return false; }
        SetEvent(ready);
        DWORD result=WaitForSingleObject(advance,INFINITE);
        if(result!=WAIT_OBJECT_0) zc255_host::fatal("Host command wait failed");
        if(shared->stop) { Quit=qEXIT; skipcont=1; return false; }
        if(shared->command==101) { zc255_host::request_unload(); return false; }
        if (shared->command == ZC255_GET_STATE) {
            if (death_depth) {
                // The native death animation/OnDeath stack is not serializable.
                // Keep RetroArch's history valid with the last complete snapshot;
                // never put a half-destroyed world into a new state.
                shared->result=!cached_state.empty() && cached_frame>=0;
                shared->state_size=shared->result?uint32_t(cached_state.size()):0;
                shared->state_frame=cached_frame;
                if(shared->result) {
                    std::memcpy(shared->state_data,cached_state.data(),cached_state.size());
                    std::memcpy(shared->state_pixels,cached_pixels,sizeof(cached_pixels));
                }
                continue;
            }
            if ((!Playing && !zc255_death_menu_active()) || Quit || modal_depth || (GameFlags & (GAMEFLAG_SCRIPTMENU_ACTIVE | GAMEFLAG_F6SCRIPT_ACTIVE))) {
                shared->result=0; shared->state_size=0; continue;
            }
            if (!canonical) { capture_pending=true; return true; }
            capture_response();
        } else if (shared->command == ZC255_SET_STATE) {
            if (death_depth || (!Playing && !zc255_death_menu_active()) || Quit || modal_depth || (GameFlags & (GAMEFLAG_SCRIPTMENU_ACTIVE | GAMEFLAG_F6SCRIPT_ACTIVE))) { shared->result = 0; continue; }
            restore_pending = true;
            return true; // Continue to the main loop boundary before changing live objects.
        } else if (shared->command == ZC255_SAVE) {
            // Quest-controlled save points/death menus own progress commits.
            shared->result=1;
        } else {
            pending_policy_mask=shared->cheat_mask; pending_policy_values=shared->cheat_values; pending_policy_events=shared->cheat_events; policy_pending=true;
            if(canonical) apply_pending_policy();
            zc255_bridge_input();
            // A canonical wait occurs after the engine's normal input sampling.
            // Sample the newly requested frame before gameplay observes its keys.
            if (canonical) update_keys();
            return false;
        }
    }
}

void zc255_bridge_checkpoint() {
    if (!shared || !zc255_host::content_ready()) return;
    apply_pending_policy();
    for (;;) {
    if(shared->stop || zc255_host::unloading()) {
        Quit=shared->stop?qEXIT:qQUIT;skipcont=1;
        break;
    }
    while (restore_pending || capture_pending) {
        if (capture_pending) {
            capture_pending=false;
            capture_response();
            command_loop(true);
            continue;
        }
        restore_pending = false;
        shared->result = shared->state_size && shared->state_size <= ZC255_STATE_CAPACITY &&
            zc255_snapshot_import(shared->state_data, shared->state_size);
        if (shared->result) {
            cached_state.assign(shared->state_data, shared->state_data + shared->state_size);
            cached_frame = replay_get_frame();
            copy_frame_pixels(cached_pixels);
            shared->playing = Playing;
            shared->cheat_level = cheat;
            zc255_death_menu_trace("restore");
            shared->frame = replay_get_frame();
            shared->hero_x = int32_t(Hero.getX().getZLong());
            shared->hero_y = int32_t(Hero.getY().getZLong());
            shared->life = game->get_life();
            shared->max_life = game->get_maxlife();
            shared->rupees = game->get_rupies();
            shared->clock = getClock();
            shared->no_walls = toogam;
        }
        command_loop(true);
    }
    const int f = replay_get_frame();
    // Focused regression injection: runs the real HP-zero/OnDeath/animation
    // path once. Inert unless explicitly requested by the test environment.
    static bool death_test_fired;
    if(!death_test_fired && Playing && !zc255_death_menu_active()) {
        if(const char* at=std::getenv("ZC255_TEST_DEATH_AT")) {
            if(f>=std::atoi(at)) {
                death_test_fired=true;
                if(const char* rules=std::getenv("ZC255_TEST_DEATH_RULES")) {
                    if(!std::strcmp(rules,"no-save")) set_qr(qr_NOSAVE,true);
                    else if(!std::strcmp(rules,"no-continue")) set_qr(qr_NOCONTINUE,true);
                    else if(!std::strcmp(rules,"instant-continue")) {set_qr(qr_INSTANT_RESPAWN,false);set_qr(qr_INSTANT_CONTINUE,true);}
                }
                fprintf(stderr,"death regression: quest version=%x\n",FFCore.getQuestHeaderInfo(vZelda));
                game->set_life(0);
            }
        }
    }
    zc255_frame_conversion_selftest();
    zc255_definitions_selftest();
    zc255_structure_selftest();
    zc255_collision_selftest();
    zc255_mblock_selftest();
    zc255_screen_cache_selftest();
    static bool slopes_test_done;
    if (!slopes_test_done && std::getenv("ZC255_SLOPES_TEST")) {
        slopes_test_done=true;
        bool passed=snapshot_collision_state::semantic_selftest();
        if(FILE* out=fopen(std::getenv("ZC255_SLOPES_TEST"),"w")) {
            fprintf(out,"{\"passed\":%s,\"nonempty_full_engine_restore\":%s}\n",passed?"true":"false",
                snapshot_collision_state::full_engine_selftest_passed()?"true":"false"); fclose(out);
        }
    }
    static bool ui_test_done;
    if (!ui_test_done && std::getenv("ZC255_UI_TEST")) {
        ui_test_done = true;
        std::string reason;
        bool passed = snapshot_ui::semantic_selftest(reason);
        FILE* report = fopen(std::getenv("ZC255_UI_TEST"),"w");
        if (report) { fprintf(report,"{\"passed\":%s,\"reason\":\"%s\"}\n",passed?"true":"false",reason.c_str()); fclose(report); }
    }
    static bool inventory_test_done;
    if (!inventory_test_done && f >= 30 && std::getenv("ZC255_INVENTORY_TEST")) {
        inventory_test_done = true;
        zc255_inventory_selftest();
    }
    static bool resources_test_done;
    if (!resources_test_done && f >= 30 && std::getenv("ZC255_RESOURCES_TEST")) {
        resources_test_done = true;
        zc255_resources_selftest();
    }
    static bool audio_test_done;
    if (!audio_test_done && f >= 160 && std::getenv("ZC255_AUDIO_TEST")) {
        sfx(WAV_SECRET);
        fprintf(stderr,"bridge effect test: voices=%d length=%d rate=%d\n",sfx_count(),sfx_get_length(WAV_SECRET),sfx_get_default_freq(WAV_SECRET));
        audio_test_done = true;
    }
    // No forced periodic progress save. Keep the legacy counter in snapshots
    // for schema stability; native quest saves still use saves_write normally.
    // Export only on GET at a complete checkpoint; gameplay need not serialize every frame.
    if (zc255_death_menu_active()) zc255_death_menu_step();
    else if (zc255_map_active()) zc255_map_step();
    else if (zc255_ui_active()) zc255_ui_step();
    else break;
    }
}

void zc255_bridge_frame() {
    if (!shared || !zc255_host::content_ready()) return;
    if (restore_pending || capture_pending) return;
    copy_frame_pixels(shared->pixels);
    shared->width = ZC255_WIDTH;
    shared->height = ZC255_HEIGHT;
    shared->frame = rewind_is_previewing() ? rewind_preview_frame() : replay_get_frame();
    shared->hero_x = int32_t(Hero.getX().getZLong());
    shared->hero_y = int32_t(Hero.getY().getZLong());
    shared->cheat_level = cheat;
    shared->playing = Playing;
    shared->life = game ? game->get_life() : 0;
    shared->max_life = game ? game->get_maxlife() : 0;
    shared->clock = getClock();
    shared->no_walls = toogam;
    shared->rupees = game ? game->get_rupies() : 0;
    ++shared->sequence;
    zc255_audio_read();
    command_loop();
}

void zc255_bridge_reset_content() { zc255_map_reset(); zc255_death_menu_reset(); policy_pending=false; pending_policy_mask=pending_policy_values=pending_policy_events=0; previous_buttons=0; last_auto_save=-1; cached_frame=-1; cached_state.clear(); restore_pending=capture_pending=false; if(shared) { shared->buttons=0; shared->state_size=0; shared->audio_frames=0; } snapshot_forget_history(); snapshot_definitions::clear_definition_decode_cache(); snapshot_definitions::clear_prepared_state_cache(); zc255_audio_content_reset(); }

// Worker joined; audio callbacks quiesced. Never dereference old shared.
void zc255_bridge_host_release() {
    zc255_map_reset(); zc255_death_menu_reset();
    shared=nullptr; ready=advance=nullptr;
    previous_buttons=0; last_auto_save=-1; cached_frame=-1;
    restore_pending=capture_pending=false;
    std::vector<uint8_t>().swap(cached_state);
    snapshot_forget_history();
    snapshot_definitions::clear_definition_decode_cache();
    snapshot_definitions::clear_prepared_state_cache();
}

// INACTIVE worker-side proposal. Invoke ONCE when consuming a RUN command,
// at the canonical input boundary, never on every update_keys invocation.
// `policy` is an owned command value, copied through the synchronized host
// request while the worker is waiting. Do not put it in serialized key arrays.
void zc255_apply_frontend_cheat_policy(uint32_t mask,uint32_t values,uint32_t events) {
    cheat=maxcheat=(mask&0x80000000u)?0:4;
    if(!game || !Playing) return;
    cheats_apply_frontend_policy(mask,values,events);
}

static void apply_pending_policy() { if(!policy_pending || !game || !Playing || Quit) return; zc255_apply_frontend_cheat_policy(pending_policy_mask,pending_policy_values,pending_policy_events); policy_pending=false; pending_policy_events=0; if(shared) shared->cheat_policy_applied=1; }

