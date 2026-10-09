#ifndef _GUYS_H_
#define _GUYS_H_

#include <list>
#include "sprite.h"
#include "zc/weapons.h"
#include "base/zfix.h"

class item;

extern int32_t repaircharge;
extern bool adjustmagic;
extern bool learnslash;
extern int32_t itemindex;
extern int32_t wallm_load_clk;
extern int32_t sle_x,sle_y,sle_cnt,sle_clk;
extern int32_t vhead;

// The "recently visited screens" ring that decides whether a room's enemies respawn (guys.cpp).
void guys_get_visited_state(int32_t out[6], int32_t& head);
void guys_set_visited_state(const int32_t in[6], int32_t head);

// The message menu a `Menu` message puts on screen. a menu is a multi-frame animation - the cursor
// moves, a timer holds the input, the chosen entry is remembered and an entry's text is placed once
// - so its state is game state, not a drawing cache, and a restore during a menu has to put the
// whole thing back instead of letting the frames the rewind skipped finish choosing. The counters
// are indexed by the MNU_* enum in guys.cpp.
enum
{
	MNU_CURSOR_TILE, MNU_CURSOR_CSET,
	MNU_CURSOR_WID, MNU_CURSOR_HEI, MNU_CURSOR_FLIP,

	MNU_CHOSEN, MNU_TIMER, MNU_CAN_CONFIRM,

	MNU_DATA_MAX
};
// One entry of a message menu: where its text is, and which entry each direction moves to.
struct menu_choice
{
	int32_t x, y;
	int32_t pos;
	int32_t upos, dpos, lpos, rpos;
	menu_choice() : x(0), y(0), pos(0), upos(0), dpos(0), lpos(0), rpos(0)
	{}
	menu_choice(int32_t x, int32_t y, int32_t pos, int32_t upos,
		int32_t dpos, int32_t lpos, int32_t rpos)
		: x(x), y(y), pos(pos), upos(upos), dpos(dpos), lpos(lpos), rpos(rpos)
	{}
};
struct msg_menu_state
{
	int32_t data[MNU_DATA_MAX] = {};
	bool running = false;       // a menu is being run right now
	bool wait_advance = false;  // the menu is waiting for the player to press a button
	bool end_str = false;       // the message is finished; the menu is on its last frame
	bool name_insert = false;   // the name-entry prompt is being typed into
	char namebuf[9] = {};       // ... and the name so far
	int32_t name_offset = 0;    // the typing position, as an offset into namebuf
	// The height of the last tile measured while laying the message out, which decides where the
	// next line starts; both are -1 until a tile has been measured.
	int32_t tile_hei = -1;
	int32_t tile_hei_buf = -1;
	std::vector<std::pair<int32_t, menu_choice>> options;
};
void guys_get_msg_menu_state(msg_menu_state& out);
void guys_set_msg_menu_state(const msg_menu_state& in);
extern int32_t guycarryingitem;

int32_t random_layer_enemy();
int32_t count_layer_enemies();
bool can_do_clock();
int32_t hero_on_wall();
bool tooclose(int32_t x,int32_t y,int32_t d);
bool isflier(int32_t id);
bool never_in_air(int32_t id);
int32_t weaponToDefence(int32_t wid);
int32_t getWeaponID(weapon *w);

// Start spinning tiles - called by load_default_enemies
void awaken_spinning_tile(mapscr *s, int32_t pos);

// Used to remove/add fireball shooter enemies
void update_slope_comboposes();
void update_slope_combopos(int32_t lyr, int32_t pos);
void screen_combo_modify_preroutine(mapscr *s, int32_t pos);
void screen_ffc_modify_preroutine(word index);
void screen_combo_modify_postroutine(mapscr *s, int32_t pos);
void screen_ffc_modify_postroutine(word index);
void screen_combo_modify_pre(int32_t cid);
void screen_combo_modify_post(int32_t cid);

// Find the IDs of enemies spawned by combos and flags. Called once on loading a quest.
void identifyCFEnemies();

byte get_def_ignrflag(int32_t edef);
int32_t conv_edef_unblockable(int32_t edef, byte unblockable);

/**********************************/
/*******  Enemy Base Class  *******/
/**********************************/

class enemy : public sprite
{
public:
	guydata *d;
	// Approximately all of these variables are accessed by either ffscript.cpp or inherited classes
	int32_t o_tile, frate, hp, hclk, clk3, stunclk, timer, fading, superman, mainguy, did_armos;
	byte movestatus, item_set, posframe;
	bool itemguy, count_enemy, dying, ceiling, leader, scored, script_spawned;
	zfix  step, floor_y;
	
	//d variables
	dword flags;
	dword flags2;
	
	int16_t  family, dcset, anim;
	int16_t  dp, wdp, wpn;
	
	int16_t  rate, hrate, homing, grumble;
	zfix dstep;
	int32_t dmisc1, dmisc2, dmisc3, dmisc4, dmisc5, dmisc6, dmisc7, dmisc8, dmisc9, dmisc10, dmisc11, dmisc12, dmisc13, dmisc14, dmisc15;
	int16_t bgsfx, bosspal;
	byte defense[edefLAST255];
	byte hitsfx,deadsfx;
	bool submerged;
	
	word ffcactivated;

	int32_t  clk2,sclk;
	int32_t  starting_hp;
	int32_t  ox, oy;
	int32_t  s_tile; //secondary (additional) tile(s)
	
	int32_t hitby[NUM_HIT_TYPES_USED];
	int16_t firesfx;
	bool isCore;
	int16_t parentCore; 
	int32_t script_UID; 	//used to determine the UID of an enemy by the user (in scripts), plus
			//in-engine assignment of children to a parent and
			//for future use in npc scripts (as the ref for `this`)--the easy way to determine to
			//what npc a script on the stack is bound.
	
	int32_t wpnsprite; //wpnsprite is new for 2.6 -Z
	int32_t SIZEflags; //Flags for size panel offsets. The user must enable these to override defaults. 
	int32_t frozentile, frozencset, frozenclock;
	
	bool hashero;
	
	int16_t frozenmisc[10];
	
	int32_t dmisc16, dmisc17, dmisc18, dmisc19, dmisc20, dmisc21, dmisc22, dmisc23, 
	dmisc24, dmisc25, dmisc26, dmisc27, dmisc28, dmisc29, dmisc30, dmisc31, dmisc32;
	int32_t movement[32]; //Reserved for npc movement types and args. 
	int32_t new_weapon[32]; //Reserved for weapon patterns and args.
	word npcscript; 
	int32_t initD[8], initA[2];
	word weaponscript;
	int32_t weap_initiald[8];
	int32_t weap_initiala[2];
	byte stickclk;
	int32_t parent_script_UID;
   
	int32_t dialogue_str; //WIll be set in spawn flags. 
	int32_t editorflags; //Enemy editor flags 1 to 16
	
	bool immortal;
	bool noSlide;
	int16_t hitdir;
	
	int8_t deathexstate;
	
	bool didScriptThisFrame; //Hack for enemies which are spawned by shit like scripted load enemies so they don't run their script a second time in a frame.
	
	int32_t getScriptUID();
	void setScriptUID(int32_t new_id);
	//void explode(int32_t type);
	
	zfix  getX();
	zfix  getY();
	int32_t  getID();
	
	// Savestate serialization (see solidobject.h).
	// `d` is a pointer into quest enemy data (guysbuf) and is not state: it is re-derived from
	// `id` after a restore (see fixup_sprite_state in savestate_snapshot.cpp).
	template <typename S> void visit_state(S& s)
	{
		sprite::visit_state(s);
		s(o_tile, frate, hp, hclk, clk3, stunclk, timer, fading, superman, mainguy, did_armos);
		s(movestatus, item_set, posframe);
		s(itemguy, count_enemy, dying, ceiling, leader, scored, script_spawned);
		s(step, floor_y);
		s(flags, flags2);
		s(family, dcset, anim);
		s(dp, wdp, wpn);
		s(rate, hrate, homing, grumble);
		s(dstep);
		s(dmisc1, dmisc2, dmisc3, dmisc4, dmisc5, dmisc6, dmisc7, dmisc8);
		s(dmisc9, dmisc10, dmisc11, dmisc12, dmisc13, dmisc14, dmisc15);
		s(bgsfx, bosspal);
		s(defense);
		s(hitsfx, deadsfx);
		s(submerged);
		s(ffcactivated);
		s(clk2, sclk);
		s(starting_hp);
		s(ox, oy);
		s(s_tile);
		s(hitby);
		s(firesfx);
		s(isCore);
		s(parentCore);
		s(script_UID);
		s(wpnsprite);
		s(SIZEflags);
		s(frozentile, frozencset, frozenclock);
		s(hashero);
		s(frozenmisc);
		s(dmisc16, dmisc17, dmisc18, dmisc19, dmisc20, dmisc21, dmisc22, dmisc23);
		s(dmisc24, dmisc25, dmisc26, dmisc27, dmisc28, dmisc29, dmisc30, dmisc31, dmisc32);
		s(movement);
		s(new_weapon);
		s(npcscript);
		s(initD, initA);
		s(weaponscript);
		s(weap_initiald);
		s(weap_initiala);
		s(stickclk);
		s(parent_script_UID);
		s(dialogue_str);
		s(editorflags);
		s(immortal);
		s(noSlide);
		s(hitdir);
		s(deathexstate);
		s(didScriptThisFrame);
		s(shieldCanBlock);
	}


	enemy(zfix X,zfix Y,int32_t Id,int32_t Clk);                      // : sprite()
	virtual ~enemy();
	
	bool is_move_paused();
	bool scr_walkflag(int32_t dx,int32_t dy,int32_t special, int32_t dir, int32_t input_x, int32_t input_y, bool kb);
	bool scr_canmove(zfix dx, zfix dy, int32_t special, bool kb = false, bool ign_sv = false);
	bool scr_canplace(zfix dx, zfix dy, int32_t special, bool kb = false);
	bool scr_canplace(zfix dx, zfix dy, int32_t special, bool kb, int32_t nwid, int32_t nhei);
	bool movexy(zfix dx, zfix dy, int32_t special, bool kb = false, bool ign_sv = false, bool earlyret = false);
	bool moveDir(int32_t dir, zfix px, int32_t special, bool kb = false, bool earlyret = false);
	bool moveAtAngle(zfix degrees, zfix px, int32_t special, bool kb = false, bool earlyret = false);
	bool can_movexy(zfix dx, zfix dy, int32_t special, bool kb = false);
	bool can_moveDir(int32_t dir, zfix px, int32_t special, bool kb = false);
	bool can_moveAtAngle(zfix degrees, zfix px, int32_t special, bool kb = false);
	
	// Handle pitfalls
	bool do_falling(int32_t index);
	// Handle drowning
	bool do_drowning(int32_t index);
	// Supplemental animation code that all derived classes should call
	// as a return value for animate().
	// Handles the death animation and returns true when enemy is finished.
	virtual bool Dead(int32_t index);
	// Basic animation code that all derived classes should call.
	// The one with an index is the one that is called by
	// the guys sprite list; index is the enemy's index in the list.
	virtual bool animate(int32_t index);
	virtual bool setSolid(bool set);
	virtual void solid_push(solid_object* pusher);
	//Overload to do damage to Hero on pushing them
	virtual void doContactDamage(int32_t hdir);
	//Overload to give 'riding sideview platform' behaviors
	virtual bool sideview_mode() const;
	virtual bool is_unpushable() const;
	
	// auomatically kill off enemy (for rooms with ringleaders)
	virtual void kickbucket();
	virtual bool isSubmerged() const;
	// Stop BG SFX only if no other enemy is playing it
	void stop_bgsfx(int32_t index);
	bool m_walkflag_simple(int32_t dx,int32_t dy);
	bool m_walkflag(int32_t dx,int32_t dy,int32_t special, int32_t dir, int32_t x=-1000,int32_t y=-1000, bool kb = false);
	bool m_walkflag_old(int32_t dx,int32_t dy,int32_t special, int32_t x=-1000, int32_t y=-1000);
	// Take damage or ignore it
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	// override hit detection to check for invicibility, stunned, etc
	virtual bool hit();
	virtual bool hit(sprite *s);
	virtual bool hit(int32_t tx,int32_t ty,int32_t tz,int32_t txsz,int32_t tysz,int32_t tzsz);
	virtual bool hit(int32_t tx,int32_t ty,int32_t txsz,int32_t tysz);
	virtual bool hit(weapon *w);
	virtual void break_shield() {}; // Overridden by types that can have shields
	
	bool can_pitfall(bool checkspawning = true);
	bool death_event();
	void try_death(bool force_kill = false);
	 // returns true if next step is ok, false if there is something there
	bool canmove(int32_t ndir,zfix s,int32_t special,int32_t dx1,int32_t dy1,int32_t dx2,int32_t dy2, bool kb);
	bool canmove_old(int32_t ndir,zfix s,int32_t special,int32_t dx1,int32_t dy1,int32_t dx2,int32_t dy2);
	bool canmove(int32_t ndir,zfix s,int32_t special, bool kb);
	bool canmove(int32_t ndir,int32_t special, bool kb);
	bool canmove(int32_t ndir, bool kb);
	bool enemycanfall(int32_t id, bool checkgrav = true);
	// 8-directional
	void newdir_8_old(int32_t rate,int32_t homing, int32_t special,int32_t dx1,int32_t dy1,int32_t dx2,int32_t dy2);
	void newdir_8(int32_t rate,int32_t homing, int32_t special,int32_t dx1,int32_t dy1,int32_t dx2,int32_t dy2);
	void newdir_8_old(int32_t rate,int32_t homing, int32_t special);
	void newdir_8(int32_t rate,int32_t homing, int32_t special);
	// makes the enemy slide backwards when hit
	// sclk: first byte is clk, second byte is dir
	int32_t slide();
	bool can_slide();
	bool fslide();
	bool overpit(enemy *e);
	bool shadow_overpit(enemy *e);
	virtual bool knockback(int32_t time, int32_t dir, int32_t speed);
	virtual bool runKnockback();
	// changes enemy's direction, checking restrictions
	// rate:   0 = no random changes, 16 = always random change
	// homing: 0 = none, 256 = always
	// grumble 0 = none, 4 = strongest appetite
	void newdir(int32_t rate,int32_t homing,int32_t special);
	void newdir();
	zfix distance_left();
	// keeps walking around
	void constant_walk(int32_t rate,int32_t homing,int32_t special);
	void constant_walk();
	// falls through the Z axis;
	void falldown();
	int32_t pos(int32_t x,int32_t y);
	// for variable step rates
	void variable_walk(int32_t rate,int32_t homing,int32_t special);
	// pauses for a while after it makes a complete move (to a new square)
	void halting_walk(int32_t rate,int32_t homing,int32_t special,int32_t hrate, int32_t haltcnt);
	// 8-directional movement, aligns to 8 pixels
	void constant_walk_8_old(int32_t rate,int32_t homing,int32_t special);
	void constant_walk_8(int32_t rate,int32_t homing,int32_t special);
	// 8-directional movement, halting
	void halting_walk_8(int32_t newrate,int32_t newhoming, int32_t newclk,int32_t special,int32_t newhrate, int32_t haltcnt);
	// 8-directional movement, no alignment
	void variable_walk_8(int32_t rate,int32_t homing,int32_t newclk,int32_t special);
	// same as above but with variable enemy size
	void variable_walk_8(int32_t rate,int32_t homing,int32_t newclk,int32_t special,int32_t dx1,int32_t dy1,int32_t dx2,int32_t dy2);
	// the variable speed floater movement
	// ms is max speed
	// ss is step speed
	// s is step count
	// p is pause count
	// g is graduality :)
	void floater_walk(int32_t rate,int32_t newclk,zfix ms,zfix ss,int32_t s,int32_t p, int32_t g);
	void floater_walk(int32_t rate,int32_t newclk,zfix s);
	// Checks if enemy is lined up with Hero. If so, returns direction Hero is
	// at as compared to enemy. Returns -1 if not lined up. Range is inclusive.
	int32_t lined_up(int32_t range, bool dir8);
	// returns true if Hero is within 'range' pixels of the enemy
	bool HeroInRange(int32_t range);
	// Breathe fire
	void FireBreath(bool seekhero);
	// Shoot weapons
	void FireWeapon();
	// place the enemy in line with Hero (red wizzrobes)
	void place_on_axis(bool floater, bool solid_ok);
	void update_enemy_frame();
	int32_t n_frame_n_dir(int32_t frames, int32_t dir, int32_t f4);
	void tiledir_small(int32_t ndir, bool fourdir);
	void tiledir_three(int32_t ndir);
	void tiledir(int32_t ndir, bool fourdir);
	void tiledir_big(int32_t ndir, bool fourdir);
	// Enemies that cannot ever be penetrated by weapons
	bool cannotpenetrate();
	bool isOnSideviewPlatform(); //This handles large enemies, too!
	
	// Returns true iff a combo type or flag precludes enemy movement.
	bool groundblocked(int32_t dx, int32_t dy, bool isKB = false);
	// Returns true iff enemy is floating and blocked by a combo type or flag.
	bool flyerblocked(int32_t dx, int32_t dy, int32_t special, bool isKB = false);
	virtual bool ignore_msg_freeze()
	{
		return false;
	}
	bool IsBigAnim();
	int32_t getFlashingCSet();
	bool is_hitflickerframe(bool olddrawing);
	virtual int32_t run_script(int32_t mode);
	virtual ALLEGRO_COLOR hitboxColor(byte opacity = 255) const;
	virtual optional<ScriptType> get_scrtype() const {return ScriptType::NPC;}
protected:
	
	
	// to allow for different sfx on defeating enemy
	virtual void death_sfx();
	virtual void move(zfix dx,zfix dy);
	virtual void removearmos(int32_t ax,int32_t ay, word ffcactive = 0);
	virtual void removearmosffc(int32_t pos);
	virtual void move(zfix s);
	void leave_item();
	
	// take damage or ignore it
	virtual bool hitshield(int32_t wpnx, int32_t wpny, int32_t xdir);
	virtual int32_t defend(int32_t wpnId, int32_t *power, int32_t edef);
//New 2.55 Weapon System
	int32_t resolveEnemyDefence(weapon *w);
	int32_t defendNewInt(int32_t wpnId, int32_t *power, int32_t edef, byte unblockable, weapon* w=nullptr);
	virtual int32_t defendNew(int32_t wpnId, int32_t *power, int32_t edef, byte unblockable);
	//virtual int32_t defend_wdmg(int32_t wpnId, int32_t dmg, int32_t edef);
	bool candamage(int32_t power, int32_t edef, byte unblockable);
	int32_t defenditemclass(int32_t wpnId, int32_t *power);
	int32_t defenditemclassNew(int32_t wpnId, int32_t *power, weapon *w, weapon* realweap = nullptr);
	
	bool dont_draw();
	// base drawing function to be used by all derived classes instead of
	// sprite::draw()
	virtual void draw(BITMAP *dest);
	virtual void drawzcboss(BITMAP *dest);
	// similar to the overblock function--can do up to a 32x32 sprite
	void drawblock(BITMAP *dest,int32_t mask);
	virtual void drawshadow(BITMAP *dest, bool translucent);
	void masked_draw(BITMAP *dest,int32_t mx,int32_t my,int32_t mw,int32_t mh);
	
	//                         --==**==--
	//   Movement routines that can be used by derived classes as needed
	//                         --==**==--
	void fix_coords(bool bound=false);
   
	
	
private:
	bool shieldCanBlock;
};

/********************************/
/*********  Guy Class  **********/
/********************************/

// good guys, fires, fairy, and other non-enemies
// based on enemy class b/c guys in dungeons act sort of like enemies
// also easier to manage all the guys this way
class guy : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	guy(zfix X,zfix Y,int32_t Id,int32_t Clk,bool mg);                // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual bool ignore_msg_freeze()
	{
		return true;
	}
};

/*******************************/
/*********   Enemies   *********/
/*******************************/

class eFire : public enemy
{
public:
	int32_t clk4;
	bool shield;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(clk4, shield);
	}


	eFire(zfix X,zfix Y,int32_t Id,int32_t Clk);                      // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	virtual void break_shield();
};

class eOther : public enemy
{
public:
	int32_t clk4;
	bool shield;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(clk4, shield);
	}


	eOther(zfix X,zfix Y,int32_t Id,int32_t Clk);                      // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	virtual void break_shield();
};

class eScript : public enemy
{
public:
	int32_t clk4;
	bool shield;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(clk4, shield);
	}


	eScript(zfix X,zfix Y,int32_t Id,int32_t Clk);                      // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	virtual void break_shield();
};

class eFriendly : public enemy
{
public:
	int32_t clk4;
	bool shield;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(clk4, shield);
	}


	eFriendly(zfix X,zfix Y,int32_t Id,int32_t Clk);                      // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	virtual void break_shield();
};

void removearmos(int32_t ax,int32_t ay, word ffcactive = 0);
void removearmosffc(int32_t pos);

class eGhini : public enemy
{
public:
	int32_t clk4, ox, oy, c;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(clk4, ox, oy, c);
	}


	eGhini(zfix X,zfix Y,int32_t Id,int32_t Clk);                     // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual void kickbucket();
};

class eTektite : public enemy
{
	zfix old_y; // a kludge used to check for ZScript modification of Y
public:
	int32_t c;
	int32_t clk2start;
	int32_t cstart;

	// Savestate serialization (see solidobject.h).
	// `old_y` is private and declared before the first access label; it is still state.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(old_y, c, clk2start, cstart);
	}


	eTektite(zfix X,zfix Y,int32_t Id,int32_t Clk);                   // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void drawshadow(BITMAP *dest,bool translucent);
	virtual void draw(BITMAP *dest);
};

class eItemFairy : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	eItemFairy(zfix X,zfix Y,int32_t Id,int32_t Clk);                 // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
};

class ePeahat : public enemy
{
public:
	int32_t ox, oy, c;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(ox, oy, c);
	}


	ePeahat(zfix X,zfix Y,int32_t Id,int32_t Clk);                    // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void drawshadow(BITMAP *dest, bool translucent);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	virtual void kickbucket();
};

class eLeever : public enemy
{
public:
	bool temprule;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(temprule);
	}


	eLeever(zfix X,zfix Y,int32_t Id,int32_t Clk);                    // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	bool canplace(int32_t d);
	virtual void draw(BITMAP *dest);
	virtual bool isSubmerged() const;
};

class eWallM : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	eWallM(zfix X,zfix Y,int32_t Id,int32_t Clk);                     // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	void wallm_crawl();
	void grabhero();
	virtual void draw(BITMAP *dest);
	virtual bool isSubmerged() const;
};

class eTrap : public enemy
{
public:
	int32_t  ox, oy;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(ox, oy);
	}


	eTrap(zfix X,zfix Y,int32_t Id,int32_t Clk);                      // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	bool trapmove(int32_t ndir);
	bool clip();
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
};

class eTrap2 : public enemy                                 //trap that goes back and forth constantly
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	eTrap2(zfix X,zfix Y,int32_t Id,int32_t Clk);                     // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	bool trapmove(int32_t ndir);
	bool clip();
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
};

class eRock : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	eRock(zfix X,zfix Y,int32_t Id,int32_t Clk);                      // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void drawshadow(BITMAP *dest, bool translucent);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
};

class eBoulder : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	eBoulder(zfix X,zfix Y,int32_t Id,int32_t Clk);                      // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void drawshadow(BITMAP *dest, bool translucent);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
};

// Helper for launching fireballs from statues, etc.
// It's invisible and can't be hit.
// Pass the range value through the clk variable in the constuctor.
class eProjectile : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(minRange);
	}


	eProjectile(zfix X,zfix Y,int32_t Id,int32_t Clk);                     // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	
	// How far away it may fire from. Not const, because a savestate has to be able to write the
	// captured value back into a rebuilt projectile.
	int32_t minRange;
};

class eTrigger : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	eTrigger(zfix X,zfix Y,int32_t Id,int32_t Clk);                   // : enemy(X,Y,Id,Clk)
	virtual void draw(BITMAP *dest);
	virtual void death_sfx();
};

class eNPC : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	eNPC(zfix X,zfix Y,int32_t Id,int32_t Clk);                       // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
};

class eSpinTile : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	eSpinTile(zfix X,zfix Y,int32_t Id,int32_t Clk);                       // : enemy(X,Y,Id,Clk)
	virtual void facehero();
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual void drawshadow(BITMAP *dest, bool translucent);
};

class eZora : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	eZora(zfix X,zfix Y,int32_t Id,int32_t Clk);                      // : enemy(X,Y,Id,0)
	virtual void facehero();
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual bool isSubmerged() const;
};

class eStalfos : public enemy
{
public:
	int32_t clk4, //Tribble clock
		clk5; // Like Like clock
	bool fired; // Darknut5, Bombchu
	bool shield; // Darknut
	bool dashing; // Rope
	int32_t multishot; // Shoot twice/thrice/n times in one halt
	zfix fy, shadowdistance; // Pols Voice
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(clk4, clk5, fired, shield, dashing, multishot, fy, shadowdistance);
	}


	eStalfos(zfix X,zfix Y,int32_t Id,int32_t Clk);                   // : enemy(X,Y,Id,Clk)
	bool WeaponOut();
	void KillWeapon();
	void charge_attack();
	void eathero();
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	void vire_hop();
	virtual void drawshadow(BITMAP *dest, bool translucent);
	virtual void break_shield();
};

class eKeese : public enemy
{
public:
	int32_t ox, oy, c;
	int32_t clk4, clk5;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(ox, oy, c, clk4, clk5);
	}


	eKeese(zfix X,zfix Y,int32_t Id,int32_t Clk);                     // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void drawshadow(BITMAP *dest, bool translucent);
	virtual void draw(BITMAP *dest);
};

class eWizzrobe : public enemy
{
public:
	bool charging;
	bool firing;
	int32_t fclk;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(charging, firing, fclk);
	}


	eWizzrobe(zfix X,zfix Y,int32_t Id,int32_t Clk);                  // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	void wizzrobe_attack();
	void wizzrobe_attack_for_real();
	void wizzrobe_newdir(int32_t homing);
	void submerge(bool set);
	virtual void draw(BITMAP *dest);
};

/*********************************/
/**********   Bosses   ***********/
/*********************************/

class eDodongo : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	eDodongo(zfix X,zfix Y,int32_t Id,int32_t Clk);                   // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
};

class eDodongo2 : public enemy
{
public:
	int32_t previous_dir;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(previous_dir);
	}


	eDodongo2(zfix X,zfix Y,int32_t Id,int32_t Clk);                  // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
};

class eAquamentus : public enemy
{
public:
	int32_t clk4, fbx; //fireball x position
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(clk4, fbx);
	}


	eAquamentus(zfix X,zfix Y,int32_t Id,int32_t Clk); // : enemy((zfix)176,(zfix)64,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual bool hit(weapon *w);
};

class eGohma : public enemy
{
public:
	int32_t clk4; // Movement clk must be separate from animation clk because of the Clock item
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(clk4);
	}


	eGohma(zfix X,zfix Y,int32_t Id,int32_t Clk);                     // : enemy((zfix)128,(zfix)48,Id,0)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
};

class eLilDig : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	eLilDig(zfix X,zfix Y,int32_t Id,int32_t Clk);                    // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
};

class eBigDig : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	eBigDig(zfix X,zfix Y,int32_t Id,int32_t Clk);                    // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
};

class eGanon : public enemy
{
public:
	int32_t Stunclk;
	int32_t fakeX;
	int32_t fakeY;
	int32_t fakeTile;
	int32_t AmuletClk;
	int32_t AmuletClk2;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(Stunclk, fakeX, fakeY, fakeTile, AmuletClk, AmuletClk2);
	}


	eGanon(zfix X,zfix Y,int32_t Id,int32_t Clk);                     // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	virtual void draw(BITMAP *dest);
	void draw_guts(BITMAP *dest);
	void draw_flash(BITMAP *dest);
};

void getBigTri(int32_t id2);

/**********************************/
/***  Multiple-Segment Enemies  ***/
/**********************************/

// segment manager
class eMoldorm : public enemy
{
public:
	int32_t segcnt;
	int32_t segid;
	// tell it the segment count through the clk param
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(segcnt, segid);
	}


	eMoldorm(zfix X,zfix Y,int32_t Id,int32_t Clk);                   // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
};

// segment class
class esMoldorm : public enemy
{
public:
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(parentclk);
	}


	esMoldorm(zfix X,zfix Y,int32_t Id,int32_t Clk);                  // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	virtual void draw(BITMAP *dest);
	int32_t parentclk; //because of custom step speed, clk is not sufficient to keep track
	//of when to check the parent Moldorm's direction, since the frequency
	//of the parent's checking might not divide the Moldorm segment's
	//number of animation frames. -DD
};

class eBaseLanmola : public enemy
{
public:
	// Savestate serialization (see solidobject.h). `prevState` is a std::list, so it travels
	// element by element (the nodes are the list's own business).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s.list(prevState);
	}


	eBaseLanmola(zfix X, zfix Y, int32_t Id, int32_t Clk) : enemy(X, Y, Id, Clk) {}
	std::list<std::pair< std::pair<zfix, zfix>, int32_t> > prevState;
};

// segment manager
class eLanmola : public eBaseLanmola
{
public:
	int32_t segcnt;
	// tell it the segment count through the clk param
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		eBaseLanmola::visit_state(s);
		s(segcnt);
	}


	eLanmola(zfix X,zfix Y,int32_t Id,int32_t Clk);                   // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
};

// segment class
class esLanmola : public eBaseLanmola
{
public:
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		eBaseLanmola::visit_state(s);
	}


	esLanmola(zfix X,zfix Y,int32_t Id,int32_t Clk);                  // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	virtual void draw(BITMAP *dest);
};

class eManhandla : public enemy
{
public:
	char arm[8];                                            // side the arm is on
	int32_t armcnt;
	bool adjusted;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(arm, armcnt, adjusted);
	}


	eManhandla(zfix X,zfix Y,int32_t Id,int32_t Clk);                 // : enemy(X,Y,Id,0)
	virtual bool animate(int32_t index);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	virtual void draw(BITMAP *dest);
};

class esManhandla : public enemy
{
public:
	// use clk param to tell which arm it is
	// Savestate serialization (see solidobject.h).
	// No data members of its own.
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
	}


	esManhandla(zfix X,zfix Y,int32_t Id,int32_t Clk);                // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
};

class eGleeok : public enemy
{
public:
	int32_t flameclk, flamehead;
	int32_t necktile;
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(flameclk, flamehead, necktile);
	}


	eGleeok(zfix X,zfix Y,int32_t Id,int32_t Clk);                    // : enemy((zfix)120,(zfix)48,Id,Clk)
	virtual bool animate(int32_t index);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	virtual void draw(BITMAP *dest);
	virtual void draw2(BITMAP *dest);
};

// head class
class esGleeok : public enemy
{
public:
	int32_t nx[255],ny[255];                                        //neck x and y?
	int32_t headtile;
	int32_t flyingheadtile;
	int32_t necktile;
	sprite * parent; //parent Gleeok to whose neck this segment is attached
	zfix xoffset, yoffset; //coords relative to the parent Gleeok
	int32_t nxoffset[255], nyoffset[255]; //random "jiggling" of the next tiles
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(nx, ny);
		s(headtile, flyingheadtile, necktile);
		s(xoffset, yoffset);
		s(nxoffset, nyoffset);
	}

	// The parent this segment hangs off is a reference to another object, so it is
	// serialized as that object's UID.
	template <typename S> void visit_refs(S& s)
	{
		s(parent);
	}


	esGleeok(zfix X,zfix Y,int32_t Id,int32_t Clk, sprite * prnt);                   // : enemy(X,Y,Id,Clk)
	virtual bool animate(int32_t index);
	virtual int32_t takehit(weapon *w, weapon* realweap = nullptr);
	virtual void draw(BITMAP *dest);
	virtual void draw2(BITMAP *dest);
};

class ePatra : public enemy
{
public:
	int32_t flycnt,flycnt2, loopcnt, lookat;
	double circle_x, circle_y;
	double temp_x, temp_y;
	bool adjusted;
	int32_t clk4, //Slow down clock
		clk5, //Segment firing clock
		clk6, //Center firing clock
		clk7; //Breath clock
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(flycnt, flycnt2, loopcnt, lookat);
		s(circle_x, circle_y);
		s(temp_x, temp_y);
		s(adjusted);
		s(clk4, clk5, clk6, clk7);
	}


	ePatra(zfix X,zfix Y,int32_t Id,int32_t Clk);                     // : enemy((zfix)128,(zfix)48,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void FirePatraWeapon();
	virtual void draw(BITMAP *dest);
	virtual int32_t defend(int32_t wpnId, int32_t *power, int32_t edef);
	virtual int32_t defendNew(int32_t wpnId, int32_t *power, int32_t edef, byte unblockable);
};

// segment class
class esPatra : public enemy
{
public:
	int32_t clk4, //Breath clock
		clk5; //Firing clock
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(clk4, clk5);
	}

	// The parent this segment hangs off is a reference to another object, so it is
	// serialized as that object's UID.
	template <typename S> void visit_refs(S& s)
	{
		s(parent);
	}


	esPatra(zfix X,zfix Y,int32_t Id,int32_t Clk,sprite * prnt);                    // : enemy(X,Y,Id,Clk)
	sprite * parent;
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
};

class ePatraBS : public enemy
{
public:
	int32_t flycnt,flycnt2, loopcnt, lookat;
	double temp_x, temp_y;
	bool adjusted;
	int32_t clk4, //Slow down clock
		clk5; //Firing clock
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(flycnt, flycnt2, loopcnt, lookat);
		s(temp_x, temp_y);
		s(adjusted);
		s(clk4, clk5);
	}


	ePatraBS(zfix X,zfix Y,int32_t Id,int32_t Clk);                   // : enemy((zfix)128,(zfix)48,Id,Clk)
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
	virtual int32_t defend(int32_t wpnId, int32_t *power, int32_t edef);
	virtual int32_t defendNew(int32_t wpnId, int32_t *power, int32_t edef, byte unblockable);
};

// segment class
class esPatraBS : public enemy
{
public:
	int32_t clk4, //Unused
		clk5; //Firing clock
	// Savestate serialization (see solidobject.h).
	template <typename S> void visit_state(S& s)
	{
		enemy::visit_state(s);
		s(clk4, clk5);
	}

	// The parent this segment hangs off is a reference to another object, so it is
	// serialized as that object's UID.
	template <typename S> void visit_refs(S& s)
	{
		s(parent);
	}


	esPatraBS(zfix X,zfix Y,int32_t Id,int32_t Clk,sprite * prnt);                  // : enemy(X,Y,Id,Clk)
	sprite * parent;
	virtual bool animate(int32_t index);
	virtual void draw(BITMAP *dest);
};

/**********************************/
/**********  Misc Code  ***********/
/**********************************/
void addEwpn(int32_t x,int32_t y,int32_t z,int32_t id,int32_t type,int32_t power,int32_t dir, int32_t parentid, byte script_gen = 0, int32_t fakez = 0);
// Used by Hero's swords & stomp boots
int32_t hit_enemy(int32_t index,int32_t wpnId,int32_t power,int32_t wpnx,int32_t wpny,int32_t dir, int32_t enemyHitWeapon, weapon* realweap = nullptr);
void enemy_scored(int32_t index);
void addguy(int32_t x,int32_t y,int32_t id,int32_t clk,bool mainguy);
void additem(int32_t x,int32_t y,int32_t id,int32_t pickup);
void additem(int32_t x,int32_t y,int32_t id,int32_t pickup,int32_t clk);
void kill_em_all();
bool can_kill_em_all();
// For Hero's hit detection. Don't count them if they are stunned or are guys.
int32_t GuyHit(int32_t tx,int32_t ty,int32_t tz,int32_t txsz,int32_t tysz,int32_t tzsz);
int32_t GuyHitFrom(int32_t index,int32_t tx,int32_t ty,int32_t tz,int32_t txsz,int32_t tysz,int32_t tzsz);
// For Hero's hit detection. Count them if they are dying.
int32_t GuyHit(int32_t index,int32_t tx,int32_t ty,int32_t tz,int32_t txsz,int32_t tysz,int32_t tzsz);
bool hasMainGuy();
void EatHero(int32_t index);
void GrabHero(int32_t index);
bool CarryHero();
void movefairy(zfix &x,zfix &y,int32_t misc);
void movefairy2(zfix x,zfix y,int32_t misc);
void movefairynew(zfix &x,zfix &y,item const &itemfairy);
void movefairynew2(zfix x,zfix y,item const &itemfairy);
void killfairy(int32_t misc);
int32_t getGuyIndex(const int32_t eid);
void killfairynew(item const &itemfairy);
int32_t addenemy(int32_t x,int32_t y,int32_t id,int32_t clk);
int32_t addenemy(int32_t x,int32_t y,int32_t z,int32_t id,int32_t clk);
int32_t addchild(int32_t x,int32_t y,int32_t id,int32_t clk, int32_t parent_scriptUID);
int32_t addchild(int32_t x,int32_t y,int32_t z,int32_t id,int32_t clk, int32_t parent_scriptUID);
bool isjumper(int32_t id);
bool canfall(int32_t id);
void addfires();
void loadguys();
void loaditem();
void never_return(int32_t index);
bool hasBoss();
bool slowguy(int32_t id);
bool ok2add(int32_t id);
void load_default_enemies();
void nsp(bool random);
int32_t next_side_pos(bool random);
bool can_side_load(int32_t id);
void side_load_enemies();
bool scriptloadenemies();
void loadenemies();
void moneysign();
void putprices(bool sign);
void setupscreen();
FONT *setmsgfont();
bool parsemsgcode();
void putmsg();
int32_t message_more_y();
int32_t wpnsfx(int32_t wpn); 

/***  Collision detection & handling  ***/

void check_enemy_lweapon_collision(weapon *w);
void check_collisions();
void clear_script_one_frame_conditions();
void dragging_item();
void roaming_item();
int32_t more_carried_items();
#endif
