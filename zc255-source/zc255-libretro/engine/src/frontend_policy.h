#pragma once
// Inactive frontend proposal: include in the libretro frontend TU only.
#include "libretro.h"
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <cctype>
namespace zc255_frontend_policy {
enum Effect : uint32_t { Clock=1, Walls=2, Refill=4 };
// Configuration, deliberately separate from emulated state/button edges.
struct CheatPolicy { uint32_t mask=0,values=0,events=0; };
struct Entry { Effect effect=Refill; bool enabled=false,pending=false; };
constexpr uint32_t CheatsDisabled=0x80000000u, RefillSelectionMask=0x1f00u;
struct Settings { bool swap_ab=false,swap_xy=false,shortcuts=true,analog=false,cheats_enabled=true,invulnerability=false,walls=false; uint32_t refills=0; };
class Policy {
    std::map<unsigned,Entry> cheats;
    uint32_t release_mask=0;
    uint32_t option_owned_mask=0;
    unsigned device=RETRO_DEVICE_JOYPAD;
    // Native no-walls also grants invulnerability. Own both desired values so
    // disabling/deleting that entry removes its side effect unless a separate
    // enabled Clock entry still requests it.
    static uint32_t bit(Effect effect) { return effect==Refill?0:effect==Walls?3:1; }
    static bool parse(const char* code,Effect& effect) {
        if(!code) return false;
        std::string value(code);
        for(char& c:value) c=char(std::tolower(static_cast<unsigned char>(c)));
        if(value=="clock" || value=="invulnerability") { effect=Clock; return true; }
        if(value=="walls" || value=="no_walls") { effect=Walls; return true; }
        if(value=="refill") { effect=Refill; return true; }
        return false;
    }
public:
    Settings settings;
    static void register_controllers(retro_environment_t environment) {
        if(!environment) return;
        static const retro_controller_description types[]={{"RetroPad",RETRO_DEVICE_JOYPAD}};
        static const retro_controller_info ports[]={{types,1},{nullptr,0}};
        environment(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO,const_cast<retro_controller_info*>(ports));
    }
    static void register_options(retro_environment_t environment) {
        if(!environment) return;
        // The legacy API remains valid and works with old and new frontends.
        static const retro_variable options[]={
            {"zc255_ab_layout","A/B item buttons; normal|swapped"},
            {"zc255_xy_layout","Extra item buttons; normal|swapped"},
            {"zc255_cheat_shortcuts","Controller cheat shortcuts; enabled|disabled"},
            {"zc255_cheats_enabled","Allow cheats (master switch); enabled|disabled"},
            {"zc255_invulnerability","Cheat: invulnerability; disabled|enabled"},
            {"zc255_walk_through_walls","Cheat: walk through walls (also invulnerable); disabled|enabled"},
            {"zc255_refill_life","R2 refill: hearts; disabled|enabled"},
            {"zc255_refill_magic","R2 refill: magic; disabled|enabled"},
            {"zc255_refill_rupees","R2 refill: rupees; disabled|enabled"},
            {"zc255_refill_bombs","R2 refill: bombs (including super bombs); disabled|enabled"},
            {"zc255_refill_arrows","R2 refill: arrows; disabled|enabled"},
            {"zc255_analog_dpad","Left analog stick movement; disabled|enabled"},
            {"zc255_clear_progress","Clear this quest's saved progress (A confirms / B cancels); keep|request clear"},
            {nullptr,nullptr}
        };
        environment(RETRO_ENVIRONMENT_SET_VARIABLES,const_cast<retro_variable*>(options));
    }
    void read_options(retro_environment_t environment) {
        settings={}; if(!environment) return;
        auto is=[&](const char* key,const char* expected) {
            retro_variable v{key,nullptr};
            return environment(RETRO_ENVIRONMENT_GET_VARIABLE,&v) && v.value && std::strcmp(v.value,expected)==0;
        };
        settings.swap_ab=is("zc255_ab_layout","swapped"); settings.swap_xy=is("zc255_xy_layout","swapped");
        settings.shortcuts=!is("zc255_cheat_shortcuts","disabled"); settings.analog=is("zc255_analog_dpad","enabled");
        settings.cheats_enabled=!is("zc255_cheats_enabled","disabled");
        settings.invulnerability=is("zc255_invulnerability","enabled");
        settings.walls=is("zc255_walk_through_walls","enabled");
        const char* refill_keys[]={"zc255_refill_life","zc255_refill_magic","zc255_refill_rupees","zc255_refill_bombs","zc255_refill_arrows"};
        for(unsigned i=0;i<5;++i) if(is(refill_keys[i],"enabled")) settings.refills|=1u<<(8+i);
        // Only own effects requested by this menu; an untouched OFF default
        // must not remove invulnerability from an ordinary in-game clock.
        if(settings.invulnerability) option_owned_mask|=1;
        if(settings.walls) option_owned_mask|=3;
    }
    void set_device(unsigned port,unsigned type) { if(port==0) device=type&RETRO_DEVICE_MASK; }
    uint32_t buttons(retro_input_state_t input) const {
        if(!input || device!=RETRO_DEVICE_JOYPAD) return 0;
        uint32_t value=0;
        for(unsigned id=0;id<16;++id) if(input(0,RETRO_DEVICE_JOYPAD,0,id)) value|=1u<<id;
        // Native preview must not compete with RetroArch's rewind hotkey.
        value&=~(1u<<RETRO_DEVICE_ID_JOYPAD_L2);
        auto swap=[&](unsigned a,unsigned b) {
            bool av=(value&(1u<<a))!=0,bv=(value&(1u<<b))!=0;
            value&=~((1u<<a)|(1u<<b)); if(av)value|=1u<<b;if(bv)value|=1u<<a;
        };
        if(settings.swap_ab) swap(RETRO_DEVICE_ID_JOYPAD_A,RETRO_DEVICE_ID_JOYPAD_B);
        if(settings.swap_xy) swap(RETRO_DEVICE_ID_JOYPAD_X,RETRO_DEVICE_ID_JOYPAD_Y);
        value&=~((1u<<14)|(1u<<15)); // No added save or cheat actions on stick clicks.
        if(!settings.shortcuts || !settings.cheats_enabled) value&=~(1u<<13);
        if(settings.analog) {
            int x=input(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_X);
            int y=input(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_Y);
            // Explicit D-pad directions win on their axis; no opposing bit pair.
            if(!(value&((1u<<6)|(1u<<7)))) { if(x< -16384)value|=1u<<6;else if(x>16384)value|=1u<<7; }
            if(!(value&((1u<<4)|(1u<<5)))) { if(y< -16384)value|=1u<<4;else if(y>16384)value|=1u<<5; }
        }
        return value;
    }
    // Returns false for unsupported code or bounded registry exhaustion;
    // caller should log it. Updating one index never alters another index.
    bool set_cheat(unsigned index,bool enabled,const char* code) {
        auto found=cheats.find(index);
        if(!code || !*code) {
            if(found!=cheats.end()) { release_mask|=bit(found->second.effect); cheats.erase(found); }
            return true;
        }
        Effect effect;
        if(!parse(code,effect)) {
            if(found!=cheats.end()) { release_mask|=bit(found->second.effect); cheats.erase(found); }
            return false;
        }
        if(found==cheats.end() && cheats.size()>=4096) return false;
        bool rising=enabled && (found==cheats.end() || !found->second.enabled || found->second.effect!=effect);
        bool pending=found!=cheats.end() && found->second.effect==effect && found->second.pending;
        if(found!=cheats.end() && found->second.effect!=effect) release_mask|=bit(found->second.effect);
        cheats[index]={effect,enabled,enabled && effect==Refill && (rising || pending)};
        return true;
    }
    CheatPolicy requested() const {
        CheatPolicy p; p.mask=release_mask|option_owned_mask; p.values=settings.refills;
        if(settings.invulnerability)p.values|=1;
        if(settings.walls)p.values|=3;
        for(const auto& pair:cheats) { const auto& e=pair.second;
            p.mask|=bit(e.effect); if(e.enabled)p.values|=bit(e.effect);
            if(e.effect==Refill && e.pending)p.events|=Refill;
        }
        if(!settings.cheats_enabled) { p.mask|=CheatsDisabled; p.values&=RefillSelectionMask; p.events=0; }
        return p;
    }
    void committed_run() { release_mask=0; for(auto& pair:cheats) pair.second.pending=false; }
    void restore_pending_events() { for(auto& pair:cheats) pair.second.pending=false; }
    void reset_cheats() { for(const auto& pair:cheats) release_mask|=bit(pair.second.effect); cheats.clear(); }
    // Call only after content was unloaded: that world's effects no longer exist.
    void content_unloaded() { cheats.clear(); release_mask=0; option_owned_mask=0; }
};
}
