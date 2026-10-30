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
    {"interactions.dominant_hand",View(1),0,content::RowKind::Choice,"Dominant hand",2,0},
    {"turn.smooth",View(1),0,content::RowKind::Toggle,"Smooth turning",0,0},
    {"turn.snap",View(1),0,content::RowKind::Stepper,"Snap turn angle",0,0},
    {"turn.speed",View(1),0,content::RowKind::Slider,"Smooth turn speed",0,0},
    {"gameplay.run_speed_pct",View(1),0,content::RowKind::Slider,"Movement speed",0,12},   // main's #308: Walk, Base run, Run 10..90%, Full run
    {"gameplay.run_walk_toggle",View(1),0,content::RowKind::Toggle,"Stick toggle",0,0},   // drawn inside Movement speed's row
    {"interactions.seated",View(1),1,content::RowKind::Toggle,"Seated play",0,0},
    {"interactions.waist_below_eyes",View(1),1,content::RowKind::Slider,"Belt below eyes",0,0},
    {"interactions.belt_forward",View(1),1,content::RowKind::Slider,"Belt forward reach",0,0},
    {"calibration.version",View(1),2,content::RowKind::Button,"Height and arm span",0,0},
    {"gameplay.motion_controls",View(2),0,content::RowKind::Toggle,"Motion combat",0,0},
    {"gameplay.melee_hit_speed",View(2),0,content::RowKind::Stepper,"Swing speed to hit",0,0},
    {"gameplay.melee_thrust_speed",View(2),0,content::RowKind::Slider,"Stab speed to hit",0,0},
    {"gameplay.skill_gestures",View(2),1,content::RowKind::Toggle,"Skills on your moves",0,0},
    {"interactions.weapon_skill_indicators",View(2),1,content::RowKind::Toggle,"Skill icons on weapons",0,0},
    {"gameplay.throw_lift_deg",View(2),1,content::RowKind::Stepper,"Thrown weapon lift",0,1},
    {"gameplay.companion_item_skills",View(2),1,content::RowKind::Toggle,"Mercenary item skills",0,0},
    {"gameplay.spell_lock",View(3),0,content::RowKind::Choice,"Skills go to the aimed monster",4,0},
    {"gameplay.projectile_aim_assist",View(3),0,content::RowKind::Choice,"Spell aim assist",3,0},
    {"gameplay.planar_spell_aim",View(3),0,content::RowKind::Toggle,"Keep spell shots level",0,0},
    {"gameplay.staff_point",View(3),0,content::RowKind::Toggle,"Aim staff along arm",0,0},
    {"gameplay.aim_follows_view",View(3),0,content::RowKind::Toggle,"Aim with your view",0,0},
    {"gameplay.ground_aim",View(3),1,content::RowKind::Choice,"Ground skill aim",3,0},
    {"gameplay.landing_circles",View(3),1,content::RowKind::Toggle,"Landing circles",0,0},
    {"gameplay.target_outline_pct",View(4),0,content::RowKind::Slider,"Monster outline",0,1},
    {"gameplay.aim_highlights",View(4),0,content::RowKind::Choice,"Outlines shown",2,0},
    {"gameplay.highlight_left_hue",View(4),0,content::RowKind::Swatch,"Left hand outline color",0,1},
    {"gameplay.highlight_right_hue",View(4),0,content::RowKind::Swatch,"Right hand outline color",0,1},
    {"bow.path_laser_hue",View(4),1,content::RowKind::Swatch,"Arrow path line",0,1},
    {"gameplay.shield_bash_speed",View(5),0,content::RowKind::Slider,"Bash speed to hit",0,0},
    {"gameplay.guard_strain_vignette",View(5),0,content::RowKind::Toggle,"Guard strain shadow",0,0},
    {"gameplay.shield_grip_cm",View(5),1,content::RowKind::Slider,"Shield along forearm",0,0},
    {"gameplay.shield_out_cm",View(5),1,content::RowKind::Slider,"Shield out from arm",0,0},
    {"gameplay.shield_up_cm",View(5),1,content::RowKind::Slider,"Shield across forearm",0,0},
    {"gameplay.shield_follow_pct",View(5),1,content::RowKind::Slider,"Shield wrist turn",0,2},
    {"gameplay.shield_roll_deg",View(5),1,content::RowKind::Slider,"Shield roll",0,0},
    {"interactions.vitals_location",View(6),0,content::RowKind::Choice,"Life and mana",4,0},
    {"interactions.vitals_tilt",View(6),0,content::RowKind::Slider,"Life and mana tilt",0,0},
    {"interactions.navigation_location",View(6),1,content::RowKind::Choice,"Map and compass",4,0},
    {"interactions.map_top_down",View(6),1,content::RowKind::Choice,"Map view",4,0},
    {"interactions.map_rotate",View(6),1,content::RowKind::Toggle,"Turn map with you",0,0},
    {"interactions.companion_counts",View(6),1,content::RowKind::Toggle,"Buffs and companions",0,0},
    {"hud.menu_distance",View(7),0,content::RowKind::Choice,"Menu distance",3,0},
    {"interactions.leather_menu",View(7),0,content::RowKind::Stepper,"Hold menu",0,3},   // main's #360, drawn inside Menu distance's row; Off / On / Menu only (2026-10-29)
    {"hud.tooltip_timing",View(7),0,content::RowKind::Choice,"Item details delay",3,0},
    {"interactions.controller_labels",View(7),0,content::RowKind::Choice,"Button names",5,0},
    {"hud.labels_size",View(7),1,content::RowKind::Slider,"Loot name size",0,0},
    {"hud.ordinary_mob_bars",View(7),1,content::RowKind::Toggle,"Weak monster bars",0,0},
    {"interactions.critical_alerts",View(8),0,content::RowKind::Toggle,"Low life and mana alert",0,0},
    {"gameplay.low_life_heart_start",View(8),0,content::RowKind::Stepper,"Heartbeat starts at",0,1},
    {"hud.blood_visor",View(8),0,content::RowKind::Toggle,"Blood on visor",0,0},
    {"hud.blood_wipe",View(8),0,content::RowKind::Toggle,"Wipe blood by hand",0,0},
    {"gameplay.offline_players",View(8),1,content::RowKind::Stepper,"Offline difficulty",0,2},
    {"gameplay.enemy_sight_pct",View(8),1,content::RowKind::Slider,"Monster sight range",0,0},
    {"gameplay.companion_ai",View(8),1,content::RowKind::Toggle,"Smarter mercenary",0,0},
    {"gameplay.companion_tactical_commands",View(8),1,content::RowKind::Toggle,"Tactical commands",0,0},
    {"sky.enabled",View(9),0,content::RowKind::Toggle,"Outdoor sky",0,0},
    {"fog.enabled",View(9),0,content::RowKind::Toggle,"Distance fog",0,0},
    {"fog.end",View(9),0,content::RowKind::Slider,"Fog distance",0,0},   // main's #366, drawn inside Distance fog's row
    {"ceiling.enabled",View(9),0,content::RowKind::Toggle,"Dungeon ceilings",0,0},
    {"interactions.bottle_physics",View(9),1,content::RowKind::Toggle,"Roll and break bottles",0,0},
    {"interactions.prop_physics",View(9),1,content::RowKind::Toggle,"Knock over small props",0,0},
    {"interactions.body_physics",View(9),1,content::RowKind::Toggle,"Pushable bodies",0,0},   // main's #361
    {"interactions.elemental_hands",View(9),1,content::RowKind::Toggle,"Spell glow on hands",0,0},
    {"torch.flame",View(9),1,content::RowKind::Toggle,"Torch flame",0,0},
    {"haptics.strength",View(10),0,content::RowKind::Slider,"Overall vibration",0,1},
    {"haptics.melee",View(10),0,content::RowKind::Slider,"Melee hits",0,1},
    {"haptics.short_melee",View(10),0,content::RowKind::Toggle,"Short melee taps",0,0},
    {"haptics.bow_draw",View(10),0,content::RowKind::Slider,"Bow draw",0,1},
    {"haptics.bow_release",View(10),0,content::RowKind::Slider,"Bow and throw release",0,1},
    {"haptics.spells",View(10),0,content::RowKind::Slider,"Spell casts",0,1},
    {"haptics.interactions",View(10),0,content::RowKind::Slider,"Loot, potions and menus",0,1},
    {"haptics.alerts",View(10),0,content::RowKind::Slider,"Low life and mana alerts",0,1},
    {"stereo.rendering",View(11),0,content::RowKind::Choice,"Eyes drawn per frame",2,0},
    {"render.vr_resolution",View(11),0,content::RowKind::Choice,"Render resolution",7,0},
    {"render.fixed_foveation",View(11),0,content::RowKind::Choice,"Lower detail at edges",3,0},
    {"render.foveation_strength",View(11),0,content::RowKind::Choice,"Edge detail reduction",4,0},
    {"render.frame_generation",View(11),0,content::RowKind::Choice,"Frame generation",3,0},   // main's #313, in Image; its NVIDIA rows on More (no page)
    {"render.rings",View(11),1,content::RowKind::Stepper,"Terrain distance",0,0},
    {"render.model_radius",View(11),1,content::RowKind::Slider,"Scenery distance",0,0},
    {"render.big_radius",View(11),1,content::RowKind::Slider,"Big objects",0,1},   // main's #366, drawn inside Scenery distance's row
    {"render.defer_gpu_wait",View(11),1,content::RowKind::Offer,"Defer GPU wait",0,0},
    {"render.frame_generation_preset",View(14),0,content::RowKind::Choice,"NVIDIA flow quality",3,0},
    {"render.frame_generation_scale",View(14),0,content::RowKind::Choice,"NVIDIA flow scale",3,0},
};

static bool PopoverLayer(L::Role r){return r==L::Role::PopoverDim||r==L::Role::Popover||r==L::Role::PopoverTitle||r==L::Role::PopoverOption||r==L::Role::PopoverHint||r==L::Role::ColorDot;}
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
    if(argc>1)return Dump(std::atoi(argv[1]),argc>2?std::atoi(argv[2]):NoHelp);
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
            Check(!(RowShown(Snap,v,c)&&RowShown(TurnSpeed,v,c)),"Snap turn angle and Smooth turn speed are never shown together (the compile-time count relies on it)");
            if(content::PageOf(View(view)).settings&&View(view)!=View::More)Check(n>=1,std::string("a settings page shows rows: ")+ViewName(View(view)));
        }
    }
    // Title screen, everything on: the fullest pages.
    {
        Values v=state(0b0100'0101);v[GroundAim]=2;
        Check(L::CountRows(View::Vibration,v,{})==8&&L::CountRows(View::Shield,v,{})==7,"Vibration holds 8 rows; Shield 7 (one guard strain switch since main's #310)");
        Check(L::RowsOf(View::Targets,v,{}).count==5,"Targets: the outline, both hand colors as their own rows, and the arrow path line");
        bool right=false;const auto rows=L::RowsOf(View::Targets,v,{});
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
                    if(e.role==L::Role::Row||e.role==L::Role::Tab||e.role==L::Role::PresetCard){
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
        Check(m.view==View::Guide&&m.lastPage==View::Body,"the panel opens on the Controller guide");
        auto guide=L::Build(m,{},{});CheckLayout(guide,"guide");
        Check(CountRole(guide,L::Role::GuideLine)>=16,"the guide lists both hands");
        Check(!FindRole(guide,L::Role::FooterResetPage)&&!FindRole(guide,L::Role::FooterApply)&&FindRole(guide,L::Role::FooterClose),"the guide's footer: Close only");
        L::Press(m,{Kind::Open,int(View::Shield),-1},0);Check(m.view==View::Shield&&m.lastPage==View::Shield,"a tab opens its page");
        L::Press(m,{Kind::Open,int(View::Guide),-1},0);L::Press(m,{Kind::Open,int(View::Guide),-1},0);
        Check(m.view==View::Shield,"the Guide link again returns to the page");
        L::Press(m,{Kind::Open,int(View::Targets),TargetHighlight},0);Check(m.view==View::Targets&&m.focusKey==TargetHighlight,"Open points the card at its row");
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
        L::Press(m,{Kind::Open,int(View::Aim),-1},0);
        const auto aim=L::Build(m,m.context,{});CheckLayout(aim,"aim paladin");
        int chips=0;const L::Element* on=nullptr;
        for(const auto& e:aim.elements)if(e.role==L::Role::Chip&&e.key==SpellLock){++chips;if(e.text=="On")on=&e;}
        Check(chips==2&&on,"Off / On for a class without bow skills");
        L::Press(m,on->hit,0);Check(m.Get()[SpellLock]==3,"On sets spells and keeps bow skills");
        L::Press(m,{Kind::Default,SpellLock},0);Check(m.Get()[SpellLock]==2,"Default turns spells off, bow skills kept");
        // Presets never touch the owner's keys, calibration or rows hidden for the class.
        m.Set(StereoRendering,1);m.Set(CompanionAi,1);m.Set(ElementalHands,0);
        for(size_t p=0;p<tables.presetCount;++p){
            L::ApplyPreset(m,int(p));   // the preset logic, while presets are off (content::PresetsReady)
            Check(m.Get()[StereoRendering]==1&&m.Get()[CompanionAi]==1&&m.Get()[ElementalHands]==0&&m.Get()[DeferGpuWait]==0&&m.Get()[EyeHeight]==m.Applied()[EyeHeight]&&m.Get()[ArmSpan]==m.Applied()[ArmSpan],
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
        m.Open(Ready());m.view=View::World;m.confirmClose=true;m.undoResetUntil=9;m.focusKey=3;m.pickedPreset=1;m.closeWhenSaved=true;m.resetKeys=ChangeMask{1};
        m.Open(Ready());
        Check(m.view==View::World&&!m.confirmClose&&m.undoResetUntil==0&&m.focusKey==-1&&m.pickedPreset==-1&&!m.closeWhenSaved&&!m.resetKeys,"Open resets every new field");
        L::Press(m,{Kind::Close},0);Check(!m.open,"Close without a draft closes at once");
    }
    {   // Calibrate is off while a draft waits; offline difficulty locks without its adapter.
        Model m;Snapshot s=Ready();s.offlineDifficultyAvailable=false;m.Open(s);
        L::Press(m,{Kind::Open,int(View::Body),-1},0);
        auto body=L::Build(m,{},{});CheckLayout(body,"body");
        const L::Element* calibrate=nullptr;for(const auto& e:body.elements)if(e.hit.kind==Kind::Calibrate)calibrate=&e;
        Check(calibrate&&calibrate->Has(L::Enabled),"Calibrate body with no draft");
        m.Set(Dominant,1);body=L::Build(m,{},{});
        for(const auto& e:body.elements)if(e.hit.kind==Kind::Calibrate)calibrate=&e;
        Check(!calibrate->Has(L::Enabled)&&FindRole(body,L::Role::Label,BodyCalibration)->sub=="Apply or discard first","Calibrate is disabled while Dirty, with the reason");
        L::Press(m,{Kind::Open,int(View::Survival),-1},0);
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
        static const char* const lines[]={"More natural shield wrists","Guard strain edge: Pale glow, Dark edge or Off","Grab a menu with right grip to move it","Performance settings in one place",
            "Menus and wrist maps fixed at high resolutions","Cleaner One by turns view","Smoother frames","Scenery distance now starts at 210"};
        L::LayoutInput notes;notes.notes={"D2R VR Unleashed / v0.3.24","A mod by monkeylashes","https://github.com/bearstonem/d2r-pcvr","beadlebytes; ChuckRibbits; Sergious.",lines,8};
        L::Press(m,{Kind::Open,int(View::WhatsNew),-1},0);
        const auto news=L::Build(m,{},notes);CheckLayout(news,"what's new");Check(CountRole(news,L::Role::NotesText)>=10,"What's new lists its lines");
        Snapshot off=Ready();off.ready=false;m.Open(off);L::Press(m,{Kind::Open,int(View::World),-1},0);
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
        // A grid: 9 named choices in two columns (a made-up list for Offline difficulty).
        static constexpr content::Option nine[]={{0,"Game's setting","a"},{1,"1 player","b"},{2,"2 players","c"},{3,"3 players","d"},{4,"4 players","e"},
                                                  {5,"5 players","f"},{6,"6 players","g"},{7,"7 players","h"},{8,"8 players","i"}};
        static constexpr content::Override grid[]={{.id="gameplay.offline_players",.page=View::Survival,.section="Offline monsters",.options=nine,.optionCount=9},
                                                   {.id="gameplay.shield_grip_cm",.page=View::Shield,.section="Fit on the arm",.options=nine,.optionCount=9}};
        static const content::MenuData gridMenu=content::BuildMenu(grid,std::size(grid),nullptr,0);
        const L::Tables gt=L::TablesOf(gridMenu);
        Model g;g.Open(Ready());L::Press(g,{Kind::Open,int(View::Survival),-1},0,gt);
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
    {   // Frame generation (main's #313): a row in Performance > Image; NVIDIA's quality and scale on More, only while NVIDIA is chosen.
        Check(tables.Row(FrameGeneration)&&tables.Row(FrameGeneration)->page==View::Performance&&std::string(tables.Section(View::Performance,tables.Row(FrameGeneration)->section))=="Image",
              "frame generation sits in Performance > Image");
        Check(tables.Row(FrameGenPreset)->page==View::More&&tables.Row(FrameGenScale)->page==View::More&&tables.Row(FrameGenPreset)->fallback&&std::string(tables.Row(FrameGenPreset)->label)=="NVIDIA flow quality",
              "NVIDIA's quality and scale: our words, on More (Performance is full)");
        Values off=Defaults();Check(L::CountRows(View::More,off,{})==0,"More stays hidden while frame generation is Off or FidelityFX");
        Values nvidia=Defaults();nvidia[FrameGeneration]=2;nvidia[StereoRendering]=0;   // frame generation runs with Both eyes only
        Check(L::CountRows(View::More,nvidia,{})==2,"NVIDIA brings its two rows to More");
        {   // By turns (#343, owner 2026-10-25): the Frame generation row, its status line and NVIDIA's two rows are left out, not locked
            Values turns=nvidia;turns[StereoRendering]=1;
            Check(!RowShown(FrameGeneration,turns,{})&&!RowShown(FrameGenPreset,turns,{})&&!RowShown(FrameGenScale,turns,{}),"By turns hides Frame generation and NVIDIA's rows");
            Check(L::CountRows(View::More,turns,{})==0&&!L::KeyVisible(FrameGeneration,turns,{})&&!L::KeyVisible(FrameGenPreset,turns,{}),"and More has nothing to show");
            Check(L::CountRows(View::Performance,turns,{})+1==L::CountRows(View::Performance,nvidia,{}),"Performance has exactly one row fewer");
            Values fidelity=Defaults();fidelity[StereoRendering]=0;fidelity[FrameGeneration]=1;
            Check(RowShown(FrameGeneration,fidelity,{})&&!RowShown(FrameGenPreset,fidelity,{}),"Both eyes shows Frame generation; NVIDIA's rows only with NVIDIA");
            Model hid;hid.Open(Ready(turns));L::Press(hid,{Kind::Open,int(View::Performance),-1},0);
            L::LayoutInput inTurns;inTurns.frameGen={framevr::framegen::State::NeedsBothEyes,1,1,2,1,1,0,0,0};   // no card: the pointer cannot point at a row that is not drawn
            const auto hidden=L::Build(hid,{},inTurns);CheckLayout(hidden,"performance, by turns, frame generation hidden");
            std::string anyFg;for(const auto& e:hidden.elements)if(e.key==FrameGeneration||e.key==FrameGenPreset||e.key==FrameGenScale)anyFg+=" role"+std::to_string(int(e.role))+" '"+e.text+"'";
            Check(anyFg.empty()&&!FindRole(hidden,L::Role::Label,FrameGeneration),"offending:"+anyFg+" "+"no Frame generation row, chip, status line or orphan header is drawn");
            Check(std::string(HelpText(FrameGeneration)).find("Both eyes")!=std::string::npos,"its help still tells how to get it: Both eyes");
        }
        Check(L::CountRows(View::Performance,nvidia,{})<=L::MaxViewRows,"Performance stays within 8 rows");
        Model m;m.Open(Ready(nvidia));L::Press(m,{Kind::Open,int(View::Performance),-1},0);
        L::LayoutInput in;in.frameGen={framevr::framegen::State::Active,1,1,2,1,1,0,0,0};in.card=FrameGeneration;
        const auto perf=L::Build(m,{},in);CheckLayout(perf,"performance, frame generation active");
        Check(FindRole(perf,L::Role::Label,FrameGeneration)->sub=="Now: active"&&!FindRole(perf,L::Role::Label,FrameGeneration)->Has(L::Warn),
              "its row says what OFXR Bridge is doing");
        {L::LayoutInput paused=in;paused.frameGen.state=framevr::framegen::State::NeedsBothEyes;const auto p=L::Build(m,{},paused);CheckLayout(p,"frame generation paused");
         Check(FindRole(p,L::Role::Label,FrameGeneration)->sub=="Now: needs both eyes"&&FindRole(p,L::Role::Label,FrameGeneration)->Has(L::Warn),"by turns it is paused, in amber (#316)");}
        // By turns the row is gone, so it shows only with Both eyes; switching the eyes brings it back.
        Model t;t.Open(Ready());L::Press(t,{Kind::Open,int(View::Performance),-1},0);
        Check(t.Get()[StereoRendering]==1&&!FindRole(L::Build(t,{},{}),L::Role::Label,FrameGeneration),"by turns there is no Frame generation row to press");
        L::Press(t,{Kind::Set,StereoRendering,0,0},0);
        Check(FindRole(L::Build(t,{},{}),L::Role::Label,FrameGeneration)&&!FindRole(L::Build(t,{},{}),L::Role::Label,FrameGeneration)->Has(L::Locked),"with Both eyes the row returns, not locked");
        L::Press(t,{Kind::Set,FrameGeneration,0,1},0);
        Check(t.Get()[FrameGeneration]==1,"with Both eyes it turns on");
        L::Press(t,{Kind::Set,StereoRendering,0,1},0);
        Check(!FindRole(L::Build(t,{},{}),L::Role::Label,FrameGeneration),"back to By turns the row goes again, with no orphan");
        Check(FindRole(perf,L::Role::CardState)&&FindRole(perf,L::Role::CardState)->text=="Active, OFXR Bridge NVIDIA Medium, 50% scale","its card has main's status line");
        const auto band=L::Build(m,{},{});bool link=false;for(const auto& e:band.elements)if(e.hit.kind==Kind::Open&&e.hit.index==int(View::More)&&e.text=="2 more")link=true;
        Check(link,"the band links More with NVIDIA's two rows");
    }
    {   // Main's frame generation text and readout note (#313, #343), with the Virtual Desktop sentence
        const char* fgHelp=HelpText(FrameGeneration);const std::string vd="With Virtual Desktop, set its SSW to Disabled.";
        const std::string rowHelp=tables.Row(FrameGeneration)->help;
        Check(rowHelp.size()<=160,"the card text (what the pointed row shows) is at most 160 characters");
        Check(rowHelp.size()>=vd.size()&&rowHelp.compare(rowHelp.size()-vd.size(),vd.size(),vd)==0,"and it ends with the Virtual Desktop SSW sentence");
        (void)fgHelp;
        // while frame generation is active the readout gives way to the note (main's FrameGenPerformanceNote); otherwise the numbers
        Values both=Defaults();both[StereoRendering]=0;both[FrameGeneration]=1;
        Model m;m.Open(Ready(both));L::Press(m,{Kind::Open,int(View::Performance),-1},0);
        const auto readoutText=[&](const L::Layout& l){std::string all;for(const auto& e:l.elements)if(e.role==L::Role::Readout)all+=e.text+"\n";return all;};
        L::LayoutInput active;active.frameGen={framevr::framegen::State::Active,1,1,2,1,1,0,0,0};
        active.performance=PerformanceReadout{true,true,52,11.1f,1.0f,7.0f,9.4f,1.5f};
        const auto with=L::Build(m,{},active);CheckLayout(with,"performance, frame generation active, readout note");
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
        Values nvidia=Defaults();nvidia[FrameGeneration]=2;nvidia[StereoRendering]=0;
        Model m;m.Open(Ready(nvidia));L::Press(m,{Kind::Open,int(View::More),-1},0);
        Check(m.view==View::More,"More opens while NVIDIA has rows there");
        m.Close();m.Open(Ready(nvidia));L::SettleView(m);
        Check(m.view==View::More&&m.morePage==0,"reopened with rows: still More");
        m.morePage=5;L::SettleView(m);Check(m.view==View::More&&m.morePage==0,"a kept More page past the last clamps to the last");
        m.Close();m.Open(Ready());L::SettleView(m);   // NVIDIA no longer chosen: More has no rows
        Check(m.view==View::Guide&&m.morePage==0,"a kept More with no rows falls to the Guide");
        Model by;by.Open(Ready(nvidia));L::Press(by,{Kind::Open,int(View::More),-1},0);by.Close();
        Values turns=nvidia;turns[StereoRendering]=1;by.Open(Ready(turns));L::SettleView(by);
        Check(by.view==View::Guide,"By turns empties More too: back to the Guide");
        Model keep;keep.Open(Ready());L::Press(keep,{Kind::Open,int(View::Combat),-1},0);keep.Close();keep.Open(Ready());L::SettleView(keep);
        Check(keep.view==View::Combat,"a page with rows is kept");
        keep.Close();keep.StartOver();keep.Open(Ready());L::SettleView(keep);
        Check(keep.view==View::Guide&&keep.lastPage==View::Body&&keep.morePage==0&&keep.notesPage==0,"StartOver (a new game): the Guide, page 1");
        Model pre;pre.Open(Ready());pre.view=View::Presets;L::SettleView(pre);
        Check(pre.view==View::Guide,"Presets, unavailable, fall to the Guide");
    }
    {   // Movement speed and Stick toggle (main's #308, our rows): two adjacent full rows on one page, Body, with room to spare
        const auto* speed=tables.Row(RunSpeed);const auto* toggle=tables.Row(RunWalkToggle);
        Check(speed&&toggle&&speed->page==View::Body&&toggle->page==View::Body&&!speed->fallback&&!toggle->fallback,"both rows are on the Body page, not the More fallback");
        Check(speed&&toggle&&speed->section==toggle->section,"in one section");
        for(const auto& c:contexts)for(bool seated:{false,true}){
            Values v=Defaults();v[Seated]=seated;
            Check(L::CountRows(View::Body,v,c)<=L::MaxViewRows&&L::KeyVisible(RunSpeed,v,c)&&L::KeyVisible(RunWalkToggle,v,c),"Body keeps both settings within 8 rows for every class");
        }
        Model b;b.Open(Ready());L::Press(b,{Kind::Open,int(View::Body),-1},0);
        const auto page=L::Build(b,{},{});CheckLayout(page,"body with movement rows");
        const auto* label=FindRole(page,L::Role::Label,RunSpeed);Check(label!=nullptr,"Movement speed is drawn");
        // the designer's shape: the toggle is a Value in Movement speed's row, 16 px or more after the + button, never a slab of its own
        const auto* tog=FindRole(page,L::Role::Value,RunWalkToggle);const auto* plus=FindRole(page,L::Role::StepPlus,RunSpeed);
        Check(tog&&plus&&tog->hit==Hit{Kind::Set,RunWalkToggle,0,1.f}&&tog->rect.x-(plus->rect.x+plus->rect.w)>=16,"Stick toggle: a press sets it, 16 px or more from the + button");
        b.Set(RunWalkToggle,1);
        const auto* on=FindRole(L::Build(b,{},{}),L::Role::Value,RunWalkToggle);Check(on&&on->hit==Hit{Kind::Set,RunWalkToggle,0,0.f},"and a press when On sets it Off");
        // the value labels: Walk, Base run, Run N%, Full run (main's ValueLabel)
        const auto value=[&](float v)->std::string{Model x;x.Open(Ready());x.Set(RunSpeed,v);L::Press(x,{Kind::Open,int(View::Body),-1},0);
            const auto l=L::Build(x,{},{});for(const auto& e:l.elements)if(e.key==RunSpeed&&e.role==L::Role::Value)return e.text;return "?";};
        Check(value(-10)=="Walk"&&value(0)=="Base run"&&value(40)=="Run 40%"&&value(100)=="Full run","value labels read Walk, Base run, Run N%, Full run");
    }
    {   // Every override's warn line reaches its row, and draws: Motion combat, Spell glow on hands, Frame generation.
        int warned=0;
        for(const auto& o:content::Overrides)if(o.warn){
            const int k=content::KeyOfId(o.id);if(k<0)continue;++warned;
            Check(tables.Row(Key(k))&&tables.Row(Key(k))->warn==o.warn,std::string("the warn line reaches its row: ")+o.id);
        }
        Check(warned>=4,"the overrides carry warn lines");
        Values v=Defaults();v[StereoRendering]=0;   // Both eyes: Frame generation is not locked
        Model m;m.Open(Ready(v));m.SetContext({heroclass::Sorceress});
        auto sub=[&](View view,Key k){L::Press(m,{Kind::Open,int(view),-1},0);const auto* e=FindRole(L::Build(m,m.context,{}),L::Role::Label,k);return e?std::make_pair(e->sub,e->Has(L::Warn)):std::make_pair(std::string(),false);};
        for(auto [view,k]:{std::pair{View::Combat,Motion},std::pair{View::World,ElementalHands},std::pair{View::Performance,FrameGeneration}}){
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
        Model m;m.Open(Ready(v));L::Press(m,{Kind::Open,int(View::Body),-1},0);
        const auto body=L::Build(m,{},{});
        Check(FindRole(body,L::Role::Label,SmoothTurn)->sub=="Can cause motion sickness"&&FindRole(body,L::Role::Label,SmoothTurn)->Has(L::Warn)&&
              FindRole(body,L::Role::Label,TurnSpeed)->sub=="Fast turns are harder on the stomach","warn lines above their thresholds");
        m.Set(TurnSpeed,150);Check(FindRole(L::Build(m,{},{}),L::Role::Label,TurnSpeed)->sub.empty(),"and none at or under them");
        L::Press(m,{Kind::Open,int(View::Performance),-1},0);
        Check(FindRole(L::Build(m,{},{}),L::Role::Label,StereoRendering)->sub=="Fast monsters can look doubled","One by turns warns");
        Check(tables.Row(SmoothTurn)->warnWhen.text&&tables.Row(StereoRendering)->warnWhen.above==.5f,"they are the overrides' data");
    }
    {   // The color grid (docs/VR_SETTINGS_MENU.md): By range a chip, 24 hues as dots, 6 a row in hue order, two presses to any color.
        Values v=Defaults();v[TargetHighlight]=60;Model m;m.Open(Ready(v));L::Press(m,{Kind::Open,int(View::Targets),-1},0);
        const auto page=L::Build(m,{},{});CheckLayout(page,"targets");
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
        Values a=Defaults();Model bow;bow.Open(Ready(a));bow.SetContext({heroclass::Amazon});L::Press(bow,{Kind::Open,int(View::Targets),-1},0);
        L::Press(bow,{Kind::Popover,BowLaser},0);L::LayoutInput bi;bi.nowMs=400;const auto laser=L::Build(bow,bow.context,bi);CheckLayout(laser,"arrow path grid");
        int laserDots=0;const L::Element* off=nullptr;for(const auto& e:laser.elements){if(e.role==L::Role::ColorDot)++laserDots;if(e.role==L::Role::PopoverOption)off=&e;}
        Check(laserDots==24&&off&&off->text=="Off"&&off->hit.value==LaserOff&&off->Has(L::Selected),"the arrow path line: Off a chip (chosen by default), 24 hues");
    }
    {   // The fallback (docs/VR_SETTINGS_MENU.md): settings with no override still show, with upstream's words.
        const auto& demo=content::FallbackDemo();const L::Tables t=L::TablesOf(demo);
        auto rowOf=[&](Key k)->const content::ContentRow*{return t.Row(k);};
        Check(rowOf(TorchFlame)&&rowOf(TorchFlame)->page==View::World&&rowOf(TorchFlame)->fallback&&
              std::string(t.Section(View::World,rowOf(TorchFlame)->section))=="New","an unsorted [torch] setting: World's New section, at the bottom");
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
        bool moreReported=false;for(const auto& l:demo.report)if(l.find("the More page is not empty: 12 settings")!=std::string::npos)moreReported=true;
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
        L::Press(w,{Kind::Open,int(View::Aim),-1},0,t);
        for(const auto& e:L::Build(w,{},{},wide,t).elements)if(e.role==L::Role::Label)Check(e.text.size()<3||e.text.compare(e.text.size()-3,3,"...")!=0,"our own labels are never cut");
    }
    std::printf("VR settings layout tests passed: %d checks, %d panels built\n",checks,built);
}
