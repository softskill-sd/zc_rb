#pragma once

#include "subscr.h"
#include "tiles.h"
#include "pal.h"
#include "zc/rendertarget.h"
#include "zc/render.h"
#include "zc/zelda.h"
#include "zc/savestate_serialize.h"
#include "snapshot_ui_runtime.h"
#include <memory>
#include <array>
extern int32_t palclk[3],palpos[3];
extern ZCSubscreen* subscr_anim;
extern SubscrTransition subscr_pg_transition;
extern BITMAP *subscr_pg_bmp1,*subscr_pg_bmp2,*subscr_pg_subbmp;
extern bool stayLit,usingdrypal;
extern RGB olddrypal;
extern byte drycolors[11];

// Value-based graphics/UI section. No PACKFILE, bitmap, or widget pointers enter the stream.
namespace snapshot_ui {
struct MemoryFile {
    std::vector<uint8_t> bytes;
    size_t pos = 0;
    bool error = false;
};
inline int close(void*) { return 0; }
inline int getc(void* u) { auto& m=*(MemoryFile*)u; if(m.pos>=m.bytes.size()){m.error=true;return EOF;} return m.bytes[m.pos++]; }
inline int ungetc(int c,void* u) {auto& m=*(MemoryFile*)u;if(!m.pos){m.error=true;return EOF;} --m.pos;return c;}
inline long read(void* p,long n,void* u) {auto& m=*(MemoryFile*)u;if(n<0){m.error=true;return 0;} size_t count=std::min(size_t(n),m.bytes.size()-m.pos); if(count)memcpy(p,m.bytes.data()+m.pos,count);m.pos+=count;if(count!=size_t(n))m.error=true;return long(count);}
inline int putc(int c,void* u) {auto& m=*(MemoryFile*)u;if(m.bytes.size()>=16*1024*1024){m.error=true;return EOF;}m.bytes.push_back(uint8_t(c));return c;}
inline long write(const void* p,long n,void* u) {auto& m=*(MemoryFile*)u;if(n<0||size_t(n)>16*1024*1024-m.bytes.size()){m.error=true;return 0;}auto* a=(const uint8_t*)p;m.bytes.insert(m.bytes.end(),a,a+n);return n;}
inline int seek(void* u,int n) {auto& m=*(MemoryFile*)u;if(n<0||size_t(n)>m.bytes.size()-m.pos){m.error=true;return -1;}m.pos+=n;return 0;}
inline int eof(void* u) {auto& m=*(MemoryFile*)u;return m.pos>=m.bytes.size();}
inline int error(void* u) {return ((MemoryFile*)u)->error;}
inline const PACKFILE_VTABLE vtable={close,getc,ungetc,read,putc,write,seek,eof,error};
template<class T> inline bool pack_value(SaveStateBlob& b,const T& value) {
    // ZC's p_put helpers inspect normal.flags even for a custom vtable. Allegro's
    // pack_fopen_vtable allocates only the prefix, so provide the complete struct
    // here with its mode flag rather than reading beyond that allocation.
    MemoryFile m;PACKFILE f{};f.vtable=&vtable;f.userdata=&m;f.is_normal_packfile=0;f.normal.flags=PACKFILE_FLAG_WRITE;
    int ret=value.write(&f);if(ret||m.error){fprintf(stderr,"UI value writer failed ret=%d error=%d bytes=%zu\n",ret,int(m.error),m.bytes.size());return false;}
    b.put(uint32_t(m.bytes.size()));if(!m.bytes.empty())b.put(m.bytes.data(),m.bytes.size());return true;
}
template<class T> inline bool unpack_value(SaveStateBlob& b,T& value) {
    uint32_t n=0;b.get(n);if(b.failed||n>16*1024*1024||n>b.data.size()-b.pos){b.failed=true;return false;}
    MemoryFile m;m.bytes.assign(b.data.begin()+b.pos,b.data.begin()+b.pos+n);b.pos+=n;
    PACKFILE f{};f.vtable=&vtable;f.userdata=&m;f.is_normal_packfile=0;f.normal.flags=0;
    int ret=value.read(&f,V_SUBSCREEN);return !ret&&!m.error&&m.pos==m.bytes.size();
}
template<class S> inline void selector(S& s,SubscrSelectorInfo& value) {
    s(value.x,value.y,value.w,value.h);for(auto& t:value.tileinfo)s(t.sw,t.sh,t.tile,t.cset,t.frames,t.speed,t.delay);
}
template<class S> inline void transition(S& s,SubscrTransition& value) {
    s(value.type,value.tr_sfx,value.flags);for(auto& arg:value.arg)s(arg);
}
inline void put_extras(SaveStateBlob& b,const ZCSubscreen& original) {
    auto& sub=const_cast<ZCSubscreen&>(original);StateWriter s{b};
    b.put_str(sub.name);
    s(sub.curpage,sub.sub_type,sub.btn_left,sub.btn_right,sub.script);selector(s,sub.selector_setting);transition(s,sub.trans_left);transition(s,sub.trans_right);
    s.pod_array(sub.def_btns,4);s.pod_array(sub.initd,8);
    for(auto& page:sub.pages)for(size_t i=0;i<page.size();++i){auto& w=*page[i];
        s(w.pos,w.pos_up,w.pos_down,w.pos_left,w.pos_right,w.generic_script,w.gen_script_btns,w.pg_btns,w.pg_mode,w.pg_targ);
        s.pod_array(w.generic_initd,8);b.put_str(w.override_text);b.put_str(w.label);selector(s,w.selector_override);transition(s,w.pg_trans);
    }
}
inline bool get_extras(SaveStateBlob& b,ZCSubscreen& sub) {
    StateReader s{b};b.get_str(sub.name);s(sub.curpage,sub.sub_type,sub.btn_left,sub.btn_right,sub.script);selector(s,sub.selector_setting);transition(s,sub.trans_left);transition(s,sub.trans_right);
    s.pod_array(sub.def_btns,4);s.pod_array(sub.initd,8);
    for(auto& page:sub.pages)for(size_t i=0;i<page.size();++i){auto& w=*page[i];
        s(w.pos,w.pos_up,w.pos_down,w.pos_left,w.pos_right,w.generic_script,w.gen_script_btns,w.pg_btns,w.pg_mode,w.pg_targ);
        s.pod_array(w.generic_initd,8);b.get_str(w.override_text);b.get_str(w.label);selector(s,w.selector_override);transition(s,w.pg_trans);
    }return !b.failed&&sub.sub_type<sstMAX;
}
inline bool put_subscreens(SaveStateBlob& b,const std::vector<ZCSubscreen>& list) {
    b.put(uint32_t(list.size()));
    for(const auto& sub:list) {
        if(sub.pages.size()>MAX_SUBSCR_PAGES||!pack_value(b,sub))return false;
        b.put(sub.curpage);b.put(sub.sub_type);b.put(sub.def_btns,sizeof(sub.def_btns));b.put(sub.initd,sizeof(sub.initd));
        // The quest writer reduces passive/overlay screens to one page. Capture each page again
        // independently to preserve runtime-created pages and cursors without that reduction.
        b.put(uint32_t(sub.pages.size()));for(const auto& page:sub.pages)if(!pack_value(b,page))return false;
        put_extras(b,sub);
    }return true;
}
inline bool get_subscreens(SaveStateBlob& b,std::vector<ZCSubscreen>& list) {
    uint32_t n=0;b.get(n);if(b.failed||n>65535)return false;list.resize(n);
    for(auto& sub:list) {
        if(!unpack_value(b,sub))return false;
        byte cur=0,type=0;b.get(cur);b.get(type);b.get(sub.def_btns,sizeof(sub.def_btns));b.get(sub.initd,sizeof(sub.initd));
        uint32_t pages=0;b.get(pages);if(b.failed||pages>MAX_SUBSCR_PAGES)return false;
        sub.pages.clear();
        for(uint32_t i=0;i<pages;++i){if(!sub.add_page(byte(i))||!unpack_value(b,sub.pages[i]))return false;}
        sub.curpage=cur;sub.sub_type=type;
        if(!get_extras(b,sub))return false;
    }return !b.failed;
}
inline int pointer_index(const std::vector<ZCSubscreen>& v,const ZCSubscreen* p) {
    if(!p)return -1;for(size_t i=0;i<v.size();++i)if(&v[i]==p)return int(i);return -2;
}
struct BitmapDelete {void operator()(BITMAP* p)const {if(p)destroy_bitmap(p);}};
using BitmapOwner=std::unique_ptr<BITMAP,BitmapDelete>;
struct BitmapValue {
    bool present=false;
    int32_t w=0,h=0,depth=0,cl=0,ct=0,cr=0,cb=0,clip=0;
    std::vector<uint8_t> bytepixels;
    std::vector<int32_t> pixels;
};
inline bool valid_bitmap_metadata(const BitmapValue& v) {
    return v.w>0&&v.h>0&&v.w<=4096&&v.h<=2048
        &&(v.depth==8||v.depth==15||v.depth==16||v.depth==24||v.depth==32)
        &&v.cl>=0&&v.ct>=0&&v.cr<=v.w&&v.cb<=v.h&&v.cl<=v.cr&&v.ct<=v.cb;
}
inline void put_byte_runs(SaveStateBlob& b,const std::vector<uint8_t>& pixels) {
    b.put(uint32_t(pixels.size()));
    for(size_t i=0;i<pixels.size();) {
        size_t end=i+1;while(end<pixels.size()&&pixels[end]==pixels[i])++end;
        b.put(uint32_t(end-i));b.put(pixels[i]);i=end;
    }
}
inline bool get_byte_runs(SaveStateBlob& b,std::vector<uint8_t>& pixels,size_t count) {
    uint32_t stored=0;b.get(stored);if(b.failed||stored!=count)return false;
    pixels.resize(count);
    for(size_t i=0;i<count;) {
        uint32_t run=0;uint8_t value=0;b.get(run);b.get(value);
        if(b.failed||!run||run>count-i)return false;
        memset(pixels.data()+i,value,run);i+=run;
    }return true;
}
inline bool get_bitmap(SaveStateBlob& b,BitmapValue& out) {
    uint8_t present=0;b.get(present);if(b.failed||present>1)return false;out.present=present!=0;if(!present){out.bytepixels.clear();out.pixels.clear();return true;}
    b.get(out.w);b.get(out.h);b.get(out.depth);
    if(b.failed||out.w<=0||out.h<=0||out.w>4096||out.h>2048||!(out.depth==8||out.depth==15||out.depth==16||out.depth==24||out.depth==32))return false;
    if(out.depth==8) {
        out.pixels.clear();if(!get_byte_runs(b,out.bytepixels,size_t(out.w)*out.h))return false;
    }else{
        out.bytepixels.clear();out.pixels.resize(size_t(out.w)*out.h);StateReader{b}.runs(out.pixels.data(),out.pixels.size());if(b.failed)return false;
        const uint32_t max_pixel=out.depth==32?UINT32_MAX:out.depth==24?0xFFFFFFu:out.depth==16?0xFFFFu:0x7FFFu;
        for(int32_t pixel:out.pixels)if(uint32_t(pixel)>max_pixel)return false;
    }
    b.get(out.cl);b.get(out.ct);b.get(out.cr);b.get(out.cb);b.get(out.clip);
    return !b.failed&&valid_bitmap_metadata(out);
}
inline bool same_bitmap_shape(BITMAP* bitmap,const BitmapValue& value) {
    return bitmap&&value.present&&bitmap->w==value.w&&bitmap->h==value.h&&bitmap_color_depth(bitmap)==value.depth;
}
inline bool same_bitmap(BITMAP* bitmap,const BitmapValue& value) {
    if(!bitmap||!value.present)return !bitmap&&!value.present;
    if(!same_bitmap_shape(bitmap,value)||bitmap->cl!=value.cl||bitmap->ct!=value.ct||bitmap->cr!=value.cr||bitmap->cb!=value.cb||bitmap->clip!=value.clip)return false;
    if(value.depth==8) {
        if(value.bytepixels.size()!=size_t(value.w)*value.h)return false;
        for(int y=0;y<value.h;++y)if(memcmp(bitmap->line[y],value.bytepixels.data()+size_t(y)*value.w,value.w))return false;
    }else{
        if(value.pixels.size()!=size_t(value.w)*value.h)return false;
        for(int y=0;y<value.h;++y)for(int x=0;x<value.w;++x)
            if(getpixel(bitmap,x,y)!=value.pixels[size_t(y)*value.w+x])return false;
    }return true;
}
inline void apply_bitmap_pixels(BITMAP* bitmap,const BitmapValue& value) {
    bitmap->clip=0;
    if(value.depth==8)for(int y=0;y<value.h;++y)memcpy(bitmap->line[y],value.bytepixels.data()+size_t(y)*value.w,value.w);
    else for(int y=0;y<value.h;++y)for(int x=0;x<value.w;++x)putpixel(bitmap,x,y,value.pixels[size_t(y)*value.w+x]);
    bitmap->cl=value.cl;bitmap->ct=value.ct;bitmap->cr=value.cr;bitmap->cb=value.cb;bitmap->clip=value.clip;
}
struct BitmapCacheEntry {
    bool valid=false;
    BitmapValue value;
    SaveStateBlob encoded;
};
constexpr size_t BITMAP_CACHE_LIMIT=64u*1024u*1024u;
// Keys describe the owning slot, never an allocator address or a pixel hash.
inline std::array<BitmapCacheEntry,29>& bitmap_encode_cache(){static std::array<BitmapCacheEntry,29> cache;return cache;}
inline uint64_t& bitmap_encode_cache_hits(){static uint64_t n=0;return n;}
inline uint64_t& bitmap_encode_cache_rebuilds(){static uint64_t n=0;return n;}
inline size_t bitmap_cache_entry_bytes(const BitmapCacheEntry& e){return e.value.bytepixels.capacity()+e.value.pixels.capacity()*sizeof(int32_t)+e.encoded.data.capacity();}
inline size_t bitmap_encode_cache_bytes(){size_t n=0;for(auto& e:bitmap_encode_cache())n+=bitmap_cache_entry_bytes(e);return n;}
inline bool read_live_bitmap(BITMAP* bitmap,BitmapValue& value) {
    value.present=bitmap!=nullptr;
    if(!bitmap){std::vector<uint8_t>().swap(value.bytepixels);std::vector<int32_t>().swap(value.pixels);return true;}
    value.w=bitmap->w;value.h=bitmap->h;value.depth=bitmap_color_depth(bitmap);
    value.cl=bitmap->cl;value.ct=bitmap->ct;value.cr=bitmap->cr;value.cb=bitmap->cb;value.clip=bitmap->clip;
    if(!valid_bitmap_metadata(value))return false;
    const size_t count=size_t(value.w)*value.h;
    if(value.depth==8){
        std::vector<int32_t>().swap(value.pixels);value.bytepixels.resize(count);
        for(int y=0;y<value.h;++y)memcpy(value.bytepixels.data()+size_t(y)*value.w,bitmap->line[y],value.w);
    }else{
        std::vector<uint8_t>().swap(value.bytepixels);value.pixels.resize(count);
        for(int y=0;y<value.h;++y)for(int x=0;x<value.w;++x)value.pixels[size_t(y)*value.w+x]=getpixel(bitmap,x,y);
    }return true;
}
inline void encode_bitmap_value(SaveStateBlob& b,const BitmapValue& value) {
    b.put(uint8_t(value.present));if(!value.present)return;
    b.put(value.w);b.put(value.h);b.put(value.depth);
    if(value.depth==8)put_byte_runs(b,value.bytepixels);else StateWriter{b}.runs(value.pixels.data(),value.pixels.size());
    b.put(value.cl);b.put(value.ct);b.put(value.cr);b.put(value.cb);b.put(value.clip);
}
inline void put_bitmap(SaveStateBlob& b,BITMAP* bitmap,size_t slot=28) {
    auto& cache=bitmap_encode_cache();
    if(slot<cache.size()&&cache[slot].valid&&same_bitmap(bitmap,cache[slot].value)){
        ++bitmap_encode_cache_hits();b.put(cache[slot].encoded.data.data(),cache[slot].encoded.data.size());return;
    }
    BitmapCacheEntry next;
    // Reuse the changed slot's allocations; invalidate before reading live data.
    if(slot<cache.size()){next=std::move(cache[slot]);cache[slot]=BitmapCacheEntry{};}
    next.valid=false;next.encoded.data.clear();next.encoded.pos=0;next.encoded.failed=false;
    if(!read_live_bitmap(bitmap,next.value)){b.failed=true;return;}
    encode_bitmap_value(next.encoded,next.value);++bitmap_encode_cache_rebuilds();
    b.put(next.encoded.data.data(),next.encoded.data.size());
    const size_t bytes=bitmap_cache_entry_bytes(next);
    if(slot<cache.size()&&bytes<=BITMAP_CACHE_LIMIT){
        for(size_t i=0;bitmap_encode_cache_bytes()>BITMAP_CACHE_LIMIT-bytes&&i<cache.size();++i)
            if(i!=slot)cache[i]=BitmapCacheEntry{};
        next.valid=true;cache[slot]=std::move(next);
    }
}
inline bool allocate_bitmap(const BitmapValue& value,BitmapOwner& owner) {
    owner.reset(create_bitmap_ex(value.depth,value.w,value.h));if(!owner)return false;
    apply_bitmap_pixels(owner.get(),value);return true;
}
inline std::vector<BITMAP**> bitmap_slots() {
    return {&msg_txt_bmp_buf,&msg_portrait_display_buf,&msg_txt_display_buf,&msg_bg_display_buf,&msg_bg_bmp_buf,&msg_menu_bmp_buf,&msg_portrait_bmp_buf,&pricesdisplaybuf,&scrollbuf,&script_menu_buf,&darkscr_bmp_curscr,&darkscr_bmp_scrollscr,&darkscr_bmp_curscr_trans,&darkscr_bmp_scrollscr_trans,&lightbeam_bmp,&subscr_pg_bmp1,&subscr_pg_bmp2,&subscr_pg_subbmp,&framebuf,&presentbuf,&zc255_map_bitmap};
}
template<class S> inline void map_runtime(S& s,ZC255MapRuntime& r) {
    s(r.phase,r.px,r.py,r.lx,r.ly,r.sc,r.delay,r.showmode,r.done,r.redraw);
}
template<class S> inline void modal_runtime(S& s,ZC255UiRuntime& r) {
    s(r.phase,r.y,r.distance,r.offy,r.showtime,r.use_a,r.use_x,r.use_y,r.b_only,r.compat,r.noverify,r.done,r.legacy_btn_press_peek,r.opening_bottom,r.saved_skipdraws,r.saved_bombpal);
    s.pod_array(r.temppal,256);
}
template<class S> inline void death_runtime(S& s,ZC255DeathMenuRuntime& r) {
    s(r.phase,r.type,r.pos,r.flash,r.waiting_release);
}
inline bool capture(SaveStateBlob& b) {
    b.put(uint32_t(4));
    StateWriter modal_writer{b};modal_runtime(modal_writer,zc255_ui_runtime); map_runtime(modal_writer,zc255_map_runtime);
    death_runtime(modal_writer,zc255_death_menu_runtime);
    b.put(framebuf_composed_since_present);
    if(!put_subscreens(b,subscreens_active)||!put_subscreens(b,subscreens_passive)||!put_subscreens(b,subscreens_overlay))return false;
    b.put(new_sub_indexes,sizeof(new_sub_indexes));b.put(active_sub_yoff);
    int indices[]={pointer_index(subscreens_active,new_subscreen_active),pointer_index(subscreens_passive,new_subscreen_passive),pointer_index(subscreens_overlay,new_subscreen_overlay)};
    for(int i:indices){if(i< -1){fprintf(stderr,"UI active pointer not in its owner vector\n");return false;}b.put(i);}
    int anim_index=pointer_index(subscreens_active,subscr_anim);if(anim_index< -1){fprintf(stderr,"UI animation pointer not in active vector\n");return false;}b.put(anim_index);transition(modal_writer,subscr_pg_transition);
    b.put(RAMpal,sizeof(RAMpal));b.put(CSET_SIZE);b.put(CSET_SHFT);
    b.put(refreshpal);b.put(stayLit);b.put(usingdrypal);b.put(olddrypal);b.put(drycolors,sizeof(drycolors));
    b.put(palclk,sizeof(palclk));b.put(palpos,sizeof(palpos));
    b.put(rgb_table);b.put(trans_table);b.put(trans_table2);
    int cmap=color_map==nullptr?-1:color_map==&trans_table?0:color_map==&trans_table2?1:-2;
    if(cmap== -2||(rgb_map&&rgb_map!=&rgb_table)){fprintf(stderr,"UI palette map pointer has unknown owner\n");return false;}b.put(cmap);b.put(uint8_t(rgb_map!=nullptr));
    b.put(animated_combos);b.put(animated_combos2);
    StateWriter s{b};s.runs(&animated_combo_table[0][0],MAXCOMBOS*2);s.runs(&animated_combo_table4[0][0],MAXCOMBOS*2);s.runs(&animated_combo_table2[0][0],MAXCOMBOS*2);s.runs(&animated_combo_table24[0][0],MAXCOMBOS*2);
    b.put(uint8_t(zscriptDrawingRenderTarget!=nullptr));
    if(zscriptDrawingRenderTarget){b.put(zscriptDrawingRenderTarget->GetCurrentRenderTarget());for(int i=0;i<7;++i)put_bitmap(b,zscriptDrawingRenderTarget->GetBitmapPtr(i),size_t(i));}
    size_t bitmap_slot=7;for(auto slot:bitmap_slots())put_bitmap(b,*slot,bitmap_slot++);
    return !b.failed;
}
using AnimationValue=std::remove_reference_t<decltype(animated_combo_table[0][0])>;
static_assert(sizeof(AnimationValue)==sizeof(int32_t),"Animation field width changed");
struct PreparedState {
    PreparedState()=default;
    PreparedState(const PreparedState&)=delete;
    PreparedState& operator=(const PreparedState&)=delete;
    PreparedState(PreparedState&&) noexcept=default;
    PreparedState& operator=(PreparedState&&) noexcept=default;
    bool ready=false,frame_composed=false,refresh=false,lit=false,dry=false;
    ZC255UiRuntime modal; ZC255MapRuntime map; ZC255DeathMenuRuntime death;
    std::vector<ZCSubscreen> active,passive,overlay;
    int indexes[3]{},yoff=0,pointers[3]{},anim_index=-1;
    SubscrTransition anim_transition{};
    RGB palette[256]{},olddry{};
    int csize=0,cshift=0,ac=0,ac2=0,cmap=0,target=-1;
    byte dry_colors[11]{};
    int32_t cycle_clocks[3]{},cycle_positions[3]{};
    RGB_MAP rgb{};COLOR_MAP trans{},trans2{};
    uint8_t rgb_present=0,targets=0;
    std::vector<AnimationValue> tables[4];
    BitmapValue target_bitmaps[7];
    std::vector<BitmapValue> bitmaps;
};
// Decode and validate once into owned values; the game remains untouched.
inline bool prepare(SaveStateBlob& b,PreparedState& out) {
    out.ready=false;PreparedState next;
    const char* stage="version";
    auto fail=[&](){fprintf(stderr,"UI decode failed stage=%s pos=%zu failed=%d\n",stage,b.pos,int(b.failed));return false;};
    uint32_t version=0;b.get(version);if(version!=4)return fail();
    stage="modal";
    auto& modal=next.modal;StateReader modal_reader{b};modal_runtime(modal_reader,modal);map_runtime(modal_reader,next.map);
    death_runtime(modal_reader,next.death);
    const auto& death=next.death;
    if(b.failed||death.phase<0||death.phase>3||death.type<0||death.type>1||death.pos<0||death.pos>2||death.flash< -1||death.flash>65||
       (death.type&&death.pos==1)||(death.phase==1&&death.flash!=-1)||(death.phase==2&&(death.flash<0||death.flash>=65))||
       (death.phase==3&&death.flash!=65)||(death.phase&&(modal.phase||next.map.phase)))return fail();
    auto& frame_composed=next.frame_composed;b.get(frame_composed);
    if(b.failed||modal.phase<0||modal.phase>4||(modal.phase>=2&&(modal.distance!=168&&modal.distance!=176))||modal.y< -2048||modal.y>2048)return fail();
    const auto& map=next.map;
    if(map.phase<0||map.phase>3||map.sc<0||map.sc>16||map.delay<0||map.delay>8||map.px< -4096||map.px>4096||map.py< -1408||map.py>1408||(map.phase&&modal.phase))return fail();
    stage="subscreens";
    auto& active=next.active;auto& passive=next.passive;auto& overlay=next.overlay;
    if(!get_subscreens(b,active)||!get_subscreens(b,passive)||!get_subscreens(b,overlay))return fail();
    stage="identities";
    auto& indexes=next.indexes;auto& yoff=next.yoff;auto& pointers=next.pointers;b.get(indexes,sizeof(indexes));b.get(yoff);b.get(pointers,sizeof(pointers));
    size_t sizes[]={active.size(),passive.size(),overlay.size()};for(int i=0;i<3;++i)if(pointers[i]<-1||(pointers[i]>=0&&size_t(pointers[i])>=sizes[i]))return fail();
    auto& anim_index=next.anim_index;auto& anim_transition=next.anim_transition;b.get(anim_index);transition(modal_reader,anim_transition);
    if(anim_index< -1||(anim_index>=0&&size_t(anim_index)>=active.size()))return fail();
    stage="palette";
    auto& palette=next.palette;auto& csize=next.csize;auto& cshift=next.cshift;auto& ac=next.ac;auto& ac2=next.ac2;b.get(palette,sizeof(palette));b.get(csize);b.get(cshift);
    auto& refresh=next.refresh;auto& lit=next.lit;auto& dry=next.dry;auto& olddry=next.olddry;auto& dry_colors=next.dry_colors;b.get(refresh);b.get(lit);b.get(dry);b.get(olddry);b.get(dry_colors,sizeof(dry_colors));
    auto& cycle_clocks=next.cycle_clocks;auto& cycle_positions=next.cycle_positions;b.get(cycle_clocks,sizeof(cycle_clocks));b.get(cycle_positions,sizeof(cycle_positions));
    auto& rgb=next.rgb;auto& trans=next.trans;auto& trans2=next.trans2;auto& cmap=next.cmap;auto& rgb_present=next.rgb_present;b.get(rgb);b.get(trans);b.get(trans2);b.get(cmap);b.get(rgb_present);
    if(cmap< -1||cmap>1||rgb_present>1||csize<1||csize>256||cshift<0||cshift>8)return fail();b.get(ac);b.get(ac2);
    if(ac<0||ac>MAXCOMBOS||ac2<0||ac2>MAXCOMBOS)return fail();
    stage="combo_animation";
    auto& tables=next.tables;StateReader s{b};for(auto& table:tables){table.resize(MAXCOMBOS*2);s.runs(table.data(),table.size());}
    stage="render_targets";
    auto& targets=next.targets;b.get(targets);if(b.failed||targets>1)return fail();auto& target=next.target;
    auto& target_bitmaps=next.target_bitmaps;if(targets){b.get(target);if(target< -1||target>=7)return fail();for(auto& bitmap:target_bitmaps){if(!get_bitmap(b,bitmap))return fail();if(bitmap.present&&(bitmap.w!=512||bitmap.h!=512||bitmap.depth!=8))return fail();}}
    stage="persistent_bitmaps";
    auto slots=bitmap_slots();auto& bitmaps=next.bitmaps;bitmaps.resize(slots.size());for(auto& bitmap:bitmaps)if(!get_bitmap(b,bitmap))return fail();
    const auto& mapbmp=bitmaps.back();
    if(mapbmp.present!=(map.phase==3))return fail();
    if(mapbmp.present&&(mapbmp.depth!=8||mapbmp.w>4096||mapbmp.h>1408||mapbmp.w*11!=mapbmp.h*32))return fail();
    if(b.failed)return fail();next.ready=true;out=std::move(next);return true;
}
// Commit prepared values without revisiting the serialized stream. All changed
// bitmap shapes are allocated before the first live UI mutation.
inline bool apply(PreparedState& state) {
    if(!state.ready)return false;
    auto& modal=state.modal;
    auto& frame_composed=state.frame_composed;
    auto& active=state.active;
    auto& passive=state.passive;
    auto& overlay=state.overlay;
    auto& indexes=state.indexes;
    auto& yoff=state.yoff;
    auto& pointers=state.pointers;
    auto& anim_index=state.anim_index;
    auto& anim_transition=state.anim_transition;
    auto& palette=state.palette;
    auto& csize=state.csize;
    auto& cshift=state.cshift;
    auto& ac=state.ac;
    auto& ac2=state.ac2;
    auto& refresh=state.refresh;
    auto& lit=state.lit;
    auto& dry=state.dry;
    auto& olddry=state.olddry;
    auto& dry_colors=state.dry_colors;
    auto& cycle_clocks=state.cycle_clocks;
    auto& cycle_positions=state.cycle_positions;
    auto& rgb=state.rgb;
    auto& trans=state.trans;
    auto& trans2=state.trans2;
    auto& cmap=state.cmap;
    auto& rgb_present=state.rgb_present;
    auto& tables=state.tables;
    auto& targets=state.targets;
    auto& target=state.target;
    auto& target_bitmaps=state.target_bitmaps;
    auto& bitmaps=state.bitmaps;
    auto slots=bitmap_slots();if(bitmaps.size()!=slots.size())return false;

    bool replace_targets=bool(targets)!=bool(zscriptDrawingRenderTarget);
    if(targets&&zscriptDrawingRenderTarget)for(int i=0;i<7;++i)
        if(bool(zscriptDrawingRenderTarget->GetBitmapPtr(i))!=target_bitmaps[i].present||(target_bitmaps[i].present&&!same_bitmap_shape(zscriptDrawingRenderTarget->GetBitmapPtr(i),target_bitmaps[i])))replace_targets=true;
    std::unique_ptr<ZScriptDrawingRenderTarget> new_targets;
    if(replace_targets&&targets){
        new_targets=std::make_unique<ZScriptDrawingRenderTarget>();new_targets->SetCurrentRenderTarget(target);
        for(int i=0;i<7;++i)if(target_bitmaps[i].present){auto* dest=new_targets->GetTargetBitmap(i);if(!dest)return false;apply_bitmap_pixels(dest,target_bitmaps[i]);}
    }
    std::vector<BitmapOwner> replacements(slots.size());
    for(size_t i=0;i<slots.size();++i)
        if(bitmaps[i].present&&!same_bitmap_shape(*slots[i],bitmaps[i])&&!allocate_bitmap(bitmaps[i],replacements[i]))return false;
    // Swapping transfers ownership without quest-editor copy_settings normalization.
    subscreens_active.swap(active);subscreens_passive.swap(passive);subscreens_overlay.swap(overlay);
    memcpy(new_sub_indexes,indexes,sizeof(indexes));active_sub_yoff=yoff;zc255_ui_runtime=modal;zc255_map_runtime=state.map;subscreen_open=modal.phase>=2;
    zc255_death_menu_runtime=state.death;
    new_subscreen_active=pointers[0]<0?nullptr:&subscreens_active[pointers[0]];
    new_subscreen_passive=pointers[1]<0?nullptr:&subscreens_passive[pointers[1]];
    new_subscreen_overlay=pointers[2]<0?nullptr:&subscreens_overlay[pointers[2]];
    subscr_anim=anim_index<0?nullptr:&subscreens_active[anim_index];subscr_pg_transition=anim_transition;
    memcpy(RAMpal,palette,sizeof(palette));CSET_SIZE=csize;CSET_SHFT=cshift;refreshpal=refresh;stayLit=lit;usingdrypal=dry;olddrypal=olddry;memcpy(drycolors,dry_colors,sizeof(drycolors));
    memcpy(palclk,cycle_clocks,sizeof(palclk));memcpy(palpos,cycle_positions,sizeof(palpos));
    rgb_table=rgb;trans_table=trans;trans_table2=trans2;color_map=cmap<0?nullptr:cmap==0?&trans_table:&trans_table2;rgb_map=rgb_present?&rgb_table:nullptr;
    animated_combos=ac;animated_combos2=ac2;
    memcpy(animated_combo_table,tables[0].data(),sizeof(animated_combo_table));memcpy(animated_combo_table4,tables[1].data(),sizeof(animated_combo_table4));memcpy(animated_combo_table2,tables[2].data(),sizeof(animated_combo_table2));memcpy(animated_combo_table24,tables[3].data(),sizeof(animated_combo_table24));
    // Preserve owners and aliases when allocation shape is unchanged. Compare
    // all pixels plus clipping metadata exactly; no checksum-only fast path.
    if(replace_targets){delete zscriptDrawingRenderTarget;zscriptDrawingRenderTarget=new_targets.release();}
    else if(targets){
        zscriptDrawingRenderTarget->SetCurrentRenderTarget(target);
        for(int i=0;i<7;++i)if(target_bitmaps[i].present){auto* dest=zscriptDrawingRenderTarget->GetBitmapPtr(i);if(!same_bitmap(dest,target_bitmaps[i]))apply_bitmap_pixels(dest,target_bitmaps[i]);}
    }
    for(size_t i=0;i<slots.size();++i){
        auto*& live=*slots[i];auto& value=bitmaps[i];
        if(!value.present){if(live)destroy_bitmap(live);live=nullptr;}
        else if(replacements[i]){if(live)destroy_bitmap(live);live=replacements[i].release();}
        else if(!same_bitmap(live,value))apply_bitmap_pixels(live,value);
    }
    // The render tree caches the presentation bitmap; its identity changes
    // whenever snapshot-owned pixels replace the live buffer.
    rti_game.a4_bitmap=presentbuf;
    framebuf_composed_since_present=frame_composed;
    state.ready=false;return true;
}

inline bool restore(SaveStateBlob& b,bool apply_value) {
    PreparedState state;
    return prepare(b,state)&&(!apply_value||apply(state));
}
// Runs at a complete bridge checkpoint; does not advance scripts or gameplay.
// It deliberately changes independent live fields, then checks both preflight
// purity and exact capture/restore. This tests fields beyond inventory checksums.
inline bool semantic_selftest(std::string& reason) {
    // Local buffers exercise the optimized representation without changing any
    // engine-owned bitmap. Slot 27 is reserved for these cache regressions.
    auto bitmap_fail=[&](const char* text){bitmap_encode_cache()[27]=BitmapCacheEntry{};reason=text;return false;};
    BitmapOwner byte_test(create_bitmap_ex(8,9,5));
    if(!byte_test)return bitmap_fail("UI byte bitmap test allocation failed");
    for(int y=0;y<5;++y)for(int x=0;x<9;++x)byte_test->line[y][x]=uint8_t((x+y)%3);
    SaveStateBlob byte_first;put_bitmap(byte_first,byte_test.get(),27);
    uint64_t hit_count=bitmap_encode_cache_hits();
    SaveStateBlob byte_same;put_bitmap(byte_same,byte_test.get(),27);
    if(byte_first.failed||byte_same.data!=byte_first.data||bitmap_encode_cache_hits()!=hit_count+1)
        return bitmap_fail("UI unchanged bitmap cache did not reuse exact encoding");
    uint64_t rebuild_count=bitmap_encode_cache_rebuilds();
    byte_test->line[3][4]^=0x7f;
    SaveStateBlob byte_changed;put_bitmap(byte_changed,byte_test.get(),27);
    if(byte_changed.data==byte_first.data||bitmap_encode_cache_rebuilds()!=rebuild_count+1)
        return bitmap_fail("UI direct byte write was hidden by bitmap cache");
    BitmapValue byte_decoded;SaveStateBlob byte_read=byte_changed;
    if(!get_bitmap(byte_read,byte_decoded)||byte_read.pos!=byte_read.data.size()
        ||!byte_decoded.pixels.empty()||byte_decoded.bytepixels.size()!=45||!same_bitmap(byte_test.get(),byte_decoded))
        return bitmap_fail("UI byte bitmap RLE roundtrip failed");
    byte_test->cl=1;
    SaveStateBlob clip_changed;put_bitmap(clip_changed,byte_test.get(),27);
    if(clip_changed.data==byte_changed.data)return bitmap_fail("UI clipping change was hidden by bitmap cache");
    SaveStateBlob malformed=byte_changed;
    // presence + three int32 shape fields + uint32 pixel count precede first run.
    if(malformed.data.size()<21)return bitmap_fail("UI bitmap test record unexpectedly short");
    memset(malformed.data.data()+17,0,4);
    BitmapValue invalid;
    if(get_bitmap(malformed,invalid))return bitmap_fail("UI zero bitmap RLE run accepted");
    malformed=byte_changed;malformed.data.pop_back();
    if(get_bitmap(malformed,invalid))return bitmap_fail("UI truncated bitmap clipping accepted");
    BitmapOwner wide_test(create_bitmap_ex(32,7,3));
    if(!wide_test)return bitmap_fail("UI nonbyte bitmap test allocation failed");
    clear_to_color(wide_test.get(),0x123456);putpixel(wide_test.get(),2,1,0x654321);
    SaveStateBlob wide;put_bitmap(wide,wide_test.get(),27);
    BitmapValue wide_decoded;SaveStateBlob wide_read=wide;
    if(!get_bitmap(wide_read,wide_decoded)||wide_read.pos!=wide_read.data.size()
        ||!wide_decoded.bytepixels.empty()||!same_bitmap(wide_test.get(),wide_decoded))
        return bitmap_fail("UI nonbyte bitmap support or cache shape invalidation failed");
    SaveStateBlob absent;put_bitmap(absent,nullptr,27);
    hit_count=bitmap_encode_cache_hits();SaveStateBlob absent_same;put_bitmap(absent_same,nullptr,27);
    if(absent.data!=std::vector<uint8_t>{0}||absent_same.data!=absent.data
        ||bitmap_encode_cache_hits()!=hit_count+1||bitmap_encode_cache_bytes()>BITMAP_CACHE_LIMIT)
        return bitmap_fail("UI absent bitmap cache or memory bound failed");
    bitmap_encode_cache()[27]=BitmapCacheEntry{};
    SaveStateBlob original;if(!capture(original)){reason="UI baseline capture failed";return false;}
    auto original_slots=bitmap_slots();std::vector<BITMAP*> original_pointers;for(auto slot:original_slots)original_pointers.push_back(*slot);
    BITMAP* test_bitmap=presentbuf?presentbuf:scrollbuf;int original_pixel=test_bitmap?getpixel(test_bitmap,0,0):0;
    auto load=[&](const SaveStateBlob& source,bool apply){SaveStateBlob copy=source;copy.pos=0;return restore(copy,apply)&&copy.pos==copy.data.size();};
    auto fail=[&](const char* message){load(original,true);reason=message;return false;};
    RGB old_color=RAMpal[17];RAMpal[17].r=(old_color.r+1)%64;
    if(test_bitmap){int clip=test_bitmap->clip;test_bitmap->clip=0;putpixel(test_bitmap,0,0,original_pixel^1);test_bitmap->clip=clip;}
    palclk[0]+=123;palpos[1]+=2;animated_combo_table4[0][1]+=17;
    if(!subscreens_active.empty()&&!subscreens_active[0].pages.empty()){
        auto& sub=subscreens_active[0];sub.curpage=0;auto& page=sub.pages[0];page.cursor_pos+=3;
        if(page.size()){page[0]->generic_initd[0]+=91;page[0]->selector_override.x+=2;}
    }
    if(!zscriptDrawingRenderTarget)zscriptDrawingRenderTarget=new ZScriptDrawingRenderTarget();
    putpixel(zscriptDrawingRenderTarget->GetTargetBitmap(6),2,3,73);zscriptDrawingRenderTarget->SetCurrentRenderTarget(6);
    SaveStateBlob altered;if(!capture(altered))return fail("UI changed capture failed");
    if(!load(original,false)||RAMpal[17].r==old_color.r)return fail("UI preflight mutated live palette");
    PreparedState prepared;SaveStateBlob prepared_blob=original;
    if(!prepare(prepared_blob,prepared)||prepared_blob.pos!=prepared_blob.data.size()||RAMpal[17].r==old_color.r)return fail("UI prepared decode mutated live state");
    if(!apply(prepared)||apply(prepared))return fail("UI prepared apply failed or was applied twice");
    for(size_t i=0;i<original_slots.size();++i)if(*original_slots[i]!=original_pointers[i])return fail("UI bitmap owner changed despite identical allocation shape");
    if(test_bitmap&&getpixel(test_bitmap,0,0)!=original_pixel)return fail("UI changed bitmap pixel was not restored");
    SaveStateBlob check;if(!capture(check)||check.data!=original.data)return fail("UI exact roundtrip failed");
    if(!load(altered,true))return fail("UI modified roundtrip failed");
    for(size_t i=0;i<original_slots.size();++i)if(*original_slots[i]!=original_pointers[i])return fail("UI unchanged bitmap owner was replaced");
    if(test_bitmap&&getpixel(test_bitmap,0,0)!=(original_pixel^1))return fail("UI modified bitmap pixel did not restore");
    if(!zscriptDrawingRenderTarget||getpixel(zscriptDrawingRenderTarget->GetBitmapPtr(6),2,3)!=73||zscriptDrawingRenderTarget->GetCurrentRenderTarget()!=6)return fail("UI render target pixels/selection failed");
    if(RAMpal[17].r==old_color.r)return fail("UI palette did not restore changed value");
    // Verify arbitrary modal phase values survive, without executing that phase.
    zc255_ui_runtime.phase=3;zc255_ui_runtime.distance=168;zc255_ui_runtime.y=0;zc255_ui_runtime.showtime=true;
    SaveStateBlob modal;if(!capture(modal)||!load(original,true)||!load(modal,true)||zc255_ui_runtime.phase!=3||!subscreen_open)return fail("UI modal phase roundtrip failed");
    if(!load(original,true))return fail("UI baseline cleanup failed");
    reason="UI values, modal phase, palette/animation, byte RLE, exact bounded bitmap cache, nonbyte pixels, prepared decode/apply, preflight purity and exact roundtrip passed";return true;
}
} // namespace snapshot_ui

