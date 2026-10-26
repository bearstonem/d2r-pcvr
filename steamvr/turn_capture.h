#pragma once
#include <windows.h>
#include <wincodec.h>
#include <dxgiformat.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace framevr::turncapture {
// By turns capture ([debug] capture_pair or F8 while Stereo rendering is By
// turns): after the stereo pair it forces, the next Frames frames by turns,
// each eye as it goes to the headset, the drawn eye's picture as the warp
// read it and its depth, written next to the frame log. Transport reads them
// back; this is what names and converts them.
inline constexpr int Frames=4;
// Frame-turns-<serial>-<frame>-<left|right>-<what>.<ext>
inline std::string Name(unsigned serial,int frame,int eye,const char* what,const char* ext) {
    char name[96];std::snprintf(name,sizeof name,"Frame-turns-%u-%d-%s-%s.%s",serial,frame,eye?"right":"left",what,ext);
    return name;
}
// Frame-turns-<serial>-<frame>.txt: the frame's warp, for replaying it
inline std::string Sidecar(unsigned serial,int frame) {
    char name[64];std::snprintf(name,sizeof name,"Frame-turns-%u-%d.txt",serial,frame);
    return name;
}
// A depth texel as read back (plane 0 of a depth-stencil format)
enum class Depth {None,Unorm24,Float32,Unorm16};
inline Depth DepthOf(DXGI_FORMAT format) {
    switch(format) {
    case DXGI_FORMAT_R24G8_TYPELESS:case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:return Depth::Unorm24;
    case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:return Depth::Float32;
    case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_D16_UNORM:case DXGI_FORMAT_R16_UNORM:return Depth::Unorm16;
    default:return Depth::None;
    }
}
inline uint32_t DepthBytes(Depth d) {return d==Depth::Unorm16?2:d==Depth::None?0:4;}
// The depth as the game wrote it (reverse-Z: distance = near plane / depth)
inline float DepthTexel(Depth d,const uint8_t* texel) {
    if(d==Depth::Unorm16){uint16_t v;std::memcpy(&v,texel,2);return v/65535.f;}
    uint32_t v;std::memcpy(&v,texel,4);
    if(d==Depth::Unorm24)return float(v&0xFFFFFFu)/float(0xFFFFFFu);
    float f;std::memcpy(&f,&v,4);return d==Depth::Float32?f:0.f;
}
inline bool Bgra(DXGI_FORMAT format) {
    return format==DXGI_FORMAT_B8G8R8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||format==DXGI_FORMAT_B8G8R8A8_TYPELESS;
}
// 8-bit colour, whose bytes are what the headset shows (sRGB-encoded): as is
inline bool Readable(DXGI_FORMAT format) {
    return Bgra(format)||format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||format==DXGI_FORMAT_R8G8B8A8_TYPELESS;
}
// Rows `pitch` bytes apart into tight BGR rows (PNG, 24-bit: alpha dropped)
inline std::vector<uint8_t> Bgr(const uint8_t* rows,uint32_t pitch,uint32_t width,uint32_t height,DXGI_FORMAT format) {
    std::vector<uint8_t> out(size_t(width)*height*3);
    const bool bgra=Bgra(format);
    for(uint32_t y=0;y<height;++y) {
        const uint8_t* s=rows+size_t(y)*pitch;uint8_t* o=out.data()+size_t(y)*width*3;
        for(uint32_t x=0;x<width;++x,s+=4,o+=3){o[0]=bgra?s[0]:s[2];o[1]=s[1];o[2]=bgra?s[2]:s[0];}
    }
    return out;
}
// Rows `pitch` bytes apart into tight float32 (Frame-turns-...-depth.bin, little-endian, width x height)
inline std::vector<float> Depths(const uint8_t* rows,uint32_t pitch,uint32_t width,uint32_t height,Depth d) {
    std::vector<float> out(size_t(width)*height);
    const uint32_t step=DepthBytes(d);
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x)out[size_t(y)*width+x]=DepthTexel(d,rows+size_t(y)*pitch+size_t(x)*step);
    return out;
}
// Tight BGR rows as a 24-bit PNG (WIC; any thread)
inline bool SavePng(const wchar_t* path,std::vector<uint8_t>& bgr,UINT width,UINT height) {
    const HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IWICImagingFactory* wic=nullptr;IWICStream* stream=nullptr;
    IWICBitmapEncoder* encoder=nullptr;IWICBitmapFrameEncode* frame=nullptr;
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;
    const bool ok=bgr.size()==size_t(width)*height*3&&SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)))&&
        SUCCEEDED(wic->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path,GENERIC_WRITE))&&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&
        SUCCEEDED(encoder->Initialize(stream,WICBitmapEncoderNoCache))&&SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&
        SUCCEEDED(frame->Initialize(nullptr))&&SUCCEEDED(frame->SetSize(width,height))&&
        SUCCEEDED(frame->SetPixelFormat(&format))&&IsEqualGUID(format,GUID_WICPixelFormat24bppBGR)&&
        SUCCEEDED(frame->WritePixels(height,width*3,UINT(bgr.size()),bgr.data()))&&
        SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit());
    for(IUnknown* p:{static_cast<IUnknown*>(frame),static_cast<IUnknown*>(encoder),static_cast<IUnknown*>(stream),static_cast<IUnknown*>(wic)})if(p)p->Release();
    if(SUCCEEDED(co))CoUninitialize();
    return ok;
}
}
