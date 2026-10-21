#include "memory.h"
#include "guarded_text.h"
#include <cstdio>
#include <cstring>
#include <atomic>

static std::atomic<unsigned> exceptions{0};
static LONG CALLBACK Observe(EXCEPTION_POINTERS*){++exceptions;return EXCEPTION_CONTINUE_SEARCH;}

int main(int argc,char** argv) {
    auto observer=AddVectoredExceptionHandler(1,Observe);if(!observer)return 12;
    SYSTEM_INFO info{};GetSystemInfo(&info);
    const size_t page=info.dwPageSize;
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!pages)return 1;
    unsigned char out[32]{};
    std::memset(pages,42,page*2);
    if(!framevr::ReadMemory(out,pages,sizeof out)||out[0]!=42)return 2;
    out[0]=7;
    if(!framevr::WriteMemory(pages,out,sizeof out)||pages[0]!=7)return 9;
    DWORD old=0;
    if(!VirtualProtect(pages+page,page,PAGE_NOACCESS,&old))return 3;
    if(framevr::ReadMemory(out,pages+page-16,sizeof out))return 4;
    if(framevr::WriteMemory(pages+page,out,sizeof out))return 10;
    if(framevr::WriteMemory(pages+page-16,out,sizeof out))return 13;
    if(framevr::WriteMemory(pages,pages+page,sizeof out))return 14;
    if(!VirtualProtect(pages+page,page,PAGE_READONLY,&old))return 15;
    if(framevr::WriteMemory(pages+page,out,sizeof out))return 16;
    if(pages[page]!=42)return 17; // never unprotect/patch a read-only destination
    if(!VirtualFree(pages,0,MEM_RELEASE))return 5;
    if(framevr::ReadMemory(out,pages,sizeof out))return 6;
    if(framevr::ReadMemory(out,nullptr,sizeof out))return 7;
    if(framevr::WriteMemory(pages,out,sizeof out))return 11;
    if(framevr::WriteMemory(nullptr,out,sizeof out))return 18;
    {   // strings one copy per page, never into a page they do not reach
        auto* text=static_cast<char*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!text)return 20;
        int copies=0;
        auto copy=[&](void* dst,const void* src,size_t size){++copies;return framevr::ReadMemory(dst,src,size);};
        auto at=[&](size_t offset){return uintptr_t(text)+offset;};
        char name[96];const int cap=int(sizeof name);
        std::memcpy(text+100,"bolt_helper",12);
        if(framevr::ReadText(at(100),name,cap,copy)!=11||std::strcmp(name,"bolt_helper")||copies!=1)return 21;
        std::memset(text+page-4,'x',4);std::memcpy(text+page,"yz",3);   // across the page line, both readable
        copies=0;if(framevr::ReadText(at(page-4),name,cap,copy)!=6||std::strcmp(name,"xxxxyz")||copies!=2)return 22;
        std::memset(text,'a',page);   // no NUL within the cap: its first cap-1 characters
        if(framevr::ReadText(at(0),name,cap,copy)!=cap-1||name[cap-1]||name[cap-2]!='a')return 23;
        DWORD before=0;if(!VirtualProtect(text+page,page,PAGE_NOACCESS,&before))return 24;
        std::memcpy(text+page-4,"cog",4);   // its NUL on the last byte of the page
        if(framevr::ReadText(at(page-4),name,cap,copy)!=3||std::strcmp(name,"cog"))return 25;
        std::memset(text+page-4,'x',4);   // runs on into the unreadable page
        if(framevr::ReadText(at(page-4),name,cap,copy)!=-1)return 26;
        std::memcpy(text+page-3,"a\x01z",3);copies=0;   // junk: refused where met, the next page never read
        if(framevr::ReadText(at(page-3),name,cap,copy)!=-1||copies!=1)return 29;
        if(framevr::ReadText(0x1000,name,cap,copy)!=-1)return 27;
        if(!VirtualFree(text,0,MEM_RELEASE))return 28;
    }
    if(exceptions.load()!=0)return 19; // errors stay inside the OS, not D2R's crash handler
    RemoveVectoredExceptionHandler(observer);
    // A caller may request a result file when stdout is not forwarded.
    FILE* f=argc>1?std::fopen(argv[1],"w"):stdout;if(!f)return 8;
    std::fputs("Checked local copies succeed; inaccessible, freed, null, read-only and boundary-crossing buffers fail without entering application exception handlers; strings are copied a page at a time, never past their own page or a byte that is not printable.\n",f);
    if(f!=stdout)std::fclose(f);return 0;
}
