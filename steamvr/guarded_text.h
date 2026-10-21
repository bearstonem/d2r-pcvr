#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace framevr {
// A NUL-terminated string of printable ASCII through a guarded copy, one copy
// per page instead of one per character: natively every guarded copy is a
// system call, and a bone name read a character at a time cost a call per
// letter. A page is the unit of protection, so no copy reaches into a page the
// string does not, and none past the first byte that is not printable.
// out holds cap bytes. Returns the length, the string NUL-terminated in out;
// without a NUL in the first cap-1 characters, those. -1 at a byte that is not
// printable, a page before the end that cannot be read, or the low 64 KB.
constexpr uintptr_t TextPage=0x1000;
template<class Copy> int ReadText(uintptr_t at,char* out,int cap,Copy&& copy) {
    if(cap<1)return -1;
    int n=0;
    while(n<cap-1) {
        const uintptr_t from=at+uintptr_t(n);
        if(from<=0x10000)return -1;
        const int take=int(std::min<uintptr_t>(TextPage-(from&(TextPage-1)),uintptr_t(cap-1-n)));
        if(!copy(out+n,reinterpret_cast<const void*>(from),size_t(take)))return -1;
        for(const int end=n+take;n<end;++n) {
            if(!out[n])return n;
            if(out[n]<32||out[n]>126)return -1;
        }
    }
    out[n]=0;return n;
}
}
