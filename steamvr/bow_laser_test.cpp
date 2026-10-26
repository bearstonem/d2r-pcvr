#include "bow_laser.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;
using namespace framevr::bowlaser;
static void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b){return std::fabs(a-b)<1e-4f;}
int main() {
    const Vec forward{0,0,-1};
    Check(Shown(true,forward,forward)&&!Shown(false,forward,forward),"shown only at full draw");
    Check(!Shown(true,forward,{0,0,1}),"a draw that would not shoot shows no line");
    Check(!Shown(true,{NAN,0,0},forward),"an invalid direction shows no line");
    {   // Level shot: the floor is never met, the line runs to its full length.
        const auto p=Trace({0,1.5f,0},forward,true,0);
        Check(Near(p.start.z,-bowflight::ArrowLength)&&Near(p.start.y,1.5f),"the line starts at the arrow's tip");
        Check(Near(p.length,MaxLength)&&!p.floor&&Near(p.end.z,p.start.z-MaxLength),"a level shot ends at the maximum length");
    }
    {   // Aimed down 45 degrees: the flight's own floor stop, with its clearance.
        const float s=std::sqrt(.5f);const Vec down{0,-s,-s};
        const auto p=Trace({0,1.5f,0},down,true,0);
        const float above=1.5f-s*bowflight::ArrowLength-bowflight::FloorClearance;
        Check(p.floor&&Near(p.length,above/s)&&Near(p.end.y,bowflight::FloorClearance),"a downward shot ends on the launch floor");
        Check(!Trace({0,1.5f,0},down,false,0).floor,"without a known floor the line runs to its maximum");
        Check(Near(Trace({0,11.5f,0},down,true,0).length,MaxLength),"a floor beyond the maximum is not reached");
        Check(Trace({0,.3f,0},down,true,0).length==0,"a tip already at the floor shows no line");
    }
    Check(Trace({0,1.5f,0},{0,0,-2},true,0).length==0,"a non-unit direction shows no line");
    Check(Near(Trace({0,1.5f,0},{0,1,0},true,0).length,MaxLength),"an upward shot never meets the floor");
    {   // The atlas in the chosen hue: faint beams, a brighter dot of the same color.
        const auto gold=Paint(45),blue=Paint(240);
        const auto at=[](const std::vector<uint8_t>& p,int x,int y){return p.data()+(y*AtlasWidth+x)*4;};
        Check(gold.size()==size_t(AtlasWidth*AtlasHeight*4),"one RGBA atlas");
        Check(at(gold,10,5)[0]==255&&at(gold,10,5)[1]>180&&at(gold,10,5)[2]<80,"45 degrees is the gold beam");
        Check(at(blue,10,5)[2]==255&&at(blue,10,5)[0]<80,"another hue tints the whole atlas");
        Check(at(gold,10,SteadyY+5)[3]==BeamAlpha&&BeamAlpha<128,"the steady beam stays faint");
        Check(at(gold,10,FadeY)[3]==0&&at(gold,10,FadeY+FadeHeight-1)[3]==BeamAlpha,"the fading beam is clear at its far (top) end");
        Check(at(gold,32,DotY+32)[3]==DotAlpha&&DotAlpha>BeamAlpha&&at(gold,0,DotY)[3]==0,"the end dot is brighter, round and edged with clear texels");
        Check(at(gold,32,DotY+32)[1]==at(gold,10,5)[1],"the dot has the beam's color");
    }
    std::puts("Bow laser: draw gate, arrow-tip start, floor stop, maximum length and colored atlas passed");
}
