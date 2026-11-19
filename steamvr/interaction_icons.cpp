#include <windows.h>
#include <wincodec.h>
#include "interaction_icons.h"
#include "logging.h"
#include <atomic>
#include <cstdio>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace framevr::interaction::icons {
namespace {
std::mutex mutex;
std::condition_variable wake,finished;
std::thread worker;
bool stop=false,busy=false;
std::wstring directory;
std::deque<std::string> pending;
std::unordered_map<std::string,std::shared_ptr<const Image>> cache;
std::atomic<uint64_t> revision{0};
// The first frame, its long side at most 160 px, as straight RGBA. The caller keeps and releases decoder.
std::shared_ptr<const Image> DecodeFrame(IWICImagingFactory* wic,IWICBitmapDecoder* decoder) {
    IWICBitmapFrameDecode* frame=nullptr;
    IWICBitmapScaler* scaler=nullptr;IWICFormatConverter* converter=nullptr;
    auto result=std::make_shared<Image>();UINT w=0,h=0;
    if(wic&&decoder&&
       SUCCEEDED(decoder->GetFrame(0,&frame))&&SUCCEEDED(frame->GetSize(&w,&h))&&w&&h&&w<=8192&&h<=8192&&
       SUCCEEDED(wic->CreateBitmapScaler(&scaler))) {
        const float scale=std::min(1.f,160.f/std::max(w,h));
        result->width=std::max(1u,unsigned(w*scale));result->height=std::max(1u,unsigned(h*scale));
        if(SUCCEEDED(scaler->Initialize(frame,result->width,result->height,WICBitmapInterpolationModeFant))&&
           SUCCEEDED(wic->CreateFormatConverter(&converter))&&
           SUCCEEDED(converter->Initialize(scaler,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) {
            result->rgba.resize(result->width*result->height*4);
            if(FAILED(converter->CopyPixels(nullptr,result->width*4,UINT(result->rgba.size()),result->rgba.data())))result->rgba.clear();
        }
    }
    for(IUnknown* p:{static_cast<IUnknown*>(converter),static_cast<IUnknown*>(scaler),static_cast<IUnknown*>(frame)})if(p)p->Release();
    return result->rgba.empty()?nullptr:result;
}
std::shared_ptr<const Image> Decode(IWICImagingFactory* wic,const std::filesystem::path& path) {
    IWICBitmapDecoder* decoder=nullptr;
    if(!wic||FAILED(wic->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)))return nullptr;
    auto result=DecodeFrame(wic,decoder);decoder->Release();return result;
}
// Generic skill 1, Kick: the game's own icon is an empty plate, so the wheels
// draw new art embedded with the plugin (assets/kick-icon.png) for this key.
// Every other key, and Kick if that art is missing, reads the local cache.
constexpr const char* KickKey="s1";
std::atomic<BuiltIn> builtIn{nullptr};
std::shared_ptr<const Image> DecodeKick(IWICImagingFactory* wic) {
    HMODULE module=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Revision),&module);
    const HRSRC found=module?FindResourceW(module,L"D2RVR_KICK_ICON",MAKEINTRESOURCEW(10)):nullptr;
    const DWORD bytes=found?SizeofResource(module,found):0;
    auto* data=found?static_cast<BYTE*>(LockResource(LoadResource(module,found))):nullptr;
    IWICStream* stream=nullptr;IWICBitmapDecoder* decoder=nullptr;std::shared_ptr<const Image> result;
    if(wic&&bytes&&data&&SUCCEEDED(wic->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromMemory(data,bytes))&&
       SUCCEEDED(wic->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,&decoder)))result=DecodeFrame(wic,decoder);
    if(decoder)decoder->Release();if(stream)stream->Release();
    char line[160];
    if(result)std::snprintf(line,sizeof line,"Frame kick icon: skill 1 (Kick) uses the built-in art, %ux%u, in place of the game's empty plate",result->width,result->height);
    else std::snprintf(line,sizeof line,"Frame kick icon: the built-in art is %s; skill 1 (Kick) uses the local icon cache",found?"unreadable":"missing");
    framevr::logging::Write(line);
    return result;
}
void Run(std::wstring location) {
    const auto co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IWICImagingFactory* wic=nullptr;
    CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));
    std::unordered_map<std::string,std::string> files;
    try {
        std::ifstream input(std::filesystem::path(location)/L"manifest.tsv");std::string line;
        while(files.size()<8192&&std::getline(input,line)) {
            const auto tab=line.find('\t');if(tab==std::string::npos)continue;
            const auto key=line.substr(0,tab),file=line.substr(tab+1);
            if(key.size()>32||file.size()!=68||file.substr(64)!=".png"||
               file.substr(0,64).find_first_not_of("0123456789abcdef")!=std::string::npos)continue;
            files[key]=file;
        }
    } catch(...) {} // Unavailable/malformed local cache leaves readable labels.
    for(;;) {
        std::string key;
        {std::unique_lock lock(mutex);wake.wait(lock,[]{return stop||!pending.empty();});if(stop)break;
         key=std::move(pending.front());pending.pop_front();busy=true;}
        std::shared_ptr<const Image> image;
        try {
            if(key==KickKey)image=DecodeKick(wic);
            if(!image)if(const auto made=builtIn.load(std::memory_order_acquire))image=made(key,wic);
            auto it=files.find(key);if(!image&&it!=files.end())image=Decode(wic,std::filesystem::path(location)/it->second);
        }catch(...){}
        {std::lock_guard lock(mutex);cache[key]=std::move(image);++revision;busy=false;finished.notify_all();}
    }
    if(wic)wic->Release();if(SUCCEEDED(co))CoUninitialize();
}
}
std::shared_ptr<const Image> Get(const std::string& rawKey) {
    if(rawKey.empty())return {};
    std::string key=rawKey;
    if(key.size()==5&&key[0]=='s'&&key[1]=='3'&&key[2]=='9'&&key[3]=='0'&&key[4]>='1'&&key[4]<='7') {
        const int cmdId=3900+(key[4]-'0');
        const int iconId=commander::CommanderSkillIconId(cmdId);
        if(iconId>=0)key="s"+std::to_string(iconId);
    }
    std::lock_guard lock(mutex);
    if(auto it=cache.find(key);it!=cache.end())return it->second;
    if(cache.size()>=2048)return {};
    if(!worker.joinable()) {
        if(directory.empty()) {
            HMODULE module=nullptr;wchar_t path[32768]{};
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Revision),&module);
            GetModuleFileNameW(module,path,32768);
            directory=(std::filesystem::path(path).parent_path()/L"d2r_vr_icons").wstring();
        }
        stop=false;worker=std::thread(Run,directory);
    }
    cache.emplace(key,nullptr);pending.push_back(key);wake.notify_one();return {};
}
std::shared_ptr<const Image> Get(Item item) {
    const auto key=ItemKey(item);item.quality=0;const auto base=ItemKey(item);
    if(!key.empty()&&key!=base) {
        if(auto variant=Get(key+"_"+base.substr(1)))return variant;
        if(auto specific=Get(key))return specific;
    }
    return Get(base);
}
uint64_t Revision(){return revision.load();}
void SetBuiltIn(BuiltIn decode){builtIn.store(decode,std::memory_order_release);}
void Prefetch(const Snapshot& s) {
    for(unsigned i=0;i<s.quick.count&&i<s.quick.items.size();++i)Get(s.quick.items[i].item);
    for(const auto& d:companions::Definitions)if(d.skill>=0&&(s.companions.relevant&companions::Bit(d.type)))Get("s"+std::to_string(d.skill));
    for(const auto& skill:s.skills)if(skill.known&&skill.id>=0)Get("s"+std::to_string(skill.id));
    for(unsigned i=0;i<s.skillCount&&i<s.catalog.size();++i)if(s.catalog[i].id>=0)Get("s"+std::to_string(s.catalog[i].id));
    if(s.equipmentKnown)for(const auto& set:s.sets)for(const auto& item:set)Get(item);
    for(const auto& belt:s.belt)if(belt.known&&belt.count)Get(belt.Next());
    for(const auto& d:effects::Definitions)if(d.skill>=0&&s.effects.Has(d.state))Get("s"+std::to_string(d.skill));
    for(int i=0;i<s.cooldowns.count&&i<cooldowns::MaxSampled;++i)Get("s"+std::to_string(s.cooldowns.list[size_t(i)].skill));
    if(HasSummonArmy(s))for(int id=commander::SkillPhalanxDef;id<=commander::SkillFallOut;++id)Get("s"+std::to_string(id));
    Get("journal");
}
void Shutdown() {
    {std::lock_guard lock(mutex);stop=true;wake.notify_all();}
    if(worker.joinable())worker.join();
    std::lock_guard lock(mutex);pending.clear();cache.clear();busy=false;++revision;
}
void Directory(const std::wstring& path){std::lock_guard lock(mutex);if(!worker.joinable())directory=path;}
bool Wait(unsigned milliseconds) {
    std::unique_lock lock(mutex);return finished.wait_for(lock,std::chrono::milliseconds(milliseconds),[]{return pending.empty()&&!busy;});
}
}
