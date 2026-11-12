// VR Settings redesign: the layout (vr_settings_layout.h) over the real
// content tables: every key on one page, at most 8 rows for every class and
// dependency state, nothing overlapping or leaving the panel, VR-sized hit
// targets, and the model's presses with their guards.
#include "vr_settings_layout.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
using namespace framevr::settings;
namespace L=framevr::settings::layout;

static int checks=0;
static void Check(bool ok,const std::string& what){
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",what.c_str());std::exit(1);}
}
static bool Overlap(const Rect& a,const Rect& b){return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
static bool Inside(const Rect& inner,const Rect& outer){return inner.x>=outer.x&&inner.y>=outer.y&&inner.x+inner.w<=outer.x+outer.w&&inner.y+inner.h<=outer.y+outer.h;}
static int Gap(const Rect& a,const Rect& b){
    const int dx=std::max({0,b.x-(a.x+a.w),a.x-(b.x+b.w)}),dy=std::max({0,b.y-(a.y+a.h),a.y-(b.y+b.h)});
    return std::max(dx,dy);
}
static bool Container(L::Role r){return r==L::Role::Row||r==L::Role::HelpCard||r==L::Role::ReadoutBox||r==L::Role::PresetCard;}
static bool CardPart(L::Role r){
    return r==L::Role::CardTitle||r==L::Role::CardText||r==L::Role::CardState||r==L::Role::CardButton||r==L::Role::PendingEntry||r==L::Role::ChangeLine;
}
static const char* ViewName(View v){return framevr::settings::content::PageOf(v).name;}
// The rows derived from Definitions plus the ini-id overrides equal the
// hand-keyed RowTable they replaced, with changes by design since: the three
// cyclers (spell_lock, controller_labels, vr_resolution) are named lists
// (Choice), and main's frame generation settings arrived.
struct GoldenRow {const char* id;View page;int section;content::RowKind kind;const char* label;int options,names;};
static const GoldenRow Golden[]={
    {"gameplay.motion_controls",View::Combat,0,content::RowKind::Toggle,"Motion combat",0,0},
    {"gameplay.melee_hit_speed",View::Combat,0,content::RowKind::Stepper,"Swing speed to hit",0,0},
    {"gameplay.melee_thrust_speed",View::Combat,0,content::RowKind::Slider,"Stab speed to hit",0,0},
    {"gameplay.melee_reach_cm",View::Combat,0,content::RowKind::Slider,"Melee reach",0,0},   // Discord, 2026-11-08
    {"gameplay.weapon_heft",View::Combat,0,content::RowKind::Toggle,"Weapon heft",0,0},   // #443
    {"gameplay.bow_bash",View::Combat,0,content::RowKind::Toggle,"Bow bash",0,0},   // #443, drawn inside Weapon heft's row
    {"gameplay.skill_gestures",View::Combat,1,content::RowKind::Toggle,"Skills on your moves",0,0},
    {"interactions.weapon_skill_indicators",View::Combat,1,content::RowKind::Toggle,"Skill icons on weapons",0,0},
    {"gameplay.throw_lift_deg",View::Combat,1,content::RowKind::Stepper,"Thrown weapon lift",0,1},
    {"gameplay.spell_lock",View::Targeting,0,content::RowKind::Choice,"Skills go to the aimed monster",4,0},
    {"gameplay.projectile_aim_assist",View::Targeting,0,content::RowKind::Choice,"Spell aim assist",3,0},
    {"gameplay.planar_spell_aim",View::Targeting,1,content::RowKind::Toggle,"Keep spell shots level",0,0},
    {"gameplay.staff_point",View::Targeting,1,content::RowKind::Toggle,"Aim staff along arm",0,0},
    {"gameplay.aim_follows_view",View::Targeting,1,content::RowKind::Toggle,"Aim with your view",0,0},
    {"gameplay.aim_with_eyes",View::Targeting,1,content::RowKind::Toggle,"Aim with your eyes",0,0},   // drawn inside Aim with your view's row, eye trackers only (2026-11-08)
    {"bow.path_laser_hue",View::Targeting,1,content::RowKind::Swatch,"Arrow path line",0,1},
    {"gameplay.ground_aim",View::Targeting,2,content::RowKind::Choice,"Ground skill aim",3,0},
    {"gameplay.landing_circles",View::Targeting,2,content::RowKind::Toggle,"Landing circles",0,0},
    {"gameplay.shield_bash_speed",View::Shield,0,content::RowKind::Slider,"Bash speed to hit",0,0},
    {"gameplay.guard_strain_vignette",View::Shield,0,content::RowKind::Toggle,"Guard strain shadow",0,0},
    {"gameplay.shield_grip_cm",View::Shield,1,content::RowKind::Slider,"Shield along forearm",0,0},
    {"gameplay.shield_out_cm",View::Shield,1,content::RowKind::Slider,"Shield out from arm",0,0},
    {"gameplay.shield_up_cm",View::Shield,1,content::RowKind::Slider,"Shield across forearm",0,0},
    {"gameplay.shield_follow_pct",View::Shield,1,content::RowKind::Slider,"Shield wrist turn",0,2},
    {"gameplay.shield_roll_deg",View::Shield,1,content::RowKind::Slider,"Shield roll",0,0},
    {"gameplay.offline_players",View::Monsters,0,content::RowKind::Stepper,"Offline difficulty",0,2},
    {"gameplay.enemy_sight_pct",View::Monsters,0,content::RowKind::Slider,"Monster sight range",0,0},
    {"gameplay.companion_ai",View::Monsters,1,content::RowKind::Toggle,"Smarter mercenary",0,0},
    {"gameplay.companion_tactical_commands",View::Monsters,1,content::RowKind::Toggle,"Tactical commands",0,0},
    {"gameplay.merc_potion_assist",View::Monsters,1,content::RowKind::Toggle,"Merc potion throw autoaim",0,0},   // #424
    {"gameplay.summon_range_multiplier",View::Monsters,2,content::RowKind::Slider,"Summon range",0,0},   // #426
    {"interactions.bottle_physics",View::Physics,0,content::RowKind::Toggle,"Roll and break bottles",0,0},
    {"interactions.prop_physics",View::Physics,0,content::RowKind::Toggle,"Knock over small props",0,0},
    {"interactions.prop_heft",View::Physics,0,content::RowKind::Toggle,"Props have weight",0,0},   // prop_heft.h, 2026-11-12
    {"interactions.body_physics",View::Physics,0,content::RowKind::Toggle,"Pushable bodies",0,0},   // main's #361
    {"gameplay.look_at_you",View::Physics,1,content::RowKind::Toggle,"Heads turn to you",0,0},   // Monsters and townsfolk (2026-11-09)
    {"gameplay.hit_stagger",View::Physics,2,content::RowKind::Toggle,"Monsters stagger",0,0},   // hit reactions, 2026-11-09
    {"gameplay.hit_knockback",View::Physics,2,content::RowKind::Toggle,"Heavy hits knock back",0,0},
    {"turn.smooth",View::Moving,0,content::RowKind::Toggle,"Smooth turning",0,0},
    {"turn.snap",View::Moving,0,content::RowKind::Stepper,"Snap turn angle",0,0},
    {"turn.speed",View::Moving,0,content::RowKind::Slider,"Smooth turn speed",0,0},
    {"gameplay.run_speed_pct",View::Moving,1,content::RowKind::Slider,"Movement speed",0,12},   // main's #308: Walk, Base run, Run 10..90%, Full run
    {"interactions.seated",View::Body,0,content::RowKind::Toggle,"Seated play",0,0},
    {"gameplay.room_steps",View::Body,0,content::RowKind::Toggle,"Room follows you",0,0},   // drawn inside Seated play's row (2026-11-08)
    {"calibration.version",View::Body,0,content::RowKind::Button,"Height and arm span",0,0},
    {"interactions.t_cross_calibrate",View::Body,0,content::RowKind::Toggle,"T cross to calibrate",0,0},   // #423
    {"interactions.waist_below_eyes",View::Body,1,content::RowKind::Slider,"Belt below eyes",0,0},
    {"interactions.belt_forward",View::Body,1,content::RowKind::Slider,"Belt forward reach",0,0},
    {"interactions.click_to_drink",View::Body,1,content::RowKind::Toggle,"Click to drink",0,0},   // #425
    {"stick.walk_direction",View::Hands,0,content::RowKind::Stepper,"Walk direction",0,2},   // a row of its own since Dominant hand's row went (2026-11-10)
    {"interactions.left_handed",View::Hands,0,content::RowKind::Toggle,"Left-handed (experimental)",0,0},   // #307, a row of its own
    {"interactions.left_handed_sticks_right",View::Hands,0,content::RowKind::Toggle,"Sticks stay right-handed",0,0},   // under it, shown while it is on
    {"gameplay.run_walk_toggle",View::Hands,0,content::RowKind::Toggle,"Stick toggle",0,0},   // a row of its own
    {"interactions.controller_labels",View::Hands,0,content::RowKind::Choice,"Button names",5,0},
    {"haptics.strength",View::Vibration,0,content::RowKind::Slider,"Overall vibration",0,1},
    {"haptics.melee",View::Vibration,0,content::RowKind::Slider,"Melee hits",0,1},
    {"haptics.short_melee",View::Vibration,0,content::RowKind::Toggle,"Short melee taps",0,0},
    {"haptics.hit_patterns",View::Vibration,0,content::RowKind::Toggle,"Hit feel by target",0,0},   // richer haptics, drawn inside Short melee taps' row
    {"haptics.bow_draw",View::Vibration,0,content::RowKind::Slider,"Bow draw",0,1},
    {"haptics.bow_release",View::Vibration,0,content::RowKind::Slider,"Bow and throw release",0,1},
    {"haptics.spells",View::Vibration,0,content::RowKind::Slider,"Spell casts",0,1},
    {"haptics.interactions",View::Vibration,0,content::RowKind::Slider,"Loot, potions and menus",0,1},
    {"haptics.alerts",View::Vibration,0,content::RowKind::Slider,"Low life and mana alerts",0,1},
    {"interactions.vitals_location",View::Wrist,0,content::RowKind::Choice,"Life and mana",4,0},
    {"interactions.vitals_tilt",View::Wrist,0,content::RowKind::Slider,"Life and mana tilt",0,0},
    {"interactions.skill_cooldowns",View::Wrist,0,content::RowKind::Toggle,"Skill cooldowns",0,0},   // wrist skill cooldowns (2026-11-09)
    {"interactions.navigation_location",View::Wrist,1,content::RowKind::Choice,"Map and compass",4,0},
    {"interactions.map_top_down",View::Wrist,1,content::RowKind::Choice,"Map view",4,0},
    {"interactions.map_rotate",View::Wrist,1,content::RowKind::Toggle,"Turn map with you",0,0},
    {"interactions.companion_counts",View::Wrist,1,content::RowKind::Toggle,"Buffs and companions",0,0},
    {"hud.menu_distance",View::Menus,0,content::RowKind::Choice,"Menu distance",3,0},
    {"interactions.leather_menu",View::Menus,0,content::RowKind::Stepper,"Hold menu",0,3},   // main's #360, drawn inside Menu distance's row; Off / On / Menu only (2026-10-29)
    {"hud.tooltip_timing",View::Menus,0,content::RowKind::Choice,"Item details delay",3,0},
    {"hud.labels_size",View::Labels,0,content::RowKind::Slider,"Loot name size",0,0},
    {"interactions.loot_pick",View::Labels,0,content::RowKind::Toggle,"Loot pick",0,0},   // #257: Loot pick, Off / On
    {"hud.monster_names",View::Labels,0,content::RowKind::Choice,"Monster names",3,0},   // All / Bosses and elites / None (2026-11-11; the Weak monster bars toggle before)
    {"loot.beams_brightness",View::Labels,1,content::RowKind::Filter,"Loot beams",0,0},   // loot beams: one row, its brightness slider
    {"loot.beams_unique",View::Labels,1,content::RowKind::Toggle,"Unique",0,0},           // (2026-11-01) and a Filter button whose
    {"loot.beams_set",View::Labels,1,content::RowKind::Toggle,"Set",0,0},                 // card holds Unique, Set, Rare and Runes
    {"loot.beams_rare",View::Labels,1,content::RowKind::Toggle,"Rare",0,0},               // (inFilter: no rows of their own)
    {"loot.beams_runes",View::Labels,1,content::RowKind::Toggle,"Runes",0,0},
    {"gameplay.target_outline_pct",View::Outlines,0,content::RowKind::Slider,"Monster outline",0,1},
    {"gameplay.aim_highlights",View::Outlines,0,content::RowKind::Choice,"Outlines shown",2,0},
    {"gameplay.highlight_left_hue",View::Outlines,0,content::RowKind::Swatch,"Left hand outline color",0,1},
    {"gameplay.highlight_right_hue",View::Outlines,0,content::RowKind::Swatch,"Right hand outline color",0,1},
    {"interactions.critical_alerts",View::Warnings,0,content::RowKind::Toggle,"Low life and mana alert",0,0},
    {"gameplay.low_life_heart_start",View::Warnings,0,content::RowKind::Stepper,"Heartbeat starts at",0,1},
    {"haptics.heartbeat",View::Warnings,0,content::RowKind::Toggle,"Felt heartbeat",0,0},   // richer haptics, drawn inside Heartbeat starts at's row
    {"hud.blood_visor",View::Warnings,0,content::RowKind::Toggle,"Blood on visor",0,0},
    {"hud.blood_wipe",View::Warnings,0,content::RowKind::Toggle,"Wipe blood by hand",0,0},
    {"hud.arcane_decay",View::Warnings,1,content::RowKind::Toggle,"Arcane Decay",0,0},
    {"hud.arcane_fade",View::Warnings,1,content::RowKind::Toggle,"Fade animation",0,0},
    {"hud.mana_threshold_pct",View::Warnings,1,content::RowKind::Slider,"Starts below",0,0},
    {"hud.arcane_intensity_pct",View::Warnings,1,content::RowKind::Slider,"Effect intensity",0,0},
    {"stereo.rendering",View::Performance,0,content::RowKind::Choice,"Eyes drawn per frame",2,0},
    {"render.vr_resolution",View::Performance,0,content::RowKind::Choice,"Render resolution",7,0},
    {"render.fixed_foveation",View::Performance,0,content::RowKind::Choice,"Lower detail at edges",3,0},
    {"render.foveation_strength",View::Performance,0,content::RowKind::Choice,"Edge detail reduction",4,0},
    {"render.rings",View::Performance,1,content::RowKind::Stepper,"Terrain distance",0,0},
    {"render.model_radius",View::Performance,1,content::RowKind::Slider,"Scenery distance",0,0},
    {"render.big_radius",View::Performance,1,content::RowKind::Slider,"Big objects",0,1},   // main's #366, drawn inside Scenery distance's row
    {"render.frame_generation",View::Frames,0,content::RowKind::Choice,"Frame generation",3,0},   // main's #313
    {"render.frame_generation_preset",View::Frames,0,content::RowKind::Choice,"NVIDIA flow quality",3,0},
    {"render.frame_generation_scale",View::Frames,0,content::RowKind::Choice,"NVIDIA flow scale",3,0},
    {"render.defer_gpu_wait",View::Frames,0,content::RowKind::Offer,"Defer GPU wait",0,0},
    {"sky.enabled",View::Environment,0,content::RowKind::Toggle,"Outdoor sky",0,0},
    {"fog.enabled",View::Environment,0,content::RowKind::Toggle,"Distance fog",0,0},
    {"fog.end",View::Environment,0,content::RowKind::Slider,"Fog distance",0,0},   // main's #366, drawn inside Distance fog's row
    {"ceiling.enabled",View::Environment,0,content::RowKind::Toggle,"Dungeon ceilings",0,0},
    {"ceiling.torches",View::Environment,0,content::RowKind::Toggle,"Dungeon torchlight",0,0},   // the game's own lights, drawn inside Dungeon ceilings' row
    {"audio.echo",View::Environment,0,content::RowKind::Choice,"Echo",3,0},   // world sound: the game's own reverb, raised (2026-11-09)
    {"torch.flame",View::Environment,1,content::RowKind::Toggle,"Torch flame",0,0},
    {"torch.spell_hands_pct",View::Environment,1,content::RowKind::Slider,"Spell hand light",0,1},   // a Sorceress's (2026-11-06)
    {"light.radius_boost_pct",View::Environment,1,content::RowKind::Slider,"Light Radius boost",0,0},   // #440
};

static bool PopoverLayer(L::Role r){return r==L::Role::PopoverDim||r==L::Role::Popover||r==L::Role::PopoverTitle||r==L::Role::PopoverOption||r==L::Role::PopoverHint||r==L::Role::ColorDot||r==L::Role::PopoverLabel||r==L::Role::PopoverSwitch;}
// Every geometric rule of one built panel. Pressables are checked by their hit
// box (64 x 64 at least; a row's controls take the full row height and meet
// their neighbors in the gap, so hit boxes never overlap) and drawn at least
// 12 px apart. An open popover is a layer over the page: its parts are checked
// among themselves, and only they take presses.
static void CheckLayout(const L::Layout& layout,const std::string& where){
    const auto& e=layout.elements;
    Check(!e.empty(),where+": elements");
    size_t dim=e.size();for(size_t i=0;i<e.size();++i)if(e[i].role==L::Role::PopoverDim){dim=i;break;}
    for(size_t i=0;i<e.size();++i){
        const auto& a=e[i];
        const std::string name=where+" element "+std::to_string(i)+" ("+a.text+")";
        Check(a.rect.w>0&&a.rect.h>0,name+": has a size");
        Check(Inside(a.rect,L::PanelInner)&&Inside(a.HitBox(),L::PanelInner),name+": inside the panel");
        Check(Inside(a.rect,a.HitBox()),name+": drawn inside its hit box");
        if(CardPart(a.role))Check(Inside(a.rect,layout.card),name+": inside the card");
        if(a.hit.kind!=Kind::None&&a.role!=L::Role::HelpCard&&a.role!=L::Role::PopoverDim&&a.role!=L::Role::Popover){
            Check(a.HitBox().h>=L::MinTarget&&a.HitBox().w>=L::MinTarget,name+": hit target at least 64 x 64");
            if(a.Has(L::Enabled)&&(i>dim||dim==e.size()))Check(L::HitTest(layout,a.rect.x+a.rect.w/2.f,a.rect.y+a.rect.h/2.f)==a.hit,name+": the hit test finds it at its center");
        }
        if(i<dim&&dim<e.size()&&a.hit.kind!=Kind::None)Check(PopoverLayer(L::ElementAt(layout,a.rect.x+a.rect.w/2.f,a.rect.y+a.rect.h/2.f)->role),
                                                            name+": under an open popover, nothing on the page takes a press");
        if(Container(a.role)||a.role==L::Role::PopoverDim||a.role==L::Role::Popover)continue;
        for(size_t j=i+1;j<e.size();++j){
            const auto& b=e[j];
            if(Container(b.role)||b.role==L::Role::PopoverDim||b.role==L::Role::Popover)continue;
            if(PopoverLayer(a.role)!=PopoverLayer(b.role))continue;   // the popover floats over the page
            Check(!Overlap(a.rect,b.rect),name+" overlaps element "+std::to_string(j)+" ("+b.text+")");
            if(a.hit.kind!=Kind::None&&b.hit.kind!=Kind::None){
                Check(Gap(a.rect,b.rect)>=L::MinGap,name+": at least 12 px from element "+std::to_string(j)+" ("+b.text+")");
                Check(!Overlap(a.HitBox(),b.HitBox()),name+": hit box apart from element "+std::to_string(j)+" ("+b.text+")");
            }
        }
    }
    // Containers: rows and preset cards never overlap one another or the card.
    for(size_t i=0;i<e.size();++i)for(size_t j=i+1;j<e.size();++j){
        const auto& a=e[i];const auto& b=e[j];
        const bool ra=a.role==L::Role::Row||a.role==L::Role::PresetCard||a.role==L::Role::ReadoutBox||a.role==L::Role::HelpCard;
        const bool rb=b.role==L::Role::Row||b.role==L::Role::PresetCard||b.role==L::Role::ReadoutBox||b.role==L::Role::HelpCard;
        if(ra&&rb)Check(!Overlap(a.rect,b.rect),where+": containers apart ("+a.text+" / "+b.text+")");
    }
    // The help card's rectangle holds the HelpCard element.
    for(const auto& a:e)if(a.role==L::Role::HelpCard)Check(a.rect.x==layout.card.x&&a.rect.y==layout.card.y&&a.rect.w==layout.card.w&&a.rect.h==layout.card.h,where+": card rect");
    // An open popover: inside the content area, at most 600 px tall, never over its own row; its parts inside it.
    if(layout.popover.w>0){
        const Rect& p=layout.popover;
        Check(p.x>=L::ContentX&&p.x+p.w<=L::ContentRight&&p.y>=L::ViewTop&&p.y+p.h<=L::FooterY,where+": the popover inside the content area");
        Check(p.h<=L::PopMaxH,where+": the popover at most 600 px tall");
        Check(!Overlap(p,layout.popoverRow),where+": the popover never covers its own row");
        for(const auto& a:e)if(PopoverLayer(a.role)&&a.role!=L::Role::PopoverDim)Check(Inside(a.HitBox(),p),where+": popover part inside it ("+a.text+")");
    }
}

static Snapshot Ready(const Values& v=Defaults()){Snapshot s;s.values=v;s.ready=true;s.offlineDifficultyAvailable=true;return s;}
static const L::Element* FindRole(const L::Layout& l,L::Role role,Key key=Count){
    for(const auto& e:l.elements)if(e.role==role&&(key==Count||e.key==key))return &e;
    return nullptr;
}
static int CountRole(const L::Layout& l,L::Role role){int n=0;for(const auto& e:l.elements)if(e.role==role)++n;return n;}

// vr-settings-layout-test <view> [card]: print that view's elements (title screen, defaults) and exit.
static int Dump(int view,int card){
    Model m;m.Open(Ready());L::Press(m,{Kind::Open,view,-1},0);
    L::LayoutInput in;in.card=card;in.runtimeStatus="Foveated shading available.";
    const auto l=L::Build(m,{},in);
    for(const auto& e:l.elements)std::printf("%2d %4d %4d %4d %4d hit%d/%d/%d f%03x %s | %s\n",int(e.role),e.rect.x,e.rect.y,e.rect.w,e.rect.h,int(e.hit.kind),e.hit.index,e.hit.delta,e.flags,e.text.c_str(),e.sub.c_str());
    std::printf("card %d %d %d %d\n",l.card.x,l.card.y,l.card.w,l.card.h);
    return 0;
}
int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])!="--left-handed")return Dump(std::atoi(argv[1]),argc>2?std::atoi(argv[2]):NoHelp);
    const auto& tables=L::ContentTables();
    // ---- every key reachable: one row, or an override keeping it off the menu ----
    {
        for(int k=0;k<Count;++k){
            int n=0;for(size_t i=0;i<tables.rowCount;++i)if(tables.rows[i].key==k)++n;
            bool hidden=false;
            for(const auto& o:content::Overrides)if(o.page==content::NotInMenu&&content::KeyOfId(o.id)==k)hidden=true;
            Check(n==(hidden?0:1),std::string("key reachable once (a row or NotInMenu): ")+Definitions[k].label);
        }
        for(size_t i=0;i<tables.rowCount;++i)Check(content::PageOf(tables.rows[i].page).settings,"rows live on settings pages");
    }
    // ---- the idle card: the page's intro when the page did not draw it, then a sentence that stands alone ----
    for(int view=0;view<int(View::Count);++view)if(content::PageOf(View(view)).settings&&View(view)!=View::More){
        Model m;m.Open(Ready());L::Press(m,{Kind::Open,view,-1},0);
        const auto l=L::Build(m,{},{});const auto* text=FindRole(l,L::Role::CardText);
        const std::string what="A setting's help shows here, with Undo and Default when they apply.",intro=content::PageOf(View(view)).intro;
        Check(text&&text->text==(FindRole(l,L::Role::Intro)?what:intro+" "+what),std::string("the idle card reads whole: ")+ViewName(View(view)));
    }
    // ---- the map: no row lost, none left to More, the names and the owner's pairs ----
    {
        Check(tables.rowCount==114,"114 settings on the menu, each one row");   // + Props have weight (2026-11-12)   // + Sticks stay right-handed (2026-11-11)   // - Dominant hand (2026-11-10: Left-handed is the one switch)   // + Left-handed (#307)   // 94 in #419, + Melee reach, Walk direction, Room follows you (2026-11-08), Light Radius boost (#440), + Weapon heft, Bow bash (#443), + Aim with your eyes (2026-11-08), Merc potion throw autoaim (#424), Click to drink (#425), Skill cooldowns (2026-11-09), + Summon range (#426), T cross to calibrate (#423), Heads turn to you (2026-11-09), Echo (world sound, 2026-11-09), + Monsters stagger, Heavy hits knock back (hit reactions, 2026-11-09), + Hit feel by target, Felt heartbeat (richer haptics)
        for(size_t i=0;i<tables.rowCount;++i)Check(!tables.rows[i].fallback&&tables.rows[i].page!=View::More,std::string("every setting has its page: ")+tables.rows[i].label);
        Check(content::Menu().report.empty(),"the tidy report is empty");
        struct Want {content::Group group;const char* name;std::vector<const char*> pages;};
        const std::vector<Want> rail={
            {content::Group::Gameplay,"Gameplay",{"Combat","Targeting","Shield","Monsters","Physics"}},
            {content::Group::Comfort,"Comfort",{"Moving","Body"}},
            {content::Group::Controls,"Controls",{"Hands","Vibration"}},
            {content::Group::Interface,"Interface",{"Wrist","Menus","Labels","Outlines","Warnings"}},
            {content::Group::Graphics,"Graphics",{"Performance","Frames","Environment"}}};
        Check(std::size(content::GroupTable)==rail.size(),"five groups");
        for(size_t g=0;g<rail.size()&&g<std::size(content::GroupTable);++g){
            Check(content::GroupTable[g].id==rail[g].group&&std::string(content::GroupTable[g].name)==rail[g].name,std::string("group in order: ")+rail[g].name);
            std::vector<std::string> names;for(const auto& p:content::PageTable)if(p.group==rail[g].group)names.push_back(p.name);
            Check(names==std::vector<std::string>(rail[g].pages.begin(),rail[g].pages.end()),std::string("its pages in order: ")+rail[g].name);
        }
        // A pair the owner placed stays a pair: the switch right after its row, on it.
        const auto besideOf=[&](Key k){for(size_t i=1;i<tables.rowCount;++i)if(tables.rows[i].key==k&&tables.rows[i].beside)return int(tables.rows[i-1].key);return -1;};
        Check(besideOf(LeatherMenu)==MenuDistance,"Hold menu beside Menu distance");
        Check(besideOf(FogDistance)==Fog,"Fog distance beside Distance fog");
        Check(besideOf(CeilingTorches)==Ceiling,"Dungeon torchlight beside Dungeon ceilings");
        Check(besideOf(CompanionTactical)==CompanionAi,"Commands beside Smarter mercenary");
        Check(besideOf(LootPick)==LootLabelSize,"Loot pick beside Loot name size");
        Check(besideOf(BowBash)==WeaponHeft,"Bow bash beside Weapon heft (#443: the Combat page's 8th row holds both)");
        Check(L::CountRows(View::Combat,Defaults(),{})==8,"Combat holds 8 rows at the title screen, every class's");
        Check(besideOf(AimWithEyes)==AimFollowsView,"Eyes beside Aim with your view");
        Check(L::FilterOf(LootBeamBrightness).count==4,"Loot beams with its 4 filter switches");
        int readouts=0;for(int v=0;v<int(View::Count);++v)if(L::HasReadout(View(v)))++readouts;
        Check(readouts==2&&L::HasReadout(View::Performance)&&L::HasReadout(View::Frames),"the frame times on Performance and Frames, nowhere else");
    }
    // ---- golden: the merged rows equal the table they replaced ----
    {
        Check(tables.rowCount==std::size(Golden),"as many rows as before");
        for(size_t i=0;i<std::size(Golden)&&i<tables.rowCount;++i){
            const auto& g=Golden[i];const auto& r=tables.rows[i];
            const std::string where=std::string("golden row ")+g.id;
            Check(content::IdOf(r.key)==g.id&&r.page==g.page&&r.section==g.section&&r.kind==g.kind&&std::string(r.label)==g.label&&
                  r.optionCount==g.options&&r.nameCount==g.names,where);
        }
        Check(content::KeyOfId("gameplay.highlight_left_hue")==HighlightLeft&&content::KeyOfId("gameplay.no_such_key")<0&&content::KeyOfId("gameplay")<0,"ini ids resolve to their Key");
    }
    // ---- at most 8 rows, every class, weapon and dependency state ----
    std::vector<Context> contexts;
    for(int c=-1;c<=6;++c)contexts.push_back({c});
    contexts.push_back({heroclass::Paladin,true});contexts.push_back({heroclass::Assassin,false,true});contexts.push_back({heroclass::Sorceress,false,false,true});
    contexts.push_back({heroclass::Barbarian,true,true,true});
    contexts.push_back({-1,false,false,false,true});contexts.push_back({heroclass::Amazon,true,false,false,true});   // a headset with eye tracking: Eyes beside Aim with your view
    auto state=[](int bits){
        Values v=Defaults();
        v[SmoothTurn]=float(bits&1);v[Seated]=float((bits>>1)&1);v[TargetHighlight]=(bits>>2)&1?60.f:0.f;
        v[SpellLock]=(bits>>3)&1?3.f:0.f;v[Foveation]=float((bits>>4)&1);v[BloodVisor]=float((bits>>5)&1);v[DeferGpuWait]=float((bits>>6)&1);
        v[GroundAim]=float((bits>>7)%3);
        return v;
    };
    constexpr int States=128*3;
    for(const auto& c:contexts)for(int s=0;s<States;++s){
        const Values v=state(s);
        for(int view=0;view<int(View::Count);++view){
            const int n=L::CountRows(View(view),v,c);
            Check(n<=L::MaxViewRows,std::string("at most 8 rows: ")+ViewName(View(view)));
            if(View(view)==View::More)Check(n==0,"More shows nothing in the real menu, every class and state");
            Check(!(RowShown(Snap,v,c)&&RowShown(TurnSpeed,v,c)),"Snap turn angle and Smooth turn speed are never shown together (the compile-time count relies on it)");
            if(content::PageOf(View(view)).settings&&View(view)!=View::More)Check(n>=1,std::string("a settings page shows rows: ")+ViewName(View(view)));
        }
    }
    {
        const auto* drinking=content::RowOf(ClickToDrink);
        Check(drinking&&!drinking->beside&&drinking->page==View::Body,"Click to drink has its own row on Body");
        Values v=Defaults();v[Seated]=0;   // standing: Belt below eyes shows too
        Check(L::CountRows(View::Body,v,{})<=8,"Body fits eight rows with Click to drink, standing");   // #425: Survival, ==8, before the menu groups
        const auto rows=L::RowsOf(View::Body,v,{});
        bool found=false;
        for(int i=0;i<rows.count;++i)if(rows.keys[size_t(i)]==ClickToDrink)found=true;
        Check(found,"Click to drink appears on Body");
    }
    // Title screen, everything on: the fullest pages.
    {
        Values v=state(0b0100'0101);v[GroundAim]=2;
        Check(L::CountRows(View::Vibration,v,{})==8&&L::CountRows(View::Shield,v,{})==7,"Vibration holds 8 rows; Shield 7 (one guard strain switch since main's #310)");
        Check(L::RowsOf(View::Outlines,v,{}).count==4,"Outlines: the outline, which hands, and both hand colors as their own rows");
        Check(L::CountRows(View::Monsters,v,{})<=8,"Monsters: difficulty, the mercenary and merc potion throw autoaim fit on eight rows");   // #424 (on Aim, ==8, before the menu groups)
        bool right=false;const auto rows=L::RowsOf(View::Outlines,v,{});
        for(int i=0;i<rows.count;++i)if(rows.keys[size_t(i)]==HighlightRight)right=true;
        Check(right&&RowShown(HighlightRight,v,{}),"the right hand's color has its own row");
        v[TargetHighlight]=0;Check(!RowShown(HighlightRight,v,{})&&!RowShown(HighlightLeft,v,{}),"and both hand colors go with the outline");
    }

    // ---- geometry: every view, class and state, with cards, drafts and footers ----
    int built=0;
    for(size_t ci=0;ci<contexts.size();++ci){
        const Context& c=contexts[ci];
        for(int s=0;s<States;s+=(ci<2?1:23)){
            Model m;Snapshot snap=Ready(state(s));m.Open(snap);m.SetContext(c);
            for(int view=0;view<int(View::Count);++view){
                L::Press(m,{Kind::Open,view,-1},0);
                const std::string where=std::string(ViewName(View(view)))+" class "+std::to_string(c.heroClass)+" state "+std::to_string(s);
                L::LayoutInput in;in.runtimeStatus="Foveated shading available. Eye tracking unavailable on this headset/runtime.";
                in.performance={true,true,90,11.1f,3.4f,2.6f,4.1f,.9f};in.game.vsync=s&1;in.monoAudio=(s%5)==0;
                const auto layout=L::Build(m,c,in);CheckLayout(layout,where);++built;
                // Point at each row: its card (buttons, hints) still fits.
                if(s%7==0)for(const auto& e:layout.elements)if(e.role==L::Role::Row){
                    L::LayoutInput pointed=in;pointed.card=e.key;CheckLayout(L::Build(m,c,pointed),where+" card "+Definitions[e.key].label);++built;
                }
            }
        }
    }
    // Drafts everywhere: pending cards, row cards with Undo / Default / Feel it, confirm, presets.
    {
        Model m;m.Open(Ready());
        for(int k=0;k<Count;++k){
            const auto* r=tables.Row(Key(k));
            if(!r||r->kind==content::RowKind::Button)continue;
            const auto& d=Definitions[k];
            L::Press(m,{Kind::Set,k,0,Normalize(Key(k),d.defaultValue==d.min?d.min+d.step*2:d.min)},0);
        }
        Check(m.Dirty(),"a draft on every page");
        for(int view=0;view<int(View::Count);++view){
            L::Press(m,{Kind::Open,view,-1},0);
            const std::string where=std::string("draft ")+ViewName(View(view));
            for(int saving=0;saving<2;++saving){
                L::LayoutInput in;
                const auto layout=L::Build(m,m.context,in);CheckLayout(layout,where);
                if(content::PageOf(View(view)).settings&&view!=int(View::Performance))Check(FindRole(layout,L::Role::PendingEntry)||FindRole(layout,L::Role::ChangeLine),where+": the idle card lists what is not applied");
                for(const auto& e:layout.elements){
                    if(e.role==L::Role::Row||e.role==L::Role::Tab||e.role==L::Role::GroupTab||e.role==L::Role::PresetCard){
                        L::LayoutInput p;p.card=e.help;p.hover=e.hit;CheckLayout(L::Build(m,m.context,p),where+" card "+std::to_string(e.help));
                    }
                }
                for(int id:{HelpApply,HelpClose,HelpRecenter,HelpCalibrate,HelpMonoAudio,HelpPerformance,HelpResetPage,HelpUndoReset,HelpGuide,HelpWhatsNew,HelpPresets,HelpDiscard,HelpKeepEditing,HelpApplyClose,HelpPending}){
                    L::LayoutInput p;p.card=id;const auto pl=L::Build(m,m.context,p);CheckLayout(pl,where+" help "+std::to_string(id));
                    Check(FindRole(pl,L::Role::CardTitle)&&!FindRole(pl,L::Role::CardTitle)->text.empty(),where+": every help subject has a title");
                }
            }
        }
        // Status strip and confirm row on a full page.
        L::Press(m,{Kind::Open,int(View::Vibration),-1},0);
        L::Press(m,{Kind::Close},1000);
        Check(m.confirmClose,"Close with a draft asks first");
        const auto confirm=L::Build(m,m.context,{});CheckLayout(confirm,"confirm row");
        Check(FindRole(confirm,L::Role::ConfirmDiscard)&&FindRole(confirm,L::Role::ConfirmKeep)&&FindRole(confirm,L::Role::ConfirmApply)&&!FindRole(confirm,L::Role::FooterApply),"the footer turns into Discard / Keep editing / Apply and close");
        Check(FindRole(confirm,L::Role::CardTitle)->text=="Close without applying?","and the card asks");
        Request r=L::Press(m,{Kind::ApplyClose},1100);
        Check(r.sequence==0&&m.confirmClose,"the confirm row ignores presses for 300 ms");
        r=L::Press(m,{Kind::ApplyClose},1300);
        Check(r.sequence!=0&&!m.confirmClose&&m.closeWhenSaved&&m.open,"Apply and close saves first");
        const auto saving=L::Build(m,m.context,{});CheckLayout(saving,"saving");
        Check(FindRole(saving,L::Role::StatusStrip)&&FindRole(saving,L::Role::StatusStrip)->text=="Saving...","the save state shows above the footer");
        for(const auto& e:saving.elements)if(e.role==L::Role::StepPlus||e.role==L::Role::Chip||e.role==L::Role::Toggle)Check(!e.Has(L::Enabled),"no change while saving");
        Check(FindRole(saving,L::Role::FooterClose)->Has(L::Enabled)&&[&]{for(const auto& e:saving.elements)if(e.role==L::Role::Tab&&e.hit.index!=int(View::Presets)&&!e.Has(L::Enabled))return false;return true;}(),"tabs (Presets is grayed out while presets are off) and Close still work while saving");
        Snapshot done;done.values=r.values;done.ready=true;done.sequence=r.sequence;done.result=Result::Saved;
        m.Observe(done);
        Check(!m.open&&!m.Dirty(),"and closes once saved");
    }

    // ---- presses ----
    {
        Model m;Snapshot snap=Ready();m.Open(snap);
        Check(m.view==View::Guide&&m.lastPage==View::Combat,"the panel opens on the Controller guide");
        auto guide=L::Build(m,{},{});CheckLayout(guide,"guide");
        Check(CountRole(guide,L::Role::GuideLine)>=16,"the guide lists both hands");
        Check(!FindRole(guide,L::Role::FooterResetPage)&&!FindRole(guide,L::Role::FooterApply)&&FindRole(guide,L::Role::FooterClose),"the guide's footer: Close only");
        L::Press(m,{Kind::Open,int(View::Shield),-1},0);Check(m.view==View::Shield&&m.lastPage==View::Shield,"a tab opens its page");
        L::Press(m,{Kind::Open,int(View::Guide),-1},0);L::Press(m,{Kind::Open,int(View::Guide),-1},0);
        Check(m.view==View::Shield,"the Guide link again returns to the page");
        L::Press(m,{Kind::Open,int(View::Outlines),TargetHighlight},0);Check(m.view==View::Outlines&&m.focusKey==TargetHighlight,"Open points the card at its row");
        // Set and its rules.
        L::Press(m,{Kind::Set,ProjectileAimAssist,0,1},0);
        Check(m.Get()[ProjectileAimAssist]==1&&m.Applied()[ProjectileAimAssist]==2&&m.Dirty(),"Set changes the draft only");
        L::Press(m,{Kind::Undo,ProjectileAimAssist},0);Check(!m.Dirty(),"Undo: back to the applied value");
        L::Press(m,{Kind::Set,ShieldRoll,0,90},0);L::Press(m,{Kind::Default,ShieldRoll},0);Check(m.Get()[ShieldRoll]==0,"Default");
        L::Press(m,{Kind::Set,DeferGpuWait,0,1},0);Check(m.Get()[DeferGpuWait]==1,"Defer GPU wait: the offer turns it on");
        L::Press(m,{Kind::Set,DeferGpuWait,0,0},0);L::Press(m,{Kind::Default,DeferGpuWait},0);Check(m.Get()[DeferGpuWait]==1,"and nothing in the menu turns it off");
        L::Press(m,{Kind::Undo,DeferGpuWait},0);Check(m.Get()[DeferGpuWait]==0,"except Undo before Apply");
        L::Press(m,{Kind::Open,int(View::Performance),-1},0);
        {L::LayoutInput in;in.performance.valid=false;auto perf=L::Build(m,{},in);CheckLayout(perf,"perf");
         const auto* offer=FindRole(perf,L::Role::Button,DeferGpuWait);Check(offer&&offer->Has(L::Enabled),"the readout offers Defer GPU wait while it is Off");
         m.Set(DeferGpuWait,1);perf=L::Build(m,{},in);Check(!FindRole(perf,L::Role::Button,DeferGpuWait),"and not once it is On");m.Set(DeferGpuWait,0);}
        // Lists wrap both ways: a cycler or color list goes round; numbers stop at their ends.
        m.Set(ControllerLabels,4);L::Press(m,{Kind::Change,ControllerLabels,1},0);Check(m.Get()[ControllerLabels]==0,"a cycler wraps from its last option to the first");
        L::Press(m,{Kind::Change,ControllerLabels,-1},0);Check(m.Get()[ControllerLabels]==4,"and from the first to the last");
        m.Set(TargetHighlight,60);m.Set(HighlightRight,360);L::Press(m,{Kind::Change,HighlightRight,1},0);Check(m.Get()[HighlightRight]==0,"a hand color wraps from its last hue to By range");
        L::Press(m,{Kind::Change,HighlightRight,-1},0);Check(m.Get()[HighlightRight]==360,"and back");
        m.Set(BowLaser,345);L::Press(m,{Kind::Change,BowLaser,1},0);Check(m.Get()[BowLaser]==LaserOff,"the arrow path line wraps to Off");
        L::Press(m,{Kind::Change,BowLaser,-1},0);Check(m.Get()[BowLaser]==345,"and back from Off");
        m.Set(ShieldRoll,180);L::Press(m,{Kind::Change,ShieldRoll,1},0);Check(m.Get()[ShieldRoll]==180,"a number stops at its end");
        m.Set(ShieldRoll,-180);L::Press(m,{Kind::Change,ShieldRoll,-1},0);Check(m.Get()[ShieldRoll]==-180,"at both ends");
        for(size_t i=0;i<tables.rowCount;++i){   // every list row wraps, every number stops
            const auto& r=tables.rows[i];using content::RowKind;
            const bool list=r.kind==RowKind::Cycler||r.kind==RowKind::Choice||r.kind==RowKind::Swatch;
            const bool number=r.kind==RowKind::Stepper||r.kind==RowKind::Slider;
            if(list)Check(L::WrapsRound(r),std::string("a list wraps: ")+r.label);
            if(number)Check(!L::WrapsRound(r),std::string("a number stops: ")+r.label);
        }
        L::Press(m,{Kind::Change,MeleeThrustSpeed,0},0);Check(m.Get()[MeleeThrustSpeed]==2,"a drag of zero steps changes nothing");
        L::Press(m,{Kind::Change,MeleeThrustSpeed,4},0);Check(m.Get()[MeleeThrustSpeed]==3,"a drag of n steps");
        m.Discard();Check(!m.Dirty(),"Discard drops the draft");
    }
    {   // Spells go to the aimed monster: a Paladin keeps the saved bow bit.
        Values v=Defaults();v[SpellLock]=2;Model m;m.Open(Ready(v));m.SetContext({heroclass::Paladin});
        L::Press(m,{Kind::Open,int(View::Targeting),-1},0);
        const auto aim=L::Build(m,m.context,{});CheckLayout(aim,"aim paladin");
        int chips=0;const L::Element* on=nullptr;
        for(const auto& e:aim.elements)if(e.role==L::Role::Chip&&e.key==SpellLock){++chips;if(e.text=="On")on=&e;}
        Check(chips==2&&on,"Off / On for a class without bow skills");
        L::Press(m,on->hit,0);Check(m.Get()[SpellLock]==3,"On sets spells and keeps bow skills");
        L::Press(m,{Kind::Default,SpellLock},0);Check(m.Get()[SpellLock]==2,"Default turns spells off, bow skills kept");
        // Presets never touch the owner's keys, calibration or rows hidden for the class.
        m.Set(StereoRendering,1);m.Set(CompanionAi,1);
        for(size_t p=0;p<tables.presetCount;++p){
            L::ApplyPreset(m,int(p));   // the preset logic, while presets are off (content::PresetsReady)
            Check(m.Get()[StereoRendering]==1&&m.Get()[CompanionAi]==1&&m.Get()[DeferGpuWait]==0&&m.Get()[EyeHeight]==m.Applied()[EyeHeight]&&m.Get()[ArmSpan]==m.Applied()[ArmSpan],
                  std::string("preset leaves the owner's keys and calibration: ")+tables.presets[p].name);
            Check(m.Get()[GroundAim]==0&&m.Get()[LandingCircles]==Defaults()[LandingCircles]&&m.Get()[BowLaser]==Defaults()[BowLaser]&&m.Get()[StaffPoint]==Defaults()[StaffPoint],
                  std::string("and the Paladin's hidden rows: ")+tables.presets[p].name);
            Check((int(m.Get()[SpellLock])&2)==2,"and the saved bow-skills bit");
            Check(m.pickedPreset==int(p),"the picked preset is remembered");
        }
        m.view=View::Presets;   // the Presets view as it will be (its tab opens nothing while presets are off)
        for(int p=0;p<int(tables.presetCount);++p){L::LayoutInput in;in.card=HelpPresetFirst+p;CheckLayout(L::Build(m,m.context,in),"preset card");}
    }
    {   // Presets are off (content::PresetsReady false): the tab is grayed out and explains; no path applies one.
        static_assert(!content::PresetsReady,"this build ships with presets off");
        Model m;m.Open(Ready());L::Press(m,{Kind::Open,int(View::Body),-1},0);
        const auto body=L::Build(m,{},{});CheckLayout(body,"body with presets off");
        const L::Element* tab=nullptr;for(const auto& e:body.elements)if(e.role==L::Role::Tab&&e.hit==Hit{Kind::Open,int(View::Presets),-1})tab=&e;
        Check(tab&&!tab->Has(L::Enabled)&&tab->text=="Presets","the Presets tab stays in the rail, grayed out (not pressable)");
        Check(L::HitTest(body,tab->rect.x+20.f,tab->rect.y+20.f).kind==Kind::None,"a press on it lands on nothing");
        Check(L::HelpAt(body,tab->rect.x+20.f,tab->rect.y+20.f)==HelpTabFirst+int(View::Presets),"pointing at it still brings its card");
        L::LayoutInput card;card.card=HelpTabFirst+int(View::Presets);
        const auto explained=L::Build(m,{},card);
        Check(FindRole(explained,L::Role::CardText)&&FindRole(explained,L::Role::CardText)->text==content::PresetsLater,"its card says presets come later");
        const Values before=m.Get();
        L::Press(m,{Kind::Open,int(View::Presets),-1},0);Check(m.view==View::Body,"Open never reaches the Presets view (rail, Guide, What's new or a card)");
        L::Press(m,{Kind::Open,int(View::Guide),-1},0);L::Press(m,{Kind::Open,int(View::Guide),-1},0);Check(m.view==View::Body,"the Guide link again returns to the page, not Presets");
        for(size_t p=0;p<tables.presetCount;++p){
            L::Press(m,{Kind::Preset,int(p)},0);
            Check(m.Get()==before&&!m.Dirty()&&m.pickedPreset<0,std::string("no preset applies by a press: ")+tables.presets[p].name);
        }
        Check(m.Click({Kind::Apply}).sequence==0,"so there is nothing to Apply");
        m.view=View::Presets;L::Press(m,{Kind::Preset,0},0);Check(!m.Dirty(),"not even from the Presets view itself");
        for(int v=0;v<int(View::Count);++v){   // no other view offers a preset card
            Model x;x.Open(Ready());L::Press(x,{Kind::Open,v,-1},0);
            for(const auto& e:L::Build(x,{},{}).elements)Check(e.hit.kind!=Kind::Preset,std::string("no preset card on ")+ViewName(View(v)));
        }
    }
    {   // The rail (docs/VR_SETTINGS_MENU.md): Presets, a row per group, the open group's pages under it.
        using content::Group;
        const int groups=int(std::size(content::GroupTable));
        auto railOf=[](const L::Layout& l){std::vector<const L::Element*> list;for(const auto& e:l.elements)if(e.role==L::Role::Tab||e.role==L::Role::GroupTab)list.push_back(&e);return list;};
        auto pagesOf=[](Group g){std::vector<View> list;for(const auto& p:content::PageTable)if(p.group==g)list.push_back(p.id);return list;};
        auto groupHit=[](Group g){return Hit{Kind::OpenGroup,int(g),-1};};
        auto at=[](const L::Layout& l,const L::Element* e){return L::HitTest(l,float(e->rect.x+e->rect.w/2),float(e->rect.y+e->rect.h/2));};
        for(int gi=0;gi<groups;++gi){
            const auto& g=content::GroupTable[size_t(gi)];const auto pages=pagesOf(g.id);
            const std::string name=std::string("rail, ")+g.name+" open";
            Model m;m.Open(Ready());
            L::Press(m,groupHit(g.id),0);
            Check(m.view==pages.front(),name+": a fresh group opens its first page");
            for(View page:pages){
                L::Press(m,{Kind::Open,int(page),-1},0);
                const auto l=L::Build(m,{},{});CheckLayout(l,name+" on "+ViewName(page));   // 64 px targets, 12 px apart
                const auto rail=railOf(l);
                Check(int(rail.size())==1+groups+int(pages.size()),name+": Presets, every group row and the open group's pages");
                for(size_t i=0;i<rail.size();++i)
                    Check(rail[i]->rect==Rect{L::RailX,L::TabY+int(i)*L::TabPitch,L::RailW,L::TabH},name+": one pitch apart, top down");
                Check(rail[0]->hit==Hit{Kind::Open,int(View::Presets),-1}&&!rail[0]->Has(L::Enabled),name+": Presets heads the rail, still grayed out");
                size_t row=1;
                for(int k=0;k<groups;++k){
                    const auto* e=rail[row++];const Group id=content::GroupTable[size_t(k)].id;
                    Check(e->role==L::Role::GroupTab&&e->hit==groupHit(id)&&e->Has(L::Enabled)&&at(l,e)==e->hit,name+": the group rows in order, each pressable");
                    Check(e->Has(L::Selected)==(k==gi),name+": only the open group is Selected");
                    // A group row's place depends only on which group is open: the rows above it sit where they sit
                    // with nothing open, the rows below move down by its pages.
                    Check(e->rect.y==L::TabY+(1+k+(k>gi?int(pages.size()):0))*L::TabPitch,name+": the rows above the open group never move");
                    if(k!=gi)continue;
                    for(size_t p=0;p<pages.size();++p){
                        const auto* t=rail[row++];
                        Check(t->role==L::Role::Tab&&t->hit==Hit{Kind::Open,int(pages[p]),-1}&&t->option==(p+1==pages.size()?2:1),name+": its pages under it, indented, the last marked");
                        Check(t->Has(L::Selected)==(pages[p]==page)&&t->Has(L::Enabled),name+": the page shown is Selected");
                    }
                }
                for(int other=0;other<groups;++other){   // every group's card fits, open or not
                    L::LayoutInput in;in.card=HelpGroupFirst+int(content::GroupTable[size_t(other)].id);
                    const auto c=L::Build(m,{},in);CheckLayout(c,name+" card of "+content::GroupTable[size_t(other)].name);
                    Check(FindRole(c,L::Role::CardTitle)->text==content::GroupTable[size_t(other)].name&&FindRole(c,L::Role::CardText)&&
                          FindRole(c,L::Role::CardText)->text.find(content::PageOf(pagesOf(content::GroupTable[size_t(other)].id).back()).name)!=std::string::npos,
                          name+": a group's card names it and lists its pages");
                }
            }
        }
        // Every rail page in 2 presses or fewer, from every view: its tab when its group is open, else its group's row, then its tab.
        for(int from=0;from<int(View::Count);++from)for(const auto& p:content::PageTable){
            if(!content::GroupOf(p.group))continue;
            Model m;m.Open(Ready());L::Press(m,{Kind::Open,int(View::Shield),-1},0);L::Press(m,{Kind::Open,int(View::Menus),-1},0);
            m.view=View(from);
            int presses=0;
            while(m.view!=p.id&&presses<3){
                const auto l=L::Build(m,{},{});
                const L::Element* tab=nullptr;for(const auto& e:l.elements)if(e.role==L::Role::Tab&&e.hit==Hit{Kind::Open,int(p.id),-1})tab=&e;
                const L::Element* press=tab?tab:L::Find(l,groupHit(p.group));
                Check(press!=nullptr,std::string("a way to ")+p.name);
                L::Press(m,at(l,press),0);++presses;
            }
            Check(m.view==p.id&&presses<=2,std::string(p.name)+" in 2 presses or fewer from "+ViewName(View(from)));
        }
        {   // Last-page memory (in memory only, #352) and the open group's row.
            Model m;m.Open(Ready());
            L::Press(m,{Kind::Open,int(View::Shield),-1},0);
            L::Press(m,groupHit(Group::Interface),0);Check(m.view==View::Wrist,"a fresh group opens its first page");
            L::Press(m,{Kind::Open,int(View::Menus),-1},0);
            L::Press(m,groupHit(Group::Gameplay),0);Check(m.view==View::Shield,"a group opens the page last opened in it");
            L::Press(m,groupHit(Group::Interface),0);Check(m.view==View::Menus,"each group keeps its own");
            const uint64_t before=m.revision;L::Press(m,groupHit(Group::Interface),0);Check(m.view==View::Menus&&m.revision==before,"pressing the open group stays put");
            L::Press(m,{Kind::Open,int(View::Guide),-1},0);
            {const auto l=L::Build(m,{},{});const auto* row=L::Find(l,groupHit(Group::Interface));
             Check(row&&row->Has(L::Selected)&&L::Find(l,{Kind::Open,int(View::Menus),-1}),"the Guide keeps the last page's group open: the rail holds still");}
            L::Press(m,groupHit(Group::Interface),0);Check(m.view==View::Menus,"and its row opens that page again");
            L::Press(m,{Kind::Open,int(View::Targeting),SpellLock},0);L::Press(m,groupHit(Group::Graphics),0);L::Press(m,groupHit(Group::Gameplay),0);
            Check(m.view==View::Targeting,"a page opened any way (a pending entry) is its group's last page");
            m.Close();m.Open(Ready());L::Press(m,groupHit(Group::Graphics),0);L::Press(m,groupHit(Group::Gameplay),0);
            Check(m.view==View::Targeting,"kept while the panel closes and opens");
            m.Close();m.StartOver();m.Open(Ready());L::Press(m,groupHit(Group::Gameplay),0);
            Check(m.view==View::Combat,"StartOver (a new game): every group back to its first page");
            for(int bad:{int(Group::Tools),int(Group::Header),-1,MaxRailGroups,99})L::Press(m,{Kind::OpenGroup,bad,-1},0);
            Check(m.view==View::Combat,"no group row, no press: Tools, Header, out of range");
        }
        {   // The changed mark: a group row shows any change on its pages, open or closed.
            Model m;m.Open(Ready());L::Press(m,{Kind::Open,int(View::Wrist),-1},0);m.Set(ShieldRoll,90);
            auto l=L::Build(m,{},{});CheckLayout(l,"rail, a change on Shield");
            Check(L::Find(l,groupHit(Group::Gameplay))->Has(L::Changed)&&!L::Find(l,groupHit(Group::Interface))->Has(L::Changed),"a closed group's row shows its pages' change");
            L::LayoutInput in;in.card=HelpGroupFirst+int(Group::Gameplay);
            const auto c=L::Build(m,{},in);CheckLayout(c,"Gameplay's card with a change");
            Check(FindRole(c,L::Role::CardState)&&FindRole(c,L::Role::CardState)->text=="1 not applied","its card counts what is not applied");
            Check(FindRole(c,L::Role::CardText)->text=="Its pages: Combat, Targeting, Shield, Monsters and Physics. Press to open Combat.","its card names its pages and the page a press opens");
            L::Press(m,groupHit(Group::Gameplay),0);l=L::Build(m,{},{});
            Check(L::Find(l,groupHit(Group::Gameplay))->Has(L::Changed)&&L::Find(l,{Kind::Open,int(View::Shield),-1})->Has(L::Changed)&&
                  !L::Find(l,{Kind::Open,int(View::Combat),-1})->Has(L::Changed),"open: the row and the changed page");
        }
    }
    {   // Reset page and Undo reset, with their guards.
        Model m;m.Open(Ready());L::Press(m,{Kind::Open,int(View::Shield),-1},0);
        auto shield=L::Build(m,{},{});Check(!FindRole(shield,L::Role::FooterResetPage)->Has(L::Enabled),"Reset page is off while every row is at its default");
        m.Set(ShieldRoll,90);m.Set(ShieldGrip,10);m.Set(HapticMelee,50);
        shield=L::Build(m,{},{});Check(FindRole(shield,L::Role::FooterResetPage)->Has(L::Enabled),"and on once one differs");
        L::Press(m,{Kind::ResetPage},10000);
        Check(m.Get()[ShieldRoll]==0&&m.Get()[ShieldGrip]==0&&m.Get()[HapticMelee]==50,"Reset page resets this page's rows only");
        L::LayoutInput in;in.nowMs=10100;auto undo=L::Build(m,{},in);CheckLayout(undo,"undo reset");
        const auto* slot=FindRole(undo,L::Role::FooterResetPage);
        Check(slot&&slot->hit.kind==Kind::UndoReset&&!slot->Has(L::Enabled)&&FindRole(undo,L::Role::DrainBar),"the slot reads Undo reset with its bar, not yet armed");
        L::Press(m,{Kind::UndoReset},10300);Check(m.Get()[ShieldRoll]==0,"a double press never lands on Undo reset (400 ms)");
        L::Press(m,{Kind::UndoReset},10500);Check(m.Get()[ShieldRoll]==90&&m.Get()[ShieldGrip]==10,"Undo reset puts the rows back");
        L::Press(m,{Kind::ResetPage},20000);L::Press(m,{Kind::UndoReset},26000);Check(m.Get()[ShieldRoll]==0,"Undo reset is gone after 6 s");
        in.nowMs=26000;Check(FindRole(L::Build(m,{},in),L::Role::FooterResetPage)->hit.kind==Kind::ResetPage,"and the slot reads Reset page again");
        // Confirm close: discard, keep editing, Right B.
        L::Press(m,{Kind::Close},30000);Check(m.confirmClose&&m.open,"Close asks");
        L::Press(m,{Kind::KeepEditing},30100);Check(m.confirmClose,"Keep editing waits 300 ms");
        L::Press(m,{Kind::Close},30400);Check(!m.confirmClose&&m.open&&m.Dirty(),"Close again (Right B) keeps editing");
        L::Press(m,{Kind::Close},31000);L::Press(m,{Kind::ConfirmClose},31100);Check(m.open,"Discard waits 300 ms");
        L::Press(m,{Kind::ConfirmClose},31400);Check(!m.open&&!m.Dirty(),"Discard drops the draft and closes");
        // Open resets the redesign's transient state but keeps the view (#352); StartOver goes to the Guide.
        m.Open(Ready());m.view=View::Environment;m.confirmClose=true;m.undoResetUntil=9;m.focusKey=3;m.pickedPreset=1;m.closeWhenSaved=true;m.resetKeys=ChangeMask{1};
        m.Open(Ready());
        Check(m.view==View::Environment&&!m.confirmClose&&m.undoResetUntil==0&&m.focusKey==-1&&m.pickedPreset==-1&&!m.closeWhenSaved&&!m.resetKeys,"Open resets every new field");
        L::Press(m,{Kind::Close},0);Check(!m.open,"Close without a draft closes at once");
    }
    {   // Calibrate is off while a draft waits; offline difficulty locks without its adapter.
        Model m;Snapshot s=Ready();s.offlineDifficultyAvailable=false;m.Open(s);
        L::Press(m,{Kind::Open,int(View::Body),-1},0);
        auto body=L::Build(m,{},{});CheckLayout(body,"body");
        const L::Element* calibrate=nullptr;for(const auto& e:body.elements)if(e.hit.kind==Kind::Calibrate)calibrate=&e;
        Check(calibrate&&calibrate->Has(L::Enabled),"Calibrate body with no draft");
        m.Set(LeftHanded,1);body=L::Build(m,{},{});
        for(const auto& e:body.elements)if(e.hit.kind==Kind::Calibrate)calibrate=&e;
        Check(!calibrate->Has(L::Enabled)&&FindRole(body,L::Role::Label,BodyCalibration)->sub=="Apply or discard first","Calibrate is disabled while Dirty, with the reason");
        L::Press(m,{Kind::Open,int(View::Monsters),-1},0);
        // As main: without the adapter the row shows the notice and stays editable (never locked).
        m.Discard();
        const auto survival=L::Build(m,{},{});CheckLayout(survival,"survival without the adapter");
        const auto* offline=FindRole(survival,L::Role::Label,OfflinePlayers);
        Check(!offline->Has(L::Locked)&&offline->sub==content::OfflineUnavailableShort,"offline difficulty shows main's notice, not a lock");
        L::LayoutInput card;card.card=OfflinePlayers;const auto pointed=L::Build(m,{},card);
        const auto* text=FindRole(pointed,L::Role::CardText);
        Check(text&&text->text.rfind(content::OfflineUnavailable,0)==0&&text->text.find(content::OfflineNote)==std::string::npos,"its card leads with the notice instead of the difficulty note");
        Check(!(FindRole(pointed,L::Role::CardTitle)->flags&L::Locked),"and is not locked");
        L::Press(m,{Kind::Change,OfflinePlayers,1},0);Check(m.Get()[OfflinePlayers]==1,"it steps without the adapter");
        m.offlineDifficultyAvailable=true;L::Press(m,{Kind::Change,OfflinePlayers,1},0);Check(m.Get()[OfflinePlayers]==2,"and with it");
    }
    {   // Calibration view, What's new, mono audio and not-ready.
        Model m;m.Open(Ready());
        L::LayoutInput in;in.calibration.active=true;in.calibration.progress=.5f;
        const auto calib=L::Build(m,{},in);CheckLayout(calib,"calibration");
        Check(!FindRole(calib,L::Role::Tab)&&L::HitTest(calib,640,512).kind==Kind::None,"calibration replaces the panel and blocks presses");
        {   // The T cross: the figure beside the steps, on the canvas and over nothing; the line only when the T cross can open this.
            const auto* figure=FindRole(calib,L::Role::CalibFigure);const auto* tip=FindRole(calib,L::Role::CalibTip);
            Check(figure&&figure->rect.w==figure->rect.h&&figure->rect.w>=300,"a square figure, big enough to read in the headset");
            Check(figure->rect.x>=0&&figure->rect.y>=0&&figure->rect.x+figure->rect.w<=1280&&figure->rect.y+figure->rect.h<=1024,"the figure stays on the canvas");
            for(const auto& e:calib.elements)if(&e!=figure&&!e.text.empty())
                Check(e.rect.x>=figure->rect.x+figure->rect.w||e.rect.x+e.rect.w<=figure->rect.x||e.rect.y>=figure->rect.y+figure->rect.h||e.rect.y+e.rect.h<=figure->rect.y,
                      std::string("the figure covers no text: ")+e.text);
            Check(tip&&tip->text.find("T cross at any time")!=std::string::npos,"the prompt says the T cross works at any time");
            L::LayoutInput off=in;off.calibration.anyTime=false;const auto noTip=L::Build(m,{},off);CheckLayout(noTip,"calibration, no T cross");
            Check(!FindRole(noTip,L::Role::CalibTip)&&FindRole(noTip,L::Role::CalibFigure),"T cross off or seated: the figure stays, the promise goes");
        }
        static const char* const lines[]={"More natural shield wrists","Guard strain edge: Pale glow, Dark edge or Off","Grab a menu with right grip to move it","Performance settings in one place",
            "Menus and wrist maps fixed at high resolutions","Cleaner One by turns view","Smoother frames","Scenery distance now starts at 210"};
        L::LayoutInput notes;notes.notes={"D2R VR Unleashed / v0.3.24","A mod by monkeylashes","https://github.com/bearstonem/d2r-pcvr","beadlebytes; ChuckRibbits; Sergious.",lines,8};
        L::Press(m,{Kind::Open,int(View::WhatsNew),-1},0);
        const auto news=L::Build(m,{},notes);CheckLayout(news,"what's new");Check(CountRole(news,L::Role::NotesText)>=10,"What's new lists its lines");
        Snapshot off=Ready();off.ready=false;m.Open(off);L::Press(m,{Kind::Open,int(View::Environment),-1},0);
        L::LayoutInput mono;mono.monoAudio=true;
        const auto world=L::Build(m,{},mono);CheckLayout(world,"not ready");
        Check(FindRole(world,L::Role::Warning)&&FindRole(world,L::Role::StatusStrip),"mono warning and the unavailable strip");
        L::Press(m,{Kind::Set,Sky,0,1},0);Check(m.Get()[Sky]==0,"no edits while not ready");
        Check(L::HelpAt(world,FindRole(world,L::Role::Warning)->rect.x+5.f,FindRole(world,L::Role::Warning)->rect.y+5.f)==HelpMonoAudio,"pointing at the warning explains it");
    }
    {   // Control choice (docs/VR_SETTINGS_MENU.md): named lists are chips while up to 6 fit, else a popover.
        for(const auto& c:contexts)for(size_t i=0;i<tables.rowCount;++i){
            const auto& r=tables.rows[i];int n=0;content::OptionsFor(r,c,n);
            const auto control=L::ControlOf(r,Defaults(),c);
            const std::string where=std::string("control of ")+r.label+" class "+std::to_string(c.heroClass);
            if(control==L::Control::Chips)Check(n>=2&&n<=L::MaxChips,where+": chips only up to 6 options");
            if((r.kind==content::RowKind::Choice||r.kind==content::RowKind::Cycler)&&n>L::MaxChips)Check(control==L::Control::Popover,where+": more than 6: a popover");
            if(r.kind==content::RowKind::Toggle)Check(control==L::Control::Toggle,where+": 0 / 1 is a switch");
            if(r.kind==content::RowKind::Slider||r.kind==content::RowKind::Stepper)Check(control==L::Control::Number,where+": a number has - / +");
        }
        Check(L::ControlOf(*tables.Row(RenderResolution),Defaults(),{})==L::Control::Popover,"Render resolution (7 sizes): a popover");
        Check(L::ControlOf(*tables.Row(TooltipTiming),Defaults(),{})==L::Control::Chips&&L::ControlOf(*tables.Row(MenuDistance),Defaults(),{})==L::Control::Chips,
              "three short choices stay chips: one press, the value always visible");
        Check(L::ControlOf(*tables.Row(SpellLock),Defaults(),{heroclass::Amazon})==L::Control::Popover&&L::ControlOf(*tables.Row(SpellLock),Defaults(),{heroclass::Paladin})==L::Control::Chips,
              "a class's own list decides: an Amazon's four choices a popover, a Paladin's Off / On chips");
        // The layout follows it: Render resolution's value opens a popover; < > still step and wrap.
        Model m;m.Open(Ready());L::Press(m,{Kind::Open,int(View::Performance),-1},0);
        const auto perf=L::Build(m,{},{});CheckLayout(perf,"performance");
        const L::Element* face=nullptr;for(const auto& e:perf.elements)if(e.hit.kind==Kind::Popover&&e.hit.index==RenderResolution)face=&e;
        Check(face&&face->Has(L::Enabled)&&face->role==L::Role::Value&&!FindRole(perf,L::Role::Chip,RenderResolution),"the value names the size and opens the list");
        Check(FindRole(perf,L::Role::StepMinus,RenderResolution)&&FindRole(perf,L::Role::StepPlus,RenderResolution),"its < > stay");
        const Hit open=face->hit;
        L::Press(m,open,1000);Check(m.popoverKey==RenderResolution&&m.popoverAt==1000,"a press on the value opens it");
        L::LayoutInput early;early.nowMs=1100;const auto shown=L::Build(m,{},early);CheckLayout(shown,"popover open");
        for(const auto& e:perf.elements)if(e.role==L::Role::Row){
            bool same=false;for(const auto& f:shown.elements)if(f.role==L::Role::Row&&f.key==e.key&&f.rect==e.rect)same=true;
            Check(same,"opening it moves no row under the pointer");
        }
        int options=0,armed=0;std::vector<Rect> cells;
        for(const auto& e:shown.elements)if(e.role==L::Role::PopoverOption){++options;if(e.Has(L::Enabled))++armed;cells.push_back(e.HitBox());}
        Check(options==7&&armed==0,"every size, not yet taking presses (300 ms)");
        for(size_t i=1;i<cells.size();++i)if(cells[i].x==cells[i-1].x)Check(cells[i].y==cells[i-1].y+cells[i-1].h,"option hit boxes meet: no dead zone");
        Check(L::HitTest(shown,100,500).kind==Kind::PopoverClose&&FindRole(shown,L::Role::PopoverDim),"outside it, the dimmed panel: a press there closes it");
        L::LayoutInput later;later.nowMs=1400;const auto armedLayout=L::Build(m,{},later);
        const L::Element* pick=nullptr;for(const auto& e:armedLayout.elements)if(e.role==L::Role::PopoverOption&&e.hit.value==4)pick=&e;
        Check(pick&&pick->Has(L::Enabled)&&L::HitTest(armedLayout,pick->rect.x+5.f,pick->rect.y+pick->rect.h/2.f)==pick->hit,"after 300 ms its options take presses");
        L::Press(m,pick->hit,1100);Check(m.Get()[RenderResolution]==1&&m.popoverKey==RenderResolution,"a pick before 300 ms is ignored");
        L::Press(m,pick->hit,1400);Check(m.Get()[RenderResolution]==4&&m.popoverKey<0&&m.Dirty(),"one press picks and closes; a draft like any change");
        L::Press(m,open,2000);L::Press(m,{Kind::Change,RenderResolution,1},2500);
        Check(m.popoverKey<0&&m.Get()[RenderResolution]==4,"any other press only closes it, without a change");
        L::Press(m,open,3000);L::Press(m,{Kind::PopoverClose},3500);Check(m.popoverKey<0&&m.Get()[RenderResolution]==4,"an outside press closes it without a change");
        L::Press(m,open,4000);L::Press(m,{Kind::Close},4100);Check(m.popoverKey<0&&m.open&&!m.confirmClose,"Right B closes the popover first, not the panel");
        L::Press(m,open,5000);L::Press(m,{Kind::Open,int(View::Body),-1},5100);Check(m.popoverKey<0&&m.view==View::Performance,"a tab under the dim does not change the page");
        Model locked;locked.Open(Ready());locked.ready=false;L::Press(locked,open,0);Check(locked.popoverKey<0,"a row that cannot change does not open");
        {   // A Filter row (Loot beams, owner 2026-10-31 and 2026-11-01): one row, its brightness
            // slider and a Filter button; the card holds four switches, each flipping its own
            // setting, and stays open between presses.
            const Key B=LootBeamBrightness;
            Model f;f.Open(Ready());L::Press(f,{Kind::Open,int(View::Labels),-1},0);
            const auto page=L::Build(f,{},{});CheckLayout(page,"interface: loot beams row");
            int rows=0;for(const auto& e:page.elements)if(e.role==L::Role::Row&&(e.key==B||e.key==LootBeamUnique||e.key==LootBeamSet||e.key==LootBeamRare||e.key==LootBeamRunes))++rows;
            Check(rows==1&&FindRole(page,L::Role::Row,B),"Loot beams: one row, the brightness slider's");
            const L::Element* button=nullptr;for(const auto& e:page.elements)if(e.hit.kind==Kind::Popover&&e.hit.index==B)button=&e;
            const auto* value=FindRole(page,L::Role::Value,B);
            Check(value&&value->text=="100%"&&value->hit.kind==Kind::Change&&FindRole(page,L::Role::StepMinus,B)&&FindRole(page,L::Role::StepPlus,B),"its slider: - 100% +");
            Check(button&&button->role==L::Role::Button&&button->text=="Filter"&&button->Has(L::Enabled)&&value&&button->rect.x>value->rect.x+value->rect.w,"then a Filter button, right of it");
            Check(!FindRole(page,L::Role::Toggle,LootBeamUnique)&&!FindRole(page,L::Role::Value,LootBeamSet)&&!FindRole(page,L::Role::Toggle,LootBeamRunes),"no switch on the page");
            const auto* label=FindRole(page,L::Role::Label,B);
            Check(label&&label->text=="Loot beams"&&label->sub=="Unique, Set, Rare, Runes","the label says which beams show");
            L::Press(f,{Kind::Change,B,-1},10);Check(f.Get()[B]==90,"- lowers the brightness a step (10%)");
            for(int i=0;i<12;++i)L::Press(f,{Kind::Change,B,-1},20);Check(f.Get()[B]==10,"and stops at 10%");
            L::Press(f,{Kind::Undo,B},30);Check(f.Get()[B]==100&&!f.Dirty(),"Undo: back to 100%");
            const auto keys=L::RowsOf(View::Labels,f.Get(),{});bool all[5]={};
            for(int i=0;i<keys.count;++i){const Key k=keys.keys[size_t(i)];if(k==B)all[4]=true;for(int b=0;b<4;++b)if(k==LootBeamUnique+b)all[b]=true;}
            Check(all[0]&&all[1]&&all[2]&&all[3]&&all[4],"the page's keys hold all five (Reset page resets them)");
            L::Press(f,button->hit,1000);Check(f.popoverKey==B,"Filter opens the card");
            L::LayoutInput armedAt;armedAt.nowMs=1400;
            const auto card=L::Build(f,{},armedAt);CheckLayout(card,"loot beams filter open");
            std::vector<const L::Element*> switches;for(const auto& e:card.elements)if(e.role==L::Role::PopoverSwitch)switches.push_back(&e);
            Check(switches.size()==4,"four switches in the card, not the slider");
            const char* words[]={"Unique","Set","Rare","Runes"};int labels=0;
            for(const auto& e:card.elements)if(e.role==L::Role::PopoverLabel&&labels<4&&e.text==words[labels])++labels;
            Check(labels==4,"Unique, Set, Rare, Runes, in that order");
            for(size_t i=0;i<switches.size();++i){
                const auto& e=*switches[i];
                Check(e.hit.kind==Kind::Set&&e.hit.index==LootBeamUnique+int(i)&&e.hit.value==0&&e.Has(L::Selected)&&e.Has(L::Enabled)&&e.text=="On","each its own setting, On");
                Check(L::HitTest(card,float(e.HitBox().x+8),float(e.rect.y+e.rect.h/2))==e.hit,"the whole line presses its switch");
                if(i)Check(e.HitBox().y==switches[i-1]->HitBox().y+switches[i-1]->HitBox().h,"lines meet: no dead zone");
            }
            L::Press(f,switches[2]->hit,1100);Check(f.Get()[LootBeamRare]==1,"a press before 300 ms is ignored");
            L::Press(f,switches[2]->hit,1400);Check(f.Get()[LootBeamRare]==0&&f.popoverKey==B&&f.Dirty(),"Rare Off; the card stays open");
            L::Press(f,switches[0]->hit,1500);Check(f.Get()[LootBeamUnique]==0&&f.popoverKey==B,"and Unique Off");
            const auto after=L::Build(f,{},armedAt);
            const L::Element* rare=nullptr;for(const auto& e:after.elements)if(e.role==L::Role::PopoverSwitch&&e.key==LootBeamRare)rare=&e;
            Check(rare&&!rare->Has(L::Selected)&&rare->text=="Off"&&rare->hit.value==1&&rare->Has(L::Changed),"Rare reads Off, changed; a press turns it back On");
            L::Press(f,{Kind::PopoverClose},1600);Check(f.popoverKey<0&&f.Get()[LootBeamRare]==0,"an outside press closes it, the changes kept as a draft");
            L::Press(f,{Kind::Change,B,-3},1650);Check(f.Get()[B]==70,"the slider: 70%");
            {   // its help card: Undo and Default take the slider and all four switches
                L::LayoutInput help;help.card=B;const auto card2=L::Build(f,{},help);
                int buttons=0;for(const auto& e:card2.elements)if(e.role==L::Role::CardButton)++buttons;
                Check(buttons==2,"the Loot beams card: Undo and Default");
                Model u=f;L::Press(u,{Kind::Undo,B},1700);
                Check(!u.Dirty()&&u.Get()[B]==100&&u.Get()[LootBeamUnique]==1&&u.Get()[LootBeamRare]==1,"Undo puts the slider and every switch back");
                Model d=f;d.Set(LootBeamSet,0);L::Press(d,{Kind::Default,B},1700);
                Check(d.Get()[B]==100&&d.Get()[LootBeamUnique]==1&&d.Get()[LootBeamSet]==1&&d.Get()[LootBeamRare]==1&&d.Get()[LootBeamRunes]==1,"Default: 100%, all four On");
                Model one=f;L::Press(one,{Kind::Undo,ProjectileAimAssist},1700);Check(one.Get()[LootBeamRare]==0&&one.Get()[B]==70,"another row's Undo leaves them");
            }
            const auto closed=L::Build(f,{},{});
            const auto* summary=FindRole(closed,L::Role::Label,B);
            Check(summary&&summary->sub=="Set, Runes"&&summary->Has(L::Changed),"the row: what shows now, marked changed");
            Check(L::ChangedCount(f,View::Labels,{})==3,"three changes on the page");
            L::Press(f,button->hit,2000);L::Press(f,{Kind::Set,LootBeamSet,0,0},2400);L::Press(f,{Kind::Set,LootBeamRunes,0,0},2500);
            L::Press(f,{Kind::Close},2600);Check(f.popoverKey<0&&f.open,"Right B closes the card first");
            const auto none=L::Build(f,{},{});
            Check(FindRole(none,L::Role::Label,B)->sub=="Off: none","all four Off: none");
            L::Press(f,{Kind::ResetPage},3000);
            Check(f.Get()[B]==100&&f.Get()[LootBeamUnique]==1&&f.Get()[LootBeamSet]==1&&f.Get()[LootBeamRare]==1&&f.Get()[LootBeamRunes]==1,"Reset page: 100%, all four On");
            Model g;g.Open(Ready());L::Press(g,{Kind::Open,int(View::Labels),-1},0);L::Press(g,{Kind::Popover,B},100);
            L::Press(g,{Kind::Set,ProjectileAimAssist,0,1},500);Check(g.popoverKey<0&&g.Get()[ProjectileAimAssist]!=1,"a setting outside the card only closes it");
        }
        // A grid: 9 named choices in two columns (a made-up list for Offline difficulty).
        static constexpr content::Option nine[]={{0,"Game's setting","a"},{1,"1 player","b"},{2,"2 players","c"},{3,"3 players","d"},{4,"4 players","e"},
                                                  {5,"5 players","f"},{6,"6 players","g"},{7,"7 players","h"},{8,"8 players","i"}};
        static constexpr content::Override grid[]={{.id="gameplay.offline_players",.page=View::Monsters,.section="Offline difficulty",.options=nine,.optionCount=9},
                                                   {.id="gameplay.shield_grip_cm",.page=View::Shield,.section="Fit on the arm",.options=nine,.optionCount=9}};
        static const content::MenuData gridMenu=content::BuildMenu(grid,std::size(grid),nullptr,0);
        const L::Tables gt=L::TablesOf(gridMenu);
        Model g;g.Open(Ready());L::Press(g,{Kind::Open,int(View::Monsters),-1},0,gt);
        Check(L::ControlOf(*gt.Row(OfflinePlayers),Defaults(),{})==L::Control::Popover,"9 choices: a popover");
        L::Press(g,{Kind::Popover,OfflinePlayers},0,gt);L::LayoutInput gi;gi.nowMs=500;
        const auto gl=L::Build(g,{},gi,{},gt);CheckLayout(gl,"popover grid");
        std::set<int> columns;for(const auto& e:gl.elements)if(e.role==L::Role::PopoverOption)columns.insert(e.rect.x);
        Check(columns.size()==2,"9-12 choices: a grid of two columns");
        Check(gt.Row(ShieldGrip)&&gt.Row(ShieldGrip)->kind==content::RowKind::Slider&&gt.Row(ShieldGrip)->optionCount==0,"more than 24 values is no list: the number, and the report says split it");
        // Every popover row opened, every view, class and some states: the geometry holds.
        for(const auto& c:contexts)for(int s=0;s<States;s+=41){
            Model x;x.Open(Ready(state(s)));x.SetContext(c);
            for(int view=0;view<int(View::Count);++view){
                L::Press(x,{Kind::Open,view,-1},0);
                const auto rows=L::RowsOf(View(view),x.Get(),c);
                for(int i=0;i<rows.count;++i){
                    const auto* r=tables.Row(rows.keys[size_t(i)]);
                    {const auto control=L::ControlOf(*r,x.Get(),c);if(control!=L::Control::Popover&&control!=L::Control::Color)continue;}
                    L::Press(x,{Kind::Popover,r->key},0);
                    L::LayoutInput pin;pin.nowMs=1000;
                    const auto pl=L::Build(x,c,pin);CheckLayout(pl,std::string("popover ")+r->label+" "+ViewName(View(view)));++built;
                    Check(pl.popover.w>0,"it shows");
                    L::Press(x,{Kind::PopoverClose},2000);
                }
            }
        }
    }
    {   // Frames: the live Eyes control on top, frame generation under it, grayed while By turns is on.
        const auto* fg=tables.Row(FrameGeneration);
        Check(fg&&fg->page==View::Frames&&std::string(tables.Section(View::Frames,fg->section))=="Frame generation","frame generation sits in Frames");
        for(Key k:{FrameGenPreset,FrameGenScale})Check(tables.Row(k)->page==View::Frames&&tables.Row(k)->section==fg->section&&!tables.Row(k)->fallback,"NVIDIA's quality and scale: our words, under it");
        Check(tables.Row(DeferGpuWait)->page==View::Frames,"Defer GPU wait's row is on Frames");
        // One setting on two pages: one row in the tables, one key.
        int eyesRows=0;for(size_t i=0;i<tables.rowCount;++i)if(tables.rows[i].key==StereoRendering)++eyesRows;
        Check(eyesRows==1&&tables.Row(StereoRendering)->page==View::Performance&&tables.Row(StereoRendering)->alsoOn==View::Frames,"Eyes drawn per frame: one row, drawn on Performance and on Frames");
        int shared=0;for(size_t i=0;i<tables.rowCount;++i)if(tables.rows[i].alsoOn!=View::Count)++shared;
        Check(shared==1,"it is the only row on two pages");
        Check(L::CountRows(View::Performance,Defaults(),{})<=L::MaxViewRows,"Performance stays within 8 rows");
        // Every state: the control first, and the rows gray or live with their reason. NVIDIA's rows follow the mode alone.
        for(float stereo:{0.f,1.f})for(float mode:{0.f,1.f,2.f})for(float defer:{0.f,1.f}){
            Values v=Defaults();v[StereoRendering]=stereo;v[FrameGeneration]=mode;v[DeferGpuWait]=defer;
            const auto rows=L::RowsOf(View::Frames,v,{});
            Check(rows.count>=1&&rows.keys[0]==StereoRendering,"Frames always shows at least the control, first");
            Check(RowShown(FrameGeneration,v,{}),"Frame generation never hides");
            Check(RowShown(FrameGenPreset,v,{})==(mode==2.f)&&RowShown(FrameGenScale,v,{})==(mode==2.f),"NVIDIA's rows only with the NVIDIA mode");
            for(Key k:{FrameGeneration,FrameGenPreset,FrameGenScale})Check(RowEnabled(v,k)==(stereo==0.f),"gray by turns, live with Both eyes");
            Check(L::CountRows(View::Frames,v,{})==(mode==2.f?4:2)+(defer==0.f?1:0),"the control, its rows and the offer's row");
            Model x;x.Open(Ready(v));L::Press(x,{Kind::Open,int(View::Frames),-1},0);
            for(int card:{-1,int(FrameGeneration),int(StereoRendering)}){
                L::LayoutInput in;in.card=card;in.frameGen={framevr::framegen::State::Active,1,1,2,1,1,0,0,0};
                const auto l=L::Build(x,{},in);CheckLayout(l,"frames, every state");
                const auto* reason=FindRole(l,L::Role::Reason);
                Check(reason&&reason->text==(stereo==1.f?"Off while One by turns is on: pick Both to use these.":"Available while Both is on."),"the reason line reads true in each state, at every card");
                Check(!FindRole(l,L::Role::Intro),"no intro on Frames: its rows stay put");
                const auto* eyes=FindRole(l,L::Role::Label,StereoRendering);const auto* gen=FindRole(l,L::Role::Label,FrameGeneration);
                Check(eyes&&gen&&reason&&eyes->rect.y<reason->rect.y&&reason->rect.y<gen->rect.y,"the control, then the line, then the rows");
                Check(gen&&gen->Has(L::Locked)==(stereo==1.f)&&(stereo==0.f||gen->sub=="Needs Both"),"a gray row says what it needs");
                Check(!eyes||!eyes->Has(L::Locked),"the control itself is never gray");
                Check(CountRole(l,L::Role::ReadoutBox)==1&&(defer==0.f)==(FindRole(l,L::Role::Button,DeferGpuWait)!=nullptr),"the readout fits under them, with the offer while it is Off");
            }
        }
        // The rows stay put: the Frame generation row and the control above it do not move when NVIDIA's rows come or go.
        for(float stereo:{0.f,1.f})for(float defer:{0.f,1.f})for(int card:{-1,int(FrameGeneration)}){
            const auto rowY=[&](float mode,Key k){
                Values v=Defaults();v[StereoRendering]=stereo;v[FrameGeneration]=mode;v[DeferGpuWait]=defer;
                Model x;x.Open(Ready(v));L::Press(x,{Kind::Open,int(View::Frames),-1},0);
                L::LayoutInput in;in.card=card;
                const auto* e=FindRole(L::Build(x,{},in),L::Role::Label,k);return e?e->rect.y:-1;};
            Check(rowY(0,FrameGeneration)>0&&rowY(0,FrameGeneration)==rowY(2,FrameGeneration),"the Frame generation row's y is the same with and without the NVIDIA rows");
            Check(rowY(0,StereoRendering)>0&&rowY(0,StereoRendering)==rowY(2,StereoRendering),"and the control's");
        }
        {   // The intro's words stay on the card.
            Model x;x.Open(Ready());L::Press(x,{Kind::Open,int(View::Frames),-1},0);
            const auto l=L::Build(x,{},{});const auto* text=FindRole(l,L::Role::CardText);
            Check(text&&text->text.find(content::PageOf(View::Frames).intro)!=std::string::npos,"the idle card keeps the intro's text");
        }
        // Pressing it on either page changes the same value, and both pages show it at once.
        const auto chosen=[](Model x,View view){L::Press(x,{Kind::Open,int(view),-1},0);for(const auto& e:L::Build(x,{},{}).elements)if(e.key==StereoRendering&&e.hit.kind==Kind::Set&&e.Has(L::Selected))return e.hit.value;return -1.f;};
        Model two;two.Open(Ready());L::Press(two,{Kind::Open,int(View::Frames),-1},0);
        Check(chosen(two,View::Frames)==1&&chosen(two,View::Performance)==1,"By turns on both pages");
        L::Press(two,{Kind::Set,StereoRendering,0,0},0);
        Check(two.Get()[StereoRendering]==0&&chosen(two,View::Frames)==0&&chosen(two,View::Performance)==0,"a press on Frames: Both eyes on both pages");
        Check(L::ChangedCount(two,View::Frames,{})==1&&L::ChangedCount(two,View::Performance,{})==1,"each page says it is not applied");
        {L::LayoutInput in;in.card=HelpGroupFirst+int(content::Group::Graphics);
         L::Press(two,{Kind::Open,int(View::Combat),-1},0);
         const auto c=L::Build(two,{},in);Check(FindRole(c,L::Role::CardState)&&FindRole(c,L::Role::CardState)->text=="1 not applied","Graphics' card counts the one setting once");}
        L::Press(two,{Kind::Open,int(View::Performance),-1},0);L::Press(two,{Kind::Set,StereoRendering,0,1},0);
        Check(two.Get()[StereoRendering]==1&&chosen(two,View::Frames)==1&&chosen(two,View::Performance)==1,"a press on Performance: By turns on both pages");
        // A press on a gray row changes nothing; the same press with Both eyes does.
        Model t;t.Open(Ready());L::Press(t,{Kind::Open,int(View::Frames),-1},0);
        {const auto gray=L::Build(t,{},{});bool anyLive=false;int drawn=0;
         for(const auto& e:gray.elements)if(e.key==FrameGeneration&&e.hit.kind!=Kind::None){++drawn;anyLive=anyLive||e.Has(L::Enabled);}
         Check(drawn>0&&!anyLive,"by turns no control on the gray row is live");}
        L::Press(t,{Kind::Set,FrameGeneration,0,1},0);Check(t.Get()[FrameGeneration]==0,"by turns a press on it changes nothing");
        L::Press(t,{Kind::Change,FrameGeneration,1},0);Check(t.Get()[FrameGeneration]==0,"nor does a step");
        L::Press(t,{Kind::Default,FrameGeneration},0);Check(!t.Dirty(),"nor Default");
        {L::LayoutInput in;in.card=FrameGeneration;const auto c=L::Build(t,{},in);
         Check(FindRole(c,L::Role::CardState)&&FindRole(c,L::Role::CardState)->text=="Locked. Now: Off","its card says it is locked and what it holds");
         Check(std::string(HelpText(FrameGeneration)).find("Eyes drawn per frame: Both")!=std::string::npos,"its help tells how to get it, in the chip's word");}
        L::Press(t,{Kind::Set,StereoRendering,0,0},0);
        Check(!FindRole(L::Build(t,{},{}),L::Role::Label,FrameGeneration)->Has(L::Locked),"with Both eyes the row is live");
        L::Press(t,{Kind::Set,FrameGeneration,0,1},0);Check(t.Get()[FrameGeneration]==1,"and turns on");
        L::Press(t,{Kind::Set,StereoRendering,0,1},0);
        Check(t.Get()[FrameGeneration]==1&&FindRole(L::Build(t,{},{}),L::Role::Label,FrameGeneration)->Has(L::Locked),"back to By turns it grays and keeps its value");
        // Reset page on Frames resets the rows too, though it also takes the control back to By turns.
        {Model r;r.Open(Ready());L::Press(r,{Kind::Open,int(View::Frames),-1},0);
         L::Press(r,{Kind::Set,StereoRendering,0,0},0);L::Press(r,{Kind::Set,FrameGeneration,0,2},0);L::Press(r,{Kind::Set,FrameGenScale,0,1},0);
         L::Press(r,{Kind::ResetPage},0);
         Check(r.Get()[StereoRendering]==1&&r.Get()[FrameGeneration]==0&&r.Get()[FrameGenScale]==Definitions[FrameGenScale].defaultValue,"Reset page: the rows and the control");}
        // Its status with Both eyes.
        Values nvidia=Defaults();nvidia[FrameGeneration]=2;nvidia[StereoRendering]=0;
        Check(L::CountRows(View::More,nvidia,{})==0,"NVIDIA's rows left More");
        Model m;m.Open(Ready(nvidia));L::Press(m,{Kind::Open,int(View::Frames),-1},0);
        L::LayoutInput in;in.frameGen={framevr::framegen::State::Active,1,1,2,1,1,0,0,0};in.card=FrameGeneration;
        const auto perf=L::Build(m,{},in);CheckLayout(perf,"frames, frame generation active");
        Check(FindRole(perf,L::Role::Label,FrameGeneration)->sub=="Now: active"&&!FindRole(perf,L::Role::Label,FrameGeneration)->Has(L::Warn),
              "its row says what OFXR Bridge is doing");
        {L::LayoutInput paused=in;paused.frameGen.state=framevr::framegen::State::NeedsBothEyes;const auto p=L::Build(m,{},paused);CheckLayout(p,"frame generation paused");
         Check(FindRole(p,L::Role::Label,FrameGeneration)->sub=="Now: needs Both"&&FindRole(p,L::Role::Label,FrameGeneration)->Has(L::Warn),"the applied By turns pauses it, in amber (#316), while the draft is Both eyes");}
        Check(FindRole(perf,L::Role::CardState)&&FindRole(perf,L::Role::CardState)->text=="Active, OFXR Bridge NVIDIA Medium, 50% scale","its card has main's status line");
    }
    {   // Defer GPU wait: its row on Frames, its button on both readouts; a press from Performance marks Frames.
        Model d;d.Open(Ready());
        for(View view:{View::Performance,View::Frames}){
            L::Press(d,{Kind::Open,int(view),-1},0);const auto l=L::Build(d,{},{});CheckLayout(l,"the Defer button");
            const auto* b=FindRole(l,L::Role::Button,DeferGpuWait);Check(b&&b->Has(L::Enabled),"both readouts offer it while it is Off");
        }
        L::Press(d,{Kind::Open,int(View::Performance),-1},0);L::Press(d,{Kind::Set,DeferGpuWait,0,1},0);
        Check(d.Get()[DeferGpuWait]==1&&L::ChangedCount(d,View::Frames,{})==1&&L::ChangedCount(d,View::Performance,{})==0,"a press on Performance counts on Frames, its row's page");
        for(View view:{View::Performance,View::Frames}){L::Press(d,{Kind::Open,int(view),-1},0);Check(!FindRole(L::Build(d,{},{}),L::Role::Button,DeferGpuWait),"and neither offers it once it is On");}
    }
    {   // Main's frame generation text and readout note (#313, #343), with the Virtual Desktop sentence
        const char* fgHelp=HelpText(FrameGeneration);const std::string vd="With Virtual Desktop, set its SSW to Disabled.";
        const std::string rowHelp=tables.Row(FrameGeneration)->help;
        Check(rowHelp.size()<=160,"the card text (what the pointed row shows) is at most 160 characters");
        Check(rowHelp.size()>=vd.size()&&rowHelp.compare(rowHelp.size()-vd.size(),vd.size(),vd)==0,"and it ends with the Virtual Desktop SSW sentence");
        (void)fgHelp;
        // while frame generation is active the readout gives way to the note (main's FrameGenPerformanceNote); otherwise the numbers
        Values both=Defaults();both[StereoRendering]=0;both[FrameGeneration]=1;
        Model m;m.Open(Ready(both));L::Press(m,{Kind::Open,int(View::Frames),-1},0);
        const auto readoutText=[&](const L::Layout& l){std::string all;for(const auto& e:l.elements)if(e.role==L::Role::Readout)all+=e.text+"\n";return all;};
        L::LayoutInput active;active.frameGen={framevr::framegen::State::Active,1,1,2,1,1,0,0,0};
        active.performance=PerformanceReadout{true,true,52,11.1f,1.0f,7.0f,9.4f,1.5f};
        const auto with=L::Build(m,{},active);CheckLayout(with,"frames, frame generation active, readout note");
        const std::string noteText=readoutText(with);
        Check(noteText.find(FrameGenPerformanceNote[0])!=std::string::npos&&noteText.find(FrameGenPerformanceNote[1])!=std::string::npos,"active: the readout shows the two note lines");
        Check(noteText.find("52 fps")==std::string::npos&&noteText.find("headset wait")==std::string::npos,"active: and none of the menu's own numbers");
        L::LayoutInput idle=active;idle.frameGen.state=framevr::framegen::State::Off;
        const std::string numbers=readoutText(L::Build(m,{},idle));
        Check(numbers.find("52 fps")!=std::string::npos&&numbers.find(FrameGenPerformanceNote[0])==std::string::npos,"not active: the numbers, no note");
        Check(CountRole(L::Build(m,{},active),L::Role::ReadoutBox)==1&&CountRole(L::Build(m,{},idle),L::Role::ReadoutBox)==1,"one readout box either way (the layout does not jump)");
        for(const char* line:FrameGenPerformanceNote)Check(std::string(line).size()<=90,"each note line fits the readout's width");
    }
    {   // #352: the panel reopens where it was closed; a kept view with nothing to show falls back (SettleView)
        const L::Tables moreRows=L::TablesOf(content::FallbackDemo());   // the real menu's More is empty: the safety net's rows
        Model m;m.Open(Ready());L::Press(m,{Kind::Open,int(View::More),-1},0,moreRows);
        Check(m.view==View::More,"More opens while it has rows");
        m.Close();m.Open(Ready());L::SettleView(m,moreRows);
        Check(m.view==View::More&&m.morePage==0,"reopened with rows: still More");
        m.morePage=5;L::SettleView(m,moreRows);Check(m.view==View::More&&m.morePage==L::MorePages(m.Get(),{},moreRows)-1,"a kept More page past the last clamps to the last");
        m.morePage=0;m.Close();m.Open(Ready());L::SettleView(m);   // the real menu: More has no rows
        Check(m.view==View::Guide&&m.morePage==0,"a kept More with no rows falls to the Guide");
        Model keep;keep.Open(Ready());L::Press(keep,{Kind::Open,int(View::Combat),-1},0);keep.Close();keep.Open(Ready());L::SettleView(keep);
        Check(keep.view==View::Combat,"a page with rows is kept");
        keep.Close();keep.StartOver();keep.Open(Ready());L::SettleView(keep);
        Check(keep.view==View::Guide&&keep.lastPage==View::Combat&&keep.morePage==0&&keep.notesPage==0,"StartOver (a new game): the Guide, page 1");
        Model pre;pre.Open(Ready());pre.view=View::Presets;L::SettleView(pre);
        Check(pre.view==View::Guide,"Presets, unavailable, fall to the Guide");
    }
    {   // Movement speed on Moving; Stick toggle, which changes what the stick does, a full row on Hands.
        const auto* speed=tables.Row(RunSpeed);const auto* toggle=tables.Row(RunWalkToggle);
        Check(speed&&toggle&&speed->page==View::Moving&&toggle->page==View::Hands&&!speed->fallback&&!toggle->fallback&&!toggle->beside,"Movement speed on Moving, Stick toggle its own row on Hands");
        for(const auto& c:contexts)for(bool seated:{false,true}){
            Values v=Defaults();v[Seated]=seated;
            Check(L::CountRows(View::Moving,v,c)<=L::MaxViewRows&&L::CountRows(View::Hands,v,c)==4&&L::KeyVisible(RunSpeed,v,c)&&L::KeyVisible(RunWalkToggle,v,c),"both show for every class");
        }
        Model b;b.Open(Ready());L::Press(b,{Kind::Open,int(View::Hands),-1},0);
        const auto page=L::Build(b,{},{});CheckLayout(page,"hands with the stick toggle");
        Check(FindRole(page,L::Role::Row,RunWalkToggle)!=nullptr,"Stick toggle is a row of its own");
        const L::Element* tog=nullptr;for(const auto& e:page.elements)if(e.key==RunWalkToggle&&e.hit.kind==Kind::Set)tog=&e;
        Check(tog&&tog->hit==Hit{Kind::Set,RunWalkToggle,0,1.f},"a press sets it");
        // the value labels: Walk, Base run, Run N%, Full run (main's ValueLabel)
        const auto value=[&](float v)->std::string{Model x;x.Open(Ready());x.Set(RunSpeed,v);L::Press(x,{Kind::Open,int(View::Moving),-1},0);
            const auto l=L::Build(x,{},{});for(const auto& e:l.elements)if(e.key==RunSpeed&&e.role==L::Role::Value)return e.text;return "?";};
        Check(value(-10)=="Walk"&&value(0)=="Base run"&&value(40)=="Run 40%"&&value(100)=="Full run","value labels read Walk, Base run, Run N%, Full run");
    }
    {   // Every override's warn line reaches its row, and draws: Motion combat, Frame generation.
        int warned=0;
        for(const auto& o:content::Overrides)if(o.warn){
            const int k=content::KeyOfId(o.id);if(k<0)continue;++warned;
            Check(tables.Row(Key(k))&&tables.Row(Key(k))->warn==o.warn,std::string("the warn line reaches its row: ")+o.id);
        }
        Check(warned>=3,"the overrides carry warn lines");
        Values v=Defaults();v[StereoRendering]=0;   // Both eyes: Frame generation is not locked
        Model m;m.Open(Ready(v));m.SetContext({heroclass::Sorceress});
        auto sub=[&](View view,Key k){L::Press(m,{Kind::Open,int(view),-1},0);const auto* e=FindRole(L::Build(m,m.context,{}),L::Role::Label,k);return e?std::make_pair(e->sub,e->Has(L::Warn)):std::make_pair(std::string(),false);};
        for(auto [view,k]:{std::pair{View::Combat,Motion},std::pair{View::Frames,FrameGeneration}}){
            const auto [text,warn]=sub(view,k);
            Check(text==tables.Row(k)->warn&&warn,std::string("its warn line draws, amber: ")+tables.Row(k)->label);
        }
    }
    {   // Vibration: one Feel it per test the category has, each sending its own choice (Weapon / Shield, Bow / Throw, Test L / R).
        Values v=Defaults();Model m;m.Open(Ready(v));L::Press(m,{Kind::Open,int(View::Vibration),-1},0);
        for(int cat=0;cat<framevr::haptics::CategoryCount;++cat){
            const Key k=Key(HapticMelee+cat);const auto category=framevr::haptics::Category(cat);
            for(bool changed:{false,true}){
                if(changed)m.Set(k,40);
                L::LayoutInput in;in.card=k;in.nowMs=10000;const auto l=L::Build(m,{},in);CheckLayout(l,std::string("feel it ")+Definitions[k].label);
                std::vector<const L::Element*> feel;for(const auto& e:l.elements)if(e.role==L::Role::CardButton&&e.hit.kind==Kind::TestHaptics)feel.push_back(&e);
                Check(int(feel.size())==framevr::haptics::PreviewCount(category),std::string("a Feel it per test: ")+Definitions[k].label);
                for(size_t i=0;i<feel.size();++i){
                    Check(feel[i]->hit.index==cat&&feel[i]->hit.delta==int(i)+1,"each sends its own test");
                    if(feel.size()>1)Check(feel[i]->text.find(framevr::haptics::PreviewLabel(category,int(i)))!=std::string::npos,"named as main named it");
                }
                if(changed)m.Discard();
            }
        }
        Check(std::string(tables.Row(HapticMelee)->help).find("Weapon buzzes the right controller, Shield the left")!=std::string::npos&&
              framevr::haptics::PreviewLabel(framevr::haptics::Category::Melee,0)==std::string("Weapon")&&framevr::haptics::PreviewLabel(framevr::haptics::Category::Melee,1)==std::string("Shield"),
              "the Melee help names the two tests the card offers");
    }
    {   // The Performance readout shows the game's own graphics options when no warning needs the line, and its card always.
        Values v=Defaults();v[DeferGpuWait]=1;Model m;m.Open(Ready(v));L::Press(m,{Kind::Open,int(View::Performance),-1},0);
        L::LayoutInput in;in.game=ParseGameVideo(R"({"Shadow Quality": 4, "Character Detail": 5, "Anti Aliasing": 1, "VSync": 0})");
        const auto l=L::Build(m,{},in);CheckLayout(l,"performance with the game's options");
        bool line=false;for(const auto& e:l.elements)if(e.role==L::Role::Readout&&e.text==GameVideoLine(in.game))line=true;
        Check(line,"the readout shows the game's options");
        L::LayoutInput unsaved;const auto u=L::Build(m,{},unsaved);
        bool note=false;for(const auto& e:u.elements)if(e.role==L::Role::Readout&&e.text==GameVideoUnsaved)note=true;
        Check(note,"or that the game has not saved them yet");
        in.card=HelpPerformance;const auto card=L::Build(m,{},in);
        Check(FindRole(card,L::Role::CardState)&&FindRole(card,L::Role::CardState)->text==GameVideoLine(in.game),"the readout's card names them too");
    }
    {   // Warn lines come from the data (an override's warnWhen), not from Key checks in the layout.
        Values v=Defaults();v[SmoothTurn]=1;v[TurnSpeed]=160;v[StereoRendering]=1;
        Model m;m.Open(Ready(v));L::Press(m,{Kind::Open,int(View::Moving),-1},0);
        const auto body=L::Build(m,{},{});
        Check(FindRole(body,L::Role::Label,SmoothTurn)->sub=="Can cause motion sickness"&&FindRole(body,L::Role::Label,SmoothTurn)->Has(L::Warn)&&
              FindRole(body,L::Role::Label,TurnSpeed)->sub=="Fast turns are harder on the stomach","warn lines above their thresholds");
        m.Set(TurnSpeed,150);Check(FindRole(L::Build(m,{},{}),L::Role::Label,TurnSpeed)->sub.empty(),"and none at or under them");
        L::Press(m,{Kind::Open,int(View::Performance),-1},0);
        Check(FindRole(L::Build(m,{},{}),L::Role::Label,StereoRendering)->sub=="Fast monsters can look doubled","One by turns warns");
        Check(tables.Row(SmoothTurn)->warnWhen.text&&tables.Row(StereoRendering)->warnWhen.above==.5f,"they are the overrides' data");
    }
    {   // The color grid (docs/VR_SETTINGS_MENU.md): By range a chip, 24 hues as dots, 6 a row in hue order, two presses to any color.
        Values v=Defaults();v[TargetHighlight]=60;Model m;m.Open(Ready(v));L::Press(m,{Kind::Open,int(View::Outlines),-1},0);
        const auto page=L::Build(m,{},{});CheckLayout(page,"outlines");
        const L::Element* face=nullptr;for(const auto& e:page.elements)if(e.role==L::Role::Value&&e.key==HighlightRight)face=&e;
        Check(face&&face->hit==Hit{Kind::Popover,HighlightRight}&&face->text==content::HueNameOf(m.Get()[HighlightRight])&&face->hue==m.Get()[HighlightRight]&&face->sub.empty(),
              "a color's value: its dot and name (no \"n of m\"); a press opens the grid");
        Check(FindRole(page,L::Role::StepMinus,HighlightRight)&&FindRole(page,L::Role::StepPlus,HighlightRight),"its < > still step and wrap");
        L::Press(m,face->hit,1000);L::LayoutInput in;in.nowMs=1400;
        const auto grid=L::Build(m,{},in);CheckLayout(grid,"color grid");
        std::vector<const L::Element*> dots;const L::Element* byRange=nullptr;
        for(const auto& e:grid.elements){if(e.role==L::Role::ColorDot)dots.push_back(&e);if(e.role==L::Role::PopoverOption)byRange=&e;}
        Check(dots.size()==24&&byRange&&byRange->text=="By range"&&byRange->hue==draw::SwatchByRange&&byRange->hit.value==0,"24 dots and By range as a chip, not a dot");
        std::set<int> xs,ys;for(const auto* e:dots){xs.insert(e->rect.x);ys.insert(e->rect.y);}
        Check(xs.size()==6&&ys.size()==4,"6 columns x 4 rows");
        Check(byRange->rect.y+byRange->rect.h<=*ys.begin()&&byRange->rect.w>=5*L::PopCell,"the chip above the grid, across it");
        for(size_t i=0;i<dots.size();++i){
            Check(dots[i]->rect.w==46&&dots[i]->HitBox().w==L::PopCell&&dots[i]->HitBox().h==L::PopCell,"46 px dots in 66 px cells (2.7 degrees)");
            if(i)Check(std::fmod(dots[i-1]->hue,360.f)<std::fmod(dots[i]->hue,360.f),"hue order, a quarter of the wheel a row");
            if(i%6)Check(dots[i]->HitBox().x==dots[i-1]->HitBox().x+L::PopCell,"cells meet: no dead zone between dots");
        }
        Check(dots[0]->hue==360&&std::string(content::HueNameOf(dots[0]->hue))=="Red"&&std::string(content::HueNameOf(dots[23]->hue))=="Crimson","Red first (360: 0 is By range), Crimson last");
        int chosen=0;for(const auto* e:dots)if(e->Has(L::Selected)){++chosen;Check(e->hue==m.Get()[HighlightRight],"the chosen dot is ringed");}
        Check(chosen==1&&!byRange->Has(L::Selected),"exactly one choice ringed");
        // Pointing at a dot names it.
        const L::Element* azure=nullptr;for(const auto* e:dots)if(std::string(content::HueNameOf(e->hue))=="Azure")azure=e;
        L::LayoutInput pointed=in;pointed.hover=azure->hit;const auto named=L::Build(m,{},pointed);
        Check(FindRole(named,L::Role::PopoverHint)->text=="Azure","the pointed dot's name in the hint");
        L::Press(m,azure->hit,1500);Check(m.Get()[HighlightRight]==210&&m.popoverKey<0,"one press picks the color and closes: two presses in all");
        L::Press(m,face->hit,2000);L::Press(m,byRange->hit,2400);Check(m.Get()[HighlightRight]==0,"By range is one press away too");
        // The arrow path line: Off as the chip, 0-345 as dots.
        Values a=Defaults();Model bow;bow.Open(Ready(a));bow.SetContext({heroclass::Amazon});L::Press(bow,{Kind::Open,int(View::Targeting),-1},0);
        L::Press(bow,{Kind::Popover,BowLaser},0);L::LayoutInput bi;bi.nowMs=400;const auto laser=L::Build(bow,bow.context,bi);CheckLayout(laser,"arrow path grid");
        int laserDots=0;const L::Element* off=nullptr;for(const auto& e:laser.elements){if(e.role==L::Role::ColorDot)++laserDots;if(e.role==L::Role::PopoverOption)off=&e;}
        Check(laserDots==24&&off&&off->text=="Off"&&off->hit.value==LaserOff&&off->Has(L::Selected),"the arrow path line: Off a chip (chosen by default), 24 hues");
    }
    {   // The fallback (docs/VR_SETTINGS_MENU.md): settings with no override still show, with upstream's words.
        const auto& demo=content::FallbackDemo();const L::Tables t=L::TablesOf(demo);
        auto rowOf=[&](Key k)->const content::ContentRow*{return t.Row(k);};
        Check(rowOf(TorchFlame)&&rowOf(TorchFlame)->page==View::Environment&&rowOf(TorchFlame)->fallback&&
              std::string(t.Section(View::Environment,rowOf(TorchFlame)->section))=="New","an unsorted [torch] setting: Environment's New section, at the bottom");
        Check(rowOf(DrawDistance)&&rowOf(DrawDistance)->page==View::Performance&&std::string(t.Section(View::Performance,rowOf(DrawDistance)->section))=="New",
              "[render]: Performance's New section");
        for(Key k:{ShieldGrip,ShieldOut,ShieldUp,ShieldFollow,ShieldRoll,CompanionAi,EnemySight,OfflinePlayers,HeartbeatStart,MapTopDown})
            Check(rowOf(k)&&rowOf(k)->page==View::More&&rowOf(k)->fallback,std::string("[gameplay] / [interactions] with no override: More: ")+Definitions[k].label);
        const auto* torch=rowOf(TorchFlame);
        Check(std::string(torch->label)==Definitions[TorchFlame].label&&std::string(torch->help)==HelpText(TorchFlame)&&torch->kind==content::RowKind::Toggle,
              "upstream's label and long help; 0 / 1 is a switch");
        Check(content::ValueText(*rowOf(ShieldGrip),10)=="+10 cm"&&content::ValueText(*rowOf(ShieldRoll),-90)=="-90\xc2\xb0"&&content::ValueText(*rowOf(ShieldFollow),50)=="50%",
              "units by key suffix (_cm, _deg, _pct), signed ranges with +");
        Check(rowOf(MapTopDown)->kind==content::RowKind::Slider&&rowOf(MapTopDown)->optionCount==0&&content::ValueText(*rowOf(MapTopDown),2)=="2",
              "named choices with no names: an honest number control");
        Check(rowOf(CompanionAi)->kind==content::RowKind::Toggle&&rowOf(OfflinePlayers)->kind==content::RowKind::Slider,"the control comes from the data");
        bool moreReported=false;for(const auto& l:demo.report)if(l.find("the More page is not empty: 10 settings")!=std::string::npos)moreReported=true;
        Check(moreReported,"the report says More is not empty");
        // More: a header view with a band link while it has rows, 8 rows a page.
        Model m;m.Open(Ready());
        const auto body=L::Build(m,{},{},{},t);CheckLayout(body,"fallback body");
        const L::Element* link=nullptr;for(const auto& e:body.elements)if(e.hit.kind==Kind::Open&&e.hit.index==int(View::More))link=&e;
        Check(link&&link->text=="10 more"&&link->role==L::Role::HeaderLink,"the band links More with its count while it has rows");
        Check(!FindRole(L::Build(m,{},{}),L::Role::HeaderLink)||[&]{for(const auto& e:L::Build(m,{},{}).elements)if(e.hit.kind==Kind::Open&&e.hit.index==int(View::More))return false;return true;}(),
              "and never while More is empty (the real menu)");
        L::Press(m,{Kind::Open,int(View::More),-1},0,t);
        Check(m.view==View::More&&m.morePage==0&&L::RowsOf(View::More,m.Get(),{},t,L::MoreSkip(m,t)).count==8&&L::MorePages(m.Get(),{},t)==2,"More: 8 rows a page");
        const auto page1=L::Build(m,{},{},{},t);CheckLayout(page1,"more page 1");
        const L::Element* next=nullptr;for(const auto& e:page1.elements)if(e.hit.kind==Kind::NotesPage&&e.hit.delta>0)next=&e;
        Check(next&&next->Has(L::Enabled),"Previous / Next under the rows");
        L::Press(m,next->hit,0,t);Check(m.morePage==1&&L::RowsOf(View::More,m.Get(),{},t,L::MoreSkip(m,t)).count==2,"Next: the rest");
        L::Press(m,next->hit,0,t);Check(m.morePage==1,"and stops there");
        CheckLayout(L::Build(m,{},{},{},t),"more page 2");
        L::Press(m,{Kind::Open,int(View::Body),-1},0,t);L::Press(m,{Kind::Open,int(View::More),MapTopDown},0,t);
        bool onPage=false;const auto rows=L::RowsOf(View::More,m.Get(),{},t,L::MoreSkip(m,t));for(int i=0;i<rows.count;++i)if(rows.keys[size_t(i)]==MapTopDown)onPage=true;
        Check(onPage,"Open with a row opens More on the page that holds it");
        L::LayoutInput mono;mono.monoAudio=true;CheckLayout(L::Build(m,{},mono,{},t),"more with the Mono audio warning");
        // Every view, class and some states with the fallback rows.
        for(const auto& c:contexts)for(int s=0;s<States;s+=37){
            Model x;x.Open(Ready(state(s)));x.SetContext(c);
            for(int view=0;view<int(View::Count);++view){
                L::Press(x,{Kind::Open,view,-1},0,t);
                for(int p=0;p<2;++p){x.morePage=p;CheckLayout(L::Build(x,c,{},{},t),std::string("fallback ")+ViewName(View(view)));++built;}
            }
        }
        // Only fallback labels are cut, never ours: a wide measure (40 px a character).
        L::TextMeasure wide{[](const void*,std::string_view s,draw::TextStyle){return 40.f*float(s.size());},nullptr};
        Model w;w.Open(Ready());L::Press(w,{Kind::Open,int(View::More),-1},0,t);
        const auto cut=L::Build(w,{},{},wide,t);int cuts=0;
        for(const auto& e:cut.elements)if(e.role==L::Role::Label&&e.text.size()>3&&e.text.compare(e.text.size()-3,3,"...")==0){++cuts;Check(40.f*float(e.text.size())<=float(e.rect.w),"a cut label fits its room");}
        Check(cuts>0,"a fallback label too wide for its row ends in ...");
        L::Press(w,{Kind::Open,int(View::Targeting),-1},0,t);
        for(const auto& e:L::Build(w,{},{},wide,t).elements)if(e.role==L::Role::Label)Check(e.text.size()<3||e.text.compare(e.text.size()-3,3,"...")!=0,"our own labels are never cut");
    }
    // ---- left-handed play (hand_text.h, #307): every word the menu shows, mirrored, and still fitting ----
    // Each panel is built as written (right-handed) and as shown left-handed. Every element's words must be
    // the written ones through handtext::Mirror - so a text read past the mirror (a new literal, a table
    // read directly) fails here - except on the two rows about the physical hands themselves. The
    // left-handed panels pass the same geometry checks: no label, chip, hint or card outgrows its place.
    {
        using framevr::handtext::Mirror;
        const auto physical=[](int id){return id==LeftHanded||id==LeftHandedSticks;};
        int compared=0,changed=0,panels=0;
        std::vector<std::string> sides;
        // sticks false: Sticks stay right-handed (hand_role.h Sticks), the sticks' names kept (handtext::Mirror)
        const auto compare=[&](const L::Layout& written,const L::Layout& shown,const std::string& where,bool physicalCard,bool sticks){
            Check(written.elements.size()==shown.elements.size(),where+": the same elements left-handed");
            // Sticks stay right-handed moves the Guide's stick lines to the other hand's column (content::ColumnOf), so
            // the lines come in another order there: the same actions, mirrored, in any order (each place: below)
            std::vector<std::string> guideWritten,guideShown;
            for(size_t i=0;i<written.elements.size();++i){
                const auto& a=written.elements[i];const auto& b=shown.elements[i];
                Check(a.role==b.role&&a.key==b.key&&a.help==b.help,where+": the same element "+std::to_string(i));
                if(!sticks&&a.role==L::Role::GuideLine){guideWritten.push_back(Mirror(a.sub,false));guideShown.push_back(b.sub);++compared;continue;}
                if(!sticks&&a.role==L::Role::CardText&&a.text!=b.text){   // the Guide's columns change length there: its card may fit a sentence more or less
                    const std::string want=physicalCard?a.text:Mirror(a.text,false);
                    Check(want.rfind(b.text,0)==0||b.text.rfind(want,0)==0,where+": the card reads \""+b.text+"\", a cut of \""+want+"\" or more of it");
                    ++compared;continue;
                }
                // (the detected controllers are read off the real hands: "Detected: left Touch / right Touch")
                const bool keep=physical(int(a.key))||physical(a.help)||(physicalCard&&CardPart(a.role))||a.text.rfind("Detected: left ",0)==0||a.sub.rfind("Detected: left ",0)==0||
                    a.text.rfind("Left-handed (experimental): ",0)==0;   // that row's line in the list of changes
                // the Guide's controls are the buttons of whichever controller is in that hand (GuideControl, below)
                const bool buttons=a.role==L::Role::GuideLine||a.text.rfind("Game menus: ",0)==0;
                if(!buttons)Check(b.text==(keep?a.text:Mirror(a.text,sticks)),where+": \""+a.text+"\" reads \""+b.text+"\" left-handed, expected \""+(keep?a.text:Mirror(a.text,sticks))+"\"");
                Check(b.sub==(keep?a.sub:Mirror(a.sub,sticks)),where+": \""+a.sub+"\" reads \""+b.sub+"\" left-handed, expected \""+(keep?a.sub:Mirror(a.sub,sticks))+"\"");
                ++compared;
                if(b.text!=a.text||b.sub!=a.sub){++changed;sides.push_back(b.text!=a.text?b.text:b.sub);}
            }
            std::sort(guideWritten.begin(),guideWritten.end());std::sort(guideShown.begin(),guideShown.end());
            Check(guideWritten==guideShown,where+": the Guide's actions, mirrored, the same lines");
        };
        const auto both=[&](const Model& m,const L::LayoutInput& in,const std::string& where,bool physicalCard=false,bool sticks=true){
            const auto written=L::Build(m,m.context,in);
            Check(!framevr::handtext::Left(),"right-handed outside the scope");
            framevr::handtext::Scope left(true,!sticks);
            const auto shown=L::Build(m,m.context,in);
            CheckLayout(shown,where+(sticks?" left-handed":" left-handed, sticks right-handed"));compare(written,shown,where,physicalCard,sticks);++panels;
            return written;
        };
        for(const auto& c:contexts){
            Model m;m.Open(Ready());m.SetContext(c);
            for(int view=0;view<int(View::Count);++view){
                L::Press(m,{Kind::Open,view,-1},0);
                const std::string where=std::string("left-handed ")+ViewName(View(view))+" class "+std::to_string(c.heroClass);
                L::LayoutInput in;in.runtimeStatus="Foveated shading available. Eye tracking unavailable on this headset/runtime.";
                in.performance={true,true,90,11.1f,3.4f,2.6f,4.1f,.9f};
                const auto written=both(m,in,where);
                for(const auto& e:written.elements)if(e.role==L::Role::Row||e.role==L::Role::Tab||e.role==L::Role::GroupTab||e.role==L::Role::PresetCard){
                    L::LayoutInput pointed=in;pointed.card=e.help;pointed.hover=e.hit;
                    both(m,pointed,where+" card "+std::to_string(e.help),physical(e.help));
                }
                for(int id:{HelpApply,HelpClose,HelpRecenter,HelpCalibrate,HelpMonoAudio,HelpPerformance,HelpResetPage,HelpUndoReset,HelpGuide,HelpWhatsNew,HelpPresets,HelpDiscard,HelpKeepEditing,HelpApplyClose,HelpPending}){
                    if(&c!=&contexts[0]||view>1)break;
                    L::LayoutInput p=in;p.card=id;both(m,p,where+" help "+std::to_string(id));
                }
            }
        }
        {   // a draft on every row: the pending list, the change lines, the confirm row ("Right B: Keep editing")
            Model m;m.Open(Ready());
            for(int k=0;k<Count;++k){
                const auto* r=tables.Row(Key(k));
                if(!r||r->kind==content::RowKind::Button)continue;
                const auto& d=Definitions[k];
                L::Press(m,{Kind::Set,k,0,Normalize(Key(k),d.defaultValue==d.min?d.min+d.step*2:d.min)},0);
            }
            Check(m.Get()[LeftHanded]>.5f,"the draft has Left-handed on, so Sticks stay right-handed is shown");
            for(const bool sticks:{true,false})for(int view=0;view<int(View::Count);++view){
                L::Press(m,{Kind::Open,view,-1},0);
                const std::string mode=sticks?"left-handed draft ":"left-handed, sticks right-handed, draft ";
                const auto written=both(m,{},mode+ViewName(View(view)),false,sticks);
                for(const auto& e:written.elements)if(e.role==L::Role::Row){L::LayoutInput p;p.card=e.help;p.hover=e.hit;both(m,p,mode+"card "+std::to_string(e.help),physical(e.help),sticks);}
                if(View(view)==View::Hands)Check(FindRole(written,L::Role::Row,LeftHandedSticks)!=nullptr,"Sticks stay right-handed is a row on Hands with Left-handed on");
            }
            L::Press(m,{Kind::Close},0);both(m,{},"left-handed confirm row");both(m,{},"left-handed, sticks right-handed, confirm row",false,false);
        }
        {   // the calibration view, each of its hints ("press B to skip" names the main hand's button)
            Model m;m.Open(Ready());
            for(int hint=0;hint<12;++hint){L::LayoutInput in;in.calibration.active=true;in.calibration.progress=.5f;in.calibration.hint=hint;both(m,in,"left-handed calibration hint "+std::to_string(hint));}
        }
        // every row's own words, straight from the tables: the menu as shown is the menu as written, mirrored
        {
            const auto& written=content::WrittenMenu();
            framevr::handtext::Scope left(true);
            const auto& shown=content::Menu();
            Check(&shown!=&written&&shown.rows.size()==written.rows.size(),"left-handed: a menu of its own, row for row");
            for(size_t i=0;i<written.rows.size();++i){
                const auto& a=written.rows[i];const auto& b=shown.rows[i];
                const bool keep=physical(int(a.key));
                const auto same=[&](const char* x,const char* y){return (!x&&!y)||(x&&y&&std::string(y)==(keep?std::string(x):Mirror(x)));};
                Check(a.key==b.key&&same(a.label,b.label)&&same(a.help,b.help)&&same(a.warn,b.warn)&&same(a.besideLabel,b.besideLabel)&&same(a.warnWhen.text,b.warnWhen.text),
                      std::string("row ")+a.label+": label, help and warnings mirrored");
                Check(a.optionCount==b.optionCount&&a.nameCount==b.nameCount,std::string("row ")+a.label+": the same choices");
                for(int o=0;o<a.optionCount;++o)Check(a.options[o].value==b.options[o].value&&same(a.options[o].label,b.options[o].label)&&same(a.options[o].hint,b.options[o].hint),std::string("row ")+a.label+": its options mirrored");
                for(int n=0;n<a.nameCount;++n)Check(a.names[n].value==b.names[n].value&&same(a.names[n].label,b.names[n].label),std::string("row ")+a.label+": its value names mirrored");
                Check(std::string_view(b.label).size()<=30,std::string("row ")+b.label+": the mirrored label still fits its length");
                if(b.help)Check(std::string_view(b.help).size()<=160,std::string("row ")+b.label+": the mirrored help still fits its length");
            }
            for(int k=0;k<Count;++k){
                const std::string title=WrittenHelpTitle(k),text=WrittenHelpText(k);
                Check(std::string(HelpTitle(k))==(physical(k)?title:Mirror(title))&&std::string(HelpText(k))==(physical(k)?text:Mirror(text)),std::string("help of ")+title+": mirrored");
            }
            Check(content::RowOf(LeftHanded)&&!content::RowOf(Dominant),"Left-handed is on the menu; Dominant hand no longer is");
            Check(std::string(HelpText(LeftHanded))==WrittenHelpText(LeftHanded)&&std::string(content::RowOf(LeftHanded)->label)=="Left-handed (experimental)",
                  "Left-handed's own row reads the same: it is about the real hands");
            // what the owner asked about, by name
            Check(content::RowOf(HighlightRight)&&content::RowOf(HighlightLeft)&&
                  std::string(content::RowOf(HighlightRight)->label)=="Left hand outline color"&&std::string(content::RowOf(HighlightLeft)->label)=="Right hand outline color",
                  "the outline colors are named for the hand that has them: the main hand's is the left hand's");
            for(const Key k:{VitalsLocation,NavigationLocation,WristCompass,WristMap,LeatherMenu})if(const auto* r=content::RowOf(k)){
                const std::string words=std::string(r->label)+" "+(r->help?r->help:"")+" "+HelpText(k);
                Check(words.find("left hand")==std::string::npos&&words.find("left wrist")==std::string::npos&&words.find("left shoulder")==std::string::npos&&words.find("Left wrist")==std::string::npos,
                      std::string("the wrist and menu rows name the right hand, wrist and shoulder: ")+r->label);
            }
            Check(std::string(HelpText(BodyCalibration)).find("Left Y cancels")!=std::string::npos||std::string(HelpText(BodyCalibration)).find("Right B")==std::string::npos,"calibration's help names the main hand's button");
            Check(std::string(CloseAsksText()).find("Left Y does the same as Close")!=std::string::npos,"Close's help: Left Y");
        }
        {   // the Controller guide: each column is a hand, its buttons that hand's controller's
            using framevr::controllers::Labels;using framevr::controllers::Layout;
            const auto line=[](const char* control){for(const auto& g:content::GuideTable)if(std::string(g.control)==control)return &g;return (const content::GuideLine*)nullptr;};
            const auto* off=line("{L1} or {L2}");const auto* main=line("{R1} or {R2}");const auto* back=line("Right {R2}");const auto* shift=line("Right Grip");
            Check(off&&main&&back&&shift,"the guide's lines are there");
            Labels touch{{Layout::Touch,Layout::Touch}},index{{Layout::Index,Layout::Index}};
            Check(content::GuideControl(*off,touch)=="X or Y"&&content::GuideControl(*main,touch)=="A or B"&&content::GuideControl(*back,touch)=="Right B"&&content::GuideControl(*shift,touch)=="Right Grip",
                  "OFF: the guide as it was");
            framevr::handtext::Scope left(true);
            touch.leftMain=index.leftMain=true;   // as the layout sets them left-handed
            Check(content::GuideControl(*off,touch)=="A or B"&&content::GuideControl(*main,touch)=="X or Y","left-handed, Touch: the off hand's skills are on the right controller's A and B, the main hand's on X and Y");
            Check(content::GuideControl(*back,touch)=="Left Y"&&content::GuideControl(*shift,touch)=="Left Grip","Back is Left Y, Shift the Left Grip");
            Check(content::GuideControl(*back,index)=="Left B"&&content::GuideControl(*main,index)=="A or B","an Index controller has A and B in each hand: Left B");
            Model m;m.Open(Ready());Check(m.view==View::Guide,"the panel opens on the guide");
            L::LayoutInput in;in.detected=Labels{{Layout::Touch,Layout::Touch}};
            const auto guide=L::Build(m,{},in);CheckLayout(guide,"left-handed guide, Touch");
            const L::Element* leftHead=nullptr;const L::Element* rightHead=nullptr;const L::Element* walk=nullptr;const L::Element* turn=nullptr;std::string panels;
            for(const auto& e:guide.elements){
                if(e.role==L::Role::Section&&e.text=="Left hand")leftHead=&e;
                if(e.role==L::Role::Section&&e.text=="Right hand")rightHead=&e;
                if(e.role==L::Role::GuideLine&&e.sub=="Walk")walk=&e;
                if(e.role==L::Role::GuideLine&&e.sub=="Turn")turn=&e;
                if(e.text.rfind("Game menus: ",0)==0)panels=e.text;
            }
            Check(leftHead&&rightHead&&leftHead->rect.x<rightHead->rect.x,"the Left hand column is still on the left");
            Check(walk&&turn&&walk->rect.x==rightHead->rect.x&&turn->rect.x==leftHead->rect.x,"Walk is under Right hand now, Turn under Left hand");
            bool mainSkills=false,offSkills=false;
            for(const auto& e:guide.elements)if(e.role==L::Role::GuideLine){
                if(e.text=="X or Y"&&e.sub=="Interact, or your skills"&&e.rect.x==leftHead->rect.x)mainSkills=true;
                if(e.text=="A or B"&&e.sub=="Your right-hand skills"&&e.rect.x==rightHead->rect.x)offSkills=true;
            }
            Check(mainSkills&&offSkills,"each column lists its own controller's buttons: X / Y interact on the left, A / B are the right hand's skills");
            Check(panels.find("Right Trigger = Ctrl in game menus")!=std::string::npos&&panels.find("Left Grip = Shift; push or pull a menu")!=std::string::npos&&panels.find("Left Y = Back")!=std::string::npos,
                  "game menus: "+panels);
        }
        {   // Sticks stay right-handed (hand_role.h Sticks): mirrored, the sticks' names kept; the Guide lists each
            // stick's pushes under the hand whose controller has it, each hand's stick click under that hand
            const auto& written=content::WrittenMenu();
            const auto* mirroredMenu=[]{framevr::handtext::Scope left(true);return &content::Menu();}();
            framevr::handtext::Scope left(true,true);
            Check(framevr::handtext::SticksKept(),"the scope sets the sticks' mode");
            {framevr::handtext::Scope right(false,true);Check(!framevr::handtext::SticksKept()&&&content::Menu()==&written,"the sticks' mode is read only left-handed");}
            const auto& shown=content::Menu();
            Check(&shown!=&written&&&shown!=mirroredMenu&&shown.rows.size()==written.rows.size(),"sticks right-handed: a menu of its own, row for row");
            for(size_t i=0;i<written.rows.size();++i){
                const auto& a=written.rows[i];const auto& b=shown.rows[i];
                const bool keep=physical(int(a.key));
                const auto same=[&](const char* x,const char* y){return (!x&&!y)||(x&&y&&std::string(y)==(keep?std::string(x):Mirror(x,false)));};
                Check(same(a.label,b.label)&&same(a.help,b.help)&&same(a.warn,b.warn)&&same(a.besideLabel,b.besideLabel),std::string("sticks right-handed, row ")+a.label+": mirrored, the sticks' names kept");
            }
            for(int k=0;k<Count;++k){
                const std::string text=WrittenHelpText(k);
                Check(std::string(HelpText(k))==(physical(k)?text:Mirror(text,false)),std::string("sticks right-handed, help of ")+WrittenHelpTitle(k));
            }
            const auto rowHelp=[](Key k){const auto* r=content::RowOf(k);return std::string(r&&r->help?r->help:"");};
            Check(rowHelp(SmoothTurn).find("right stick")!=std::string::npos&&std::string(HelpText(Snap)).find("right-stick")!=std::string::npos&&
                  rowHelp(RunWalkToggle).find("right stick up")!=std::string::npos&&rowHelp(LootLabelSize).find("right stick down")!=std::string::npos,
                  "turning, the run toggle and loot names: the right stick, as written");
            Check(rowHelp(WalkDirection).find("the left stick walks")!=std::string::npos&&rowHelp(WalkDirection).find("that stick's controller")!=std::string::npos&&
                  std::string(HelpText(RoomSteps)).find("the left stick")!=std::string::npos,"walking: the left stick and its own controller, as written");
            Check(std::string(HelpText(BodyCalibration)).find("Left Y")!=std::string::npos||std::string(HelpText(BodyCalibration)).find("Right B")==std::string::npos,
                  "the buttons stay mirrored: calibration's help names the main hand's button");
            Check(std::string(framevr::handtext::T("Right hand: the right stick turns"))=="Left hand: the right stick turns","T: the hand mirrored, the stick kept");
            {Model h;h.Open(Ready());Check(h.Get()[LeftHanded]==0&&!RowShown(LeftHandedSticks,h.Get(),{}),"right-handed (saved), the row is left out");}
            using framevr::controllers::Labels;using framevr::controllers::Layout;
            Model m;m.Open(Ready());Check(m.view==View::Guide,"the panel opens on the guide");
            L::LayoutInput in;in.detected=Labels{{Layout::Touch,Layout::Touch}};
            const auto guide=L::Build(m,{},in);CheckLayout(guide,"left-handed guide, sticks right-handed, Touch");
            const L::Element* leftHead=nullptr;const L::Element* rightHead=nullptr;
            for(const auto& e:guide.elements){
                if(e.role==L::Role::Section&&e.text=="Left hand")leftHead=&e;
                if(e.role==L::Role::Section&&e.text=="Right hand")rightHead=&e;
            }
            Check(leftHead&&rightHead&&leftHead->rect.x<rightHead->rect.x,"the Left hand column is on the left");
            const auto under=[&](const char* action)->int{   // -1 not listed, 0 under Left hand, 1 under Right hand
                for(const auto& e:guide.elements)if(e.role==L::Role::GuideLine&&e.sub==action)return e.rect.x==leftHead->rect.x?0:e.rect.x==rightHead->rect.x?1:2;
                return -1;
            };
            Check(under("Walk")==0,"Walk is under Left hand: the left controller walks");
            Check(under("Turn")==1&&under("Run or walk")==1&&under("Show all loot names")==1,"Turn, run / walk and loot names are under Right hand: the right controller turns");
            Check(under("Open or close the left wheel")==0&&under("Open or close the right wheel")==1&&under("Inventory, character and skills")==1,
                  "each hand's stick click stays with its hand: the left wheel under Left hand, the right wheel and the grip + click inventory under Right hand");
            Check(under("Interact, or your skills")==0&&under("Your right-hand skills")==1,"the buttons stay mirrored: interact on the left controller");
        }
        Check(!framevr::handtext::Left()&&&content::Menu()==&content::WrittenMenu(),"OFF: the menu as written, the same table");
        Check(changed>=60,"the left-handed sweep saw sided words change ("+std::to_string(changed)+" of "+std::to_string(compared)+" texts)");
        if(argc>1&&std::string(argv[1])=="--left-handed"){   // every changed text, to read through
            std::sort(sides.begin(),sides.end());sides.erase(std::unique(sides.begin(),sides.end()),sides.end());
            for(const auto& s:sides)std::printf("%s\n",s.c_str());
            framevr::handtext::Scope left(true);
            for(int k=0;k<Count;++k)if(std::string(HelpText(k))!=WrittenHelpText(k))std::printf("[%s] %s\n",HelpTitle(k),HelpText(k));
        }
        std::printf("left-handed: %d panels built and compared, %d texts, %d mirrored\n",panels,compared,changed);
    }
    std::printf("VR settings layout tests passed: %d checks, %d panels built\n",checks,built);
}
