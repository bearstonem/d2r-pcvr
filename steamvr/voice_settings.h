#pragma once
#include <array>
#include <string>
#include <vector>
#include <algorithm>
namespace framevr::voice {
struct Assignment {std::string phrase;int skill=-1;};
struct Choice {int id=-1;std::string name;};
// UI data is copied to the settings painter; no handles or microphone on that thread.
struct Editor {
    std::array<Assignment,6> bindings{};
    std::vector<Choice> choices{{-1,"Unassigned"}};
    std::vector<std::string> microphones{"Windows default"};
    int slot=0,microphone=-1,mode=0;bool loaded=false,dirty=false;
    std::string status="Voice recognition off";
    bool Edit(int op,int value){
        auto& b=bindings[size_t(slot)];
        switch(op){
        case 0:slot=(slot+6+(value<0?-1:1))%6;return true;
        case 1:if(value>='a'&&value<='z'&&b.phrase.size()<60)b.phrase+=char(value);else if(value==' '&&!b.phrase.empty()&&b.phrase.back()!=' '&&b.phrase.size()<60)b.phrase+=' ';else return false;break;
        case 2:if(!b.phrase.empty())b.phrase.pop_back();break;
        case 3:b.phrase.clear();break;
        case 4:{auto it=std::find_if(choices.begin(),choices.end(),[&](const Choice& c){return c.id==b.skill;});int n=it==choices.end()?0:int(it-choices.begin());n=(n+int(choices.size())+(value<0?-1:1))%int(choices.size());b.skill=choices[size_t(n)].id;break;}
        case 5:mode=(mode+1)%3;break;
        case 6:microphone=(microphone+2)%int(microphones.size())-1;break;
        default:return false;
        }
        dirty=true;return true;
    }
    std::string SkillName()const{int id=bindings[size_t(slot)].skill;for(const auto& c:choices)if(c.id==id)return c.name;return "Unavailable skill "+std::to_string(id);}
};
}
