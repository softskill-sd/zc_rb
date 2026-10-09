#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "libretro.h"
#include "bridge_protocol.h"
#include "engine_host.h"
#include "frontend_policy.h"
#include "materializer.h"
#include "progress_clear.h"
#define XXH_INLINE_ALL
#include "xxhash.h"

static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_audio_sample_t audio;
static retro_audio_sample_batch_t audio_batch;
static retro_input_poll_t poll;
static retro_input_state_t input;
static retro_log_printf_t logger;
static bool engine_initialized;
static zc255_host::Frame host_frame;
static std::string path_utf8(const std::filesystem::path& path) {
    const auto bytes=path.u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());
}
static ZC255Shared* shared;
static ZC255Shared diagnostics{};
static std::string content;
static unsigned pending_cheats; // restored one-shot refill event only
static zc255_frontend_policy::Policy frontend_policy;
static void refresh_input_descriptors();
static bool failed;
static int16_t silence[800 * 2];
static uint64_t quest_identity;
static std::filesystem::path progress_save_path;
static ProgressClearPrompt progress_clear;

static constexpr uint64_t STATE_MAGIC = 0x345453533535435AULL;
struct StateHeader { uint64_t magic, quest; uint32_t bytes, reserved; uint64_t checksum; };
static uint64_t hash_bytes(const uint8_t* bytes, size_t size) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ULL; }
    return hash;
}
static void report_error(const char* operation,const zc255_host::Result& result) {
    if(logger) logger(RETRO_LOG_ERROR,"ZC255 in-process %s failed: %s\n",operation,result.error.c_str());
    else fprintf(stderr,"ZC255 in-process %s failed: %s\n",operation,result.error.c_str());
}
static void refresh_diagnostics() {
    if(shared) std::memcpy(&diagnostics,shared,offsetof(ZC255Shared,pixels));
}
static void acknowledge_policy() { if(shared && shared->cheat_policy_applied) { frontend_policy.committed_run(); pending_cheats=0; shared->cheat_policy_applied=0; } }
static bool bridge_command(uint32_t command) {
    if(!shared || failed) return false;
    zc255_host::Result result;
    if(command==ZC255_RUN) result=zc255_host::run(shared->buttons,host_frame,{shared->cheat_mask,shared->cheat_values,shared->cheat_events});
    else if(command==ZC255_GET_STATE) result=zc255_host::export_state_shared();
    else if(command==ZC255_SET_STATE) result=zc255_host::import_state(shared->state_data,shared->state_size);
    else if(command==ZC255_SAVE) result=zc255_host::save();
    else return false;
    refresh_diagnostics();
    if(!result) { report_error("command",result); if(command==ZC255_RUN) failed=true; }
    if(result.ok) acknowledge_policy();
    return result.ok;
}

static std::filesystem::path core_directory() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&core_directory), &module);
    wchar_t path[32768];
    GetModuleFileNameW(module, path, 32768);
    return std::filesystem::path(path).parent_path();
}

RETRO_API void retro_set_environment(retro_environment_t cb) { environment=cb; frontend_policy.register_options(cb); frontend_policy.register_controllers(cb); frontend_policy.read_options(cb); refresh_input_descriptors(); }
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { audio = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { poll = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input = cb; }
RETRO_API unsigned retro_api_version() { return RETRO_API_VERSION; }
RETRO_API void retro_init() {
    retro_log_callback cb{};
    if (environment && environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &cb)) logger = cb.log;
    bool no_game = false;
    if (environment) environment(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}
RETRO_API void retro_get_system_info(retro_system_info* info) {
    *info = {};
    info->library_name = "Zelda Classic 2.55 Prototype";
    info->library_version = "0.14 embedded gameplay core";
    info->valid_extensions = "qst";
    info->need_fullpath = true;
    info->block_extract = false;
}
RETRO_API void retro_get_system_av_info(retro_system_av_info* info) {
    *info = {};
    info->geometry = {256, 240, 256, 240, 4.0f / 3.0f};
    info->timing = {60.0, 44100.0};
}
RETRO_API void retro_set_controller_port_device(unsigned port,unsigned device) { frontend_policy.set_device(port,device); }

RETRO_API void retro_unload_game() {
    if(engine_initialized) { auto result=zc255_host::unload(); if(!result) { report_error("unload",result); failed=true; } }
    if(!failed) frontend_policy.content_unloaded();
    shared=nullptr; pending_cheats=0; content.clear(); host_frame={};
    progress_save_path.clear(); progress_clear.visible=false;
}
RETRO_API void retro_deinit() {
    retro_unload_game();
    if(engine_initialized) {
        auto result=zc255_host::shutdown();
        if(!result) {
            report_error("shutdown",result);
            failed=true;
            // Libretro has no unload veto. Keep worker code/data resident if its
            // shutdown failed; never terminate an in-process engine thread.
            HMODULE pinned=nullptr;
            const bool pinned_ok=GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&retro_deinit),&pinned);
            const char* message=pinned_ok?"ZC255 shutdown refused DLL release; module pinned. Restart the frontend before retrying.\n"
                :"ZC255 shutdown refused DLL release, but module pinning FAILED. Restart the frontend.\n";
            if(logger) logger(RETRO_LOG_ERROR,"%s",message); else fprintf(stderr,"%s",message);
        } else engine_initialized=false;
    }
}

RETRO_API bool retro_load_game(const retro_game_info* game) {
    retro_unload_game();
    if(failed) return false;
    if (!game || !game->path) return false;
    std::error_code path_error;
    auto quest = std::filesystem::absolute(std::filesystem::u8path(game->path), path_error);
    if (path_error || !std::filesystem::is_regular_file(quest, path_error)) return false;
    content = path_utf8(quest);
    std::ifstream quest_file(quest, std::ios::binary);
    std::vector<uint8_t> quest_bytes((std::istreambuf_iterator<char>(quest_file)), {});
    quest_identity = hash_bytes(quest_bytes.data(),quest_bytes.size());
    auto directory = core_directory();

    retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environment || !environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format)) return false;
    frontend_policy.read_options(environment); refresh_input_descriptors();
    auto save_directory=directory/"saves";
    const char* frontend_save_directory=nullptr;
    if(environment(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY,&frontend_save_directory) && frontend_save_directory && *frontend_save_directory)
        save_directory=std::filesystem::u8path(frontend_save_directory);
    save_directory=std::filesystem::absolute(save_directory,path_error);
    if(path_error) return false;
    std::filesystem::create_directories(save_directory,path_error);
    if(path_error) return false;
    auto config_directory=save_directory/"zc255-config";
    std::filesystem::create_directories(config_directory,path_error);
    if(path_error) return false;
    if(!engine_initialized) {
        std::filesystem::path embedded_root;
        std::string asset_error;
        if(!zc255_assets::prepare(save_directory,embedded_root,asset_error)) {
            report_error("embedded assets",{false,asset_error});
            content.clear();
            return false;
        }
        auto result=zc255_host::initialize({path_utf8(embedded_root),path_utf8(config_directory),path_utf8(save_directory)});
        // Failed bootstrap may still own a worker which deinit must shut down.
        engine_initialized=zc255_host::active();
        if(!result) { report_error("initialize",result); failed=true; return false; }
    }
    auto progress_save=save_directory/("zc255-"+std::to_string(quest_identity)+".sav");
    auto loaded=zc255_host::load(content,path_utf8(progress_save));
    if(!loaded) { report_error("load",loaded); return false; }
    progress_save_path=progress_save; progress_clear.reset(environment);
    shared=zc255_host::exchange(); failed=false; refresh_diagnostics();
    // RetroArch asks for an initial serialized state when it creates its rewind buffer.
    for (unsigned i = 0; i < 1200; ++i) {
        if (bridge_command(ZC255_GET_STATE)) return true;
        // A quest using unsupported external writable streams can still play.
        // The frontend will receive an explicit serialization failure.
        if (shared->playing && shared->state_frame == -2) return true;
        if(!bridge_command(ZC255_RUN)) { retro_unload_game(); return false; }
    }
    retro_unload_game();
    return false;
}
RETRO_API bool retro_load_game_special(unsigned, const retro_game_info*, size_t) { return false; }
RETRO_API void retro_reset() {
    std::string path = content;
    retro_game_info game{path.c_str(),nullptr,0,nullptr};
    retro_load_game(&game);
}
static void refresh_input_descriptors() {
    if(!environment) return;
    static retro_input_descriptor d[17];
    retro_input_descriptor next[]={
        {0,RETRO_DEVICE_JOYPAD,0,4,"Up"},{0,RETRO_DEVICE_JOYPAD,0,5,"Down"},
        {0,RETRO_DEVICE_JOYPAD,0,6,"Left"},{0,RETRO_DEVICE_JOYPAD,0,7,"Right"},
        {0,RETRO_DEVICE_JOYPAD,0,8,frontend_policy.settings.swap_ab?"B item":"A item"},
        {0,RETRO_DEVICE_JOYPAD,0,0,frontend_policy.settings.swap_ab?"A item":"B item"},
        {0,RETRO_DEVICE_JOYPAD,0,9,frontend_policy.settings.swap_xy?"Extra item 2":"Extra item 1"},
        {0,RETRO_DEVICE_JOYPAD,0,1,frontend_policy.settings.swap_xy?"Extra item 1":"Extra item 2"},
        {0,RETRO_DEVICE_JOYPAD,0,10,"L action"},{0,RETRO_DEVICE_JOYPAD,0,11,"R action"},
        {0,RETRO_DEVICE_JOYPAD,0,3,"Subscreen"},{0,RETRO_DEVICE_JOYPAD,0,2,"Map"},
        {0,RETRO_DEVICE_JOYPAD,0,12,"Reserved (use RetroArch rewind hotkey)"},
        {0,RETRO_DEVICE_JOYPAD,0,13,(frontend_policy.settings.shortcuts && frontend_policy.settings.cheats_enabled)?"Cheat: refill selected resources":"Unused"},
        {0,RETRO_DEVICE_JOYPAD,0,14,"Unused"},
        {0,RETRO_DEVICE_JOYPAD,0,15,"Unused"},
        {0,0,0,0,nullptr}
    };
    static_assert(sizeof(d)==sizeof(next));
    std::memcpy(d,next,sizeof(d));
    environment(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS,d);
}

static void progress_message(const char* text) {
    retro_message message{text,300};if(environment)environment(RETRO_ENVIRONMENT_SET_MESSAGE,&message);
    if(logger)logger(RETRO_LOG_INFO,"%s\n",text);
}
static void clear_progress_and_restart() {
    // Unload first so the old live slot cannot rewrite the archived file.
    const std::string quest=content;const auto path=progress_save_path;
    if(quest.empty() || path.empty())return;
    retro_unload_game();
    if(failed){progress_message("Could not clear progress: quest unload failed.");return;}
    std::error_code error;bool existed=std::filesystem::exists(path,error);
    if(!error && existed) {
        // Rename only this quest's exact active save; never scan/delete folders.
        auto backup=path;backup+=std::string(".cleared-")+std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        std::filesystem::rename(path,backup,error);
    }
    const bool cleared=!error;
    retro_game_info game{quest.c_str(),nullptr,0,nullptr};
    bool restarted=retro_load_game(&game);
    progress_message(!cleared?"Progress was not cleared: could not archive its save."
        :!restarted?"Progress cleared (backup kept). Reload the quest to start again."
        :"Quest progress cleared; restarted. Previous save kept as a backup.");
}
RETRO_API void retro_run() {
    acknowledge_policy();
    bool updated=false;
    if(environment && environment(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE,&updated) && updated) {
        frontend_policy.read_options(environment); refresh_input_descriptors();
        if(progress_clear.changed(environment) && shared && !failed) {
            progress_clear.open(shared->pixels);progress_clear.rearm(environment);
        }
    }
    if (poll) poll();
    if (!shared || failed) { if (video) video(nullptr,256,240,256*4); return; }
    if(progress_clear.visible) {
        int decision=progress_clear.update(input);
        if(decision>0)clear_progress_and_restart();
        else if(decision<0)progress_message("Clear progress cancelled.");
        if(video)video(progress_clear.visible?progress_clear.pixels:(shared?shared->pixels:nullptr),256,240,256*4);
        if(audio_batch)audio_batch(silence,735);
        else if(audio)for(unsigned i=0;i<735;++i)audio(0,0);
        return;
    }
    shared->buttons=frontend_policy.buttons(input);
    auto policy=frontend_policy.requested();
    shared->cheat_mask=policy.mask; shared->cheat_values=policy.values;
    shared->cheat_events=policy.events | (pending_cheats?4u:0u);
    bool ran=bridge_command(ZC255_RUN);
    if (ran && video) video(shared->pixels,256,240,256*4);
    else if (video) video(nullptr,256,240,256*4);
    if (audio_batch) audio_batch(shared->audio,shared->audio_frames);
    else if (audio) for (unsigned i = 0; i < shared->audio_frames; ++i) audio(shared->audio[i*2],shared->audio[i*2+1]);
}
RETRO_API unsigned retro_get_region() { return RETRO_REGION_NTSC; }
RETRO_API size_t retro_serialize_size() { return sizeof(StateHeader) + sizeof(ZC255Shared::pixels) + ZC255_STATE_CAPACITY; }
RETRO_API bool retro_serialize(void* data,size_t size) {
    if (!data || size < retro_serialize_size()) return false;
    static const char* profile_path=std::getenv("ZC255_PROFILE_FRONTEND");
    const bool profiling=profile_path && *profile_path;
    std::chrono::steady_clock::time_point begin,after_bridge,after_zero,after_hash,after_copy;
    if(profiling) begin=std::chrono::steady_clock::now();
    if(!bridge_command(ZC255_GET_STATE)) return false;
    if(profiling) after_bridge=std::chrono::steady_clock::now();
    const size_t used=sizeof(StateHeader)+sizeof(shared->pixels)+shared->state_size;
    if(!shared->state_size || shared->state_size>ZC255_STATE_CAPACITY) return false;
    // Every used byte is written below. Only clear the unused transport tail,
    // retaining deterministic complete states without writing the payload twice.
    std::memset(static_cast<uint8_t*>(data)+used,0,retro_serialize_size()-used);
    if(profiling) after_zero=std::chrono::steady_clock::now();
    StateHeader header{STATE_MAGIC,quest_identity,shared->state_size,(pending_cheats || (frontend_policy.requested().events&4))?0x2000u:0u,XXH64(shared->state_data,shared->state_size,0)};
    if(profiling) after_hash=std::chrono::steady_clock::now();
    auto bytes = static_cast<uint8_t*>(data);
    std::memcpy(bytes,&header,sizeof(header));
    std::memcpy(bytes + sizeof(header),shared->state_pixels,sizeof(shared->pixels));
    std::memcpy(bytes + sizeof(header) + sizeof(shared->pixels),shared->state_data,shared->state_size);
    if(profiling) {
        after_copy=std::chrono::steady_clock::now();
        auto ms=[](auto a,auto b) { return std::chrono::duration<double,std::milli>(b-a).count(); };
        if(FILE* report=std::fopen(profile_path,"a")) {
            std::fprintf(report,"{\"total_ms\":%.6f,\"bridge_ms\":%.6f,\"zero_ms\":%.6f,\"checksum_ms\":%.6f,\"copy_ms\":%.6f}\n",
                ms(begin,after_copy),ms(begin,after_bridge),ms(after_bridge,after_zero),ms(after_zero,after_hash),ms(after_hash,after_copy));
            std::fclose(report);
        }
    }
    return true;
}
RETRO_API bool retro_unserialize(const void* data,size_t size) {
    if (!data || !shared || size < sizeof(StateHeader) + sizeof(shared->pixels)) return false;
    StateHeader header;
    std::memcpy(&header,data,sizeof(header));
    if (header.magic != STATE_MAGIC || header.quest != quest_identity || (header.reserved & ~0x2000u) || !header.bytes || header.bytes > ZC255_STATE_CAPACITY ||
        size < sizeof(header) + sizeof(shared->pixels) + header.bytes) return false;
    const auto bytes = static_cast<const uint8_t*>(data);
    const auto blob = bytes + sizeof(header) + sizeof(shared->pixels);
    if (header.checksum != XXH64(blob,header.bytes,0)) return false;
    std::memcpy(shared->state_data,blob,header.bytes);
    std::memcpy(shared->state_pixels,bytes + sizeof(header),sizeof(shared->pixels));
    shared->state_size = header.bytes;
    if (!bridge_command(ZC255_SET_STATE)) return false;
    progress_clear.visible=false;
    frontend_policy.restore_pending_events(); pending_cheats = header.reserved;
    std::memcpy(shared->pixels,bytes + sizeof(header),sizeof(shared->pixels));
    if (video) video(shared->pixels,256,240,256*4);
    if (logger) logger(RETRO_LOG_DEBUG,"ZC255 restored serialized snapshot at frame %d.\n",shared->frame);
    return true;
}
RETRO_API void retro_cheat_reset() { acknowledge_policy(); pending_cheats=0; frontend_policy.reset_cheats(); }
RETRO_API void retro_cheat_set(unsigned index,bool enabled,const char* code) {
    acknowledge_policy();
    if(!enabled || !code || !*code) pending_cheats=0;
    try {
        if(!frontend_policy.set_cheat(index,enabled,code) && logger) logger(RETRO_LOG_WARN,"ZC255 unsupported cheat code or registry limit at index %u.\n",index);
    } catch(...) { if(logger) logger(RETRO_LOG_ERROR,"ZC255 cheat registry allocation failed at index %u.\n",index); }
}
// Read-only diagnostic copy, useful for the prototype's automated checks.
RETRO_API void* retro_get_memory_data(unsigned id) { return id == RETRO_MEMORY_SYSTEM_RAM ? &diagnostics : nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned id) { return id == RETRO_MEMORY_SYSTEM_RAM ? offsetof(ZC255Shared,pixels) : 0; }
