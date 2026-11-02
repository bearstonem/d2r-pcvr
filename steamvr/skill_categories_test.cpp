#include "interactions.h"
#include "interaction_paint.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace framevr::interaction;
namespace categories=framevr::skillcategories;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct Sim {
    Engine engine;Input in;Output out;
    Sim(int hand=0){
        in.active=true;in.session=10;in.epoch=1;in.tracked[0]=in.tracked[1]=true;
        in.head.p={0,1.7f,0};in.hand[0].p={.25f,1.3f,-.4f};in.hand[1].p={-.25f,1.3f,-.4f};
        in.wheelHand=hand;in.snapshot.session=10;in.snapshot.skillClass=3;
        in.snapshot.bindingsEditable=true;in.snapshot.bindingBank=0;in.snapshot.activeSet=1;
        for(auto& skill:in.snapshot.skills)skill.known=true;
        for(int id:{0,357,96,98,99,100,102,103,108,113,114,118,119,122,123,54})Add(id);
        Add(96,777);Step();Step();Step();
    }
    void Add(int id,int owner=-1){auto& skill=in.snapshot.catalog[in.snapshot.skillCount++];skill.id=id;skill.owner=owner;skill.known=skill.available=true;}
    Output Step(){in.time+=.02;in.at=1000000+uint64_t(in.time*1000);in.snapshot.at=in.at;return out=engine.Step(in);}
    Output Click(int cell){in.trigger=0;Step();in.hover=cell;in.trigger=1;return Step();}
    void Open(){in.wheel=true;Click(0);Check(out.editor==2,"hand slot opens picker");}
};
int main(){
    // Representative game skill identities, independent of tab enumeration.
    struct Example{int cls,id,group;};
    for(const auto& e:{Example{0,6,1},{0,8,2},{0,10,3},{1,36,1},{1,54,2},{1,39,3},
        {2,66,1},{2,67,2},{2,70,3},{3,96,1},{3,98,2},{3,99,3},
        {4,126,1},{4,127,2},{4,130,3},{5,221,1},{5,223,2},{5,225,3},
        {6,251,1},{6,252,2},{6,254,3},{7,373,1},{7,383,2},{7,402,3}})
        Check(categories::Category(e.cls,e.id)==e.group,"native class tree membership");
    for(int cls=0;cls<8;++cls){
        const auto& c=categories::Classes[cls];Check(std::strlen(c.pages)==30,"complete supported class ID range");
        for(int i=0;i<30;++i)Check(categories::Category(cls,c.first+i)>=1&&categories::Category(cls,c.first+i)<=3,"every class skill has a tree");
        for(int id:{-1,0,357,359,9999})Check(categories::Category(cls,id)==categories::General,"general and unknown actions stay accessible");
        Check(categories::Category(cls,c.first,123)==categories::General,"item charges separate from learned class skill");
    }
    Check(categories::Category(3,54)==categories::General,"foreign oskill without an item owner stays in General / Items");
    Check(categories::Category(-1,96)==categories::General&&categories::Category(8,373)==categories::General,"unknown class fallback is safe");
    for(int i=0;i<categories::Count;++i){
        const auto b=SkillCategoryBounds(i);
        Check(WheelCell((b.left+b.right)/2,(b.top+b.bottom)/2,-1,MenuPage::EditSkills)==SkillCategoryCell(i),"category paint and click centers agree");
        Check(b.bottom<SkillOptionBounds(0).top,"category buttons cannot overlap first skill row");
        if(i+1<categories::Count)Check(WheelCell((b.right+SkillCategoryBounds(i+1).left)/2,.24f,-1,MenuPage::EditSkills)==-1,"tab gap has no action");
    }
    for(int i=0;i<8;++i){const auto b=SkillOptionBounds(i);
        Check(WheelCell((b.left+b.right)/2,(b.top+b.bottom)/2,-1,MenuPage::EditSkills)==i,"all skill rows use shared hit geometry");
        Check(b.bottom*768<637,"last row clears hint and footer");}
    for(int hand:{0,1}){
        Sim s(hand);s.Open();
        Check(s.out.editCategory==categories::All&&s.out.editCount==8&&s.out.editPages==3,"All preserves full catalog and pagination");
        Check(s.out.editCategoryCounts==std::array<int,5>{18,1,10,2,5},"tree counts include learned skills, general actions (and Draw glyph) and charges separately");
        s.Click(10);s.Click(10);Check(s.out.editPage==2,"All reaches last page");
        s.Click(SkillCategoryCell(2));
        Check(s.out.editPage==0&&s.out.editPages==2&&s.out.editOptions[0].id==98&&s.out.editCount==8,"category change resets page and filters exact identities");
        Check(s.out.request.action==Action::None&&s.out.select==-2,"changing a category never binds or casts");
        s.in.hover=0;s.Step();Check(s.out.request.action==Action::None,"held tab trigger cannot select a newly exposed skill");
        s.Click(10);Check(s.out.editCount==2&&s.out.editOptions[0].id==122,"filtered next page uses filtered index");
        s.Click(SkillCategoryCell(categories::General));
        Check(s.out.editCount==5&&s.out.editOptions[2].id==54&&s.out.editOptions[3].owner==777,"foreign oskills and own-class item charges remain selectable");
        auto context=Context(s.in.snapshot,s.out,true);auto changed=s.out;
        changed.editCategory=categories::All;Check(context!=Context(s.in.snapshot,changed,true),"category header invalidates stale paint even for same rows");
        changed=s.out;changed.editClass=1;Check(context!=Context(s.in.snapshot,changed,true),"class headings invalidate paint");
        changed=s.out;++changed.editCategoryCounts[1];Check(context!=Context(s.in.snapshot,changed,true),"category counts invalidate paint");
        // Native catalog refresh/reordering cannot change the frozen choice.
        std::swap(s.in.snapshot.catalog[0],s.in.snapshot.catalog[16]);
        const auto r=s.Click(3).request;
        Check(r.action==Action::BindSkill&&r.skill==96&&r.skillOwner==777&&r.index==framevr::skills::HandSlot(hand,0),"filtered selection binds exact charged-skill identity to either hand");
        Check(CanBind(s.in.snapshot,r,s.in.at),"current item-owned skill can bind");
        auto gone=s.in.snapshot;gone.catalog[0].owner=888;
        Check(!CanBind(gone,r,s.in.at),"removed/replaced charges cannot bind a different item");
        gone=s.in.snapshot;gone.bindingBank=16;Check(!CanBind(gone,r,s.in.at),"changed weapon set refuses stale assignment");
        gone=s.in.snapshot;gone.session=11;Check(!CanBind(gone,r,s.in.at),"changed character refuses stale assignment");
        for(int i=0;i<5;++i)Check(s.Step().request.action==Action::None,"holding selection cannot repeat assignment");
    }
    {
        Sim s;s.in.snapshot.skillClass=4;s.in.snapshot.skillCount=0;s.Add(0);s.Add(126);s.Add(130);s.Open();
        s.Click(SkillCategoryCell(2));Check(s.out.editCategory==categories::All&&s.out.feedback==3,"empty/passive-only tree is disabled");
        s.Click(SkillCategoryCell(3));Check(s.out.editCount==1&&s.out.editOptions[0].id==130,"nonempty neighboring tree still works");
        s.Click(11);Check(s.out.editor==0&&s.out.request.action==Action::None,"Back cancels without assigning");
        s.Click(0);Check(s.out.editCategory==3&&s.out.editPage==0,"reopening picker keeps the tree picked in this game, on page 1 (menus remember)");
        s.in.snapshot.bindingBank=16;s.in.trigger=0;s.Step();Check(s.out.editor==0,"weapon swap closes frozen picker");
        s.Click(0);s.in.cancel=true;s.Step();Check(s.out.editor==0&&s.out.cancelPending,"menu cancellation drops category picker");
    }
    for(int hand:{0,1}) {   // Glyph casting: Draw glyph in the button picker, last under General / Items
        Sim s(hand);s.Open();
        s.Click(SkillCategoryCell(categories::General));
        Check(s.out.editCount==5&&s.out.editOptions[4].id==GlyphDrawId,"Draw glyph: last under General / Items");
        const auto r=s.Click(4).request;
        Check(r.action==Action::BindGlyphDraw&&r.index==framevr::skills::HandSlot(hand,0)&&r.skill==-1&&r.skillOwner==-1&&r.bank==0&&
              r.previousSkill==s.in.snapshot.skills[size_t(r.index)].id,"choosing it: this button draws, its game skill cleared");
        Check(CanBind(s.in.snapshot,r,s.in.at),"the clear is a valid binding");
        s.in.reply={r.sequence,r.session,Result::Applied};s.Step();
        Check(s.out.editor==0,"applied: back on the hand wheel");
    }
    {   // the GLYPHS page: a shape's skill, from the hero's own skills that need no target
        Sim s;s.in.snapshot.catalog[2].directed=true;   // 96 needs a target
        s.in.wheel=true;s.Step();
        s.Click(GlyphsCell);Check(s.out.glyphs&&Page(s.out)==MenuPage::Glyphs&&s.out.feedback==1,"the GLYPHS box opens the page");
        Check(WheelCell(.85f,.1f,-1,MenuPage::Glyphs,0)==GlyphsCell&&WheelCell(.1f,.1f,-1,MenuPage::Glyphs,0)!=QuickUseCell,"only the GLYPHS box on its page");
        s.Click(2);Check(s.out.editor==2&&s.out.glyphEdit==2&&s.out.editSlot==-1,"a shape opens the skill picker for it, not a button's");
        bool clean=true;
        for(int i=0;i<s.out.editCount;++i){const auto& k=s.out.editOptions[size_t(i)];clean=clean&&k.id!=357&&k.id!=96&&k.owner==-1&&k.id!=GlyphDrawId;}
        Check(clean&&s.out.editCategoryCounts[categories::All]==14,"no Interact, no targeted skill, no item charges, no Draw glyph");
        const int chosen=s.out.editOptions[1].id;
        auto r=s.Click(1).request;
        Check(r.action==Action::BindGlyph&&r.index==2&&r.skill==chosen&&r.skillOwner==-1,"choosing: the square's skill");
        s.in.reply={r.sequence,r.session,Result::Applied};s.Step();
        Check(s.out.editor==0&&s.out.glyphEdit<0&&s.out.glyphs,"applied: back on the GLYPHS page");
        s.Click(3);r=s.Click(8).request;
        Check(r.action==Action::BindGlyph&&r.index==3&&r.skill==-1,"CLEAR GLYPH: no skill");
        s.in.reply={r.sequence,r.session,Result::Refused};s.Step();
        Check(s.out.editor==2&&s.out.feedback==3,"refused: the picker stays, a buzz");
        s.Click(11);Check(s.out.editor==0&&s.out.glyphs&&s.out.request.action==Action::None,"BACK keeps the shape's skill");
        s.in.snapshot.bindingBank=16;s.Step();Check(s.out.glyphs,"a weapon swap leaves the page as it is");
        s.Click(4);s.in.snapshot.bindingBank=0;s.Step();Check(s.out.editor==2&&s.out.glyphEdit==4,"nor closes a shape's picker");
        s.Click(11);s.Click(9);Check(!s.out.glyphs&&!s.out.utility&&Page(s.out)==MenuPage::Spells,"SPELLS leaves it");
        s.Click(GlyphsCell);s.Click(GlyphsCell);Check(!s.out.glyphs,"the box goes back too");
        auto c=Context(s.in.snapshot,s.out,true);auto changed=s.out;changed.glyphSkills[0]=99;
        Check(c!=Context(s.in.snapshot,changed,true),"a shape's skill repaints the page");
    }
    std::puts("Skill categories: native classes, filtering, paging, exact assignments, cancellation and paint invalidation passed");
}
