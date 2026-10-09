bool zc255_sprite_exception_selftest();
#include "engine_host.h"
#include "player_bridge.h"
#include "base/zc_alleg.h"
#include "zc/zelda.h"
#include "zc/saves.h"
#include "zc/replay.h"
#include "zc/hero.h"
#include "zc/zc_sys.h"
#include <allegro5/allegro_audio.h>
#include <algorithm>
#include <atomic>
#include <windows.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <cstring>

int zc255_engine_main(int,char**);
extern std::string testingqst_name;
extern bool zqtesting_mode,use_testingst_start,disable_save_to_disk;
extern int32_t skipcont;
extern ZC255Shared* zc255_audio_shared;
void saves_host_reset_slots();
void zc255_engine_host_release_save_quest();
void zc255_audio_host_release();
void zc255_bridge_host_release();
void zc255_zapp_host_release();
bool zc255_engine_host_release_world();
namespace zc255_host {
namespace {
constexpr uint32_t LOAD=100,UNLOAD=101;
struct HostFatal : std::runtime_error { using std::runtime_error::runtime_error; };
struct Host {
    Options options;
    std::unique_ptr<ZC255Shared> shared;
    HANDLE ready=nullptr,advance=nullptr,finished=nullptr;
    std::thread worker;
    std::mutex api,error_lock;
    std::string error,quest,progress;
    bool bootstrap_announced=false; // Worker-owned; failed load acknowledgements must not be repeated.
    std::atomic<bool> initialized{false},ever_started{false},content{false},leaving{false},timed_out{false},loaded{false},teardown_failed{false};
};
Host& host() { static Host* value=new Host; return *value; } // lifetime is explicit; no joining under loader-lock
void set_error(std::string error) { auto& h=host(); std::lock_guard<std::mutex> guard(h.error_lock); h.error=std::move(error); }
std::string error() { auto& h=host(); std::lock_guard<std::mutex> guard(h.error_lock); return h.error; }
Result wait_ready() {
    auto& h=host(); HANDLE events[]={h.ready,h.finished};
    DWORD wait=WaitForMultipleObjects(2,events,FALSE,30000);
    if(wait==WAIT_OBJECT_0) return {h.shared->result!=0,h.shared->result?std::string{}:(error().empty()?"Engine rejected operation":error())};
    if(wait==WAIT_OBJECT_0+1) return {false,error().empty()?"Engine stopped":error()};
    h.timed_out=true;
    return {false,wait==WAIT_TIMEOUT?"Engine operation timed out; worker remains owned":"Engine wait failed"};
}
Result send(uint32_t command) {
    auto& h=host(); if(!h.initialized || WaitForSingleObject(h.finished,0)==WAIT_OBJECT_0) return {false,"Engine is not running"};
    if(h.timed_out) return {false,"Prior command timed out; shutdown is required before further commands"};
    set_error({}); ResetEvent(h.ready); h.shared->command=command; h.shared->result=1; SetEvent(h.advance); return wait_ready();
}
bool absolute_path(const std::string& path) { return !path.empty() && std::filesystem::path(path).is_absolute(); }
Result unload_locked() {
    auto& h=host(); if(!h.content) return {true,{}};
    return send(UNLOAD);
}
}
bool active() { return host().initialized; }
bool frame_audio_requested() { return active() && host().options.frame_audio; }
bool frontend_rewind_only() { return active() && host().options.frontend_rewind_only; }
const std::string& asset_root() { return host().options.asset_root; }
const std::string& save_root() { return host().options.save_root; }
std::string asset_path(const char* name) {
    std::filesystem::path path(name?name:"");
    return path.is_absolute()?path.string():(std::filesystem::path(asset_root())/path).lexically_normal().string();
}
std::string config_path(const char* name) {
    std::filesystem::path path(name?name:"");
    return path.is_absolute()?path.string():(std::filesystem::path(host().options.config_root)/path).lexically_normal().string();
}
const char* save_path() { return host().progress.c_str(); }
[[noreturn]] void fatal(const char* message) { throw HostFatal(message?message:"Engine fatal error"); }
ZC255Shared* exchange() { return host().shared.get(); }
void* ready_event() { return host().ready; }
void* advance_event() { return host().advance; }
bool unloading() { return host().leaving; }
bool content_ready() { return host().content && host().loaded; }
void request_unload() { host().leaving=true; host().content=false; host().loaded=false; Quit=qQUIT; skipcont=1; }
Result initialize(const Options& options) {
    auto& h=host(); std::lock_guard<std::mutex> guard(h.api);
    if(h.initialized) return {true,{}};
    if(h.ever_started) return {false,"Final shutdown requires a fresh engine DLL; repeated quest loads are supported"};
    if(!absolute_path(options.asset_root) || !absolute_path(options.config_root) || !absolute_path(options.save_root))
        return {false,"Asset, configuration and save roots must be absolute"};
    std::error_code path_error;
    if(!std::filesystem::is_directory(options.asset_root,path_error) || path_error
        || !std::filesystem::is_directory(options.config_root,path_error) || path_error
        || !std::filesystem::is_directory(options.save_root,path_error) || path_error)
        return {false,"Configured directories must already exist and be readable"};
    h.options=options; h.shared=std::make_unique<ZC255Shared>(); h.shared->magic=ZC255_BRIDGE_MAGIC; h.shared->result=1;
    h.ready=CreateEventW(nullptr,FALSE,FALSE,nullptr); h.advance=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    h.finished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!h.ready || !h.advance || !h.finished) {
        if(h.ready) CloseHandle(h.ready); if(h.advance) CloseHandle(h.advance); if(h.finished) CloseHandle(h.finished);
        h.ready=h.advance=h.finished=nullptr; return {false,"Could not create host synchronization events"};
    }
    h.initialized=h.ever_started=true;
    try { h.worker=std::thread([] {
        auto& h=host();
        try {
            std::string executable=asset_path("zc255_libretro.dll");
            char headless[]="-headless",console[]="-no_console";
            char* arguments[]={executable.data(),headless,console,nullptr};
            if(!zc255_sprite_exception_selftest()) throw HostFatal("Sprite exception ownership selftest failed");
            fprintf(stderr,"sprite exception ownership selftest: PASS\n");
            int code=zc255_engine_main(3,arguments);
            if(code) throw HostFatal("Engine returned error "+std::to_string(code));
        } catch(...) {
            try { throw; } catch(const std::exception& failure) { set_error(failure.what()); }
            catch(...) { set_error("Unexpected engine failure"); }
            try {
                if(!zc255_engine_host_release_world()) throw HostFatal("Unsupported partial teardown; owners retained");
                // Remaining partial script/drawing acquisition ownership is not
                // proved yet: retain these owners/module after callback quiescence.
                throw HostFatal("Partial script/UI ownership requires later lifecycle gate");
            } catch(const std::exception& failure) {
                h.teardown_failed=true;
                set_error(error()+"; "+failure.what());
            } catch(...) { h.teardown_failed=true; }
        }
        register_trace_handler(nullptr); al_register_trace_handler(nullptr);
        h.content=false; SetEvent(h.finished);
    }); } catch(const std::exception& failure) {
        CloseHandle(h.ready); CloseHandle(h.advance); CloseHandle(h.finished);
        h.ready=h.advance=h.finished=nullptr; h.initialized=false; h.ever_started=false;
        return {false,failure.what()};
    }
    return wait_ready();
}
Result load(const std::string& quest,const std::string& progress) {
    auto& h=host(); std::lock_guard<std::mutex> guard(h.api);
    std::error_code path_error;
    if(!absolute_path(quest) || quest.size()>=2048 || !std::filesystem::is_regular_file(quest,path_error)
        || path_error || !absolute_path(progress))
        return {false,"Quest must exist, fit engine path buffers, and quest/save paths must be absolute"};
    auto unloaded=unload_locked(); if(!unloaded) return unloaded;
    h.quest=quest; h.progress=progress; return send(LOAD);
}
Result run(uint32_t buttons,Frame& frame,const CheatPolicy& policy) {
    auto& h=host(); std::lock_guard<std::mutex> guard(h.api);
    if(!h.content) return {false,"No content loaded"}; h.shared->buttons=buttons;
    if((policy.mask&~0x80000003u) || (policy.values&~0x1f03u) || (policy.events&~4u)) return {false,"Invalid cheat policy"};
    h.shared->cheat_mask=policy.mask; h.shared->cheat_values=policy.values; h.shared->cheat_events=policy.events; h.shared->cheat_policy_applied=0;
    auto result=send(ZC255_RUN); if(!result) return result;
    frame.width=h.shared->width; frame.height=h.shared->height; frame.frame=h.shared->frame;
    frame.pixels.assign(h.shared->pixels,h.shared->pixels+ZC255_WIDTH*ZC255_HEIGHT);
    size_t samples=std::min<size_t>(h.shared->audio_frames,4096)*2;
    frame.audio.assign(h.shared->audio,h.shared->audio+samples); return result;
}
Result save() { auto& h=host(); std::lock_guard<std::mutex> guard(h.api); return send(ZC255_SAVE); }
Result export_state_shared() {
    auto& h=host(); std::lock_guard<std::mutex> guard(h.api); auto result=send(ZC255_GET_STATE);
    if(result && (!h.shared->state_size || h.shared->state_size>ZC255_STATE_CAPACITY))
        return {false,"Invalid native state size"};
    return result;
}
Result export_state(std::vector<uint8_t>& data) {
    auto& h=host(); std::lock_guard<std::mutex> guard(h.api); auto result=send(ZC255_GET_STATE);
    if(result && h.shared->state_size<=ZC255_STATE_CAPACITY) data.assign(h.shared->state_data,h.shared->state_data+h.shared->state_size);
    else data.clear(); return result;
}
Result import_state(const uint8_t* data,size_t bytes) {
    auto& h=host(); std::lock_guard<std::mutex> guard(h.api);
    if(!h.initialized || !data || !bytes || bytes>ZC255_STATE_CAPACITY) return {false,"Invalid native state size"};
    if(data!=h.shared->state_data) std::memcpy(h.shared->state_data,data,bytes);
    h.shared->state_size=uint32_t(bytes); return send(ZC255_SET_STATE);
}
Result unload() { auto& h=host(); std::lock_guard<std::mutex> guard(h.api); return unload_locked(); }
Result shutdown() {
    auto& h=host(); std::lock_guard<std::mutex> guard(h.api); if(!h.initialized) return {true,{}};
    h.shared->stop=1; SetEvent(h.advance);
    if(WaitForSingleObject(h.finished,30000)!=WAIT_OBJECT_0) return {false,"Shutdown timed out; do not unload the DLL while its worker is alive"};
    if(h.worker.joinable()) h.worker.join();
    if(h.teardown_failed) return {false,error().empty()?"Teardown ownership was retained":error()};
    zc255_bridge_host_release(); zc255_zapp_host_release();
    h.bootstrap_announced=false; h.content=false; h.loaded=false;
    h.leaving=false; h.timed_out=false; h.quest.clear(); h.progress.clear(); set_error({});
    // ever_started is intentionally retained: same-DLL reinitialization refused.

    CloseHandle(h.ready); CloseHandle(h.advance); CloseHandle(h.finished); h.ready=h.advance=h.finished=nullptr;
    h.initialized=false; h.shared.reset(); return {true,{}};
}
bool await_content() {
    auto& h=host();
    if(h.content && !h.leaving) return true;
    if(h.leaving) { saves_unselect(); saves_host_reset_slots(); h.leaving=false; h.shared->result=1; SetEvent(h.ready); }
    else if(!h.bootstrap_announced) { h.bootstrap_announced=true; h.shared->result=1; SetEvent(h.ready); }
    for(;;) {
        if(WaitForSingleObject(h.advance,INFINITE)!=WAIT_OBJECT_0) fatal("Host wait failed");
        if(h.shared->stop) { Quit=qEXIT; return false; }
        if(h.shared->command!=LOAD) { h.shared->result=0; set_error("No quest loaded"); SetEvent(h.ready); continue; }
        try {
            testingqst_name=h.quest; zqtesting_mode=true; use_testingst_start=false;
            save_t* slot=nullptr;
            if(std::filesystem::exists(h.progress)) slot=saves_create_test_slot(nullptr,h.progress);
            else {
                auto game_data=std::make_unique<gamedata>(); game_data->header.qstpath=h.quest;
                game_data->set_name("Hero"); game_data->set_quest(0xff); game_data->set_continue_scrn(0xff); game_data->set_timevalid(1);
                slot=saves_create_test_slot(game_data.get(),h.progress);
                game_data.release(); // Transfer only after the save vector accepted the slot.
            }
            if(!slot) { saves_host_reset_slots(); set_error("Failed creating content save slot"); h.shared->result=0; SetEvent(h.ready); continue; }
            slot->path=h.progress; slot->write_to_disk=true; disable_save_to_disk=false;
            auto selected=saves_select(slot);
            if(!selected) { saves_unselect(); saves_host_reset_slots(); set_error(selected.error()); h.shared->result=0; SetEvent(h.ready); continue; }
            game->header.qstpath=h.quest; // Requested content wins over a saved installation path.
            zc255_bridge_reset_content(); h.content=true; h.loaded=false; Quit=0; return true;
        } catch(const HostFatal&) { throw; }
        catch(const std::exception& failure) { saves_unselect(); saves_host_reset_slots(); set_error(failure.what()); h.shared->result=0; SetEvent(h.ready); }
    }
}
void load_complete(bool success,const char* reason) {
    auto& h=host(); h.loaded=success; h.shared->result=success?1:0;
    if(!success) { h.content=false; saves_unselect(); saves_host_reset_slots(); set_error(reason?reason:"Quest initialization failed"); SetEvent(h.ready); }
}
}
