#pragma once
bool zc255_bridge_active();
// Native nested menus have a live C++ continuation and cannot be flattened
// into a gameplay snapshot. Refuse state commands promptly until they return.
void zc255_bridge_modal_enter();
void zc255_bridge_modal_leave();
struct ZC255BridgeModalScope {
    ZC255BridgeModalScope() { zc255_bridge_modal_enter(); }
    ~ZC255BridgeModalScope() { zc255_bridge_modal_leave(); }
    ZC255BridgeModalScope(const ZC255BridgeModalScope&) = delete;
    ZC255BridgeModalScope& operator=(const ZC255BridgeModalScope&) = delete;
};
void zc255_bridge_death_enter();
void zc255_bridge_death_leave();
struct ZC255BridgeDeathScope {
    ZC255BridgeDeathScope() { zc255_bridge_death_enter(); }
    ~ZC255BridgeDeathScope() { zc255_bridge_death_leave(); }
    ZC255BridgeDeathScope(const ZC255BridgeDeathScope&) = delete;
    ZC255BridgeDeathScope& operator=(const ZC255BridgeDeathScope&) = delete;
};
void zc255_bridge_init();
void zc255_bridge_frame();
void zc255_bridge_input();
void zc255_bridge_checkpoint();
void zc255_audio_prepare();
bool zc255_audio_has_frame_clock();
void zc255_audio_start();
void zc255_audio_read();
const char* zc255_bridge_save_path();
struct SaveStateBlob;
void zc255_bridge_state_write(SaveStateBlob&);
bool zc255_bridge_state_read(SaveStateBlob&, bool apply);

struct SaveStateBlob;
bool zc255_audio_state_write(SaveStateBlob&);
bool zc255_audio_state_read(SaveStateBlob&, bool apply);

void zc255_bridge_reset_content();
void zc255_audio_content_reset();
