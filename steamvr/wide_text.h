#pragma once
#include <string>
#include <windows.h>

namespace framevr {
// UTF-8 to UTF-16 for the settings panel's text, sized from the text: a fixed
// 512-character buffer left long help unterminated, and the card read past it
// (#159 review).
inline std::wstring WideText(const char* value) {
    const int n=value?MultiByteToWideChar(CP_UTF8,0,value,-1,nullptr,0):0;
    if(n<=1)return {};
    std::wstring wide(size_t(n),L'\0');
    MultiByteToWideChar(CP_UTF8,0,value,-1,wide.data(),n);
    wide.resize(size_t(n-1));
    return wide;
}
}
