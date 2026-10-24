// Direct2D (software WIC target) + DirectWrite drawing layer for VR Settings.
// The embedded fonts come from vr_settings_assets.rc and are loaded through
// DirectWrite's in-memory font file loader; nothing is installed system-wide.
// The look is the redesign's Horadric theme; the icons are kept as path
// geometries (assets/menu/icons-v2.inc).
#include "vr_settings_draw.h"
#include <windows.h>
#include <objbase.h>
#include <d2d1.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <unordered_map>
#include <utility>
namespace framevr::settings::draw {
namespace {
template<class T> struct Com {
    T* p=nullptr;
    Com()=default;
    Com(const Com&)=delete;
    Com& operator=(const Com&)=delete;
    Com(Com&& other) noexcept:p(other.p){other.p=nullptr;}
    Com& operator=(Com&& other) noexcept{if(this!=&other){Reset();p=other.p;other.p=nullptr;}return *this;}
    ~Com(){Reset();}
    void Reset(){if(p){p->Release();p=nullptr;}}
    T** Put(){Reset();return &p;}
    T* operator->() const{return p;}
    operator T*() const{return p;}
};
D2D1_COLOR_F Hex(uint32_t rgb,float a=1.f){
    return D2D1::ColorF(float((rgb>>16)&255)/255.f,float((rgb>>8)&255)/255.f,float(rgb&255)/255.f,a);
}
// Horadric palette.
constexpr uint32_t FrameA=0x4A3820,FrameB=0xA07D47,FrameC=0x5A4426,FrameHi=0xE8C77E,Ink=0x070504;
constexpr uint32_t Crimson1=0x9B2A1E,Crimson2=0x5E120C,Gold=0xD9B873,Bronze=0x8D6C3F;
// Material colors that are not theme tokens.
constexpr uint32_t BevelTop=0x3A2E20,BevelLeft=0x2A2119,LockedFill=0x110E0B,LockedTop=0x24201B,LockedLeft=0x1E1A16;
constexpr uint32_t LockedSelFill=0x221C17,LockedRim=0x6B6458,LockedGlint=0x8A8274,LockedSelText=0xBDB5A6;
constexpr uint32_t BgTop=0x211C16,BgBot=0x14110E,TabIcon=0x9A8F7A,LinkText=0x9A8F7A,LinkLine=0x3A3022;
constexpr uint32_t BarBack=0x1E1913,FillA=0xA07D47,FillB=0x8D6C3F,FillC=0x6E5330,TongueA=0x241C15;
constexpr uint32_t PlateA=0xD9B873,PlateB=0xB8925A,PlateC=0xA07D47,PlateRim=0xE8C77E,PlateHiTop=0xF2DCA0,PlateHiLeft=0xE2C283;
constexpr uint32_t PlateText=0x140D06,DangerRim=0x6A2018,SocketRing=0x54452F,SocketInner=0x2A221A;
constexpr int StoneSize=256;
constexpr int TileSize=512;
// Seeded, tileable value noise: a dark stone grain that repeats every 256 px.
std::vector<uint8_t> StoneTile(uint32_t seed){
    const auto hash=[seed](int x,int y){
        uint32_t h=uint32_t(x)*374761393u+uint32_t(y)*668265263u+seed*2246822519u;
        h=(h^(h>>13))*1274126177u;return float((h^(h>>16))&0xffff)/65535.f;
    };
    const auto noise=[&](float x,float y,int period){
        const int x0=int(std::floor(x)),y0=int(std::floor(y));const float fx=x-float(x0),fy=y-float(y0);
        const float sx=fx*fx*(3-2*fx),sy=fy*fy*(3-2*fy);
        const auto at=[&](int ix,int iy){return hash(((ix%period)+period)%period,((iy%period)+period)%period+period*97);};
        const float a=at(x0,y0)+(at(x0+1,y0)-at(x0,y0))*sx,b=at(x0,y0+1)+(at(x0+1,y0+1)-at(x0,y0+1))*sx;
        return a+(b-a)*sy;
    };
    std::vector<uint8_t> bgra(StoneSize*StoneSize*4);
    for(int y=0;y<StoneSize;++y)for(int x=0;x<StoneSize;++x){
        float n=0,weight=0,amplitude=1;
        for(int cells=4;cells<=64;cells*=2){
            n+=noise(float(x)*cells/StoneSize,float(y)*cells/StoneSize,cells)*amplitude;weight+=amplitude;amplitude*=.55f;
        }
        n/=weight;
        const float speck=hash(x+1013,y+7919)>.985f?.18f:0.f;
        const float shade=std::clamp(.62f+(n-.5f)*1.1f-speck,0.f,1.4f);
        uint8_t* p=bgra.data()+(y*StoneSize+x)*4;
        p[0]=uint8_t(std::min(255.f,22*shade));p[1]=uint8_t(std::min(255.f,30*shade));p[2]=uint8_t(std::min(255.f,38*shade));p[3]=255;
    }
    return bgra;
}
// The redesign's stone: a multiply layer as black with per-pixel alpha (so it
// darkens like a multiply blend): a fine grain (feTurbulence
// .55) plus stretched dark veins (the 'c' filter), tileable every 512 px.
std::vector<uint8_t> StoneShade(uint32_t seed,float opacity){
    const auto hash=[seed](int x,int y,int salt){
        uint32_t h=uint32_t(x)*374761393u+uint32_t(y)*668265263u+uint32_t(salt)*2654435761u+seed*2246822519u;
        h=(h^(h>>13))*1274126177u;return float((h^(h>>16))&0xffff)/65535.f;
    };
    // value noise on a periodic lattice of cx x cy cells over the tile
    const auto noise=[&](float x,float y,int cx,int cy,int salt){
        const float gx=x*cx/TileSize,gy=y*cy/TileSize;
        const int x0=int(std::floor(gx)),y0=int(std::floor(gy));const float fx=gx-float(x0),fy=gy-float(y0);
        const float sx=fx*fx*(3-2*fx),sy=fy*fy*(3-2*fy);
        const auto at=[&](int ix,int iy){return hash(((ix%cx)+cx)%cx,((iy%cy)+cy)%cy,salt);};
        const float a=at(x0,y0)+(at(x0+1,y0)-at(x0,y0))*sx,b=at(x0,y0+1)+(at(x0+1,y0+1)-at(x0,y0+1))*sx;
        return a+(b-a)*sy;
    };
    std::vector<uint8_t> bgra(size_t(TileSize)*TileSize*4);
    for(int y=0;y<TileSize;++y)for(int x=0;x<TileSize;++x){
        // grain: 4 octaves from a ~2 px lattice (very fine) plus a soft 32 px mottle
        float g=0,w=0,a=1;
        for(int cells=256;cells>=32;cells/=2){g+=noise(float(x),float(y),cells,cells,cells)*a;w+=a;a*=.6f;}
        g/=w;
        const float mottle=noise(float(x),float(y),16,16,7);
        // veins: turbulence (sum of |noise-.5|) on a lattice stretched 2.5:1 sideways
        float t=0,ta=1;
        for(int o=0;o<2;++o){t+=std::fabs(noise(float(x),float(y),6<<o,16<<o,100+o)-.5f)*ta;ta*=.5f;}
        const float vein=std::clamp(4.f-9.f*(t*1.6f+.18f),0.f,1.f)*.5f;
        const float dark=std::clamp(opacity*(1.f-(.35f+.5f*g+.15f*mottle))+opacity*vein,0.f,1.f);
        uint8_t* p=bgra.data()+(size_t(y)*TileSize+x)*4;
        p[0]=p[1]=p[2]=0;p[3]=uint8_t(dark*255.f+.5f);
    }
    return bgra;
}
bool Resource(const wchar_t* name,const void*& data,UINT32& size){
    HMODULE module=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Resource),&module);
    const HRSRC found=FindResourceW(module,name,MAKEINTRESOURCEW(10));
    size=found?SizeofResource(module,found):0;
    data=found?LockResource(LoadResource(module,found)):nullptr;
    return data&&size;
}
std::wstring Wide(std::string_view utf8){
    if(utf8.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,utf8.data(),int(utf8.size()),nullptr,0);
    std::wstring w(size_t(std::max(n,0)),L'\0');
    if(n>0)MultiByteToWideChar(CP_UTF8,0,utf8.data(),int(utf8.size()),w.data(),n);
    return w;
}
// ---- icons: path data (generated table) ----
struct IconLayer { char kind; float width; int transform; const char* d; };
struct IconDef { const char* name; const IconLayer* layers; int count; };
#include "assets/menu/icons-v2.inc"
// Icon enum order -> icon table key.
constexpr const char* IconKeys[]={"",
    "presets","body","combat","aim","targets","shield","wrist","interface","survival","world","haptics","performance","controls","notes",
    "comfort","classic","fullvr","fast",
    "plus","minus","chevron","chevronLeft","chevronUp","chevronDown","check","close","reset","undo","default","feel",
    "padlock","warning","info","changed","star","starOutline","search","soundOn","soundOff"};
static_assert(sizeof IconKeys/sizeof IconKeys[0]==size_t(Icon::Count),"one icons-v2 key per Icon");
// Ornaments (original art): the cartouche in the top frame
// (340 x 22) and the band's sigil (56 x 56).
constexpr const char* CartouchePath="M170 4.5L176.5 15.5H163.5Z M166.5 11.5Q170 8.5 173.5 11.5Q170 14.5 166.5 11.5Z M170 0.5V2 M170 20V21.5 M159.5 11H158 M180.5 11H182 "
    "M24 5H32L28 17 M40 17V5L46 11L52 5V17 M60 5L66 17L72 5 M60 11H72 M80 5H90M85 5V17M80 17H90 M98 17L104 5L110 17M101 12H107 M118 5V17H128 M118 11H125 "
    "M136 5L146 17M146 5L136 17 M194 5L204 17M204 5L194 17 M212 5V17H222 M212 11H219 M230 17L236 5L242 17M233 12H239 M250 5H260M255 5V17M250 17H260 "
    "M268 5L274 17L280 5 M268 11H280 M288 17V5L294 11L300 5V17 M308 5H316L312 17 M161.5 11a8.5 8.5 0 1 0 17 0a8.5 8.5 0 1 0 -17 0z";
constexpr const char* SigilOuter="M3 28a25 25 0 1 0 50 0a25 25 0 1 0 -50 0z M28 9.5L44 37.5H12Z";
constexpr const char* SigilThin="M8.5 28a19.5 19.5 0 1 0 39 0a19.5 19.5 0 1 0 -39 0z";
constexpr const char* SigilMid="M19.5 29Q28 21.5 36.5 29Q28 36.5 19.5 29Z M28 1V5 M28 51V55 M1 28H5 M51 28H55 M8.9 8.9L11.8 11.8 M47.1 8.9L44.2 11.8 M8.9 47.1L11.8 44.2 M47.1 47.1L44.2 44.2";

// A small SVG path-data reader (M L H V C S Q T A Z, absolute and relative)
// that feeds a Direct2D geometry sink.
class SvgPath {
public:
    SvgPath(const char* d,ID2D1GeometrySink* sink,bool filled):p(d),sink(sink),filled(filled){}
    void Run(){
        char cmd=0;
        for(;;){
            Skip();
            if(!*p)break;
            if(std::isalpha(static_cast<unsigned char>(*p)))cmd=*p++;
            else if(cmd=='M')cmd='L';else if(cmd=='m')cmd='l';
            if(!Step(cmd))break;
        }
        if(open)sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }
private:
    const char* p;ID2D1GeometrySink* sink;bool filled;
    bool open=false;float cx=0,cy=0,sx=0,sy=0,qx=0,qy=0;char last=0;
    void Skip(){while(*p&&(std::isspace(static_cast<unsigned char>(*p))||*p==','))++p;}
    bool Num(float& out){
        Skip();const char* s=p;
        if(*p=='+'||*p=='-')++p;
        bool digits=false,dot=false;
        while(std::isdigit(static_cast<unsigned char>(*p))||(*p=='.'&&!dot)){if(*p=='.')dot=true;else digits=true;++p;}
        if(digits&&(*p=='e'||*p=='E')){const char* e=p+1;if(*e=='+'||*e=='-')++e;if(std::isdigit(static_cast<unsigned char>(*e))){p=e;while(std::isdigit(static_cast<unsigned char>(*p)))++p;}}
        if(!digits){p=s;return false;}
        out=float(std::strtod(std::string(s,p).c_str(),nullptr));return true;
    }
    bool Flag(bool& out){Skip();if(*p=='0'||*p=='1'){out=*p++=='1';return true;}return false;}
    void Ensure(){if(!open){sink->BeginFigure({cx,cy},filled?D2D1_FIGURE_BEGIN_FILLED:D2D1_FIGURE_BEGIN_HOLLOW);open=true;sx=cx;sy=cy;}}
    bool Step(char cmd){
        const bool rel=std::islower(static_cast<unsigned char>(cmd))!=0;const float ox=rel?cx:0,oy=rel?cy:0;
        float a[7];
        const auto read=[&](int n){for(int i=0;i<n;++i)if(!Num(a[i]))return false;return true;};
        switch(std::toupper(static_cast<unsigned char>(cmd))){
        case 'M':
            if(!read(2))return false;
            if(open)sink->EndFigure(D2D1_FIGURE_END_OPEN);
            cx=ox+a[0];cy=oy+a[1];sink->BeginFigure({cx,cy},filled?D2D1_FIGURE_BEGIN_FILLED:D2D1_FIGURE_BEGIN_HOLLOW);open=true;sx=cx;sy=cy;break;
        case 'L':if(!read(2))return false;Ensure();cx=ox+a[0];cy=oy+a[1];sink->AddLine({cx,cy});break;
        case 'H':if(!read(1))return false;Ensure();cx=ox+a[0];sink->AddLine({cx,cy});break;
        case 'V':if(!read(1))return false;Ensure();cy=(rel?cy:0)+a[0];sink->AddLine({cx,cy});break;
        case 'C':
            if(!read(6))return false;Ensure();
            sink->AddBezier(D2D1::BezierSegment({ox+a[0],oy+a[1]},{ox+a[2],oy+a[3]},{ox+a[4],oy+a[5]}));
            qx=ox+a[2];qy=oy+a[3];cx=ox+a[4];cy=oy+a[5];break;
        case 'S':{
            if(!read(4))return false;Ensure();
            const bool smooth=last=='C'||last=='S';const float x1=smooth?2*cx-qx:cx,y1=smooth?2*cy-qy:cy;
            sink->AddBezier(D2D1::BezierSegment({x1,y1},{ox+a[0],oy+a[1]},{ox+a[2],oy+a[3]}));
            qx=ox+a[0];qy=oy+a[1];cx=ox+a[2];cy=oy+a[3];break;}
        case 'Q':
            if(!read(4))return false;Ensure();
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment({ox+a[0],oy+a[1]},{ox+a[2],oy+a[3]}));
            qx=ox+a[0];qy=oy+a[1];cx=ox+a[2];cy=oy+a[3];break;
        case 'T':{
            if(!read(2))return false;Ensure();
            const bool smooth=last=='Q'||last=='T';qx=smooth?2*cx-qx:cx;qy=smooth?2*cy-qy:cy;
            cx=ox+a[0];cy=oy+a[1];sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment({qx,qy},{cx,cy}));break;}
        case 'A':{
            bool large=false,sweep=false;
            if(!Num(a[0])||!Num(a[1])||!Num(a[2])||!Flag(large)||!Flag(sweep)||!Num(a[3])||!Num(a[4]))return false;
            Ensure();const float x=ox+a[3],y=oy+a[4];
            if(x!=cx||y!=cy)sink->AddArc(D2D1::ArcSegment({x,y},{a[0],a[1]},a[2],
                sweep?D2D1_SWEEP_DIRECTION_CLOCKWISE:D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,large?D2D1_ARC_SIZE_LARGE:D2D1_ARC_SIZE_SMALL));
            cx=x;cy=y;break;}
        case 'Z':
            if(open){sink->EndFigure(D2D1_FIGURE_END_CLOSED);open=false;}
            cx=sx;cy=sy;break;
        default:return false;
        }
        last=char(std::toupper(static_cast<unsigned char>(cmd)));
        return true;
    }
};
// Collects a text layout's glyph outlines as geometries (layout space), so the
// page title can be filled with a gradient and stroked like -webkit-text-stroke.
class OutlineCollector final : public IDWriteTextRenderer {
public:
    explicit OutlineCollector(ID2D1Factory* factory):factory(factory){}
    std::vector<Com<ID2D1Geometry>> runs;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{
        if(id==__uuidof(IUnknown)||id==__uuidof(IDWritePixelSnapping)||id==__uuidof(IDWriteTextRenderer)){*out=this;return S_OK;}
        *out=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return 1;}    // lives on the stack
    ULONG STDMETHODCALLTYPE Release() override{return 1;}
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*,BOOL* disabled) override{*disabled=TRUE;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*,DWRITE_MATRIX* m) override{*m={1,0,0,1,0,0};return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*,FLOAT* p) override{*p=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*,FLOAT x,FLOAT y,DWRITE_MEASURING_MODE,const DWRITE_GLYPH_RUN* run,const DWRITE_GLYPH_RUN_DESCRIPTION*,IUnknown*) override{
        Com<ID2D1PathGeometry> path;Com<ID2D1GeometrySink> sink;
        if(FAILED(factory->CreatePathGeometry(path.Put()))||FAILED(path->Open(sink.Put())))return S_OK;
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        run->fontFace->GetGlyphRunOutline(run->fontEmSize,run->glyphIndices,run->glyphAdvances,run->glyphOffsets,run->glyphCount,
                                          run->isSideways,run->bidiLevel%2,sink);
        if(FAILED(sink->Close()))return S_OK;
        Com<ID2D1TransformedGeometry> placed;
        if(SUCCEEDED(factory->CreateTransformedGeometry(path,D2D1::Matrix3x2F::Translation(x,y),placed.Put()))){
            Com<ID2D1Geometry> g;placed->QueryInterface(IID_PPV_ARGS(g.Put()));runs.push_back(std::move(g));
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*,FLOAT,FLOAT,const DWRITE_UNDERLINE*,IUnknown*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*,FLOAT,FLOAT,const DWRITE_STRIKETHROUGH*,IUnknown*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*,FLOAT,FLOAT,IDWriteInlineObject*,BOOL,BOOL,IUnknown*) override{return S_OK;}
private:
    ID2D1Factory* factory;
};
// hsl(h, 85%, 58%) as the prototype's swatches.
uint32_t HueRgb(float hue){
    const float s=.85f,l=.58f,c=(1-std::fabs(2*l-1))*s,h=std::fmod(std::fmod(hue,360.f)+360.f,360.f)/60.f;
    const float x=c*(1-std::fabs(std::fmod(h,2.f)-1)),m=l-c/2;
    float r=0,g=0,b=0;
    if(h<1){r=c;g=x;}else if(h<2){r=x;g=c;}else if(h<3){g=c;b=x;}else if(h<4){g=x;b=c;}else if(h<5){r=x;b=c;}else{r=c;b=x;}
    const auto u=[](float v){return uint32_t(std::clamp(v,0.f,1.f)*255.f+.5f);};
    return (u(r+m)<<16)|(u(g+m)<<8)|u(b+m);
}
// Unpremultiply lookup: [alpha][channel].
const std::array<uint8_t,256*256>& Unpremultiply(){
    static const auto table=[]{
        std::array<uint8_t,256*256> t{};
        for(unsigned a=1;a<256;++a)for(unsigned c=0;c<256;++c)t[a*256+c]=uint8_t(std::min(255u,(c*255u+a/2)/a));
        return t;
    }();
    return table;
}
struct TextStyleSpec { const wchar_t* family; DWRITE_FONT_WEIGHT weight; float em; };
constexpr TextStyleSpec StyleSpecs[]={
    {L"Cinzel",DWRITE_FONT_WEIGHT_BOLD,.06f},{L"Cinzel",DWRITE_FONT_WEIGHT_BOLD,.10f},{L"Cinzel",DWRITE_FONT_WEIGHT_BOLD,.02f},
    {L"Alegreya Sans",DWRITE_FONT_WEIGHT_MEDIUM,0},{L"Alegreya Sans",DWRITE_FONT_WEIGHT_MEDIUM,0},{L"Alegreya Sans",DWRITE_FONT_WEIGHT_MEDIUM,0},
    {L"Alegreya Sans",DWRITE_FONT_WEIGHT_MEDIUM,0},{L"Alegreya Sans",DWRITE_FONT_WEIGHT_MEDIUM,0},{L"Alegreya Sans",DWRITE_FONT_WEIGHT_BOLD,0}};
static_assert(sizeof StyleSpecs/sizeof StyleSpecs[0]==size_t(TextStyle::Count),"one spec per text style");
D2D1_RECT_F R(RectF r){return {r.x,r.y,r.x+r.w,r.y+r.h};}
}
void PremultipliedBgraToStraightRgba(const uint8_t* bgra,size_t pixels,uint8_t* rgba){
    const auto& un=Unpremultiply();
    for(size_t i=0;i<pixels;++i){
        const uint8_t* s=bgra+i*4;uint8_t* d=rgba+i*4;const unsigned a=s[3];
        if(a==255){d[0]=s[2];d[1]=s[1];d[2]=s[0];d[3]=255;continue;}
        if(!a){d[0]=d[1]=d[2]=d[3]=0;continue;}
        const uint8_t* row=un.data()+a*256;
        d[0]=row[s[2]];d[1]=row[s[1]];d[2]=row[s[0]];d[3]=uint8_t(a);
    }
}
struct Painter::Impl {
    HRESULT com=E_FAIL;
    std::string error,info;
    Com<IDWriteFactory5> dwrite;
    Com<IDWriteInMemoryFontFileLoader> loader;
    Com<IDWriteFontCollection1> fonts;
    Com<IDWriteTypography> lining;
    Com<IDWriteTextFormat> styles[size_t(TextStyle::Count)];
    float ascent[size_t(TextStyle::Count)]{},descent[size_t(TextStyle::Count)]{};   // em fractions, for CSS-like line boxes
    Com<ID2D1Factory> d2d;
    Com<IWICImagingFactory> wic;
    Com<IWICBitmap> target;
    Com<ID2D1RenderTarget> rt;
    Com<ID2D1SolidColorBrush> solid;
    Com<ID2D1Bitmap> stone;
    Com<ID2D1BitmapBrush> stoneBrush;
    Com<ID2D1Layer> layer;
    Com<ID2D1StrokeStyle> roundJoin,roundCap,dashed,squareCap;
    Com<ID2D1LinearGradientBrush> crimson,bronzeFill,plate;
    Com<ID2D1Bitmap> background;
    struct IconGeometry { const IconLayer* layer; Com<ID2D1PathGeometry> geometry; };
    std::vector<IconGeometry> icons[size_t(Icon::Count)];
    Com<ID2D1PathGeometry> cartouche,sigilOuter,sigilThin,sigilMid;
    std::unordered_map<std::string,Com<IDWriteTextLayout>> layouts;
    std::string titleKey;std::vector<Com<ID2D1Geometry>> titleRuns;   // the page title's glyph outlines
    bool ok=false,drawing=false;
    bool Fail(const char* what,HRESULT hr){
        char line[160];std::snprintf(line,sizeof line,"%s failed (0x%08lx)",what,static_cast<unsigned long>(hr));error=line;return false;
    }
    bool LoadFonts(){
        HRESULT hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,__uuidof(IDWriteFactory5),reinterpret_cast<IUnknown**>(dwrite.Put()));
        if(FAILED(hr))return Fail("DWriteCreateFactory(IDWriteFactory5)",hr);
        if(FAILED(hr=dwrite->CreateInMemoryFontFileLoader(loader.Put())))return Fail("CreateInMemoryFontFileLoader",hr);
        if(FAILED(hr=dwrite->RegisterFontFileLoader(loader)))return Fail("RegisterFontFileLoader",hr);
        Com<IDWriteFontSetBuilder1> builder;
        if(FAILED(hr=dwrite->CreateFontSetBuilder(builder.Put())))return Fail("CreateFontSetBuilder",hr);
        for(const wchar_t* name:{L"D2RVR_FONT_CINZEL",L"D2RVR_FONT_ALEGREYA_SANS",L"D2RVR_FONT_ALEGREYA_SANS_BOLD"}){
            const void* data=nullptr;UINT32 size=0;
            if(!Resource(name,data,size)){error="embedded menu font missing";return false;}
            Com<IDWriteFontFile> file;   // a null owner makes the loader keep its own copy
            if(FAILED(hr=loader->CreateInMemoryFontFileReference(dwrite,data,size,nullptr,file.Put())))return Fail("CreateInMemoryFontFileReference",hr);
            if(FAILED(hr=builder->AddFontFile(file)))return Fail("AddFontFile",hr);
        }
        Com<IDWriteFontSet> set;
        if(FAILED(hr=builder->CreateFontSet(set.Put())))return Fail("CreateFontSet",hr);
        if(FAILED(hr=dwrite->CreateFontCollectionFromFontSet(set,fonts.Put())))return Fail("CreateFontCollectionFromFontSet",hr);
        // Each family must resolve to the weight the menu asks for (Cinzel is a
        // variable font; its 700 comes from the Bold named instance).
        for(const auto& [family,weight]:{std::pair{L"Cinzel",DWRITE_FONT_WEIGHT_BOLD},std::pair{L"Alegreya Sans",DWRITE_FONT_WEIGHT_MEDIUM},
                                         std::pair{L"Alegreya Sans",DWRITE_FONT_WEIGHT_BOLD}}){
            UINT32 index=0;BOOL exists=FALSE;Com<IDWriteFontFamily1> found;Com<IDWriteFont> font;
            if(FAILED(fonts->FindFamilyName(family,&index,&exists))||!exists){error="embedded font family not found";return false;}
            if(FAILED(fonts->GetFontFamily(index,found.Put()))||
               FAILED(found->GetFirstMatchingFont(weight,DWRITE_FONT_STRETCH_NORMAL,DWRITE_FONT_STYLE_NORMAL,font.Put()))){error="font match failed";return false;}
            char line[96];std::snprintf(line,sizeof line,"%s%ls %d (%u faces)",info.empty()?"":", ",family,int(font->GetWeight()),found->GetFontCount());info+=line;
            if(font->GetWeight()!=weight||font->GetSimulations()!=DWRITE_FONT_SIMULATIONS_NONE){error="embedded font weight mismatch: "+info;return false;}
        }
        if(FAILED(hr=dwrite->CreateTypography(lining.Put())))return Fail("CreateTypography",hr);
        if(FAILED(hr=lining->AddFontFeature({DWRITE_FONT_FEATURE_TAG_LINING_FIGURES,1})))return Fail("AddFontFeature(lnum)",hr);
        const auto format=[&](Com<IDWriteTextFormat>& out,const wchar_t* family,DWRITE_FONT_WEIGHT weight,float size){
            return SUCCEEDED(dwrite->CreateTextFormat(family,fonts,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",out.Put()));
        };
        for(size_t i=0;i<size_t(TextStyle::Count);++i)
        {
            if(!format(styles[i],StyleSpecs[i].family,StyleSpecs[i].weight,FontSize(TextStyle(i)))){error="CreateTextFormat failed";return false;}
            UINT32 index=0;BOOL exists=FALSE;Com<IDWriteFontFamily1> family;Com<IDWriteFont> font;
            if(SUCCEEDED(fonts->FindFamilyName(StyleSpecs[i].family,&index,&exists))&&exists&&SUCCEEDED(fonts->GetFontFamily(index,family.Put()))&&
               SUCCEEDED(family->GetFirstMatchingFont(StyleSpecs[i].weight,DWRITE_FONT_STRETCH_NORMAL,DWRITE_FONT_STYLE_NORMAL,font.Put()))){
                DWRITE_FONT_METRICS m{};font->GetMetrics(&m);
                ascent[i]=float(m.ascent)/float(m.designUnitsPerEm);descent[i]=float(m.descent)/float(m.designUnitsPerEm);
            }
        }
        return true;
    }
    Com<ID2D1PathGeometry> Svg(const char* d,bool filled,D2D1_FILL_MODE mode=D2D1_FILL_MODE_WINDING){
        Com<ID2D1PathGeometry> g;Com<ID2D1GeometrySink> sink;
        if(FAILED(d2d->CreatePathGeometry(g.Put()))||FAILED(g->Open(sink.Put()))){g.Reset();return g;}
        sink->SetFillMode(mode);SvgPath(d,sink,filled).Run();
        if(FAILED(sink->Close()))g.Reset();
        return g;
    }
    bool LoadIcons(){
        for(size_t i=1;i<size_t(Icon::Count);++i){
            const IconDef* def=nullptr;
            for(const auto& d:IconDefs)if(std::strcmp(d.name,IconKeys[i])==0)def=&d;
            if(!def){error=std::string("icon missing: ")+IconKeys[i];return false;}
            for(int l=0;l<def->count;++l){
                const IconLayer& layer=def->layers[l];
                const bool filled=layer.kind=='S'||layer.kind=='H'||layer.kind=='F';
                auto g=Svg(layer.d,filled,layer.kind=='H'?D2D1_FILL_MODE_ALTERNATE:D2D1_FILL_MODE_WINDING);
                if(!g){error=std::string("icon path failed: ")+IconKeys[i];return false;}
                icons[i].push_back({&layer,std::move(g)});
            }
        }
        cartouche=Svg(CartouchePath,false);sigilOuter=Svg(SigilOuter,false);sigilThin=Svg(SigilThin,false);sigilMid=Svg(SigilMid,false);
        if(!cartouche||!sigilOuter||!sigilThin||!sigilMid){error="ornament path failed";return false;}
        return true;
    }
    bool CreateTarget(){
        HRESULT hr=D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.Put());
        if(FAILED(hr))return Fail("D2D1CreateFactory",hr);
        if(FAILED(hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.Put()))))return Fail("WIC factory",hr);
        if(FAILED(hr=wic->CreateBitmap(CanvasWidth,CanvasHeight,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,target.Put())))return Fail("WIC bitmap",hr);
        const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96.f,96.f);
        if(FAILED(hr=d2d->CreateWicBitmapRenderTarget(target,props,rt.Put())))return Fail("CreateWicBitmapRenderTarget",hr);
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);   // no ClearType on a VR layer
        if(FAILED(hr=rt->CreateSolidColorBrush(Hex(0),solid.Put())))return Fail("brush",hr);
        const auto tile=StoneTile(0x5EEDu);
        if(FAILED(hr=rt->CreateBitmap(D2D1::SizeU(StoneSize,StoneSize),tile.data(),StoneSize*4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),stone.Put())))return Fail("stone bitmap",hr);
        if(FAILED(hr=rt->CreateBitmapBrush(stone,D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP,D2D1_EXTEND_MODE_WRAP,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR),stoneBrush.Put())))return Fail("stone brush",hr);
        if(FAILED(hr=rt->CreateLayer(layer.Put())))return Fail("layer",hr);
        const auto stroke=[&](Com<ID2D1StrokeStyle>& out,D2D1_CAP_STYLE cap,D2D1_LINE_JOIN join,D2D1_DASH_STYLE dash){
            return SUCCEEDED(d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(cap,cap,cap,join,10.f,dash,0.f),nullptr,0,out.Put()));
        };
        if(!stroke(roundJoin,D2D1_CAP_STYLE_FLAT,D2D1_LINE_JOIN_ROUND,D2D1_DASH_STYLE_SOLID)||!stroke(roundCap,D2D1_CAP_STYLE_ROUND,D2D1_LINE_JOIN_ROUND,D2D1_DASH_STYLE_SOLID)||
           !stroke(dashed,D2D1_CAP_STYLE_FLAT,D2D1_LINE_JOIN_MITER,D2D1_DASH_STYLE_DASH)||!stroke(squareCap,D2D1_CAP_STYLE_SQUARE,D2D1_LINE_JOIN_MITER,D2D1_DASH_STYLE_SOLID)){error="stroke style failed";return false;}
        crimson=Linear({0,0},{0,1},{{0,Hex(Rgb(Color::Crimson1))},{1,Hex(Rgb(Color::Crimson2))}});
        bronzeFill=Linear({0,0},{0,1},{{0,Hex(FillA)},{.5f,Hex(FillB)},{1,Hex(FillC)}});
        plate=Linear({0,0},{0,1},{{0,Hex(PlateA)},{.5f,Hex(PlateB)},{1,Hex(PlateC)}});
        if(!crimson||!bronzeFill||!plate){error="gradient brush failed";return false;}
        return true;
    }
    // ---- drawing helpers ----
    ID2D1SolidColorBrush* Solid(uint32_t rgb,float a=1.f){solid->SetColor(Hex(rgb,a));return solid;}
    ID2D1SolidColorBrush* Token(Color c,float a=1.f){return Solid(Rgb(c),a);}
    void Polygon(ID2D1GeometrySink* sink,std::initializer_list<D2D1_POINT_2F> points){
        auto it=points.begin();sink->BeginFigure(*it,D2D1_FIGURE_BEGIN_FILLED);
        for(++it;it!=points.end();++it)sink->AddLine(*it);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    void ChamferFigure(ID2D1GeometrySink* sink,D2D1_RECT_F r,float c){
        Polygon(sink,{{r.left+c,r.top},{r.right-c,r.top},{r.right,r.top+c},{r.right,r.bottom-c},
                      {r.right-c,r.bottom},{r.left+c,r.bottom},{r.left,r.bottom-c},{r.left,r.top+c}});
    }
    template<class Build> Com<ID2D1PathGeometry> Path(Build build,D2D1_FILL_MODE mode=D2D1_FILL_MODE_WINDING){
        Com<ID2D1PathGeometry> g;Com<ID2D1GeometrySink> sink;
        if(FAILED(d2d->CreatePathGeometry(g.Put()))||FAILED(g->Open(sink.Put())))return g;
        sink->SetFillMode(mode);build(sink.p);sink->Close();return g;
    }
    Com<ID2D1PathGeometry> Chamfer(D2D1_RECT_F r,float c){return Path([&](ID2D1GeometrySink* s){ChamferFigure(s,r,c);});}
    void FillPolygon(std::initializer_list<D2D1_POINT_2F> points,ID2D1Brush* brush){
        auto g=Path([&](ID2D1GeometrySink* s){Polygon(s,points);});if(g)rt->FillGeometry(g,brush);
    }
    Com<ID2D1LinearGradientBrush> Linear(D2D1_POINT_2F from,D2D1_POINT_2F to,std::initializer_list<D2D1_GRADIENT_STOP> stops){
        Com<ID2D1GradientStopCollection> collection;Com<ID2D1LinearGradientBrush> brush;
        if(SUCCEEDED(rt->CreateGradientStopCollection(stops.begin(),UINT32(stops.size()),collection.Put())))
            rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(from,to),collection,brush.Put());
        return brush;
    }
    Com<ID2D1RadialGradientBrush> Radial(D2D1_POINT_2F center,float rx,float ry,std::initializer_list<D2D1_GRADIENT_STOP> stops){
        Com<ID2D1GradientStopCollection> collection;Com<ID2D1RadialGradientBrush> brush;
        if(SUCCEEDED(rt->CreateGradientStopCollection(stops.begin(),UINT32(stops.size()),collection.Put())))
            rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(center,{0,0},rx,ry),collection,brush.Put());
        return brush;
    }
    ID2D1LinearGradientBrush* Vertical(ID2D1LinearGradientBrush* brush,float top,float bottom){
        brush->SetStartPoint({0,top});brush->SetEndPoint({0,bottom});return brush;
    }
    // ---- redesign text ----
    // CSS line boxes: the glyphs' ascent + descent centered in a line of
    // 1.1 em (wrapped) or 1.2 em (one line, centered in its box), as the
    // prototype's browser lays them out (DirectWrite's default adds the line gap).
    void LineBox(IDWriteTextLayout* layout,TextStyle style,float px,bool wrap){
        const float a=ascent[size_t(style)],d=descent[size_t(style)];
        if(a<=0)return;
        const float line=px*(wrap?1.1f:1.2f);
        layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,line,(line-(a+d)*px)/2+a*px);
    }
    // A cached layout: one per (text, style, box, wrap, align, size).
    IDWriteTextLayout* Layout(std::string_view utf8,TextStyle style,float w,float h,bool wrap,Align align,float size){
        const float px=size>0?size:FontSize(style);
        std::string key;key.reserve(utf8.size()+40);key.append(utf8);
        char tail[96];std::snprintf(tail,sizeof tail,"\x1f%d|%.1f|%.1f|%d|%d|%.2f",int(style),w,h,int(wrap),int(align),px);key+=tail;
        if(auto it=layouts.find(key);it!=layouts.end())return it->second;
        if(layouts.size()>1500)layouts.clear();
        const std::wstring wide=Wide(utf8);Com<IDWriteTextLayout> layout;
        if(FAILED(dwrite->CreateTextLayout(wide.c_str(),UINT32(wide.size()),styles[size_t(style)],std::max(w,1.f),std::max(h,1.f),layout.Put())))return nullptr;
        const DWRITE_TEXT_RANGE all{0,UINT32(wide.size())};
        layout->SetTypography(lining,all);
        if(size>0)layout->SetFontSize(px,all);
        layout->SetTextAlignment(align==Align::Center?DWRITE_TEXT_ALIGNMENT_CENTER:align==Align::Trailing?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetParagraphAlignment(wrap?DWRITE_PARAGRAPH_ALIGNMENT_NEAR:DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        layout->SetWordWrapping(wrap?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP);
        LineBox(layout,style,px,wrap);
        if(const float em=StyleSpecs[size_t(style)].em;em>0){
            Com<IDWriteTextLayout1> one;
            if(SUCCEEDED(layout->QueryInterface(IID_PPV_ARGS(one.Put()))))one->SetCharacterSpacing(0,px*em,0,all);
        }
        auto* raw=layout.p;layouts.emplace(std::move(key),std::move(layout));return raw;
    }
    static TextSize SizeOf(IDWriteTextLayout* layout,float maxWidth,float maxHeight,bool wrap,float px){
        TextSize t;t.size=px;if(!layout){t.fits=false;return t;}
        DWRITE_TEXT_METRICS m{};layout->GetMetrics(&m);
        t.width=m.widthIncludingTrailingWhitespace;t.height=m.height;t.lines=int(m.lineCount);
        t.width=m.width;
        t.fits=wrap?(maxHeight<=0||m.height<=maxHeight+.5f)&&m.widthIncludingTrailingWhitespace<=maxWidth+.5f:m.width<=maxWidth+.5f;
        return t;
    }
    // Draw a layout with the 2 px shadow (or a custom shadow color/offset).
    void DrawLayout(IDWriteTextLayout* layout,float x,float y,uint32_t rgb,float alpha=1.f,uint32_t shadow=0,float shadowAlpha=.75f,float shadowY=2){
        if(!layout)return;
        if(shadowAlpha>0)rt->DrawTextLayout({x,y+shadowY},layout,Solid(shadow,shadowAlpha*alpha),D2D1_DRAW_TEXT_OPTIONS_NONE);
        rt->DrawTextLayout({x,y},layout,Solid(rgb,alpha),D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
    TextSize DrawText(std::string_view utf8,TextStyle style,RectF box,uint32_t rgb,Align align,bool wrap,float size=0,float alpha=1.f){
        IDWriteTextLayout* layout=Layout(utf8,style,box.w,box.h,wrap,align,size);
        DrawLayout(layout,box.x,box.y,rgb,alpha);
        return SizeOf(layout,box.w,box.h,wrap,size>0?size:FontSize(style));
    }
    float Width(std::string_view utf8,TextStyle style,float size=0){
        IDWriteTextLayout* layout=Layout(utf8,style,4096,256,false,Align::Leading,size);
        DWRITE_TEXT_METRICS m{};if(layout)layout->GetMetrics(&m);return m.width;
    }
    // ---- icons ----
    void DrawIcon(Icon icon,RectF r,uint32_t rgb,float alpha=1.f){
        if(icon==Icon::None||size_t(icon)>=size_t(Icon::Count))return;
        D2D1_MATRIX_3X2_F old;rt->GetTransform(&old);
        const float s=std::min(r.w,r.h)/48.f;
        const auto place=D2D1::Matrix3x2F::Scale(s,s)*D2D1::Matrix3x2F::Translation(r.x+(r.w-48*s)/2,r.y+(r.h-48*s)/2)**D2D1::Matrix3x2F::ReinterpretBaseType(&old);
        const auto swords=[](float deg){
            return D2D1::Matrix3x2F::Translation(-24,-24)*D2D1::Matrix3x2F::Scale(1.1f,1.1f)*D2D1::Matrix3x2F::Translation(24,24)*D2D1::Matrix3x2F::Rotation(deg,{24,24});
        };
        for(const auto& g:icons[size_t(icon)]){
            const IconLayer& l=*g.layer;
            rt->SetTransform(l.transform==1?swords(45)*place:l.transform==2?swords(-45)*place:place);
            switch(l.kind){
            case 'S':case 'H':
                rt->DrawGeometry(g.geometry,Solid(Ink,alpha),2.f,roundJoin);rt->FillGeometry(g.geometry,Solid(rgb,alpha));break;
            case 'B':
                rt->DrawGeometry(g.geometry,Solid(Ink,alpha),l.width+2,roundJoin);rt->DrawGeometry(g.geometry,Solid(rgb,alpha),l.width,roundJoin);break;
            case 'D':rt->DrawGeometry(g.geometry,Solid(Ink,.55f*alpha),1.5f,roundCap);break;
            case 'F':rt->FillGeometry(g.geometry,Solid(Ink,.45f*alpha));break;
            default:break;
            }
        }
        rt->SetTransform(old);
    }
    // ---- sockets ----
    // The four 2 px bevel bands inside a chamfered rect (c: the chamfer).
    void Bevel(D2D1_RECT_F r,float c,uint32_t top,uint32_t left,uint32_t bottom,uint32_t right,float w=2){
        const float l=r.left,t=r.top,rr=r.right,b=r.bottom;
        FillPolygon({{rr,t+c},{rr,b-c},{rr-w,b-c+w},{rr-w,t+c-w}},Solid(right));
        FillPolygon({{l,t+c},{l+w,t+c-w},{l+w,b-c+w},{l,b-c}},Solid(left));
        FillPolygon({{l+c-w,b-w},{rr-c+w,b-w},{rr-c,b},{l+c,b}},Solid(bottom));
        FillPolygon({{l+c,t},{rr-c,t},{rr-c+w,t+w},{l+c-w,t+w}},Solid(top));
    }
    // The control plate: idle, chosen, locked, chosen+locked,
    // primary or danger, plus the pressed band. Rings and marks are separate.
    void Plate(D2D1_RECT_F r,bool chosen,bool off,bool pressed,draw::Plate kind=draw::Plate::Socket){
        constexpr float c=6;
        auto shape=Chamfer(r,c);if(!shape)return;
        const float cx=(r.left+r.right)/2,w=r.right-r.left;
        if(kind==draw::Plate::Primary&&!off){
            rt->FillGeometry(shape,Vertical(plate,r.top,r.bottom));
            Bevel(r,c,PlateHiTop,PlateHiLeft,FrameA,FrameC);
            auto rim=Chamfer({r.left+1.2f,r.top+1.2f,r.right-1.2f,r.bottom-1.2f},c-.5f);
            rt->DrawGeometry(rim,Solid(PlateRim,.8f),1.2f);
        }else if(chosen&&!off){
            rt->FillGeometry(shape,Vertical(crimson,r.top,r.bottom));
            rt->FillRectangle({r.left+3,r.bottom-5,r.right-3,r.bottom-3},Solid(0,.35f));
            auto rim=Chamfer({r.left+1,r.top+1,r.right-1,r.bottom-1},c-.4f);rt->DrawGeometry(rim,Token(Color::SelRim),2);
            rt->DrawRectangle({r.left+2.5f,r.top+2.5f,r.right-2.5f,r.bottom-2.5f},Solid(Ink),1);
            const float half=w*.22f;
            rt->FillRectangle({cx-half,r.top+6,cx+half,r.top+9},Token(Color::SelGlint));
            rt->FillRectangle({cx-half,r.top+9,cx+half,r.top+10},Solid(Ink));
        }else if(chosen&&off){
            rt->FillGeometry(shape,Solid(LockedSelFill));
            auto rim=Chamfer({r.left+1,r.top+1,r.right-1,r.bottom-1},c-.4f);rt->DrawGeometry(rim,Solid(LockedRim),2);
            rt->DrawRectangle({r.left+2.5f,r.top+2.5f,r.right-2.5f,r.bottom-2.5f},Solid(Ink),1);
            const float half=w*.22f;
            rt->FillRectangle({cx-half,r.top+6,cx+half,r.top+9},Solid(LockedGlint));
        }else if(off){
            rt->FillGeometry(shape,Solid(LockedFill));
            Bevel(r,c,LockedTop,LockedLeft,Ink,Ink);
        }else{
            rt->FillGeometry(shape,Token(Color::IdleFill));
            Bevel(r,c,BevelTop,BevelLeft,Ink,Ink);
            if(kind==draw::Plate::Danger){   // inset 0 0 0 2px #6A2018 over the bevel
                auto rim=Chamfer({r.left+1,r.top+1,r.right-1,r.bottom-1},c-.4f);rt->DrawGeometry(rim,Solid(DangerRim),2);
            }
        }
        if(pressed){   // inset 0 6px 0 rgba(0,0,0,.6), clipped by the chamfer
            FillPolygon({{r.left+c,r.top},{r.right-c,r.top},{r.right,r.top+c},{r.right,r.top+6},{r.left,r.top+6},{r.left,r.top+c}},Solid(0,.6f));
        }
    }
    void Ring(D2D1_RECT_F r,bool off){
        if(off)rt->DrawRectangle({r.left-2.5f,r.top-2.5f,r.right+2.5f,r.bottom+2.5f},Solid(LockedRim),3,dashed);
        else rt->DrawRectangle({r.left-2.5f,r.top-2.5f,r.right+2.5f,r.bottom+2.5f},Token(Color::RingHi),3);
    }
    void DefaultTriangle(float cx,float y){
        FillPolygon({{cx-6,y+1},{cx+6,y+1},{cx,y+8}},Solid(Ink));
        FillPolygon({{cx-6,y},{cx+6,y},{cx,y+7}},Token(Color::Bronze));
    }
    void Ember(D2D1_RECT_F r){DrawIcon(Icon::Changed,{r.right-25,r.top+3,20,20},Rgb(Color::Changed));}
    // The control's frame of states around a drawn body: returns the (pressed-shifted) rect.
    D2D1_RECT_F Shifted(RectF r,const ControlState& s){
        D2D1_RECT_F d=R(r);if(s.pressed&&!s.locked&&!s.disabled){d.top+=2;d.bottom+=2;}return d;
    }
    void Finish(D2D1_RECT_F d,RectF r,const ControlState& s){
        const bool off=s.locked||s.disabled;
        if(s.changed&&!off)Ember(d);
        if(s.isDefault)DefaultTriangle(r.x+r.w/2,r.Bottom());
        if(s.pointed)Ring(R(r),off);
    }
    // ---- gems ----
    void GemAt(float cx,float cy,float radius){
        const float k=radius/12.f;
        rt->FillEllipse(D2D1::Ellipse({cx,cy},16*k,16*k),Solid(FrameA));
        rt->FillEllipse(D2D1::Ellipse({cx,cy},14.5f*k,14.5f*k),Token(Color::BronzeHi));
        rt->FillEllipse(D2D1::Ellipse({cx,cy},13*k,13*k),Solid(0x2A0505));
        rt->FillEllipse(D2D1::Ellipse({cx,cy},12*k,12*k),Token(Color::Crimson1));
        rt->FillEllipse(D2D1::Ellipse({cx-2*k,cy-2*k},6.3f*k,6.3f*k),Token(Color::GemTop));
        rt->FillEllipse(D2D1::Ellipse({cx-4*k,cy-4*k},2.3f*k,2.3f*k),Solid(0xFFD0C0));
    }
    // The switch's socket: a dark disc and inner ring (Off) or the faceted gem (On).
    void SwitchGem(float x,float y,bool on,bool off){
        rt->FillEllipse(D2D1::Ellipse({x+20,y+20},18,18),Solid(Ink));
        rt->DrawEllipse(D2D1::Ellipse({x+20,y+20},18,18),Solid(off?0x3A342C:SocketRing),2);
        if(!on){rt->DrawEllipse(D2D1::Ellipse({x+20,y+20},12,12),Solid(SocketInner),2);return;}
        auto body=Path([&](ID2D1GeometrySink* s){Polygon(s,{{x+20,y+5},{x+34,y+16},{x+29,y+34},{x+11,y+34},{x+6,y+16}});});
        rt->FillGeometry(body,off?Solid(LockedRim):Token(Color::Crimson1));rt->DrawGeometry(body,Solid(Ink),2);
        FillPolygon({{x+20,y+5},{x+34,y+16},{x+20,y+19},{x+6,y+16}},off?Solid(LockedGlint):Token(Color::GemTop));
        rt->FillRectangle({x+14,y+10,x+18,y+14},Solid(off?0xBDB5A6:0xFFB3A0));
    }
    // ---- the cached panel ----
    void PaintBackground(){
        constexpr float W=float(CanvasWidth),H=float(CanvasHeight),B=FrameWidth;
        rt->Clear(D2D1::ColorF(0,0,0,0));
        // stone: base gradient, 30% multiply grain, vignette
        auto base=Linear({0,0},{0,H},{{0,Hex(BgTop)},{.45f,Hex(Rgb(Color::Stone))},{1,Hex(BgBot)}});
        rt->FillRectangle({0,0,W,H},base);
        Shade({0,0,W,H},.30f,0x5EEDu);
        auto vignette=Radial({W/2,H*.4f},W*.5f*1.4142f,H*.6f*1.4142f,{{0,D2D1::ColorF(0,0,0,0)},{.55f,D2D1::ColorF(0,0,0,0)},{1,D2D1::ColorF(0,0,0,.45f)}});
        rt->FillRectangle({0,0,W,H},vignette);
        // rail: its own color, 40% stone, the bronze seam on its right edge
        rt->FillRectangle({0,0,RailRight,H},Token(Color::Rail));
        Shade({0,0,RailRight,H},.40f,0xB0A7u);
        auto seam=Linear({RailRight-RailSeam,0},{RailRight,0},{{0,Hex(FrameC)},{.5f,Hex(Rgb(Color::Bronze))},{1,Hex(FrameA)}});
        rt->FillRectangle({RailRight-RailSeam,18,RailRight,H-18},seam);
        rt->FillRectangle({RailRight-RailSeam-1,18,RailRight-RailSeam,H-18},Solid(Ink));
        // cast frame: four 22 px gradient bands, highlight and ink edges
        const auto band=[&](D2D1_POINT_2F from,D2D1_POINT_2F to){return Linear(from,to,{{0,Hex(FrameA)},{.45f,Hex(FrameB)},{1,Hex(FrameC)}});};
        rt->FillRectangle({W-B,0,W,H},band({W,0},{W-B,0}));
        rt->FillRectangle({0,0,B,H},band({0,0},{B,0}));
        rt->FillRectangle({0,H-B,W,H},band({0,H},{0,H-B}));
        rt->FillRectangle({0,0,W,B},band({0,0},{0,B}));
        rt->FillRectangle({B,B,W-B,B+1},Solid(Ink));rt->FillRectangle({B,H-B-1,W-B,H-B},Solid(Ink));
        rt->FillRectangle({B,B,B+1,H-B},Solid(Ink));rt->FillRectangle({W-B-1,B,W-B,H-B},Solid(Ink));
        rt->FillRectangle({0,H-1,W,H},Solid(0));rt->FillRectangle({W-1,0,W,H},Solid(0));
        rt->FillRectangle({0,0,W,1},Solid(FrameHi));rt->FillRectangle({0,0,1,H},Solid(FrameHi));
        // the Horadric cartouche engraved in the top frame
        D2D1_MATRIX_3X2_F old;rt->GetTransform(&old);
        rt->SetTransform(D2D1::Matrix3x2F::Translation(W/2-170+.8f,.8f));
        rt->DrawGeometry(cartouche,Solid(FrameHi,.45f),1.4f,squareCap);
        rt->SetTransform(D2D1::Matrix3x2F::Translation(W/2-170,0));
        rt->DrawGeometry(cartouche,Solid(0x1E140A,.75f),1.6f,squareCap);
        rt->FillEllipse(D2D1::Ellipse({170,11.5f},1.3f,1.3f),Solid(0x1E140A,.8f));
        // corner brackets (88 px arms, 34 px boss, gem), mirrored per corner
        for(int corner=0;corner<4;++corner){
            const bool flipX=corner&1,flipY=corner&2;
            rt->SetTransform(D2D1::Matrix3x2F::Scale(flipX?-1.f:1.f,flipY?-1.f:1.f)*D2D1::Matrix3x2F::Translation(flipX?W:0,flipY?H:0));
            Bracket();
        }
        rt->SetTransform(old);
    }
    // One corner bracket in its own top-left frame.
    void Bracket(){
        constexpr std::pair<float,uint32_t> stops[]={{2,0xDDB56C},{6,0xB08A50},{15,0x8D6C3F},{21,0x4A3820},{24,Ink}};
        float from=0;
        for(const auto& [to,rgb]:stops){
            rt->FillRectangle({0,from,88,to},Solid(rgb));   // horizontal arm, bands down
            from=to;
        }
        from=0;
        for(const auto& [to,rgb]:stops){
            rt->FillRectangle({from,24,to,88},Solid(rgb));  // vertical arm below the horizontal one
            from=to;
        }
        rt->FillRectangle({0,86,24,88},Solid(Ink));rt->FillRectangle({86,0,88,24},Solid(Ink));
        // boss: 135deg hard-stop bands on 34 x 34 (x + y < k)
        const auto diagonal=[&](float k,uint32_t rgb){
            if(k<=34)FillPolygon({{0,0},{k,0},{0,k}},Solid(rgb));
            else FillPolygon({{0,0},{34,0},{34,k-34},{k-34,34},{0,34}},Solid(rgb));
        };
        rt->FillRectangle({0,0,34,34},Solid(Ink));
        diagonal(63.9f,0x4A3820);diagonal(39.4f,0x8D6C3F);diagonal(4.24f,0xDDB56C);
        GemAt(17,17,12);
        for(const auto& c:{D2D1_POINT_2F{76,11},D2D1_POINT_2F{11,76}}){
            rt->FillEllipse(D2D1::Ellipse(c,4.8f,4.8f),Solid(Ink));
            rt->FillEllipse(D2D1::Ellipse(c,3.7f,3.7f),Solid(0x8D6C3F));
            rt->FillEllipse(D2D1::Ellipse(c,1.7f,1.7f),Solid(0xF2D9A0));
        }
    }
    // A multiply stone layer (black, per-pixel alpha) tiled over r.
    struct ShadeTile { uint32_t seed; float opacity; Com<ID2D1Bitmap> bitmap; Com<ID2D1BitmapBrush> brush; };
    std::vector<ShadeTile> shades;   // built once per (seed, opacity)
    void Shade(D2D1_RECT_F r,float opacity,uint32_t seed){
        ShadeTile* found=nullptr;
        for(auto& s:shades)if(s.seed==seed&&s.opacity==opacity)found=&s;
        if(!found){
            const auto tile=StoneShade(seed,opacity);ShadeTile made{seed,opacity,{},{}};
            if(FAILED(rt->CreateBitmap(D2D1::SizeU(TileSize,TileSize),tile.data(),TileSize*4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),made.bitmap.Put())))return;
            if(FAILED(rt->CreateBitmapBrush(made.bitmap,D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP,D2D1_EXTEND_MODE_WRAP,
                D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),made.brush.Put())))return;
            shades.push_back(std::move(made));found=&shades.back();
        }
        rt->FillRectangle(r,found->brush);
    }
    bool BuildBackground(){
        rt->BeginDraw();rt->SetTransform(D2D1::Matrix3x2F::Identity());
        PaintBackground();
        HRESULT hr=rt->EndDraw();
        if(FAILED(hr))return Fail("EndDraw (panel)",hr);
        std::vector<uint8_t> pixels(size_t(CanvasWidth)*CanvasHeight*4);
        if(FAILED(hr=target->CopyPixels(nullptr,CanvasWidth*4,UINT(pixels.size()),pixels.data())))return Fail("CopyPixels (panel)",hr);
        if(FAILED(hr=rt->CreateBitmap(D2D1::SizeU(CanvasWidth,CanvasHeight),pixels.data(),CanvasWidth*4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),background.Put())))return Fail("panel bitmap",hr);
        return true;
    }
};
Painter::Painter():impl(std::make_unique<Impl>()){
    impl->com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    impl->ok=impl->LoadFonts()&&impl->CreateTarget()&&impl->LoadIcons();
}
Painter::~Painter(){
    // Release every COM object before this thread's CoUninitialize.
    auto& i=*impl;
    i.layouts.clear();i.shades.clear();i.titleRuns.clear();
    for(auto& list:i.icons)list.clear();
    i.cartouche.Reset();i.sigilOuter.Reset();i.sigilThin.Reset();i.sigilMid.Reset();
    i.background.Reset();i.crimson.Reset();i.bronzeFill.Reset();i.plate.Reset();
    i.roundJoin.Reset();i.roundCap.Reset();i.dashed.Reset();i.squareCap.Reset();
    i.layer.Reset();i.stoneBrush.Reset();i.stone.Reset();i.solid.Reset();i.rt.Reset();i.target.Reset();i.wic.Reset();i.d2d.Reset();
    for(auto& s:i.styles)s.Reset();
    i.lining.Reset();i.fonts.Reset();
    if(i.dwrite&&i.loader)i.dwrite->UnregisterFontFileLoader(i.loader);
    i.loader.Reset();i.dwrite.Reset();
    if(SUCCEEDED(i.com))CoUninitialize();
}
bool Painter::Ready() const{return impl->ok;}
const std::string& Painter::Error() const{return impl->error;}
const std::string& Painter::FontInfo() const{return impl->info;}
// ---- redesign API ----
bool Painter::Begin(){
    auto& i=*impl;
    if(!i.ok||i.drawing)return false;
    i.error.clear();
    if(!i.background&&!i.BuildBackground())return false;
    i.rt->BeginDraw();i.rt->SetTransform(D2D1::Matrix3x2F::Identity());i.rt->Clear(D2D1::ColorF(0,0,0,0));
    i.drawing=true;return true;
}
std::vector<uint8_t> Painter::End(){
    auto& i=*impl;std::vector<uint8_t> rgba;
    if(!i.drawing)return rgba;
    i.drawing=false;
    HRESULT hr=i.rt->EndDraw();
    if(FAILED(hr)){i.Fail("EndDraw",hr);return rgba;}
    Com<IWICBitmapLock> lock;const WICRect all{0,0,CanvasWidth,CanvasHeight};
    if(FAILED(hr=i.target->Lock(&all,WICBitmapLockRead,lock.Put()))){i.Fail("WIC lock",hr);return rgba;}
    UINT stride=0,size=0;BYTE* data=nullptr;
    if(FAILED(lock->GetStride(&stride))||FAILED(lock->GetDataPointer(&size,&data))||!data){i.error="WIC lock data failed";return rgba;}
    rgba.resize(size_t(CanvasWidth)*CanvasHeight*4);
    for(int y=0;y<CanvasHeight;++y)
        PremultipliedBgraToStraightRgba(data+size_t(y)*stride,CanvasWidth,rgba.data()+size_t(y)*CanvasWidth*4);
    return rgba;
}
void Painter::Panel(){
    auto& i=*impl;if(!i.drawing||!i.background)return;
    i.rt->DrawBitmap(i.background,{0,0,float(CanvasWidth),float(CanvasHeight)},1.f,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}
void Painter::Stone(RectF r,Color base,float textureOpacity){
    auto& i=*impl;if(!i.drawing)return;
    i.rt->FillRectangle(R(r),i.Token(base));
    i.Shade(R(r),textureOpacity,0x5EEDu);
}
void Painter::Fill(RectF r,Color c,float alpha){auto& i=*impl;if(i.drawing)i.rt->FillRectangle(R(r),i.Token(c,alpha));}
void Painter::Line(float x0,float y0,float x1,float y1,Color c,float width){auto& i=*impl;if(i.drawing)i.rt->DrawLine({x0,y0},{x1,y1},i.Token(c),width);}
void Painter::Outline(RectF r,Color c,float width,bool dashed){
    auto& i=*impl;if(!i.drawing)return;
    const float h=width/2;i.rt->DrawRectangle({r.x+h,r.y+h,r.Right()-h,r.Bottom()-h},i.Token(c),width,dashed?i.dashed.p:nullptr);
}
void Painter::Divider(float x0,float x1,float y){
    auto& i=*impl;if(!i.drawing)return;
    i.rt->FillRectangle({x0,y-3,x1,y-2},i.Token(Color::CarveDark));
    i.rt->FillRectangle({x0,y-2,x1,y},i.Token(Color::CarveLight));
}
void Painter::SectionHeading(RectF r,std::string_view utf8){
    auto& i=*impl;if(!i.drawing)return;
    const float w=i.Width(utf8,TextStyle::Section),cx=r.x+r.w/2,cy=r.y+r.h/2;
    i.DrawText(utf8,TextStyle::Section,{cx-w/2-20,r.y,w+40,r.h},Rgb(Color::Section),Align::Center,false);
    for(float side:{-1.f,1.f}){
        // the rules run 146 px out from the words, never past the box (a guide column's edge)
        const float inner=cx+side*(w/2+8),outer=std::clamp(inner+side*146,r.x,r.Right());
        const float a=std::min(inner,outer),b=std::max(inner,outer);
        i.rt->FillRectangle({a,cy,b,cy+2},i.Token(Color::Bronze));
        i.rt->FillRectangle({a,cy+2,b,cy+3},i.Solid(Ink));
        const float dx=cx+side*(w/2+13);
        i.FillPolygon({{dx,cy-6},{dx+7,cy+1},{dx,cy+8},{dx-7,cy+1}},i.Solid(Ink));
        i.FillPolygon({{dx,cy-7},{dx+7,cy},{dx,cy+7},{dx-7,cy}},i.Token(Color::Bronze));
    }
}
TextSize Painter::Text(std::string_view utf8,TextStyle style,RectF box,Color c,Align align,bool wrap){
    auto& i=*impl;if(!i.drawing)return Measure(utf8,style,box.w,wrap,box.h);
    return i.DrawText(utf8,style,box,Rgb(c),align,wrap);
}
TextSize Painter::Measure(std::string_view utf8,TextStyle style,float maxWidth,bool wrap,float maxHeight,float size){
    auto& i=*impl;if(!i.ok)return {};
    IDWriteTextLayout* layout=i.Layout(utf8,style,maxWidth,maxHeight>0?maxHeight:4096.f,wrap,Align::Leading,size);
    return Impl::SizeOf(layout,maxWidth,maxHeight,wrap,size>0?size:FontSize(style));
}
TextSize Painter::FitSize(std::string_view utf8,TextStyle style,RectF box,bool wrap,float minSize){
    TextSize t;
    for(float size=FontSize(style);size>=minSize-.01f;size-=1.f){
        t=Measure(utf8,style,box.w,wrap,wrap?box.h:0,size);
        if(t.fits)return t;
    }
    return t;
}
TextSize Painter::TextFit(std::string_view utf8,TextStyle style,RectF box,Color c,Align align,bool wrap,float minSize){
    auto& i=*impl;const TextSize t=FitSize(utf8,style,box,wrap,minSize);
    if(!i.drawing)return t;
    TextSize drawn=i.DrawText(utf8,style,box,Rgb(c),align,wrap,t.size==FontSize(style)?0:t.size);
    drawn.fits=t.fits;return drawn;
}
TextSize Painter::Spans(const Span* spans,size_t count,TextStyle style,RectF box,Align align,bool wrap){
    auto& i=*impl;TextSize t;if(!i.drawing||!spans||!count)return t;
    std::string all;for(size_t k=0;k<count;++k)all.append(spans[k].text);
    const std::wstring wide=Wide(all);
    Com<IDWriteTextLayout> layout;
    if(FAILED(i.dwrite->CreateTextLayout(wide.c_str(),UINT32(wide.size()),i.styles[size_t(style)],box.w,box.h,layout.Put())))return t;
    const float px=FontSize(style);
    layout->SetTypography(i.lining,{0,UINT32(wide.size())});
    layout->SetTextAlignment(align==Align::Center?DWRITE_TEXT_ALIGNMENT_CENTER:align==Align::Trailing?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment(wrap?DWRITE_PARAGRAPH_ALIGNMENT_NEAR:DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    layout->SetWordWrapping(wrap?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP);
    i.LineBox(layout,style,px,wrap);
    // weights first, then the shadow pass (no per-run brushes yet), then the colored runs
    std::vector<DWRITE_TEXT_RANGE> ranges(count);
    UINT32 at=0;
    for(size_t k=0;k<count;++k){
        const UINT32 n=UINT32(Wide(spans[k].text).size());
        ranges[k]={at,n};at+=n;
        if(n&&spans[k].strong)layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD,ranges[k]);
    }
    i.rt->DrawTextLayout({box.x,box.y+2},layout,i.Solid(0,.75f));
    std::vector<Com<ID2D1SolidColorBrush>> brushes(count);
    for(size_t k=0;k<count;++k){
        if(!ranges[k].length)continue;
        i.rt->CreateSolidColorBrush(Hex(Rgb(spans[k].color)),brushes[k].Put());
        if(brushes[k])layout->SetDrawingEffect(brushes[k].p,ranges[k]);
    }
    i.rt->DrawTextLayout({box.x,box.y},layout,i.Solid(Rgb(Color::TipBody)));
    return Impl::SizeOf(layout,box.w,box.h,wrap,px);
}
TextSize Painter::IconText(RectF box,Icon icon,std::string_view utf8,TextStyle style,Color text,Color iconColor,Align align){
    auto& i=*impl;if(!i.drawing)return {};
    const float px=FontSize(style),is=std::round(px*.92f),gap=std::round(px*.28f);
    const float tw=i.Width(utf8,style),total=(icon!=Icon::None?is+gap:0)+tw;
    float x=align==Align::Center?box.x+(box.w-total)/2:align==Align::Trailing?box.Right()-total:box.x;
    if(icon!=Icon::None){i.DrawIcon(icon,{x,box.y+(box.h-is)/2,is,is},Rgb(iconColor));x+=is+gap;}
    TextSize t=i.DrawText(utf8,style,{x,box.y,std::max(tw+2,box.Right()-x),box.h},Rgb(text),Align::Leading,false);
    t.width=total;t.fits=total<=box.w+.5f;return t;
}
void Painter::Socket(RectF r,std::string_view utf8,ControlState s,Icon icon){
    auto& i=*impl;if(!i.drawing)return;
    const bool off=s.locked||s.disabled;const D2D1_RECT_F d=i.Shifted(r,s);
    i.Plate(d,s.chosen,off,s.pressed&&!off);
    const uint32_t color=off?(s.chosen?LockedSelText:Rgb(Color::Dim)):Rgb(Color::Text);
    const TextStyle style=s.chosen&&!off?TextStyle::Strong:TextStyle::Value;
    const Icon lead=s.locked&&s.chosen?Icon::Padlock:icon;
    const RectF box{d.left,d.top,d.right-d.left,d.bottom-d.top-2};
    if(lead!=Icon::None){
        const float is=lead==Icon::Padlock?24.f:32.f,gap=lead==Icon::Padlock?6.f:10.f,tw=utf8.empty()?0:i.Width(utf8,style);
        const float total=is+(utf8.empty()?0:gap+tw);float x=box.x+(box.w-total)/2;
        i.DrawIcon(lead,{x,box.y+(box.h-is)/2,is,is},lead==Icon::Padlock?Rgb(Color::Dim):color);
        if(!utf8.empty())i.DrawText(utf8,style,{x+is+gap,box.y,tw+4,box.h},color,Align::Leading,false);
    }else i.DrawText(utf8,style,box,color,Align::Center,false);
    i.Finish(d,r,s);
}
void Painter::Toggle(RectF r,bool on,ControlState s){
    auto& i=*impl;if(!i.drawing)return;
    const bool off=s.locked||s.disabled,lit=on&&!s.locked;const D2D1_RECT_F d=i.Shifted(r,s);   // locked shows what the game does now: Off
    i.Plate(d,lit,off,s.pressed&&!off);
    i.SwitchGem(d.left+12,d.top+(d.bottom-d.top)/2-20,lit,off);
    const float x0=d.left+66,x1=d.right-16;
    const std::string_view word=lit?"On":"Off";
    const uint32_t color=off?Rgb(Color::Dim):lit?Rgb(Color::Text):Rgb(Color::Quiet);
    const RectF box{x0,d.top,x1-x0,d.bottom-d.top-2};
    if(s.locked){
        const float tw=i.Width(word,TextStyle::Strong),total=24+5+tw,x=box.x+(box.w-total)/2;
        i.DrawIcon(Icon::Padlock,{x,box.y+(box.h-24)/2,24,24},Rgb(Color::Dim));
        i.DrawText(word,TextStyle::Strong,{x+29,box.y,tw+4,box.h},color,Align::Leading,false);
    }else i.DrawText(word,TextStyle::Strong,box,color,Align::Center,false);
    i.Finish(d,r,s);
}
void Painter::StepButton(RectF r,bool plus,ControlState s,bool chevron){
    auto& i=*impl;if(!i.drawing)return;
    const bool off=s.locked||s.disabled;const D2D1_RECT_F d=i.Shifted(r,s);
    i.Plate(d,s.chosen,off,s.pressed&&!off);
    const Icon icon=chevron?(plus?Icon::Chevron:Icon::ChevronLeft):(plus?Icon::Plus:Icon::Minus);
    const float is=chevron?30.f:32.f;
    i.DrawIcon(icon,{(d.left+d.right)/2-is/2,(d.top+d.bottom)/2-is/2-1,is,is},off?Rgb(Color::Dim):Rgb(Color::Text));
    i.Finish(d,r,s);
}
void Painter::Bar(RectF r,float fill,float mark,ControlState s,float zeroAt){
    auto& i=*impl;if(!i.drawing)return;
    const bool off=s.locked||s.disabled;
    const D2D1_RECT_F b=R(r);
    i.rt->FillRectangle(b,i.Solid(BarBack));
    i.rt->DrawRectangle({b.left+.5f,b.top+.5f,b.right-.5f,b.bottom-.5f},i.Solid(Ink),1);
    if(fill>=0){
        const float f=std::clamp(fill,0.f,1.f),z=zeroAt>=0?std::clamp(zeroAt,0.f,1.f):0.f;
        const float a=b.left+r.w*std::min(f,z),e=b.left+r.w*std::max(f,z);
        if(e-a>.5f){
            if(off)i.rt->FillRectangle({a,b.top,e,b.bottom},i.Solid(0x4A443C));
            else i.rt->FillRectangle({a,b.top,e,b.bottom},i.Vertical(i.bronzeFill,b.top,b.bottom));
            const float edge=f>=z?e:a;   // the moving end gets the 2 px ink edge
            i.rt->FillRectangle({f>=z?edge-2:edge,b.top,f>=z?edge:edge+2,b.bottom},i.Solid(Ink));
        }
    }
    if(mark>=0){
        const float x=b.left+r.w*std::clamp(mark,0.f,1.f);
        i.rt->FillRectangle({x-2.5f,b.top-7,x+2.5f,b.top+17},i.Solid(Ink));
        i.rt->FillRectangle({x-1.5f,b.top-6,x+1.5f,b.top+16},off?i.Solid(0x8A8274):i.Token(Color::Gold));
    }
    if(fill>=0&&mark>=0&&!off){   // a value's handle (value bars carry a default mark; a progress bar has none): a cast-bronze grip that reads as draggable
        const float x=b.left+r.w*std::clamp(fill,0.f,1.f),top=b.top-5,bottom=b.bottom+5;
        const D2D1_ROUNDED_RECT knob=D2D1::RoundedRect({x-7,top,x+7,bottom},3,3);
        i.rt->FillRoundedRectangle(D2D1::RoundedRect({x-8,top-1,x+8,bottom+1},4,4),i.Solid(Ink));
        i.rt->FillRoundedRectangle(knob,s.pointed||s.pressed?i.Token(Color::RingHi):i.Token(Color::BronzeHi));
        for(float gx:{-3.f,0.f,3.f})i.rt->FillRectangle({x+gx-.5f,top+5,x+gx+.5f,bottom-5},i.Solid(Ink,.55f));   // grip lines
    }
}
void Painter::ValueBox(RectF r,std::string_view value,std::string_view sub,ControlState s,float fill,float mark,float zeroAt,float hue){
    auto& i=*impl;if(!i.drawing)return;
    const bool off=s.locked||s.disabled;const D2D1_RECT_F d=i.Shifted(r,s);
    i.Plate(d,s.chosen,off,s.pressed&&!off);
    const bool bar=fill>=0,two=!sub.empty();
    const float h=d.bottom-d.top;
    const float bottomPad=bar?12.f:two?26.f:0.f;
    const RectF box{d.left+12,d.top,d.right-d.left-24,h-bottomPad-(bar||two?0:2)};
    const uint32_t color=off?Rgb(Color::Dim):Rgb(Color::Text);
    // the value line: [swatch] [padlock] value, centered as a group
    const float tw=value.empty()?0.f:i.Width(value,TextStyle::Strong);
    const bool swatch=hue>-1.5f||hue==SwatchByRange;
    const float disc=value.empty()&&!two?36.f:value.empty()?30.f:32.f,sw=swatch?disc+4:0,gap=swatch&&tw>0?8.f:0,pad=s.locked?24.f+5:0;
    const float total=sw+gap+pad+tw;float x=box.x+(box.w-total)/2;
    const float cy=box.y+box.h/2;
    if(swatch){Swatch({x+2,cy-disc/2,disc,disc},hue,s);x+=sw+gap;}
    if(s.locked){i.DrawIcon(Icon::Padlock,{x,cy-12,24,24},Rgb(Color::Dim));x+=pad;}
    i.DrawText(value,TextStyle::Strong,{x,box.y,tw+4,box.h},color,Align::Leading,false);
    if(bar)Bar({d.left+14,d.bottom-18,d.right-d.left-28,10},fill,mark,s,zeroAt);
    else if(two)i.DrawText(sub,TextStyle::Small,{d.left+6,d.bottom-33,d.right-d.left-12,26},off?Rgb(Color::Dim):Rgb(Color::Quiet),Align::Center,false);
    i.Finish(d,r,s);
}
void Painter::Swatch(RectF r,float hue,ControlState s){
    auto& i=*impl;if(!i.drawing)return;
    const float cx=r.x+r.w/2,cy=r.y+r.h/2,rad=std::min(r.w,r.h)/2;
    const bool off=s.locked||s.disabled;
    if(hue==SwatchByRange){   // the outline's By range: red near, orange mid, yellow far
        i.rt->FillEllipse(D2D1::Ellipse({cx,cy},rad+4,rad+4),i.Token(off?Color::EdgeOff:Color::Bronze));
        i.rt->FillEllipse(D2D1::Ellipse({cx,cy},rad+2,rad+2),i.Solid(Ink));
        if(off){i.rt->FillEllipse(D2D1::Ellipse({cx,cy},rad,rad),i.Solid(0x5A5A5A));return;}
        auto range=i.Linear({cx-rad,cy},{cx+rad,cy},{{0,Hex(0xE8402F)},{.5f,Hex(0xF59A21)},{1,Hex(0xF4DC3A)}});
        if(range)i.rt->FillEllipse(D2D1::Ellipse({cx,cy},rad,rad),range);
        return;
    }
    if(hue<0){
        i.rt->DrawEllipse(D2D1::Ellipse({cx,cy},rad-1.5f,rad-1.5f),i.Token(off?Color::Dim:Color::Quiet),3);
        const float k=(rad-4)*.7071f;
        i.rt->DrawLine({cx-k,cy+k},{cx+k,cy-k},i.Token(off?Color::Dim:Color::Quiet),3);
        return;
    }
    i.rt->FillEllipse(D2D1::Ellipse({cx,cy},rad+4,rad+4),i.Token(off?Color::EdgeOff:Color::Bronze));
    i.rt->FillEllipse(D2D1::Ellipse({cx,cy},rad+2,rad+2),i.Solid(Ink));
    uint32_t rgb=HueRgb(hue);
    if(off){const uint32_t g=((rgb>>16&255)*30+(rgb>>8&255)*59+(rgb&255)*11)/100/2+20;rgb=(g<<16)|(g<<8)|g;}
    i.rt->FillEllipse(D2D1::Ellipse({cx,cy},rad,rad),i.Solid(rgb));
}
void Painter::Button(RectF r,std::string_view utf8,Icon icon,ControlState s,Plate plate,float textSize){
    auto& i=*impl;if(!i.drawing)return;
    const bool off=s.locked||s.disabled;const D2D1_RECT_F d=i.Shifted(r,s);
    i.Plate(d,s.chosen&&plate==Plate::Socket,off,s.pressed&&!off,plate);
    uint32_t color=Rgb(Color::Text);TextStyle style=TextStyle::Value;
    if(off)color=Rgb(Color::Dim);
    else if(plate==Plate::Primary){color=PlateText;style=TextStyle::Strong;}
    else if(plate==Plate::Danger){color=Rgb(Color::Danger);style=TextStyle::Strong;}
    else if(s.chosen)style=TextStyle::Strong;
    const float px=textSize>0?textSize:FontSize(style),size=textSize>0?textSize:0;
    const Icon lead=s.locked?Icon::Padlock:icon;
    const float is=lead==Icon::None?0:std::round(px),gap=lead==Icon::None?0:10.f;
    const float tw=utf8.empty()?0:i.Width(utf8,style,size),total=is+(utf8.empty()?0:gap)+tw;
    float x=(d.left+d.right)/2-total/2;const float cy=(d.top+d.bottom)/2;
    const bool primary=plate==Plate::Primary&&!off;
    if(lead!=Icon::None){i.DrawIcon(lead,{x,cy-is/2,is,is},color);x+=is+gap;}
    if(!utf8.empty()){
        IDWriteTextLayout* layout=i.Layout(utf8,style,tw+4,d.bottom-d.top-2,false,Align::Leading,size);
        if(primary)i.DrawLayout(layout,x,d.top,color,1.f,0xFFECBE,.35f,1);
        else i.DrawLayout(layout,x,d.top,color);
    }
    i.Finish(d,r,s);
}
void Painter::IconAt(Icon icon,RectF r,Color c,float alpha){auto& i=*impl;if(i.drawing)i.DrawIcon(icon,r,Rgb(c),alpha);}
void Painter::Tab(RectF r,Icon icon,std::string_view name,ControlState s){
    auto& i=*impl;if(!i.drawing)return;
    D2D1_RECT_F d=R(r);
    if(s.chosen){
        const float right=std::max(d.right,RailRight-RailSeam-1);
        auto tongue=i.Linear({d.left,0},{right,0},{{0,Hex(TongueA)},{1,Hex(Rgb(Color::Stone))}});
        i.rt->FillRectangle({d.left,d.top,right,d.bottom},tongue);
        i.rt->FillRectangle({d.left-1,d.top+9,d.left+7,d.bottom-9},i.Solid(Ink));
        i.rt->FillRectangle({d.left,d.top+10,d.left+6,d.bottom-10},i.Token(Color::Crimson1));
    }else if(s.pointed&&!s.disabled){
        auto slab=D2D1::RoundedRect(d,8,8);i.rt->FillRoundedRectangle(slab,i.Token(Color::Slab));
        i.rt->FillRectangle({d.right-8,d.top,d.right,d.bottom},i.Token(Color::Slab));
    }
    if(s.pressed)i.rt->FillRectangle({d.left,d.top,d.right,d.top+6},i.Solid(0,.55f));
    const bool off=s.disabled||s.locked;
    const uint32_t iconColor=off?Rgb(Color::EdgeOff):s.chosen?Rgb(Color::Gold):s.pointed?Rgb(Color::Quiet):TabIcon;
    i.DrawIcon(icon,{d.left+16,d.top+(r.h-40)/2,40,40},iconColor);
    if(s.changed)i.DrawIcon(Icon::Changed,{d.left+40,d.top+6,28,28},Rgb(Color::Changed));
    const uint32_t color=off?Rgb(Color::Dim):s.chosen?Rgb(Color::GoldHi):s.pointed?Rgb(Color::Text):Rgb(Color::Quiet);
    i.DrawText(name,s.chosen?TextStyle::Strong:TextStyle::Label,{d.left+70,d.top,d.right-d.left-76,r.h},color,Align::Leading,false,34);
    if(s.pointed)i.Ring(d,off);
}
void Painter::PageTitle(RectF r,Icon icon,std::string_view title){
    auto& i=*impl;if(!i.drawing)return;
    const float cy=r.y+r.h/2;
    i.DrawIcon(icon,{r.x,cy-24,48,48},Rgb(Color::Gold));
    IDWriteTextLayout* layout=i.Layout(title,TextStyle::Title,std::max(r.w-64,1.f),r.h,false,Align::Leading,0);
    if(!layout)return;
    // cast gold: glyph outlines, a 3 px drop shadow, the gradient fill, then a 1 px ink stroke on top
    char size[48];std::snprintf(size,sizeof size,"\x1f%.1f|%.1f",r.w,r.h);
    const std::string key=std::string(title)+size;
    if(i.titleKey!=key){
        OutlineCollector collect(i.d2d);layout->Draw(nullptr,&collect,0,0);
        i.titleRuns=std::move(collect.runs);i.titleKey=key;
    }
    const float x=r.x+64,y=r.y;
    D2D1_MATRIX_3X2_F old;i.rt->GetTransform(&old);
    const auto& base=*D2D1::Matrix3x2F::ReinterpretBaseType(&old);
    i.rt->SetTransform(D2D1::Matrix3x2F::Translation(x,y+3)*base);
    for(const auto& g:i.titleRuns)i.rt->FillGeometry(g,i.Solid(0,.8f));
    i.rt->SetTransform(D2D1::Matrix3x2F::Translation(x,y)*base);
    auto gold=i.Linear({0,cy-22-y},{0,cy+22-y},{{0,Hex(0xFFF4CC)},{.12f,Hex(0xF6E6B0)},{.45f,Hex(0xD8B865)},{.55f,Hex(0x9C7634)},{1,Hex(0xC9A55A)}});
    for(const auto& g:i.titleRuns){
        if(gold){i.rt->FillGeometry(g,gold);i.rt->DrawGeometry(g,gold,1.6f);}
        i.rt->DrawGeometry(g,i.Solid(0x1E140A),1.f);
    }
    i.rt->SetTransform(old);
}
void Painter::Sigil(RectF r){
    auto& i=*impl;if(!i.drawing)return;
    D2D1_MATRIX_3X2_F old;i.rt->GetTransform(&old);
    const float s=std::min(r.w,r.h)/56.f;
    i.rt->SetTransform(D2D1::Matrix3x2F::Scale(s,s)*D2D1::Matrix3x2F::Translation(r.x+(r.w-56*s)/2,r.y+(r.h-56*s)/2)**D2D1::Matrix3x2F::ReinterpretBaseType(&old));
    const auto bronze=i.Solid(Rgb(Color::Bronze),.45f);
    i.rt->DrawGeometry(i.sigilOuter,bronze,2);
    i.rt->DrawGeometry(i.sigilThin,i.Solid(Rgb(Color::Bronze),.45f),1.2f);
    i.rt->DrawGeometry(i.sigilMid,i.Solid(Rgb(Color::Bronze),.45f),1.6f);
    i.rt->FillEllipse(D2D1::Ellipse({28,29},2.4f,2.4f),i.Solid(Rgb(Color::Bronze),.45f));
    i.rt->SetTransform(old);
}
void Painter::HeaderLink(RectF r,Icon icon,std::string_view utf8,ControlState s){
    auto& i=*impl;if(!i.drawing)return;
    const TextStyle style=s.chosen?TextStyle::Strong:TextStyle::Value;constexpr float px=28;
    const uint32_t color=s.chosen?Rgb(Color::Gold):s.pointed?Rgb(Color::Quiet):LinkText;
    const float is=32,gap=10,tw=i.Width(utf8,style,px),total=is+gap+tw;
    float x=r.x+(r.w-total)/2;const float cy=r.y+r.h/2;
    i.DrawIcon(icon,{x,cy-is/2,is,is},color);x+=is+gap;
    IDWriteTextLayout* layout=i.Layout(utf8,style,tw+4,r.h,false,Align::Leading,px);
    i.DrawLayout(layout,x,r.y,color);
    // underline 7 px under the baseline, in its own color
    DWRITE_LINE_METRICS line{};UINT32 lines=0;float baseline=r.y+r.h/2+px*.35f;
    if(layout&&SUCCEEDED(layout->GetLineMetrics(&line,1,&lines))&&lines){
        DWRITE_TEXT_METRICS m{};layout->GetMetrics(&m);baseline=r.y+m.top+line.baseline;
    }
    const float uw=s.chosen?3.f:1.f;
    i.rt->FillRectangle({x,baseline+6,x+tw,baseline+6+uw},s.chosen?i.Token(Color::Gold):s.pointed?i.Solid(0x6E5330):i.Solid(LinkLine));
    if(s.pointed)i.rt->DrawRectangle({r.x+2.5f,r.y+2.5f,r.Right()-2.5f,r.Bottom()-2.5f},i.Token(Color::RingHi),3);
}
void Painter::Row(RectF r,bool pointed,bool divider){
    auto& i=*impl;if(!i.drawing)return;
    if(pointed)i.rt->FillRoundedRectangle(D2D1::RoundedRect(R(r),6,6),i.Token(Color::Slab));
    if(divider)Divider(r.x,r.Right(),r.Bottom());
}
void Painter::Label(RectF r,std::string_view label,std::string_view sub,ControlState s,Icon subIcon,Color subColor){
    auto& i=*impl;if(!i.drawing)return;
    const bool off=s.locked||s.disabled;
    constexpr float labelH=38,subH=32,gap=2;
    const float block=sub.empty()?labelH:labelH+gap+subH,top=r.y+(r.h-block)/2;
    const uint32_t color=off?Rgb(Color::Dim):Rgb(Color::Text);
    const TextSize t=i.DrawText(label,TextStyle::Label,{r.x,top,r.w,labelH},color,Align::Leading,false);
    if(s.changed&&!off)i.DrawIcon(Icon::Changed,{r.x+t.width+10,top+1,36,36},Rgb(Color::Changed));
    if(sub.empty())return;
    const Icon icon=s.locked&&subIcon==Icon::None?Icon::Padlock:subIcon;
    const Color tone=s.locked&&subColor==Color::Quiet?Color::Quiet:subColor;
    float x=r.x;const float sy=top+labelH+gap;
    if(icon!=Icon::None){i.DrawIcon(icon,{x,sy+(subH-26)/2,26,26},Rgb(icon==Icon::Padlock?Color::Quiet:tone));x+=26+8;}
    i.DrawText(sub,TextStyle::Value,{x,sy,r.Right()-x,subH},Rgb(tone),Align::Leading,false);
}
void Painter::PresetCard(RectF r,Icon icon,std::string_view name,std::string_view blurb,ControlState s){
    auto& i=*impl;if(!i.drawing)return;
    const D2D1_RECT_F d=i.Shifted(r,s);
    if(s.pointed||s.pressed){
        auto shape=i.Chamfer(d,6);i.rt->FillGeometry(shape,i.Token(Color::Slab));
        i.Bevel(d,6,BevelTop,BevelLeft,Ink,Ink);
        if(s.pressed)i.FillPolygon({{d.left+6,d.top},{d.right-6,d.top},{d.right,d.top+6},{d.left,d.top+6}},i.Solid(0,.6f));
    }else Divider(r.x,r.Right(),r.Bottom());
    const float cy=(d.top+d.bottom)/2;
    i.DrawIcon(icon,{d.left+22,cy-32,64,64},s.disabled?Rgb(Color::EdgeOff):Rgb(Color::Gold));
    const float tx=d.left+108,tw=d.right-tx-22;
    const TextSize body=Measure(blurb,TextStyle::Value,tw,true,0);
    const float nameH=38,block=nameH+(blurb.empty()?0:6+body.height),top=cy-block/2;
    const uint32_t color=s.disabled?Rgb(Color::Dim):s.changed?Rgb(Color::Changed):s.chosen?Rgb(Color::Gold):Rgb(Color::Text);
    i.DrawText(name,TextStyle::CardTitle,{tx,top,tw,nameH},color,Align::Leading,false);
    if(!blurb.empty())i.DrawText(blurb,TextStyle::Value,{tx,top+nameH+6,tw,body.height+4},s.disabled?Rgb(Color::Dim):Rgb(Color::Quiet),Align::Leading,true);
    if(s.pointed)i.Ring(R(r),s.disabled);
}
void Painter::Pill(RectF r,std::string_view utf8,Color c,Icon icon,ControlState s){
    auto& i=*impl;if(!i.drawing)return;
    const D2D1_RECT_F d=R(r);
    if(s.pointed)i.rt->FillRoundedRectangle(D2D1::RoundedRect(d,6,6),i.Token(Color::Slab));
    i.rt->DrawRoundedRectangle(D2D1::RoundedRect({d.left+1,d.top+1,d.right-1,d.bottom-1},6,6),i.Token(c),2);
    IconText({r.x+8,r.y,r.w-16,r.h},icon,utf8,TextStyle::Small,c,c,Align::Center);
    if(s.pointed)i.Ring(d,false);
}
void Painter::Card(RectF r){
    auto& i=*impl;if(!i.drawing)return;
    i.rt->FillRectangle(R(r),i.Solid(0x040302,.86f));
    i.rt->DrawRectangle({r.x+.5f,r.y+.5f,r.Right()-.5f,r.Bottom()-.5f},i.Token(Color::CarveLight),1);
}
void Painter::ReadoutBox(RectF r){
    auto& i=*impl;if(!i.drawing)return;
    i.rt->FillRoundedRectangle(D2D1::RoundedRect(R(r),6,6),i.Solid(0x040302,.62f));
    i.rt->DrawRoundedRectangle(D2D1::RoundedRect({r.x+.5f,r.y+.5f,r.Right()-.5f,r.Bottom()-.5f},6,6),i.Token(Color::CarveLight),1);
}
void Painter::DrainBar(RectF r,float left,Color c){
    auto& i=*impl;if(!i.drawing)return;
    i.rt->FillRectangle({r.x,r.y,r.x+r.w*std::clamp(left,0.f,1.f),r.Bottom()},i.Token(c));
}
void Painter::Gem(float cx,float cy,float radius){auto& i=*impl;if(i.drawing)i.GemAt(cx,cy,radius);}
void Painter::DefaultMark(float cx,float y){auto& i=*impl;if(i.drawing)i.DefaultTriangle(cx,y);}
void Painter::Dot(RectF r,float hue,ControlState s){
    auto& i=*impl;if(!i.drawing)return;
    // Pointed: 1.15 and the pointed ring; pressed: 0.92; chosen: a 4 px ring.
    const float scale=s.pressed?.92f:s.pointed?1.15f:1.f;
    const float cx=r.x+r.w/2,cy=r.y+r.h/2,rad=std::min(r.w,r.h)/2*scale;
    if(s.chosen)i.rt->DrawEllipse(D2D1::Ellipse({cx,cy},rad+7,rad+7),i.Token(Color::Text),4);
    if(s.pointed)i.rt->DrawEllipse(D2D1::Ellipse({cx,cy},rad+(s.chosen?12.5f:6.5f),rad+(s.chosen?12.5f:6.5f)),i.Token(Color::RingHi),3);
    Swatch({cx-rad,cy-rad,rad*2,rad*2},hue,ControlState{false,false,false,s.locked,false,false,s.disabled});
    if(s.isDefault)i.DefaultTriangle(cx,cy+rad+4);
}

// ---- the component sheet (vr-settings-preview --draw-sheet) ----
namespace {
constexpr const char* Deg="\xc2\xb0";
// The rail as on every shot: Presets, then the 11 pages; `current` is chosen.
void SheetRail(Painter& p,int current,int changed=-1,int pointed=-1){
    static const std::pair<Icon,const char*> tabs[]={{Icon::Presets,"Presets"},{Icon::Body,"Body"},{Icon::Combat,"Combat"},{Icon::Aim,"Aim"},
        {Icon::Targets,"Targets"},{Icon::Shield,"Shield"},{Icon::Wrist,"Wrist"},{Icon::Interface,"Interface"},{Icon::Survival,"Survival"},
        {Icon::World,"World"},{Icon::Haptics,"Vibration"},{Icon::Performance,"Performance"}};
    for(int k=0;k<12;++k){
        ControlState s;s.chosen=k==current;s.changed=k==changed;s.pointed=k==pointed;
        p.Tab({22,30.f+78*k,254,66},tabs[k].first,tabs[k].second,s);
    }
}
void SheetBand(Painter& p,Icon icon,const char* title,int open=-1,int pointed=-1){
    p.PageTitle({328,20,440,56},icon,title);
    p.Sigil({798,20,56,56});
    ControlState g;g.chosen=open==0;g.pointed=pointed==0;
    ControlState n;n.chosen=open==1;n.pointed=pointed==1;
    p.HeaderLink({880,15,150,66},Icon::Controls,"Guide",g);
    p.HeaderLink({1040,15,208,66},Icon::Notes,"What's new",n);
}
void SheetFooter(Painter& p,int changes,bool pointClose=false){
    ControlState reset;reset.disabled=changes==0;
    p.Button({328,934,304,66},"Reset page",Icon::Reset,reset);
    ControlState close;close.pointed=pointClose;
    p.Button({680,934,210,66},"Close",Icon::Close,close);
    ControlState apply;apply.disabled=changes==0;
    p.Button({948,934,280,66},changes?"Apply 1 change":"No changes",Icon::Check,apply,changes?Plate::Primary:Plate::Socket);
}
// pages/04-aim.png
void SheetAim(Painter& p){
    p.Panel();SheetRail(p,3);SheetBand(p,Icon::Aim,"Aim");
    p.SectionHeading({348,76,880,30},"Spells and skills");
    const float L=328,Wd=920;
    // row 1: a cycler (Skills go to the aimed monster)
    p.Row({L,106,Wd,84},false);p.Label({L+20,106,560,84},"Skills go to the aimed monster","",{});
    p.StepButton({916,117,60,62},false,{},true);
    ControlState def;def.isDefault=true;
    p.ValueBox({996,117,154,62},"Off","1 of 4",def);
    p.StepButton({1170,117,60,62},true,{},true);
    // row 2: chips
    p.Row({L,190,Wd,84},false);p.Label({L+20,190,520,84},"Spell aim assist","",{});
    p.Socket({890,201,90,62},"Off",{});p.Socket({1000,201,96,62},"Light",{});
    ControlState strong;strong.chosen=true;strong.isDefault=true;p.Socket({1114,201,116,62},"Strong",strong);
    // rows 3-4: switches
    p.Row({L,274,Wd,84},false);p.Label({L+20,274,560,84},"Keep spell shots level","",{});p.Toggle({1078,285,152,62},false,def);
    p.Row({L,358,Wd,84},false);p.Label({L+20,358,560,84},"Aim staff along arm","Sorceress, Druid, staff only",{});p.Toggle({1078,369,152,62},false,def);
    p.SectionHeading({348,442,880,30},"Ground skills");
    p.Row({L,472,Wd,84},false,false);p.Label({L+20,472,420,84},"Ground skill aim","4 classes",{});
    ControlState game;game.chosen=true;game.isDefault=true;
    p.Socket({760,483,172,62},"Game's aim",game);p.Socket({950,483,128,62},"Pointed",{});p.Socket({1096,483,134,62},"Monster",{});
    // the idle card
    p.Card({328,560,920,120});
    p.Text("Aim",TextStyle::CardTitle,{348,566,880,38},Color::TipBody,Align::Center);
    p.Text("Where your spells and skills go. Rows for other classes stay hidden. Point at a setting to read about it.",
           TextStyle::Help,{348,604,880,70},Color::TipBody,Align::Center,true);
    SheetFooter(p,0);
}
// v4-horadric-shield-dirty.png: sliders, a changed row pointed, the card's buttons, Apply.
void SheetShield(Painter& p){
    p.Panel();SheetRail(p,5,5);SheetBand(p,Icon::Shield,"Shield");
    p.SectionHeading({348,76,880,30},"Bash and block");
    const float L=328,Wd=920;
    const auto slider=[&](float y,const char* label,const char* value,float fill,float mark,float zero,bool changed,bool pointed,bool hotPlus){
        p.Row({L,y,Wd,80},pointed);
        ControlState ls;ls.changed=changed;p.Label({L+20,y,560,80},label,"",ls);
        ControlState box;box.isDefault=!changed&&std::fabs(fill-mark)<.001f;
        p.StepButton({916,y+9,60,62},false,{});
        p.ValueBox({996,y+9,154,62},value,"",box,fill,mark,zero);
        ControlState plus;plus.pointed=hotPlus;p.StepButton({1170,y+9,60,62},true,plus);
    };
    slider(106,"Bash speed to hit","2 m/s",.08f,.08f,-1,false,false,false);
    p.Row({L,186,Wd,80},false);p.Label({L+20,186,440,80},"Guard strain edge","",{});
    p.Socket({796,195,90,62},"Off",{});ControlState pale;pale.chosen=true;pale.isDefault=true;
    p.Socket({904,195,150,62},"Pale glow",pale);p.Socket({1074,195,156,62},"Dark edge",{});
    p.Row({L,266,Wd,80},false);p.Label({L+20,266,440,80},"Guard strain buildup","",{});
    p.Socket({874,275,90,62},"Slow",{});ControlState med;med.chosen=true;med.isDefault=true;
    p.Socket({984,275,136,62},"Medium",med);p.Socket({1140,275,90,62},"Fast",{});
    p.SectionHeading({348,346,880,30},"Fit on the arm");
    std::string deg0=std::string("0")+Deg,deg45=std::string("+45")+Deg;
    slider(376,"Shield along forearm","0 cm",.2f,.2f,.2f,false,false,false);
    slider(456,"Shield out from arm","0 cm",.5f,.5f,.5f,false,false,false);
    slider(536,"Shield across forearm","0 cm",.5f,.5f,.5f,false,false,false);
    slider(616,"Shield wrist turn","50%",.5f,.5f,-1,false,false,false);
    slider(696,"Shield roll",deg45.c_str(),.625f,.5f,.5f,true,true,true);
    // the card of the changed row: title, state line, Undo and Default buttons
    p.Card({328,780,920,146});
    p.Text("Shield roll",TextStyle::CardTitle,{348,786,580,38},Color::Changed,Align::Center);
    std::string state=std::string("Not applied. Applied: +90")+Deg;
    p.IconText({348,824,580,34},Icon::Changed,state,TextStyle::Value,Color::Changed,Color::Changed,Align::Center);
    std::string undo=std::string("Undo: back to +90")+Deg,dflt=std::string("Shield roll to 0")+Deg;
    p.Button({932,786,292,62},undo,Icon::Undo,{},Plate::Socket,28);
    p.Button({932,860,292,62},dflt,Icon::Reset,{},Plate::Socket,28);
    SheetFooter(p,1);
}
// Every control in every state, one column per state.
void SheetStates(Painter& p){
    p.Panel();p.Stone({22,23,1236,978},Color::Stone,.3f);   // no rail on the reference sheets
    p.Text("Controls x states",TextStyle::Title,{44,26,800,50},Color::Gold);
    static const char* heads[]={"idle","pointed","pressed","chosen","locked","locked+pt","changed+def","disabled"};
    const auto state=[](int c){
        ControlState s;
        switch(c){case 1:s.pointed=true;break;case 2:s.pressed=true;s.pointed=true;break;case 3:s.chosen=true;break;
            case 4:s.locked=true;break;case 5:s.locked=true;s.pointed=true;s.chosen=true;break;case 6:s.changed=true;s.isDefault=true;break;
            case 7:s.disabled=true;break;default:break;}
        return s;
    };
    constexpr float X0=180,CW=133,Y0=118,RH=74;
    for(int c=0;c<8;++c)p.Text(heads[c],TextStyle::Small,{X0+CW*c,80,CW,32},Color::Section,Align::Center);
    static const char* rows[]={"Chip","Toggle Off","Toggle On","Step + / -","Cycler < >","Slider","Cycler","Hue","Button","Apply","Discard","Swatch"};
    for(int row=0;row<12;++row){
        const float y=Y0+RH*row;
        p.Text(rows[row],TextStyle::Value,{40,y,140,58},Color::Quiet);
        if(row)p.Divider(40,1240,y-6);
        for(int c=0;c<8;++c){
            const ControlState s=state(c);const float x=X0+CW*c+6;const RectF r{x,y,CW-14,58};
            switch(row){
            case 0:p.Socket(r,"Light",s);break;
            case 1:p.Toggle(r,false,s);break;
            case 2:p.Toggle(r,true,s);break;
            case 3:p.StepButton({x,y,56,58},false,s);p.StepButton({x+62,y,56,58},true,s);break;
            case 4:p.StepButton({x,y,56,58},false,s,true);p.StepButton({x+62,y,56,58},true,s,true);break;
            case 5:p.ValueBox(r,"+3 cm","",s,.62f,.5f,.5f);break;
            case 6:p.ValueBox(r,"Spells","2 of 4",s);break;
            case 7:p.ValueBox(r,"Gold","3 of 14",s,-1,-1,-1,c==7?-1.f:45.f);break;
            case 8:p.Button(r,"Close",Icon::Close,s);break;
            case 9:p.Button(r,"Apply",Icon::Check,s,Plate::Primary);break;
            case 10:p.Button(r,"Discard",Icon::Close,s,Plate::Danger,26);break;
            case 11:p.Socket({x+24,y,64,58},"",s);p.Swatch({x+40,y+13,32,32},c==4?-1.f:210.f,s);break;
            default:break;
            }
        }
    }
}
// Every icon, tabs, links, labels, presets, card text, ornaments.
void SheetIcons(Painter& p){
    p.Panel();p.Stone({22,23,1236,978},Color::Stone,.3f);
    p.Text("Icons, rail, band, card",TextStyle::Title,{44,26,800,50},Color::Gold);
    static const char* names[]={"","presets","body","combat","aim","targets","shield","wrist","interface","survival","world","haptics","perf","controls","notes",
        "comfort","classic","fullvr","fast","plus","minus","chevron","chevL","chevUp","chevDown","check","close","reset","undo","default","feel",
        "padlock","warning","info","changed","star","starOut","search","soundOn","soundOff"};
    for(int k=1;k<int(Icon::Count);++k){
        const int col=(k-1)%13,row=(k-1)/13;const float x=40+col*92.f,y=84+row*108.f;
        p.IconAt(Icon(k),{x+14,y,56,56},k%3==0?Color::Text:k%3==1?Color::Gold:Color::Quiet);
        p.Text(names[k],TextStyle::Readout,{x-6,y+58,96,28},Color::Quiet,Align::Center);
    }
    // tabs: idle, pointed, current, changed, current+pointed, disabled
    const float ty=420;
    ControlState t1;t1.pointed=true;ControlState t2;t2.chosen=true;ControlState t3;t3.changed=true;ControlState t4;t4.chosen=true;t4.pointed=true;t4.changed=true;
    ControlState t5;t5.disabled=true;
    p.Tab({40,ty,254,66},Icon::Body,"Body",{});p.Tab({40,ty+78,254,66},Icon::Combat,"Combat",t1);p.Tab({40,ty+156,254,66},Icon::Aim,"Aim",t2);
    p.Tab({40,ty+234,254,66},Icon::Shield,"Shield",t3);p.Tab({40,ty+312,254,66},Icon::Performance,"Performance",t4);p.Tab({40,ty+390,254,66},Icon::World,"World",t5);
    // band: links idle / pointed / open, the title and sigil
    p.PageTitle({340,ty,420,56},Icon::Targets,"Targets");p.Sigil({780,ty,56,56});
    ControlState hp;hp.pointed=true;ControlState ho;ho.chosen=true;
    p.HeaderLink({850,ty-5,150,66},Icon::Controls,"Guide",hp);p.HeaderLink({1010,ty-5,220,66},Icon::Notes,"What's new",ho);
    // labels: plain + changed, warn sub, locked sub, comfort sub
    ControlState ch;ch.changed=true;ControlState lk;lk.locked=true;
    p.Row({330,ty+70,600,80},true);p.Label({350,ty+70,580,80},"Shield roll","",ch);
    p.Row({330,ty+150,600,84},false);p.Label({350,ty+150,580,84},"Spell glow on hands","May show through walls and weapons",{},Icon::Warning,Color::Warn);
    p.Row({330,ty+234,600,84},false);p.Label({350,ty+234,580,84},"Aim with your view","Needs Monster outline (Targets) or 2 more",lk);
    p.Pill({950,ty+90,280,44},"Recommended: On",Color::Quiet);p.Pill({950,ty+150,140,44},"PR only",Color::Magic);
    p.Pill({950,ty+210,280,50},"Mono audio is on",Color::Changed,Icon::Warning);
    // preset cards: idle, pointed, applied
    ControlState pp;pp.pointed=true;ControlState pa;pa.chosen=true;
    p.PresetCard({330,ty+330,600,110},Icon::Comfort,"Comfort","Gentle turns, strong aim help, no blood.",pp);
    p.PresetCard({330,ty+446,600,96},Icon::Fast,"Smooth frames","The fastest frame rate for older PCs.",pa);
    // card text: spans, fit-or-shrink, the readout box and drain bar
    p.Card({950,ty+270,290,140});
    const std::string zero=std::string("0")+Deg,ninety=std::string("+90")+Deg;
    const Span spans[]={{"Roll: ",Color::TipBody},{zero,Color::Quiet},{" \xe2\x86\x92 ",Color::Quiet},{ninety,Color::Changed,true}};
    p.Spans(spans,4,TextStyle::Value,{960,ty+276,270,36},Align::Center);
    p.TextFit("Shrinks to fit: 30 px to 22 px",TextStyle::Value,{960,ty+316,270,34},Color::Magic,Align::Center);
    p.ReadoutBox({960,ty+356,270,44});p.Text("90 Hz \xc2\xb7 11.1 ms",TextStyle::Readout,{970,ty+356,250,44},Color::Quiet);
    p.DrainBar({950,ty+420,290,6},.6f,Color::Gold);
    p.Gem(980,ty+470,12);p.Gem(1030,ty+470,8);p.DefaultMark(1080,ty+460);
    p.SectionHeading({330,ty+554,600,30},"Section heading");
}
}
Sheet DrawSheet(int repaints){
    using Clock=std::chrono::steady_clock;
    Sheet sheet;sheet.width=CanvasWidth*2;sheet.height=CanvasHeight*2;
    const auto start=Clock::now();
    Painter p;
    if(!p.Ready()){sheet.error=p.Error();return sheet;}
    sheet.rgba.assign(size_t(sheet.width)*sheet.height*4,0);
    void (*pages[])(Painter&)={SheetAim,SheetShield,SheetStates,SheetIcons};
    for(int k=0;k<4;++k){
        if(!p.Begin()){sheet.error=p.Error();return sheet;}
        pages[k](p);
        const auto rgba=p.End();
        if(rgba.empty()){sheet.error=p.Error().empty()?"paint failed":p.Error();return sheet;}
        if(k==0)sheet.firstMs=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        const int ox=(k%2)*CanvasWidth,oy=(k/2)*CanvasHeight;
        for(int y=0;y<CanvasHeight;++y)
            std::memcpy(sheet.rgba.data()+(size_t(oy+y)*sheet.width+ox)*4,rgba.data()+size_t(y)*CanvasWidth*4,size_t(CanvasWidth)*4);
    }
    // a full menu repaint: the Aim page, background cached, text layouts warm
    const auto loop=Clock::now();
    for(int k=0;k<repaints;++k){p.Begin();SheetAim(p);if(p.End().empty()){sheet.error=p.Error();return sheet;}}
    sheet.repaintMs=repaints>0?std::chrono::duration<double,std::milli>(Clock::now()-loop).count()/repaints:0;
    // the whole sheet again, warm: four full canvases
    const auto all=Clock::now();
    for(auto* page:pages){p.Begin();page(p);if(p.End().empty()){sheet.error=p.Error();return sheet;}}
    sheet.sheetMs=std::chrono::duration<double,std::milli>(Clock::now()-all).count();
    return sheet;
}
}
