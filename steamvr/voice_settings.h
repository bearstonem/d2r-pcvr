#pragma once
#include <array>
#include <string>
#include <vector>
#include <algorithm>
namespace framevr::voice {
struct Assignment {std::string phrase;int skill=-1;bool operator==(const Assignment&)const=default;};
struct Choice {int id=-1;std::string name;};
// UI data is copied to the settings painter; no handles or microphone on that thread.
struct Editor {
    std::array<Assignment,6> bindings{};
    std::vector<Choice> choices{{-1,"Unassigned"}};
    std::vector<std::string> microphones{"Windows default"};
    std::array<unsigned,6> samples{};
    std::array<Assignment,6> savedBindings{};int savedMode=0,savedMicrophone=-1,savedTolerance=2;
    int slot=0,sample=0,tolerance=2,microphone=-1,mode=0,pickerPage=0,level=0,activeMode=0,matchedSlot=-1;bool picker=false,signal=false;
    std::string lastResult="No sound detected yet";
    unsigned resultSerial=0;
    bool loaded=false,dirty=false;
    std::string status="Voice recognition off";
    std::string character;
    bool HasSample()const{return (samples[size_t(slot)]&(1u<<sample))!=0;}
    std::string NextStep()const{
        if(character.empty())return "Enter an offline game to set up this character's voice commands.";
        if(activeMode==3)return "Make your sound now, then pause. Recording stops automatically.";
        if(!samples[size_t(slot)])return "Record a short sound, then pause. One example is enough; two more are optional.";
        if(bindings[size_t(slot)].skill<0)return "Choose the skill for this sound. Each character has their own six bindings.";
        if(dirty)return "Save applies On/Off, skills, microphone and sensitivity. Recordings are saved immediately.";
        if(savedMode!=2)return "Repeat your sound to test it here. To cast in game, select On, then Save changes.";
        return "Repeat your sound and check Last match. Close settings to cast. No spells cast on this screen.";
    }
    bool Edit(int op,int value){
        auto& b=bindings[size_t(slot)];
        switch(op){
        case 0:slot=(slot+6+(value<0?-1:1))%6;return true;
        case 1:if(value>='a'&&value<='z'&&b.phrase.size()<60)b.phrase+=char(value);else if(value==' '&&!b.phrase.empty()&&b.phrase.back()!=' '&&b.phrase.size()<60)b.phrase+=' ';else return false;break;
        case 2:if(!b.phrase.empty())b.phrase.pop_back();break;
        case 3:b.phrase.clear();break;
        case 4:{auto it=std::find_if(choices.begin(),choices.end(),[&](const Choice& c){return c.id==b.skill;});int n=it==choices.end()?0:int(it-choices.begin());n=(n+int(choices.size())+(value<0?-1:1))%int(choices.size());b.skill=choices[size_t(n)].id;break;}
        case 5:mode=value?2:0;break;
        case 6:microphone=(microphone+2)%int(microphones.size())-1;break;
        case 7:sample=(sample+3+(value<0?-1:1))%3;return true;
        case 8:tolerance=(tolerance+1)%3;break;
        case 9:picker=!picker;pickerPage=0;return true;
        case 10:pickerPage=std::clamp(pickerPage+(value<0?-1:1),0,std::max(0,(int(choices.size())-1)/8));return true;
        case 11:if(value<0||value>=int(choices.size()))return false;b.skill=choices[size_t(value)].id;picker=false;break;
        case 12:mode=savedMode;microphone=savedMicrophone;tolerance=savedTolerance;bindings=savedBindings;dirty=false;return true;
        default:return false;
        }
        RefreshDirty();return true;
    }
    void RefreshDirty(){dirty=mode!=savedMode||microphone!=savedMicrophone||tolerance!=savedTolerance||bindings!=savedBindings;}
    void Commit(){savedMode=mode;savedMicrophone=microphone;savedTolerance=tolerance;savedBindings=bindings;dirty=false;}
    bool AllowsCast(int skill)const{return savedMode==2&&skill>=0&&std::any_of(savedBindings.begin(),savedBindings.end(),[&](const Assignment& b){return b.skill==skill;});}
    std::string SkillName()const{int id=bindings[size_t(slot)].skill;for(const auto& c:choices)if(c.id==id)return c.name;return "Unavailable skill "+std::to_string(id);}
};
}
