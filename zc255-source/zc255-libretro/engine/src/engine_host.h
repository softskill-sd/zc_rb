#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "bridge_protocol.h"
namespace zc255_host {
struct Options {
    std::string asset_root,config_root,save_root;
    // Chosen before engine startup and immutable while it runs. Normal hosted
    // gameplay uses the proven software clock and RetroArch-owned history.
    // Internal comparison harnesses may explicitly set either value false.
    bool frame_audio=true;
    bool frontend_rewind_only=true;
};
struct Result { bool ok=false; std::string error; explicit operator bool() const { return ok; } };
struct Frame { uint32_t width=0,height=0; int32_t frame=0; std::vector<uint32_t> pixels; std::vector<int16_t> audio; };
Result initialize(const Options&);
Result load(const std::string& absolute_quest,const std::string& absolute_save);
struct CheatPolicy { uint32_t mask=0,values=0,events=0; };
Result run(uint32_t buttons,Frame&,const CheatPolicy& policy = {});
Result save();
// Captures into the existing exchange buffer; avoids an unused frontend copy.
// As with exchange(), callers read it before issuing their next engine command.
Result export_state_shared();
Result export_state(std::vector<uint8_t>&);
Result import_state(const uint8_t*,size_t);
Result unload();
Result shutdown();
bool active();
bool frame_audio_requested();
bool frontend_rewind_only();
const std::string& asset_root();
const std::string& save_root();
std::string asset_path(const char*);
std::string config_path(const char*);
const char* save_path();
[[noreturn]] void fatal(const char*);
// Internal adapter hooks, not a frontend API.
ZC255Shared* exchange();
void* ready_event();
void* advance_event();
bool await_content();
void load_complete(bool,const char* error=nullptr);
void request_unload();
bool unloading();
bool content_ready();
}