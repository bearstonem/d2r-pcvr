#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include "vr_settings_ui.h"
#include "release_notes.h"
// Retain an exact summary fingerprint in the DLL for the release packager.
extern "C" __declspec(dllexport) const char* D2RPCVR_ReleaseNotesIdentity(){return framevr::releasenotes::ContentMarker;}
static_assert(framevr::releasenotes::ReleaseLines.size()<=size_t(framevr::settings::NotesMaxLines),"release_notes.py MAX_LINES and NotesMaxLines agree");
// ---- the page painter: Layout -> Painter ----
// Build lays the page out once with the painter's own text measure; every
// element is then drawn by its role with its state flags. Nothing here knows
// Direct2D: color tokens, text styles and one draw call per role.
namespace framevr::settings {
namespace {
using layout::Element;using layout::Layout;using layout::Role;using layout::Arrow;using layout::GuideLink;using layout::BandY;using layout::BandInkH;using layout::ContentW;
// The flag bits by name here (settings::Changed is also a function).
constexpr layout::Flag Enabled=layout::Enabled,Selected=layout::Selected,Changed=layout::Changed,Pressed=layout::Pressed,Hovered=layout::Hovered,
    Locked=layout::Locked,IsDefault=layout::IsDefault,Recommended=layout::Recommended,Warn=layout::Warn;
using draw::Align;using draw::Color;using draw::ControlState;using draw::Icon;using draw::Painter;using draw::Plate;using draw::RectF;using draw::TextStyle;
RectF F(Rect r){return {float(r.x),float(r.y),float(r.w),float(r.h)};}
LARGE_INTEGER Now(){LARGE_INTEGER t;QueryPerformanceCounter(&t);return t;}
double Ms(LARGE_INTEGER a,LARGE_INTEGER b){LARGE_INTEGER f;QueryPerformanceFrequency(&f);return double(b.QuadPart-a.QuadPart)*1000.0/double(f.QuadPart);}

// The worker's painter and the widths it measured (Build measures many words
// a frame; each DirectWrite layout costs, and the answers never change).
struct Worker {
    Painter painter;
    std::unordered_map<std::string,float> widths;
    float Width(std::string_view utf8,TextStyle style){
        std::string key;key.reserve(utf8.size()+1);key.push_back(char('0'+int(style)));key.append(utf8);
        if(auto it=widths.find(key);it!=widths.end())return it->second;
        if(widths.size()>8192)widths.clear();
        const float w=painter.Measure(utf8,style,1e6f).width;
        widths.emplace(std::move(key),w);return w;
    }
};
// One painter per thread: created on the thread's first page, used and
// destroyed there (COM and DirectWrite stay on that thread).
Worker& ThisWorker(){thread_local Worker worker;return worker;}
float MeasureWidth(const void* context,std::string_view utf8,TextStyle style){return static_cast<Worker*>(const_cast<void*>(context))->Width(utf8,style);}

ControlState StateOf(const Element& e){
    ControlState s;
    s.pointed=e.Has(Hovered);s.pressed=e.Has(Pressed);s.chosen=e.Has(Selected);s.locked=e.Has(Locked);
    s.changed=e.Has(Changed);s.isDefault=e.Has(IsDefault);
    s.disabled=e.hit.kind!=Kind::None&&!e.Has(Enabled)&&!e.Has(Locked);
    return s;
}
// "Label: old -> new" as runs: the old value quiet, the new one bold orange
// (a comfort line red).
void ChangeSpans(Painter& p,const std::string& text,RectF box,bool comfort){
    const std::string_view all=text,arrow=Arrow;
    const Color head=comfort?Color::Comfort:Color::TipBody;
    // A long line steps down a size (Small, then Readout) so it never runs into the next column.
    TextStyle style=TextStyle::Value;
    for(TextStyle smaller:{TextStyle::Small,TextStyle::Readout})if(p.Measure(text,style,1e6f).width*1.04f>box.w)style=smaller;
    const size_t at=all.rfind(arrow);
    if(at==std::string_view::npos){draw::Span one{all,head,false};p.Spans(&one,1,style,box);return;}
    const size_t colon=all.rfind(": ",at);
    std::vector<draw::Span> spans;
    if(colon!=std::string_view::npos){spans.push_back({all.substr(0,colon+2),head,false});spans.push_back({all.substr(colon+2,at-colon-2),Color::Quiet,false});}
    else spans.push_back({all.substr(0,at),head,false});
    spans.push_back({arrow,Color::Quiet,false});
    spans.push_back({all.substr(at+arrow.size()),comfort?Color::Comfort:Color::Changed,true});
    p.Spans(spans.data(),spans.size(),style,box);
}
// The card's state line: Value, else Small, else Readout, the first that fits w with its icon (IconText's icon and gap: 1.2 em).
TextStyle StateStyle(std::string_view text,bool icon,float w){
    for(TextStyle s:{TextStyle::Value,TextStyle::Small})if(ThisWorker().Width(text,s)+(icon?draw::FontSize(s)*1.2f:0.f)<=w)return s;
    return TextStyle::Readout;
}
Color TitleColor(uint16_t flags){
    if(flags&Changed)return Color::Changed;
    if(flags&Locked)return Color::Dim;
    if(flags&Warn)return Color::Danger;
    if(flags&Selected)return Color::Gold;
    return Color::TipBody;
}
// Body calibration: plain stone, the steps, the hint and the hold in one box.
void Calibration(Painter& p,const Layout& l){
    const RectF all{0,0,float(Width),float(Height)};
    p.Fill(all,Color::Stone);p.Stone(all,Color::Stone,.3f);
    const Element* hint=nullptr;const Element* bar=nullptr;
    for(const auto& e:l.elements){if(e.role==Role::CalibHint)hint=&e;if(e.role==Role::CalibProgress)bar=&e;}
    if(hint){
        const float bottom=bar?float(bar->rect.y+bar->rect.h+20):float(hint->rect.y+hint->rect.h);
        const RectF box{float(hint->rect.x),float(hint->rect.y),float(hint->rect.w),bottom-float(hint->rect.y)};
        p.Fill(box,Color::Slab);p.Outline(box,Color::Bronze,2);
    }
    for(const auto& e:l.elements){
        const RectF r=F(e.rect);
        switch(e.role){
            case Role::PageTitle:p.TextFit(e.text,TextStyle::Title,r,Color::Gold,Align::Center);break;
            case Role::CalibStep:p.TextFit(e.text,TextStyle::Strong,{r.x+30,r.y,r.w-30,r.h},Color::Text,Align::Leading,true);break;
            case Role::CalibHint:{   // centered in the box above the hold's bar
                const RectF box{r.x+40,r.y+8,r.w-80,r.h-16};
                const float h=std::min(box.h,p.Measure(e.text,TextStyle::Strong,box.w,true).height);
                p.TextFit(e.text,TextStyle::Strong,{box.x,box.y+(box.h-h)/2,box.w,box.h-(box.h-h)/2},Color::Magic,Align::Center,true);
                break;}
            case Role::CalibProgress:p.Bar(r,e.fill,-1,{});break;
            case Role::CalibNote:p.TextFit(e.text,TextStyle::Value,r,Color::Quiet,Align::Center,true);break;
            default:break;
        }
    }
}
// Every element of a settings, preset, guide or notes view, back to front.
void Draw(Painter& p,const Layout& l,bool monoAudio){
    p.Panel();
    // Lists and swatches step with chevrons: their value box takes no press, or opens the popover.
    std::vector<bool> chevrons(size_t(Count)+1,false);
    for(const auto& e:l.elements)if(e.role==Role::Value&&(e.hit.kind==Kind::None||e.hit.kind==Kind::Popover)&&e.key>=0&&e.key<Count)chevrons[size_t(e.key)]=true;
    for(size_t i=0;i<l.elements.size();++i){
        const Element& e=l.elements[i];const RectF r=F(e.rect);const ControlState s=StateOf(e);
        switch(e.role){
            case Role::Tab:p.Tab(r,e.icon,e.text,s);break;
            case Role::GroupLine:p.Fill(r,Color::CarveLight,.35f);break;
            case Role::PageTitle:{
                p.PageTitle(r,e.icon,e.text);
                // The faint sigil between the title and the links, when it has room.
                const float sigil=float(GuideLink.x)-92;
                if(!monoAudio&&r.Right()+16<=sigil)p.Sigil({sigil,float(BandY+BandInkH/2-28),56,56});
                break;}
            // The links keep 64 px hit areas; they are drawn in the band's top 50 px with the title.
            case Role::HeaderLink:p.HeaderLink({r.x,r.y,r.w,float(BandInkH)},e.icon,e.text,s);break;
            case Role::Warning:{ControlState w;w.pointed=e.Has(Hovered);p.Pill({r.x,float(BandY+BandInkH/2-18),r.w,36},e.text,Color::Warn,Icon::Warning,w);break;}
            case Role::Section:p.SectionHeading(r,e.text);break;
            case Role::Row:{
                // The carved line ends a row unless a section heading follows.
                bool divider=true;
                for(size_t j=i+1;j<l.elements.size();++j){
                    const Role next=l.elements[j].role;
                    if(next==Role::Section){divider=false;break;}
                    if(next==Role::Row||next==Role::ReadoutBox||next==Role::HelpCard)break;
                }
                p.Row(r,e.Has(Hovered),divider);
                break;}
            case Role::Label:{
                if(e.text.empty()){   // a preset's state tag ("Applied", "Picked. Not applied", "Edited: ...")
                    if(!e.sub.empty())p.Pill(r,e.sub,e.Has(Changed)?Color::Changed:Color::Gold);
                    break;
                }
                const bool warn=e.Has(Warn)&&!e.Has(Locked);
                const Element* row=i>0&&l.elements[i-1].role==Role::Row?&l.elements[i-1]:nullptr;
                p.Label(r,e.text,e.sub,s,warn?Icon::Warning:Icon::None,warn?Color::Warn:Color::Quiet);
                if(e.Has(Recommended)&&row&&!row->sub.empty()){
                    // "Recommended: On" after the label on its line (a tag), past the ember when changed;
                    // Painter::Label centers a 38 px label line (+ 2 + 32 with a sub line) in the row.
                    const float block=e.sub.empty()?38.f:72.f,top=r.y+(r.h-block)/2;
                    const float x=r.x+ThisWorker().Width(e.text,TextStyle::Label)+(e.Has(Changed)?56.f:16.f);
                    const float w=std::min(r.Right()-x,ThisWorker().Width(row->sub,TextStyle::Small)+28);
                    if(w>40)p.Pill({x,top+2,w,34},row->sub,Color::Quiet);
                }
                break;}
            case Role::Chip:p.Socket(r,e.text,s);break;
            case Role::StepMinus:case Role::StepPlus:p.StepButton(r,e.role==Role::StepPlus,s,e.key>=0&&e.key<Count&&chevrons[size_t(e.key)]);break;
            case Role::Value:p.ValueBox(r,e.text,e.sub,s,e.fill,e.mark,e.zeroAt,e.hue);break;
            case Role::Toggle:p.Toggle(r,e.Has(Selected),s);break;
            case Role::Button:case Role::FooterResetPage:case Role::FooterClose:case Role::ConfirmKeep:p.Button(r,e.text,e.icon,s);break;
            case Role::FooterApply:case Role::ConfirmApply:{
                ControlState a=s;a.changed=false;   // the count says it; no ember on the plate
                p.Button(r,e.text,e.icon,a,e.Has(Changed)||e.role==Role::ConfirmApply?Plate::Primary:Plate::Socket);break;}
            case Role::ConfirmDiscard:p.Button(r,e.text,e.icon,s,Plate::Danger);break;
            case Role::DrainBar:p.DrainBar(r,e.fill,Color::Changed);break;
            case Role::StatusStrip:p.TextFit(e.text,TextStyle::Value,r,e.Has(Warn)?Color::Changed:e.text=="Saved."?Color::Ok:Color::Quiet,Align::Center);break;
            case Role::HelpCard:p.Card(r);break;
            case Role::CardTitle:p.TextFit(e.text,TextStyle::CardTitle,r,TitleColor(e.flags),Align::Center);break;
            case Role::CardText:p.TextFit(e.text,TextStyle::Help,{r.x,r.y,r.w,r.h+4},Color::TipBody,Align::Center,true);break;
            case Role::CardState:{
                // Steps down a size (Small, then Readout) when the line is wider than its box (the button column).
                const Icon icon=e.Has(Changed)?Icon::Changed:e.Has(Locked)?Icon::Padlock:Icon::None;
                const TextStyle style=StateStyle(e.text,icon!=Icon::None,r.w);
                if(icon==Icon::Changed)p.IconText(r,icon,e.text,style,Color::Changed,Color::Changed,Align::Center);
                else if(icon==Icon::Padlock)p.IconText(r,icon,e.text,style,Color::Quiet,Color::Quiet,Align::Center);
                else p.TextFit(e.text,style,r,Color::Quiet,Align::Center);
                break;}
            case Role::CardButton:p.Button(r,e.text,e.icon,s,Plate::Socket,r.w<200?26.f:28.f);break;
            case Role::PendingEntry:
                {ControlState plain=s;plain.changed=false;p.Socket(r,"",plain);}   // the orange new value says it
                ChangeSpans(p,e.text,{r.x+16,r.y+(s.pressed?2.f:0.f),r.w-32,r.h},false);
                break;
            case Role::ChangeLine:ChangeSpans(p,e.text,r,e.Has(Warn));break;
            case Role::PresetCard:p.PresetCard(r,e.icon,e.text,e.sub,s);break;
            case Role::GuideLine:
                p.TextFit(e.text,TextStyle::Value,{r.x+12,r.y,r.w-24,32},Color::Magic);
                p.TextFit(e.sub,TextStyle::Value,{r.x+12,r.y+31,r.w-24,r.h-31},Color::Text);
                p.Divider(r.x,r.Right(),r.Bottom()+1);
                break;
            case Role::NotesText:
                if(e.help==HelpGuide)p.TextFit(e.text,TextStyle::Value,r,Color::Quiet,Align::Leading,true);   // the game menus line
                else if(e.Has(Selected))p.TextFit(e.text,TextStyle::Value,r,Color::Magic);                     // the project link
                else if(e.option==0)p.TextFit(e.text,TextStyle::Value,r,Color::Quiet,Align::Center);           // What's new's "Page n of m"
                else if(e.text.rfind("Credits: ",0)==0||e.rect.w<ContentW)p.TextFit(e.text,TextStyle::Value,r,Color::Quiet);
                else{   // a release line
                    p.TextFit(e.text,TextStyle::Value,{r.x+12,r.y,r.w-24,r.h},Color::Text);
                    p.Divider(r.x,r.Right(),r.Bottom()+1);
                }
                break;
            case Role::ReadoutBox:p.ReadoutBox(r);break;
            case Role::Readout:p.TextFit(e.text,TextStyle::Readout,r,e.Has(Warn)?Color::Warn:e.Has(Selected)?Color::Magic:Color::Quiet);break;
            case Role::Intro:p.TextFit(e.text,TextStyle::Value,r,Color::Quiet);break;
            // The popover: the panel dimmed 40% (a fixed overlay), a card with the row's label, its options as chips, the hint.
            case Role::PopoverDim:p.Fill(r,Color::Ink,.4f);break;
            case Role::Popover:p.Fill(r,Color::Stone);p.Card(r);p.Outline(r,Color::Bronze,2.f);break;   // opaque: nothing shows through
            case Role::PopoverTitle:p.TextFit(e.text,TextStyle::CardTitle,r,Color::Gold,Align::Center);break;
            case Role::PopoverOption:   // a color grid's special value (By range, Off) carries its swatch at the left
                p.Socket(r,e.text,s);
                if(e.hue!=draw::SwatchNone)p.Swatch({r.x+18,r.y+r.h/2-15,30,30},e.hue,s);
                break;
            case Role::ColorDot:p.Dot(r,e.hue,s);break;
            case Role::PopoverHint:p.TextFit(e.text,TextStyle::Help,r,Color::TipBody,Align::Center,true);break;
            default:break;   // unused roles (Slider, Swatch, FooterRecenter, PresetLine) and calibration's
        }
    }
}
}

PagePicture PaintPage(const Model& model,const layout::LayoutInput& given,const layout::Tables* tables){
    PagePicture out;
    Worker& w=ThisWorker();
    if(!w.painter.Ready()){out.error=w.painter.Error();return out;}
    layout::LayoutInput input=given;
    if(!input.notes.title)input.notes=ReleaseNotesView();
    const auto t0=Now();
    out.layout=layout::Build(model,model.context,input,layout::TextMeasure{&MeasureWidth,&w},tables?*tables:layout::ContentTables());
    const auto t1=Now();
    if(w.painter.Begin()){
        if(input.calibration.active)Calibration(w.painter,out.layout);
        else Draw(w.painter,out.layout,input.monoAudio);
        out.pixels=w.painter.End();
    }
    if(out.pixels.empty())out.error=w.painter.Error();
    out.buildMs=Ms(t0,t1);out.drawMs=Ms(t1,Now());
    return out;
}
std::vector<std::string> FitProblems(const layout::Layout& l){
    std::vector<std::string> out;
    Worker& w=ThisWorker();Painter& p=w.painter;
    if(!p.Ready()){out.push_back("painter: "+p.Error());return out;}
    auto fail=[&](const Element& e,const char* what,const std::string& text){
        char where[96];std::snprintf(where,sizeof where,"role %d at %d,%d %dx%d: ",int(e.role),e.rect.x,e.rect.y,e.rect.w,e.rect.h);
        out.push_back(std::string(where)+what+": "+text);
    };
    // One line at its style's size within `room` (the painter draws it unshrunk).
    auto line=[&](const Element& e,std::string_view text,TextStyle style,float room,const char* what){
        if(!text.empty()&&w.Width(text,style)>room+.5f)fail(e,what,std::string(text));
    };
    // One line the painter shrinks to fit (TextFit): it must fit by 22 px.
    auto shrinks=[&](const Element& e,std::string_view text,TextStyle style,RectF box,bool wrap,const char* what){
        if(!text.empty()&&!p.FitSize(text,style,box,wrap).fits)fail(e,what,std::string(text));
    };
    for(size_t i=0;i<l.elements.size();++i){
        const Element& e=l.elements[i];const RectF r=F(e.rect);
        switch(e.role){
            case Role::Tab:line(e,e.text,TextStyle::Label,r.w-76,"tab name");break;   // Painter::Tab draws at 34 px; Label is 36
            case Role::Section:line(e,e.text,TextStyle::Section,r.w,"section heading");break;
            case Role::Label:{
                if(e.text.empty()){line(e,e.sub,TextStyle::Small,r.w-16,"preset tag");break;}
                const float label=w.Width(e.text,TextStyle::Label)+(e.Has(layout::Changed)?46.f:0.f);
                if(label>r.w+.5f)fail(e,"row label",e.text);
                line(e,e.sub,TextStyle::Value,r.w-(e.Has(Warn)||e.Has(Locked)?34.f:0.f),"row sub line");
                const Element* row=i>0&&l.elements[i-1].role==Role::Row?&l.elements[i-1]:nullptr;
                if(e.Has(Recommended)&&row&&!row->sub.empty()&&label+(e.Has(layout::Changed)?10.f:16.f)+w.Width(row->sub,TextStyle::Small)+28>r.w+.5f)fail(e,"recommended pill",row->sub);
                break;}
            case Role::Chip:line(e,e.text,e.Has(Selected)?TextStyle::Strong:TextStyle::Value,r.w-16,"chip");break;
            case Role::Value:
                line(e,e.text,TextStyle::Strong,r.w-24-(e.Has(Locked)?29.f:0.f)-(e.hue>-1.5f||e.hue==draw::SwatchByRange?44.f:0.f),"value");
                if(e.fill<0)line(e,e.sub,TextStyle::Small,r.w-12,"value sub line");
                break;
            case Role::Button:case Role::FooterResetPage:case Role::FooterClose:case Role::FooterApply:case Role::ConfirmDiscard:case Role::ConfirmKeep:case Role::ConfirmApply:
                line(e,e.text,TextStyle::Strong,r.w-(e.icon!=Icon::None?72.f:24.f),"button");break;
            case Role::CardButton:
                if(!e.text.empty()&&p.Measure(e.text,TextStyle::Value,1e6f,false,0,r.w<200?26.f:28.f).width>r.w-(r.w>=200?72.f:24.f)+.5f)fail(e,"card button",e.text);
                break;
            case Role::CardTitle:shrinks(e,e.text,TextStyle::CardTitle,r,false,"card title");break;
            case Role::CardText:shrinks(e,e.text,TextStyle::Help,{r.x,r.y,r.w,r.h+4},true,"card help");break;
            case Role::CardState:{
                const bool icon=e.Has(layout::Changed)||e.Has(Locked);const TextStyle style=StateStyle(e.text,icon,r.w);
                line(e,e.text,style,r.w-(icon?draw::FontSize(style)*1.2f:0.f),"card state");break;}
            case Role::PendingEntry:line(e,e.text,TextStyle::Readout,(r.w-32)/1.04f,"pending entry");break;   // ChangeSpans steps down to Readout
            case Role::ChangeLine:line(e,e.text,TextStyle::Readout,r.w/1.04f,"change line");break;
            case Role::PresetCard:line(e,e.text,TextStyle::CardTitle,r.w-130,"preset name");break;
            case Role::GuideLine:shrinks(e,e.text,TextStyle::Value,{r.x+12,r.y,r.w-24,32},false,"guide control");shrinks(e,e.sub,TextStyle::Value,{r.x+12,r.y+31,r.w-24,r.h-31},false,"guide action");break;
            case Role::NotesText:shrinks(e,e.text,TextStyle::Value,e.help==HelpGuide?r:RectF{r.x+12,r.y,r.w-24,r.h},e.help==HelpGuide,"notes line");break;
            case Role::Readout:shrinks(e,e.text,TextStyle::Readout,r,false,"readout");break;
            case Role::Intro:shrinks(e,e.text,TextStyle::Value,r,false,"intro");break;
            case Role::PopoverTitle:shrinks(e,e.text,TextStyle::CardTitle,r,false,"popover title");break;
            case Role::PopoverOption:line(e,e.text,e.Has(Selected)?TextStyle::Strong:TextStyle::Value,r.w-16,"popover option");break;
            case Role::PopoverHint:shrinks(e,e.text,TextStyle::Help,r,true,"popover hint");break;
            case Role::StatusStrip:shrinks(e,e.text,TextStyle::Value,r,false,"status");break;
            default:break;
        }
    }
    return out;
}
layout::Layout MeasuredLayout(const Model& model,const layout::LayoutInput& given,const layout::Tables* tables){
    Worker& w=ThisWorker();
    layout::LayoutInput input=given;
    if(!input.notes.title)input.notes=ReleaseNotesView();
    return layout::Build(model,model.context,input,w.painter.Ready()?layout::TextMeasure{&MeasureWidth,&w}:layout::TextMeasure{},tables?*tables:layout::ContentTables());
}
layout::TextMeasure ThisThreadMeasure(){Worker& w=ThisWorker();return w.painter.Ready()?layout::TextMeasure{&MeasureWidth,&w}:layout::TextMeasure{};}
layout::CalibrationView CalibrationOf(const calibration::Flow& body){
    layout::CalibrationView v;
    v.active=body.Active();v.hint=int(body.hint);v.progress=body.progress;
    v.measured=body.phase==calibration::Phase::Saving||body.phase==calibration::Phase::Saved;
    const auto m=body.Get();v.eyeHeight=m.eyeHeight;v.armSpan=m.armSpan;
    return v;
}
layout::NotesView ReleaseNotesView(){
    layout::NotesView n;
    n.title=releasenotes::ReleaseTitle;n.author=releasenotes::Author;n.url=releasenotes::ProjectUrl;n.credits=releasenotes::Credits;
    n.lines=releasenotes::ReleaseLines.data();n.lineCount=int(releasenotes::ReleaseLines.size());
    return n;
}
int ReleaseNotesPages(){return NotesPages(int(releasenotes::ReleaseLines.size()));}

// ---- the preview set ----
std::vector<PreviewShot> PreviewShots(){
    using layout::LayoutInput;
    std::vector<PreviewShot> shots;
    constexpr uint64_t now=100000;
    const controllers::Labels touch{{controllers::Layout::Touch,controllers::Layout::Touch}};
    struct Setup {Model m;LayoutInput in;};
    auto base=[&](View v,int heroClass,Snapshot s=Snapshot{})->Setup{
        Setup x;s.ready=true;s.offlineDifficultyAvailable=true;
        x.m.Open(s);x.m.SetNotesPages(ReleaseNotesPages());x.m.SetContext({heroClass,heroClass==heroclass::Amazon,heroClass==heroclass::Sorceress,false});x.m.view=v;
        x.in.nowMs=now;x.in.detected=touch;
        x.in.runtimeStatus=v==View::Performance?"VR source: 2560 x 2560 per eye":"Foveated shading available. Eye tracking unavailable on this headset/runtime.";   // as transport.cpp
        return x;
    };
    auto add=[&](const std::string& name,const Setup& x,const layout::Tables* tables=nullptr){
        PreviewShot shot;shot.name=name;
        shot.picture=PaintPage(x.m,x.in,tables);
        const auto t=Now();const PagePicture warm=PaintPage(x.m,x.in,tables);shot.warmMs=Ms(t,Now());
        shots.push_back(std::move(shot));
    };
    static constexpr const char* names[]={"presets","body","combat","aim","targets","shield","wrist","interface","survival","world","vibration","performance","guide","whats-new","more"};
    static_assert(std::size(names)==size_t(View::Count));
    // Every view with no character (class tags shown).
    for(int v=0;v<int(View::Count);++v){
        Setup x=base(View(v),-1);
        if(View(v)==View::Performance)x.in.performance={true,true,90,11.1f,3.4f,2.6f,4.1f,.9f};
        add(names[v],x);
    }
    // A few classes on the pages that change with the character.
    const std::pair<int,const char*> classes[]={{heroclass::Amazon,"amazon"},{heroclass::Sorceress,"sorceress"},{heroclass::Paladin,"paladin"},{heroclass::Barbarian,"barbarian"}};
    for(const auto& [hero,cls]:classes)for(View v:{View::Aim,View::Targets,View::Combat,View::Shield,View::Presets})add(std::string(names[int(v)])+"-"+cls,base(v,hero));
    // States.
    {Snapshot s;s.values[ShieldRoll]=90;Setup x=base(View::Shield,-1,s);x.m.Set(ShieldRoll,45);
     x.in.hover={Kind::Change,ShieldRoll,1};x.in.card=ShieldRoll;add("shield-dirty",x);
     Setup y=x;y.in.hover={};y.in.card=NoHelp;y.m.confirmClose=true;y.m.confirmAt=now-1000;add("shield-confirm",y);
     Setup z=base(View::Shield,-1,s);z.m.Set(ShieldRoll,45);z.m.undoResetUntil=now+3000;z.m.undoResetAt=now-3000;add("shield-undoreset",z);}
    {Setup x=base(View::Body,-1);x.m.Set(Seated,1);x.m.result=Result::Failed;add("body-save-failed",x);
     Setup y=base(View::Body,-1);y.m.result=Result::Saved;y.in.hover={Kind::Calibrate};y.in.card=HelpCalibrate;add("body-saved-calibrate",y);}
    {Setup x=base(View::Aim,heroclass::Sorceress);x.in.monoAudio=true;x.in.hover={Kind::Set,ProjectileAimAssist,0,1};x.in.card=ProjectileAimAssist;add("aim-mono-pointed",x);
     Setup y=base(View::Aim,-1);y.m.ready=false;add("aim-unavailable",y);}
    {Snapshot s;s.values[DeferGpuWait]=0;Setup x=base(View::Performance,-1,s);x.in.game={true,true};
     x.in.performance={true,true,52,11.1f,1.0f,7.0f,9.4f,1.5f};add("performance-warnings",x);}
    {Setup x=base(View::Presets,-1);x.in.hover={Kind::Preset,0};x.in.card=HelpPresetFirst;add("presets-pointed",x);}
    // Presets off (content::PresetsReady): the rail's tab grayed out, its card saying they come later.
    {Setup x=base(View::Body,-1);x.in.hover={Kind::Open,int(View::Presets),-1};x.in.card=HelpTabFirst+int(View::Presets);add("presets-grayed",x);}
    {   // A long release (30 lines, three pages): the middle page with Next pointed, and the last page.
        static std::array<std::string,30> text;static std::array<const char*,30> lines;
        for(size_t i=0;i<text.size();++i){text[i]="Release line "+std::to_string(i+1)+": a change in this version, one line long";lines[i]=text[i].c_str();}
        Setup x=base(View::WhatsNew,-1);x.in.notes=ReleaseNotesView();x.in.notes.lines=lines.data();x.in.notes.lineCount=int(lines.size());
        x.m.SetNotesPages(NotesPages(x.in.notes.lineCount));x.m.notesPage=1;x.in.hover={Kind::NotesPage,0,1};add("whats-new-paged",x);
        x.m.notesPage=x.m.notesPages-1;x.in.hover={};add("whats-new-last-page",x);
    }
    {Snapshot s;s.values[MapTopDown]=2;Setup x=base(View::Wrist,-1,s);x.in.hover={Kind::Set,MapTopDown,0,2};x.in.card=MapTopDown;add("wrist-full-map",x);}
    {   // Popovers: a named list too long for chips, open over its page (options armed, one pointed).
        Setup x=base(View::Performance,-1);x.m.popoverKey=RenderResolution;x.m.popoverAt=now-1000;
        x.in.hover={Kind::Set,RenderResolution,0,3};x.in.card=RenderResolution;add("popover-list",x);
        Setup a=base(View::Aim,heroclass::Amazon);a.m.popoverKey=SpellLock;a.m.popoverAt=now-1000;a.in.card=SpellLock;add("popover-aim-amazon",a);
        Setup y=base(View::Performance,-1);y.in.hover={Kind::Popover,RenderResolution};y.in.card=RenderResolution;add("popover-face-pointed",y);
        // The color grid: a hand's outline color (Azure pointed), and the arrow path line's.
        Snapshot s;s.values[TargetHighlight]=60;s.values[HighlightRight]=45;
        Setup g=base(View::Targets,-1,s);g.m.popoverKey=HighlightRight;g.m.popoverAt=now-1000;g.in.hover={Kind::Set,HighlightRight,0,210};g.in.card=HighlightRight;add("color-grid",g);
        Setup l=base(View::Targets,heroclass::Amazon,s);l.m.popoverKey=BowLaser;l.m.popoverAt=now-1000;l.in.card=BowLaser;add("color-grid-arrow-path",l);
        Setup b=base(View::Targets,-1,s);b.m.popoverKey=HighlightLeft;b.m.popoverAt=now-1000;b.in.hover={Kind::Set,HighlightLeft,0,0};b.in.card=HighlightLeft;add("color-grid-by-range",b);
    }
    {   // Frame generation (main's #313): its row on Performance; NVIDIA's quality and scale on More while NVIDIA is chosen.
        Snapshot s;s.values[FrameGeneration]=2;
        Setup p=base(View::Performance,-1,s);p.in.performance={true,true,90,11.1f,3.4f,2.6f,4.1f,.9f};
        p.in.frameGen={framegen::State::Active,1,1,2,1,1,0,0,0};p.in.card=FrameGeneration;add("performance-frame-generation",p);
        add("more-frame-generation-nvidia",base(View::More,-1,s));
    }
    {   // The fallback (content::FallbackDemo): settings with no override, on their owner page's New section or on More.
        static const layout::Tables demo=layout::TablesOf(content::FallbackDemo());
        add("fallback-world",base(View::World,-1),&demo);
        add("fallback-performance",base(View::Performance,-1),&demo);
        Setup more=base(View::More,-1);add("fallback-more",more,&demo);
        more.m.morePage=1;more.in.card=MapTopDown;add("fallback-more-2",more,&demo);
        Setup mono=base(View::Body,-1);mono.in.monoAudio=true;add("fallback-band-mono",mono,&demo);
    }
    {Snapshot s;s.values[Motion]=0;add("combat-recommended",base(View::Combat,-1,s));}   // Motion combat Off: the "Recommended: On" tag
    {Snapshot s;s.values[TargetHighlight]=60;s.values[HighlightRight]=0;s.values[HighlightLeft]=210;s.values[BowLaser]=45;
     Setup x=base(View::Targets,-1,s);x.in.card=HighlightRight;add("targets-colors",x);}                    // swatches: By range, a hue, the arrow path line
    {Setup x=base(View::Survival,-1);x.m.offlineDifficultyAvailable=false;x.in.card=OfflinePlayers;add("survival-no-adapter",x);}
    {Snapshot s;s.values[ShieldRoll]=90;Setup x=base(View::Shield,-1,s);x.in.hover={Kind::Change,ShieldRoll,0};x.in.pressed=x.in.hover;x.in.card=ShieldRoll;add("shield-drag",x);}
    {Setup x=base(View::Interface,-1);x.m.Set(MenuDistance,0);x.m.Set(TooltipTiming,0);x.in.card=HelpPending;add("interface-pending",x);}
    {Setup x=base(View::Body,-1);calibration::Flow f;f.Begin();f.phase=calibration::Phase::Hold;f.hint=calibration::Hint::Arms;f.progress=.55f;
     x.in.calibration=CalibrationOf(f);add("calibration",x);
     x.in.calibration.hint=int(calibration::Hint::Saved);x.in.calibration.progress=1;x.in.calibration.measured=true;
     x.in.calibration.eyeHeight=1.68f;x.in.calibration.armSpan=1.52f;add("calibration-saved",x);}
    return shots;
}
}
