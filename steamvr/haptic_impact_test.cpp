#include "haptic_impact.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace framevr::haptics;
static void Check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static bool Near(float a,float b){return std::fabs(a-b)<1e-4f;}
// Everything a hand is asked to play for one sequence, frame by frame (11 ms: 90 Hz).
struct Played {uint64_t at;Sample sample;Category category;bool preview;};
static std::vector<Played> Run(Sequencer& q,int hand,uint64_t from,uint64_t to,uint64_t frame=11) {
    std::vector<Played> out;
    for(uint64_t now=from;now<=to;now+=frame){Sequencer::Out o;if(q.Due(hand,now,o))out.push_back({now,o.sample,o.category,o.preview});}
    return out;
}
int main() {
    {   // What a monster is made of, from the flags the game's record holds.
        Check(MonsterSurface(0)==Surface::Flesh,"unread flags are flesh");
        Check(MonsterSurface(0x8000|0x2)==Surface::Flesh,"an ordinary killable melee monster is flesh");
        Check(MonsterSurface(0x8000|0x2000)==Surface::Flesh,"a demon is flesh: the record names no stone or metal");
        Check(MonsterSurface(0x8000|MonLowUndead)==Surface::Bone,"low undead (skeletons, zombies, mummies) are bone");
        Check(MonsterSurface(0x8000|MonHighUndead|MonFlying)==Surface::Spirit,"a flying high undead is a spirit");
        Check(MonsterSurface(0x8000|MonHighUndead)==Surface::Flesh,"a walking high undead (a vampire) is flesh");
        Check(MonsterSurface(0x8000|MonFlying)==Surface::Flesh,"a flying living thing (a bat) is flesh");
        Check(MonLowUndead==0x800&&MonHighUndead==0x1000&&((MonLowUndead|MonHighUndead|0x2000)>>8)==0x38,
              "the same undead and demon bits holy_bolt.h reads from byte 0x3d of the record");
    }
    {   // Flesh and a block are the taps they always were.
        const auto flesh=ImpactPattern(Surface::Flesh,0),left=ImpactPattern(Surface::Flesh,1),block=ImpactPattern(Surface::Block,1);
        const auto was=SampleFor(Category::Melee,false,0),shield=SampleFor(Category::Melee,false,1);
        Check(flesh.count==1&&flesh.steps[0].at==0&&Near(flesh.steps[0].sample.amplitude,was.amplitude)&&flesh.steps[0].sample.milliseconds==was.milliseconds&&
              flesh.steps[0].sample.frequency==was.frequency&&was.milliseconds==90,"flesh on the weapon hand: the one 90 ms thud");
        Check(left.count==1&&left.steps[0].sample.milliseconds==shield.milliseconds&&shield.milliseconds==45,"flesh on the left hand: its 45 ms tap");
        Check(block.count==1&&block.steps[0].sample.milliseconds==45&&Near(block.steps[0].sample.amplitude,.95f),"a block: one 45 ms tap");
        Check(ImpactPattern(Surface::Block,0).steps[0].sample.milliseconds==45,"a block is the shield hand's tap whichever hand is asked");
        Check(ImpactPattern(Surface(99),0).count==1&&ImpactPattern(Surface(-1),0).steps[0].sample.milliseconds==90,"an unknown surface is flesh");
    }
    {   // The others differ in rhythm, not only in frequency (several controllers ignore frequency).
        const auto bone=ImpactPattern(Surface::Bone),spirit=ImpactPattern(Surface::Spirit),box=ImpactPattern(Surface::Container),parry=ImpactPattern(Surface::Parry,1);
        Check(bone.count==3&&bone.steps[0].at==0&&bone.steps[1].at==45&&bone.steps[2].at==85,"bone: three clicks");
        for(int i=0;i<bone.count;++i)Check(bone.steps[i].sample.milliseconds<=25&&bone.steps[i].sample.frequency==240,"bone clicks are short and high");
        Check(bone.steps[0].sample.amplitude>bone.steps[1].sample.amplitude&&bone.steps[1].sample.amplitude>bone.steps[2].sample.amplitude,"each bone click weaker than the last");
        Check(spirit.count==3&&spirit.steps[1].sample.amplitude>spirit.steps[0].sample.amplitude&&spirit.steps[1].sample.amplitude>spirit.steps[2].sample.amplitude,"a spirit swells and fades");
        for(int i=0;i<spirit.count;++i)Check(spirit.steps[i].sample.amplitude<=.4f&&spirit.steps[i].sample.frequency==60,"a spirit is soft and low: nothing solid was struck");
        Check(spirit.steps[1].at==spirit.steps[0].at+spirit.steps[0].sample.milliseconds&&spirit.steps[2].at==spirit.steps[1].at+spirit.steps[1].sample.milliseconds,"a spirit's swell is unbroken");
        Check(box.count==2&&Near(box.steps[0].sample.amplitude,1)&&box.steps[0].sample.milliseconds==35&&box.steps[1].sample.milliseconds==110&&
              box.steps[1].sample.amplitude<.5f&&box.steps[1].sample.frequency<box.steps[0].sample.frequency,"a container: a crack, then a long low crumble");
        Check(parry.count==2&&parry.steps[1].at==50&&parry.steps[0].sample.frequency==250&&parry.steps[0].sample.milliseconds==20,"a parry: two sharp ringing taps");
        Check(bone.Length()==105&&spirit.Length()==150&&box.Length()==155&&parry.Length()==85&&ImpactPattern(Surface::Flesh).Length()==90,"every pattern ends within 155 ms");
        // No two surfaces share (count, first duration): a count and a length tell them apart by feel.
        for(int a=0;a<SurfaceCount;++a)for(int b=a+1;b<SurfaceCount;++b) {
            const auto x=ImpactPattern(Surface(a),a>=int(Surface::Block)?1:0),y=ImpactPattern(Surface(b),b>=int(Surface::Block)?1:0);
            Check(x.count!=y.count||x.steps[0].sample.milliseconds!=y.steps[0].sample.milliseconds||x.Length()!=y.Length(),"each surface has its own rhythm");
        }
        for(int s=0;s<SurfaceCount;++s) {
            const auto p=ImpactPattern(Surface(s));Check(p.category==Category::Melee&&p.count>=1&&p.count<=Sequence::Max,"every impact is a Melee cue");
            for(int i=0;i<p.count;++i)Check(p.steps[i].sample.amplitude>0&&p.steps[i].sample.amplitude<=1&&p.steps[i].sample.milliseconds>0&&(i==0||p.steps[i].at>p.steps[i-1].at),"steps in order, in range");
            Check(SurfaceName(Surface(s))[0]!='?',"every surface is named for the log");
        }
    }
    {   // Short melee taps: the rhythm and strengths stay, every step is the 10 ms / 180 Hz tap.
        for(int s=0;s<SurfaceCount;++s) {
            const auto full=ImpactPattern(Surface(s)),tap=ImpactPattern(Surface(s),0,true);
            Check(tap.count==full.count,"short taps keep the step count");
            for(int i=0;i<tap.count;++i)Check(tap.steps[i].at==full.steps[i].at&&Near(tap.steps[i].sample.amplitude,full.steps[i].sample.amplitude)&&
                tap.steps[i].sample.milliseconds==ShortTapMs&&tap.steps[i].sample.frequency==ShortTapHz,"short taps keep each onset and strength");
        }
        const auto was=SampleFor(Category::Melee,true);Check(was.milliseconds==ShortTapMs&&was.frequency==ShortTapHz,"the same tap Short melee taps always gave");
    }
    {   // The sequencer hands out each step once, when its time has come.
        Sequencer q;q.Play(0,ImpactPattern(Surface::Bone),1000);
        Check(q.Active(0)&&!q.Active(1),"one hand plays");
        const auto played=Run(q,0,1000,1300);
        Check(played.size()==3&&played[0].at==1000&&played[1].at>=1045&&played[1].at<1045+11&&played[2].at>=1085&&played[2].at<1085+11,"three clicks, each on the first frame at or after its time");
        Check(Near(played[0].sample.amplitude,.95f)&&Near(played[2].sample.amplitude,.35f)&&played[0].category==Category::Melee&&!played[0].preview,"with their own strengths, as Melee");
        Check(!q.Active(0),"then it is over");
        Sequencer::Out o;Check(!q.Due(0,5000,o)&&!q.Due(1,5000,o)&&!q.Due(-1,0,o)&&!q.Due(2,0,o),"nothing more, and no hand out of range");
    }
    {   // Hands are independent; a new hit replaces what that hand was playing.
        Sequencer q;q.Play(0,ImpactPattern(Surface::Container),100);q.Play(1,ImpactPattern(Surface::Parry,1),100);
        Sequencer::Out o;Check(q.Due(0,100,o)&&o.sample.milliseconds==35&&q.Due(1,100,o)&&o.sample.milliseconds==20,"both first steps at once");
        q.Play(0,ImpactPattern(Surface::Bone),120);
        Check(q.Due(0,120,o)&&o.sample.milliseconds==25&&o.step==0,"the new hit starts at once");
        const auto rest=Run(q,0,121,400);Check(rest.size()==2&&rest[0].sample.milliseconds==20,"and the old crumble never plays");
        Check(q.Due(1,150,o)&&o.sample.milliseconds==35&&o.step==1,"the other hand was not disturbed");
        q.Play(0,ImpactPattern(Surface::Bone),500);q.Clear();Check(!q.Active(0)&&!q.Due(0,500,o),"Clear drops everything (focus loss, a menu)");
    }
    {   // A stall never plays a stale tail out of rhythm, and never two steps in one frame.
        Sequencer q;q.Play(0,ImpactPattern(Surface::Bone),1000);Sequencer::Out o;
        Check(q.Due(0,1000,o),"first click");
        Check(!q.Due(0,1000+85+Sequencer::LateMs+50,o)&&!q.Active(0),"both later clicks are too late after a long stall: dropped");
        q.Play(0,ImpactPattern(Surface::Bone),2000);Check(q.Due(0,2000,o),"first click");
        Check(q.Due(0,2095,o)&&o.step==1&&q.Due(0,2095,o)&&o.step==2&&!q.Due(0,2095,o),"a short hitch still plays what is within 60 ms, one per call");
    }
    {   // A harder swing scales every step; a preview carries its unsaved strengths.
        Sequencer q;q.Play(0,ImpactPattern(Surface::Bone),0,.5f);Sequencer::Out o;
        Check(q.Due(0,0,o)&&Near(o.sample.amplitude,.475f),"half strength");
        q.Play(0,ImpactPattern(Surface::Bone),0,5);Check(q.Due(0,0,o)&&Near(o.sample.amplitude,1),"never above full");
        q.Play(0,ImpactPattern(Surface::Bone),0,NAN);Check(q.Due(0,0,o)&&Near(o.sample.amplitude,.95f),"a bad scale is no scale");
        q.Play(0,ImpactPattern(Surface::Bone),0,0);Check(q.Due(0,0,o)&&o.sample.amplitude==0,"zero is silent (the pulse path refuses it)");
        Strengths unsaved;unsaved.master=40;q.Play(1,BowSnap(1,1),0,1,&unsaved);
        Check(q.Due(1,0,o)&&o.preview&&o.levels.master==40&&o.category==Category::BowRelease,"Feel it plays with the strengths on screen");
        Sequence none;q.Play(0,none,0);Check(!q.Active(0),"an empty sequence plays nothing");
        Sequence bad;bad.count=9;q.Play(0,bad,0);Check(!q.Active(0),"nor a malformed one");
        q.Play(7,ImpactPattern(Surface::Bone),0);Check(!q.Active(0)&&!q.Active(7),"nor a hand out of range");
    }
    {   // The bow: tension ramps with the draw, in strength, length, pitch and tick spacing.
        float lastRight=0,lastLeft=0;int lastMs=0;float lastHz=0,lastStep=1;
        for(int i=0;i<=20;++i) {
            const float draw=i/20.f;const auto pull=BowPull(draw);
            Check(pull[0].amplitude>lastRight&&pull[1].amplitude>lastLeft&&pull[0].milliseconds>=lastMs&&pull[0].frequency>lastHz&&BowTickStep(draw)<lastStep,"every part of the pull rises with the draw");
            Check(pull[0].amplitude>pull[1].amplitude,"the string hand feels more than the bow hand");
            lastRight=pull[0].amplitude;lastLeft=pull[1].amplitude;lastMs=pull[0].milliseconds;lastHz=pull[0].frequency;lastStep=BowTickStep(draw);
        }
        const auto slack=BowPull(0),half=BowPull(.5f),full=BowPull(1);
        Check(Near(slack[0].amplitude,.18f)&&slack[0].milliseconds==20&&slack[0].frequency==70&&Near(slack[1].amplitude,.08f)&&slack[1].milliseconds==18,"a slack string: a light 20 ms tick");
        Check(Near(full[0].amplitude,.75f)&&full[0].milliseconds==40&&full[0].frequency==200&&Near(full[1].amplitude,.40f)&&full[1].milliseconds==30,"full draw: a firm 40 ms tick");
        Check(full[0].amplitude-half[0].amplitude>half[0].amplitude-slack[0].amplitude,"tension rises faster near full draw");
        Check(Near(BowTickStep(0),.022f)&&Near(BowTickStep(1),.012f),"ticks come closer together as it tightens");
        Check(Near(BowPull(NAN)[0].amplitude,.18f)&&Near(BowPull(7)[0].amplitude,.75f)&&Near(BowPull(-3)[0].amplitude,.18f),"bad draws clamp");
        const auto preview=Preview(Category::BowDraw,0);Check(Near(preview[0].amplitude,half[0].amplitude)&&preview[1].milliseconds>0,"Feel it plays the half-draw tick on both hands");
    }
    {   // Held still: nothing below half draw, then a faint tremor in the string hand, quicker toward full draw.
        Check(BowHold(0).milliseconds==0&&BowHold(.49f).milliseconds==0,"no tremor below half draw");
        Check(BowHold(.5f).milliseconds==15&&Near(BowHold(.5f).amplitude,.05f)&&Near(BowHold(1).amplitude,.15f),"a faint tremor from half draw to full");
        Check(BowHold(1).amplitude<BowPull(.1f)[0].amplitude,"always fainter than any pull tick: it is strain, not a buzz");
        Check(std::fabs(BowHoldInterval(.5f)-.26)<1e-6&&std::fabs(BowHoldInterval(1)-.14)<1e-6&&BowHoldInterval(0)==BowHoldInterval(.5f),"every 260 ms at half draw, every 140 ms at full");
    }
    {   // The release: a crack in both hands by the draw, then the string settles in the bow hand.
        const auto right=BowSnap(1,0),left=BowSnap(1,1),weakRight=BowSnap(0,0),weakLeft=BowSnap(0,1);
        Check(right.category==Category::BowRelease&&left.category==Category::BowRelease,"scaled by Bow and throw release");
        Check(right.count==1&&Near(right.steps[0].sample.amplitude,.95f)&&right.steps[0].sample.milliseconds==20&&right.steps[0].sample.frequency==220,"string hand: one sharp crack");
        Check(left.count==2&&Near(left.steps[0].sample.amplitude,1)&&left.steps[0].sample.milliseconds==25&&left.steps[1].at==40&&
              Near(left.steps[1].sample.amplitude,.5f)&&left.steps[1].sample.milliseconds==70&&left.steps[1].sample.frequency==80,"bow hand: the crack, then 70 ms of the string settling, lower");
        Check(Near(weakRight.steps[0].sample.amplitude,.45f)&&Near(weakLeft.steps[0].sample.amplitude,.5f)&&Near(weakLeft.steps[1].sample.amplitude,.2f),"a barely drawn bow snaps half as hard");
        Check(right.steps[0].sample.milliseconds<Release(true)[0].milliseconds&&right.steps[0].sample.amplitude>Release(true)[0].amplitude,"sharper and shorter than the old 45 ms release tap");
        Check(BowSnap(1,2).count==0&&BowSnap(1,-1).count==0&&Near(BowSnap(NAN,0).steps[0].sample.amplitude,.45f),"no other hand; a bad draw is slack");
        Check(Release(false)[0].milliseconds==45&&Release(false)[1].milliseconds==0,"a javelin throw keeps its one right-hand tap");
    }
    std::puts("Haptic impacts: monster surfaces, each surface's rhythm, short taps, the sequencer, bow tension, hold and snap passed");
    return 0;
}
