#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <conio.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <deque>
#include "voice_bridge.h"
using namespace framevr::voice;
struct Profile {
    Templates samples;
    std::array<int,Count> actions{-1,-1,3904,3906,-1,-1};
    float threshold=.20f,level=.012f;
};
std::filesystem::path ProfilePath(){
    wchar_t root[32768]{};const auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",root,32768);
    if(!n||n>=32768)throw std::runtime_error("LOCALAPPDATA is unavailable");
    auto dir=std::filesystem::path(root)/L"D2R-PCVR";std::filesystem::create_directories(dir);
    return dir/L"voice-prototype-v1.bin";
}
void Save(const Profile& p){
    auto path=ProfilePath(),temp=path;temp+=L".tmp";
    std::ofstream f(temp,std::ios::binary);uint32_t magic=Magic;f.write((char*)&magic,4);
    f.write((char*)p.actions.data(),sizeof(p.actions));f.write((char*)&p.threshold,4);f.write((char*)&p.level,4);
    for(const auto& group:p.samples){uint32_t count=uint32_t(group.size());f.write((char*)&count,4);
        for(const auto& pattern:group){uint32_t n=uint32_t(pattern.size());f.write((char*)&n,4);f.write((char*)pattern.data(),n*sizeof(Frame));}}
    f.close();if(!f||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot save voice profile");
}
Profile Load(){
    Profile p;std::ifstream f(ProfilePath(),std::ios::binary);if(!f)return p;
    uint32_t magic=0;f.read((char*)&magic,4);if(magic!=Magic)return {};
    f.read((char*)p.actions.data(),sizeof(p.actions));
    for(int id:p.actions)if(id< -1||id>4095)return {};
    f.read((char*)&p.threshold,4);f.read((char*)&p.level,4);
    if(!std::isfinite(p.threshold)||p.threshold<.02f||p.threshold>.5f||!std::isfinite(p.level)||p.level<.001f||p.level>.2f)return {};
    for(auto& group:p.samples){uint32_t count=0;f.read((char*)&count,4);if(count>5)return {};
        for(unsigned i=0;i<count;++i){uint32_t n=0;f.read((char*)&n,4);if(n<1||n>400)return {};
            Pattern pattern(n);f.read((char*)pattern.data(),n*sizeof(Frame));if(!f)return {};
            for(const auto& frame:pattern)for(float v:frame)if(!std::isfinite(v)||std::fabs(v)>2)return {};
            group.push_back(std::move(pattern));}}
    return p;
}
class Mic {
    HWAVEIN device=nullptr;
    struct Buffer {WAVEHDR h{};std::array<int16_t,320> pcm{};};
    std::array<Buffer,12> buffers{};size_t next=0;
public:
    explicit Mic(UINT id){
        WAVEFORMATEX fmt{WAVE_FORMAT_PCM,1,Rate,Rate*2,2,16,0};
        auto error=waveInOpen(&device,id,&fmt,0,0,CALLBACK_NULL);
        if(error)throw std::runtime_error("Cannot open microphone at 16 kHz. Check Windows microphone access and Virtual Desktop mic forwarding.");
        for(auto& b:buffers){b.h.lpData=(char*)b.pcm.data();b.h.dwBufferLength=DWORD(b.pcm.size()*2);
            if(waveInPrepareHeader(device,&b.h,sizeof b.h)||waveInAddBuffer(device,&b.h,sizeof b.h)){Close();throw std::runtime_error("Microphone buffer error");}}
        if(waveInStart(device)){Close();throw std::runtime_error("Microphone start failed");}
    }
    void Close(){if(!device)return;waveInReset(device);for(auto& b:buffers)if(b.h.dwFlags&WHDR_PREPARED)waveInUnprepareHeader(device,&b.h,sizeof b.h);waveInClose(device);device=nullptr;}
    ~Mic(){Close();}
    bool Read(std::vector<int16_t>& out){
        auto& b=buffers[next];if(!(b.h.dwFlags&WHDR_DONE))return false;
        out.assign(b.pcm.begin(),b.pcm.begin()+std::min<size_t>(b.pcm.size(),b.h.dwBytesRecorded/2));
        b.h.dwBytesRecorded=0;if(waveInAddBuffer(device,&b.h,sizeof b.h))throw std::runtime_error("Microphone disconnected");next=(next+1)%buffers.size();return true;
    }
};
void Devices(){for(UINT i=0;i<waveInGetNumDevs();++i){WAVEINCAPSA c{};if(!waveInGetDevCapsA(i,&c,sizeof c))std::cout<<i<<": "<<c.szPname<<"\n";}}
int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])=="--help"){
        std::cout<<"D2R VR local voice prototype. Run without arguments for microphone, training and action assignment. No network or raw-audio storage.\n";return 0;}
    try{
        HANDLE single=CreateMutexW(nullptr,FALSE,L"Local\\D2RVRVoiceTrainerSingleton1");
        if(!single||GetLastError()==ERROR_ALREADY_EXISTS){std::cerr<<"A voice trainer is already open.\n";if(single)CloseHandle(single);return 1;}
        Profile profile=Load();Bridge bridge;if(!bridge.Open(true))throw std::runtime_error("Voice bridge unavailable");
        bridge.With([](Shared& s){s.enabled=false;s.command={};s.helperAt=GetTickCount64();});
        UINT device=WAVE_MAPPER;uint64_t sequence=GetTickCount64()<<10;
        std::cout<<"LOCAL VOICE PROTOTYPE -- microphone opens only for train/test/live.\n"
          <<"Train each sound 3 times. Similar shouts may need changing. Test before live.\n"
          <<"Commands: devices | mic N | train N | assign N SKILL_ID | skills | list\n"
          <<"          test | live | threshold 0.20 | level 0.012 | quit\n"
          <<"Press any key in this window to stop recording/listening. Live needs D2R focused.\n";
        Devices();
        for(int i=0;i<Count;++i)std::cout<<i+1<<": "<<Names[i]<<"\n";
        for(std::string line;;){
            std::cout<<"> "<<std::flush;if(!std::getline(std::cin,line))break;
            std::istringstream in(line);std::string cmd;in>>cmd;
            if(cmd=="quit")break;
            if(cmd=="devices"){Devices();continue;}
            if(cmd=="mic"){unsigned n;if(in>>n&&n<waveInGetNumDevs())device=n;else std::cout<<"Invalid device.\n";continue;}
            if(cmd=="list"){
                for(int i=0;i<Count;++i)std::cout<<i+1<<" "<<Names[i]<<" ("<<profile.samples[i].size()<<" samples) -> "<<profile.actions[i]<<"\n";
                continue;
            }
            if(cmd=="skills"){
                bridge.With([](Shared& s){
                    if(GetTickCount64()<s.gameAt||GetTickCount64()-s.gameAt>2000){std::cout<<"Enter an offline VR game with the updated DLL.\n";return;}
                    for(unsigned i=0;i<std::min(s.count,140u);++i)std::cout<<s.skills[i].id<<" "<<s.skills[i].name<<"\n";
                });continue;
            }
            if(cmd=="assign"){int n,id;if(in>>n>>id&&n>=1&&n<=Count&&id>=-1&&id<=4095){profile.actions[n-1]=id;Save(profile);}else std::cout<<"Use assign 1 149 (Battle Orders), or -1 to unassign.\n";continue;}
            if(cmd=="threshold"||cmd=="level"){
                float v;if(in>>v&&std::isfinite(v)&&v>=(cmd=="threshold"?.02f:.001f)&&v<=(cmd=="threshold"?.5f:.2f)){
                    (cmd=="threshold"?profile.threshold:profile.level)=v;Save(profile);
                }else std::cout<<"Threshold range .02-.5; level range .001-.2.\n";continue;
            }
            if(cmd!="train"&&cmd!="test"&&cmd!="live")continue;
            int label=0;if(cmd=="train"&&(!(in>>label)||label<1||label>Count)){std::cout<<"train 1 through train 6\n";continue;}
            const bool live=cmd=="live",train=cmd=="train";std::vector<Pattern> learned;
            if(!train){int trained=0;for(const auto& group:profile.samples)if(group.size()>=3)++trained;
                if(trained<2){std::cout<<"Train at least two distinct sounds first (three samples each).\n";continue;}}

            Segmenter segment;Mic mic(device);uint64_t last=0,shownAck=0;
            std::cout<<(train?"Record 3 examples, pause between each. ":live?"LIVE -- focus D2R. ":"TEST -- no game actions. ")<<(train?Names[label-1]:"")<<"\n";
            while(!_kbhit()){
                const auto now=GetTickCount64();
                bridge.With([&](Shared& s){s.helperAt=now;s.enabled=live;if(s.ack&&s.ack!=shownAck){shownAck=s.ack;std::cout<<"Game: "<<s.status<<"\n";}});
                std::vector<int16_t> chunk;if(!mic.Read(chunk)){Sleep(5);continue;}
                auto utterance=segment.Feed(chunk,profile.level);if(utterance.empty())continue;
                auto pattern=Features(utterance);if(pattern.empty()){std::cout<<"Too short; repeat.\n";continue;}
                if(train){learned.push_back(std::move(pattern));std::cout<<learned.size()<<"/3 recorded\n";
                    if(learned.size()==3){profile.samples[label-1]=std::move(learned);Save(profile);break;}continue;}
                auto match=Recognize(pattern,profile.samples,profile.threshold);
                std::cout<<(match.label<0?"Rejected":Names[match.label])<<" score="<<match.best<<" next="<<match.second<<"\n";
                if(!live||match.label<0||(last&&now-last<1500)||profile.actions[match.label]<0)continue;
                bridge.With([&](Shared& s){
                    if(!s.ready||!s.session||now<s.gameAt||now-s.gameAt>500){std::cout<<"Game not ready; dropped.\n";return;}
                    s.command={++sequence,now,s.session,profile.actions[match.label]};last=now;
                });
            }
            if(_kbhit())while(_kbhit())_getch();
            bridge.With([](Shared& s){s.enabled=false;s.command={};});
            std::cout<<"Microphone stopped.\n";
        }
        bridge.With([](Shared& s){s.enabled=false;s.command={};});CloseHandle(single);
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
