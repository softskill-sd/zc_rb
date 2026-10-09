// Optional isolated test TU: worker-only, empty canonical boundary before content.
#include "sprite.h"
#include <memory>
#include <stdexcept>
#include <exception>
solid_object* getCurObject();
namespace {
enum class Stage { Animate,Script,Post };
struct Probe final : sprite {
    sprite_list& owner; int& destroyed; bool& violation;
    Stage stage; bool remove_self,throws,inside=false; int calls=0;
    Probe(sprite_list& l,int& d,bool& bad,Stage s,bool remove,bool fail)
        :owner(l),destroyed(d),violation(bad),stage(s),remove_self(remove),throws(fail) { canfreeze=false; }
    ~Probe() override { if(inside || std::uncaught_exceptions()!=0) violation=true; ++destroyed; }
    void step() {
        struct Guard { bool& inside; ~Guard() { inside=false; } } guard{inside};
        inside=true; ++calls;
        if(remove_self) owner.del(owner.find(this),true);
        if(throws) throw std::runtime_error("injected sprite failure");
    }
    bool animate(int32_t) override { if(stage==Stage::Animate) step(); return false; }
    void post_animate() override { if(stage==Stage::Post) step(); }
    int32_t run_script(int32_t) override { if(stage==Stage::Script) step(); return 0; }
};
void drain(sprite_list& list) {
    list.host_quiesce_iteration();
    if(auto p=list.host_take_deferred_sprite()) delete p;
    while(list.Count()) list.del(list.Count()-1,true,false);
    list.snapshot_replace(nullptr,0);
}
bool one(Stage stage,bool remove,bool throws) {
    int destroyed=0; bool violation=false; auto list=std::make_unique<sprite_list>();
    auto probe=std::make_unique<Probe>(*list,destroyed,violation,stage,remove,throws); auto raw=probe.get();
    if(!list->add(raw)) return false; probe.release();
    bool caught=false;
    try { if(stage==Stage::Script) list->run_script(0); else list->animate(); }
    catch(const std::runtime_error&) { caught=true; }
    bool okay=caught==throws && !violation;
    if(remove && throws) {
        okay=okay && list->Count()==0 && destroyed==0 && list->host_peek_deferred_sprite()==raw;
        list->host_quiesce_iteration();
        auto pending=list->host_take_deferred_sprite(); okay=okay && pending==raw;
        delete pending;
        okay=okay && list->host_take_deferred_sprite()==nullptr && destroyed==1;
    } else if(remove) {
        // All normal callbacks, including post_animate, reach a capture-safe
        // boundary without omitted ownership or any pending sprite.
        okay=okay && list->Count()==0 && destroyed==1 && !list->host_peek_deferred_sprite();
    } else {
        okay=okay && list->Count()==1 && destroyed==0 && !list->host_peek_deferred_sprite();
        okay=list->del(0,true) && okay; // Must delete now: iterator restored.
        okay=okay && list->Count()==0 && destroyed==1 && !list->host_peek_deferred_sprite();
    }
    drain(*list); drain(*list);
    return okay && destroyed==1 && !violation && !list->host_peek_deferred_sprite();
}
bool post_survivor() {
    int retired=0,survivor_destroyed=0; bool violation=false;
    auto list=std::make_unique<sprite_list>();
    auto first=std::make_unique<Probe>(*list,retired,violation,Stage::Post,true,false);
    auto second=std::make_unique<Probe>(*list,survivor_destroyed,violation,Stage::Post,false,false);
    auto survivor=second.get();
    if(!list->add(first.get())) return false; first.release();
    if(!list->add(second.get())) { drain(*list); return false; } second.release();
    list->animate();
    bool okay=retired==1 && survivor_destroyed==0 && list->Count()==1
        && list->spr(0)==survivor && survivor->calls==1 && !list->host_peek_deferred_sprite();
    list->animate(); // Index bookkeeping must keep the survivor on later frames.
    okay=okay && survivor->calls==2 && retired==1 && !list->host_peek_deferred_sprite();
    drain(*list); drain(*list);
    return okay && retired==1 && survivor_destroyed==1 && !violation;
}
}
bool zc255_sprite_exception_selftest() {
    if(getCurObject()!=nullptr) return false; // Never clobber a borrowed context.
    auto old_uid=sprite::getUIDCounter(); bool okay=true;
    for(Stage stage:{Stage::Animate,Stage::Script,Stage::Post})
        for(bool remove:{false,true}) for(bool throws:{false,true})
            okay=one(stage,remove,throws) && okay;
    okay=post_survivor() && okay;
    sprite::setUIDCounter(old_uid); return okay;
}
