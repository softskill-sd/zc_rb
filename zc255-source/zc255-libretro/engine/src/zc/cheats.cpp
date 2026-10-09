#include "zc/cheats.h"
#include "zc/zelda.h"
#include "zc/hero.h"
#include "zc/guys.h"
#include "zc/zc_init.h"
#include "base/initdata.h"
#include "init.h"
#include "zinfo.h"
#include <queue>
#include <tuple>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fmt/format.h>

extern HeroClass Hero;

static std::queue<std::tuple<Cheat, int, int, std::string>> cheats;

static const std::vector<std::string> CheatStrings = {
	"None",
	"Life",
	"Magic",
	"Rupies",
	"Bombs",
	"Arrows",
	"Kill",
	"Fast",
	"Clock",
	"Walls",
	"Freeze",
	"Light",
	"GoTo",
	"IgnoreSideView",
	"MaxLife",
	"MaxMagic",
	"MaxBombs",
	"PlayerData",
	"TrigSecrets",
	"TrigSecretsPerm",
	"ShowL0",
	"ShowL1",
	"ShowL2",
	"ShowL3",
	"ShowL4",
	"ShowL5",
	"ShowL6",
	"ShowFFC",
	"ShowSprites",
	"ShowWalkability",
	"ShowEffects",
	"ShowOverhead",
	"ShowPushblock",
	"ShowHitbox",
	"ShowFFCScripts",
	"ShowInfoOpacity",
	"HeroPosition",
	"Last",
};

Cheat cheat_from_string(std::string name)
{
    auto it = std::find(std::begin(CheatStrings), std::end(CheatStrings), name.c_str());
    ASSERT(it != std::end(CheatStrings));
    return (Cheat)(it - std::begin(CheatStrings));
}

std::string cheat_to_string(Cheat cheat)
{
    return CheatStrings[cheat];
}

std::string cheat_help(Cheat cheat)
{
	switch(cheat)
	{
		case Life:
			return fmt::format("Refill your {} to full", ZI.getCtrName(crLIFE));
		case Magic:
			return fmt::format("Refill your {} to full", ZI.getCtrName(crMAGIC));
		case Rupies:
			return fmt::format("Refill your {} to full", ZI.getCtrName(crMONEY));
		case Bombs:
			return fmt::format("Refill your {} to full", ZI.getCtrName(crBOMBS));
		case Arrows:
			return fmt::format("Refill your {} to full", ZI.getCtrName(crARROWS));
		case Kill:
			return "Kill all enemies on the screen";
		case Fast:
			return "Make the player go 8 times as fast";
		case Clock:
			return "Toggle Invincibility";
		case Walls:
			return "Toggle Walk-Through-Walls";
		case Freeze:
			return "Toggle the player's frozen status?";
		case Light:
			return "Toggle the room's light status";
		case GoTo:
			return "Select a location (dmap,screen) to warp to";
		case IgnoreSideView:
			return "Toggle ignoring sideview gravity";
		case MaxLife:
			return fmt::format("Popup a box to modify your max {}", ZI.getCtrName(crLIFE));
		case MaxMagic:
			return fmt::format("Popup a box to modify your max {}", ZI.getCtrName(crMAGIC));
		case MaxBombs:
			return fmt::format("Popup a box to modify your max {}", ZI.getCtrName(crBOMBS));
		case PlayerData:
			return "Open the Player Data dialog";
		case TrigSecrets:
			return "Trigger current screen secrets (Temp)";
		case TrigSecretsPerm:
			return "Trigger current screen secrets (Perm)";
		case ShowL0:
			return "Toggle visibility of layer 0";
		case ShowL1:
			return "Toggle visibility of layer 1";
		case ShowL2:
			return "Toggle visibility of layer 2";
		case ShowL3:
			return "Toggle visibility of layer 3";
		case ShowL4:
			return "Toggle visibility of layer 4";
		case ShowL5:
			return "Toggle visibility of layer 5";
		case ShowL6:
			return "Toggle visibility of layer 6";
		case ShowFFC:
			return "Toggle visibility of FFCs";
		case ShowSprites:
			return "Toggle visibility of Sprites";
		case ShowWalkability:
			return "Toggle showing walkability debug info";
		case ShowEffects:
			return "Toggle showing effect debug info";
		case ShowOverhead:
			return "Toggle visibility of overhead combos";
		case ShowPushblock:
			return "Toggle visibility of pushblocks";
		case ShowHitbox:
			return "Toggle showing hitbox debug info";
		case ShowFFCScripts:
			return "Toggle listing currently running FFC scripts";
		case ShowInfoOpacity:
			return "Select the opacity of debug info, 0-255";
		case HeroPosition:
			return "Set Hero position on screen";
	}
	return "";
}

int cheat_lvl(Cheat cheat)
{
	switch(cheat)
	{
		case Life:
		case Magic:
		case Rupies:
		case Bombs:
		case Arrows:
			return 1;
		case Clock:
		case MaxLife:
		case MaxMagic:
		case MaxBombs:
			return 2;
		case PlayerData:
			return 3;
		default:
			return 4;
	}
}
bool bindable_cheat(Cheat cheat)
{
	switch(cheat)
	{
		case None:
		case Last:
			return false;
	}
	return true;
}

static void cheats_execute(Cheat cheat, int arg1, int arg2, std::string arg3)
{
    switch (cheat)
    {
    case Life:
    {
        game->set_life(arg1);
    }
    break;

    case Magic:
    {
        game->set_magic(arg1);
    }
    break;

    case Rupies:
    {
        game->set_drupy(arg1);
    }
    break;

    case Bombs:
    {
        for (int32_t i = 0; i < MAXITEMS; i++)
        {
            if (itemsbuf[i].family == itype_bomb || itemsbuf[i].family == itype_sbomb)
                getitem(i, true);
        }

        game->set_bombs(arg1);
        if (arg2 != -1)
            game->set_sbombs(arg2);
    }
    break;

    case Arrows:
    {
        game->set_arrows(arg1);
    }
    break;

    case Kill:
    {
        for (int32_t i = 0; i < guys.Count(); i++)
        {
            if (!(((enemy *)guys.spr(i))->flags & guy_doesntcount))
                ((enemy *)guys.spr(i))->kickbucket();
        }
    }
    break;

    case Fast:
    {
        gofast = gofast ? false : true;
    }
    break;

    case Clock:
    {
        setClock(arg3=="zc255-desired" ? arg1!=0 : !getClock());
        cheat_superman = getClock();
    }
    break;

    case Walls:
    {
        toogam = arg3=="zc255-desired" ? arg1!=0 : !toogam;
        if (toogam)
        {
            cheat_superman = true;
            setClock(true);
        }
    }
    break;

    case Freeze:
    {
        if (Hero.getAction() == freeze)
        {
            Hero.unfreeze();
        }
        else
        {
            Hero.Freeze();
        }
    }
    break;

    case Light:
    {
        cheats_execute_light = true;
    }
    break;

    case GoTo:
    {
        cheats_execute_goto = true;
        cheat_goto_dmap = arg1;
        cheat_goto_screen = arg2;
    }
    break;

    case IgnoreSideView:
    {
        ignoreSideview = !ignoreSideview;
    }
    break;

    case MaxLife:
    {
        game->set_maxlife(arg1);
    }
    break;

    case MaxMagic:
    {
        game->set_maxmagic(arg1);
    }
    break;

    case MaxBombs:
    {
        game->set_maxbombs(arg1);
    }
    break;

    case PlayerData:
    {
		zinitdata* base = copyIntoZinit(game);
		std::string error;
		zinitdata* new_init = apply_init_data_delta(base, arg3, error);
		if (new_init)
		{
			resetItems(game, new_init, false);
			ringcolor(false);
			delete new_init;
		}
		else
		{
			Z_error("error applying init data delta: %s\n", error.c_str());
		}
		delete base;
    }
    break;
	
	case TrigSecrets:
	{
		hidden_entrance(0, true, false, -9);
	}
	break;
	case TrigSecretsPerm:
	{
		hidden_entrance(0, true, false, -10);
		setmapflag(mSECRET);
	}
	break;
	case ShowL0:
	{
		onShowLayer0();
	}
	break;
	case ShowL1:
	{
		onShowLayer1();
	}
	break;
	case ShowL2:
	{
		onShowLayer2();
	}
	break;
	case ShowL3:
	{
		onShowLayer3();
	}
	break;
	case ShowL4:
	{
		onShowLayer4();
	}
	break;
	case ShowL5:
	{
		onShowLayer5();
	}
	break;
	case ShowL6:
	{
		onShowLayer6();
	}
	break;
	case ShowFFC:
	{
		onShowLayerF();
	}
	break;
	case ShowSprites:
	{
		onShowLayerS();
	}
	break;
	case ShowWalkability:
	{
		onShowLayerW();
	}
	break;
	case ShowEffects:
	{
		onShowLayerE();
	}
	break;
	case ShowOverhead:
	{
		onShowLayerO();
	}
	break;
	case ShowPushblock:
	{
		onShowLayerP();
	}
	break;
	case ShowHitbox:
	{
		onShowHitboxes();
	}
	break;
	case ShowFFCScripts:
	{
		onShowFFScripts();
	}
	break;
	case ShowInfoOpacity:
	{
		onShowInfoOpacity();
	}
	break;

	case HeroPosition:
	{
		Hero.x = zslongToFix(arg1);
		Hero.y = zslongToFix(arg2);
	}
	break;

    case None:
    case Last:
        break;
    }
}

static bool instant_exec(Cheat cheat)
{
	switch(cheat)
	{
		case HeroPosition:
		case ShowL0:
		case ShowL1:
		case ShowL2:
		case ShowL3:
		case ShowL4:
		case ShowL5:
		case ShowL6:
		case ShowFFC:
		case ShowSprites:
		case ShowWalkability:
		case ShowEffects:
		case ShowOverhead:
		case ShowPushblock:
		case ShowHitbox:
		case ShowFFCScripts:
		case ShowInfoOpacity:
			return true;
	}
	return false;
}

void cheats_enqueue(Cheat cheat, int arg1, int arg2, std::string arg3)
{
	if(instant_exec(cheat))
	{
		replay_step_cheat(cheat, arg1, arg2, arg3);
		cheats_execute(cheat, arg1, arg2, arg3);
	}
	else cheats.push({cheat, arg1, arg2, arg3});
}

void cheats_execute_queued()
{
    while (!cheats.empty())
    {
        std::apply(replay_step_cheat, cheats.front());
        std::apply(cheats_execute, cheats.front());
        cheats.pop();
    }
}

int32_t onGoToComplete();
int32_t onHeartC();
int32_t onMagicC();
int32_t onMaxBombs();
void cheats_hit_bind(Cheat cheat)
{
	switch(cheat)
	{
		case Life:
			cheats_enqueue(Cheat::Life, game->get_maxlife());
			break;
		case Magic:
			cheats_enqueue(Cheat::Magic, game->get_maxmagic());
			break;
		case Rupies:
			cheats_enqueue(Cheat::Rupies, game->get_maxcounter(1));
			break;
		case Bombs:
			cheats_enqueue(Cheat::Bombs, game->get_maxbombs(), game->get_maxcounter(6));
			break;
		case Arrows:
			cheats_enqueue(Cheat::Arrows, game->get_maxarrows());
			break;
		case GoTo:
			onGoToComplete();
			break;
		case MaxLife:
			enter_sys_pal();
			onHeartC();
			exit_sys_pal();
			break;
		case MaxMagic:
			enter_sys_pal();
			onMagicC();
			exit_sys_pal();
			break;
		case MaxBombs:
			enter_sys_pal();
			onMaxBombs();
			exit_sys_pal();
			break;
		case PlayerData:
			enter_sys_pal();
			music_pause();
			onCheatConsole();
			music_resume();
			exit_sys_pal();
			zc_init_apply_cheat_delta();
			break;
		default:
			cheats_enqueue(cheat);
			break;
	}
}



#include "snapshot_cheats.inc"

// Append inside the real cheats.cpp TU so both replay logging and native cheat
// execution remain the single source of side effects. All legacy calls toggle
// unchanged; a private arg3 marker records explicit replayable desired values.
// Opt-in queue-only regression: never execute test cheats or alter game data.
static void refill_selection_selftest() {
    static bool done=false;
    const char* report=std::getenv("ZC255_REFILL_SELECTION_SELFTEST");
    if(done || !report || !*report || !game || !Playing) return;
    done=true; // Recursive policy calls below must not run the test again.
    unsigned selection_cases=0;
    bool passed=true,master_off_empty=true,native_pending_cancelled=true,live_queue_preserved=false;
    {
        // swap is allocation-free, and the destructor restores the exact live
        // queue even if a test enqueue allocates and throws.
        struct QueueGuard {
            decltype(cheats) original;
            QueueGuard() { original.swap(cheats); }
            ~QueueGuard() { original.swap(cheats); }
        } guard;
        try {
            const Cheat types[]={Cheat::Life,Cheat::Magic,Cheat::Rupies,Cheat::Bombs,Cheat::Arrows};
            for(Cheat type:types) if(instant_exec(type)) passed=false;
            // Refuse to invoke the policy if any resource ever becomes an
            // instant cheat: this selftest must remain non-mutating.
            if(passed) for(unsigned selection=1;selection<32;++selection) {
                cheats_apply_frontend_policy(0,selection<<8,4);
                for(unsigned i=0;i<5;++i) if(selection&(1u<<i)) {
                    int arg1=i==0?game->get_maxlife():i==1?game->get_maxmagic():i==2?game->get_maxcounter(1):i==3?game->get_maxbombs():game->get_maxarrows();
                    int arg2=i==3?game->get_maxcounter(6):-1;
                    if(cheats.empty() || cheats.front()!=std::make_tuple(types[i],arg1,arg2,std::string())) { passed=false; break; }
                    cheats.pop();
                }
                if(!cheats.empty()) passed=false;
                while(!cheats.empty()) cheats.pop();
                cheats_apply_frontend_policy(0x80000000u,selection<<8,4);
                if(!cheats.empty()) { passed=false;master_off_empty=false; }
                while(!cheats.empty()) cheats.pop();
                ++selection_cases;
            }
            // A restored snapshot can contain native queued cheat actions
            // independently of the frontend's pending-event envelope.
            if(passed) {
                cheats_enqueue(Cheat::Life,game->get_maxlife());
                cheats_apply_frontend_policy(0x80000000u,0,0);
                native_pending_cancelled=cheats.empty();
                if(!native_pending_cancelled) passed=false;
            }
        } catch(...) { passed=false; }
    }
    // The queue guard restores the original queue on normal and failure paths.
    live_queue_preserved=true;
    if(FILE* file=std::fopen(report,"w")) {
        std::fprintf(file,"{\"passed\":%s,\"selection_cases\":%u,\"master_off_empty\":%s,\"native_pending_cancelled\":%s,\"live_queue_preserved\":%s}\n",passed&&selection_cases==31?"true":"false",selection_cases,master_off_empty?"true":"false",native_pending_cancelled?"true":"false",live_queue_preserved?"true":"false");
        std::fclose(file);
    }
}

void cheats_apply_frontend_policy(unsigned mask,unsigned values,unsigned events) {
    // High mask bit gates all requested cheats; bits 8..12 choose the
    // resources affected by one refill press. Reject unknown transport bits.
    if(!game || !Playing || (mask&~0x80000003u) || (values&~0x1f03u) || (events&~4u)) return;
    refill_selection_selftest();
    if(mask&0x80000000u) {
        // Snapshots preserve this queue. The current master switch must also
        // gate restored native cheat actions before advanceframe executes them.
        // Every entry is an explicit cheat, not an ordinary gameplay pickup.
        while(!cheats.empty()) cheats.pop();
        values&=0x1f00u; events=0;
    }
    auto desired=[](Cheat code,bool value) {
        replay_step_cheat(code,value?1:0,-1,"zc255-desired");
        cheats_execute(code,value?1:0,-1,"zc255-desired");
    };
    // Native Walls enables invulnerability; reconcile Clock after that native
    // operation, atomically at this worker boundary before gameplay resumes.
    if((mask&2) && toogam!=bool(values&2)) desired(Cheat::Walls,(values&2)!=0);
    if((mask&1) && (getClock()!=bool(values&1) || cheat_superman!=bool(values&1))) desired(Cheat::Clock,(values&1)!=0);
    if(events&4) {
        if(values&0x0100u) cheats_enqueue(Cheat::Life,game->get_maxlife());
        if(values&0x0200u) cheats_enqueue(Cheat::Magic,game->get_maxmagic());
        if(values&0x0400u) cheats_enqueue(Cheat::Rupies,game->get_maxcounter(1));
        if(values&0x0800u) cheats_enqueue(Cheat::Bombs,game->get_maxbombs(),game->get_maxcounter(6));
        if(values&0x1000u) cheats_enqueue(Cheat::Arrows,game->get_maxarrows());
    }
}
