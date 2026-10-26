#include "turn_capture.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
using namespace framevr::turncapture;

int main() {
    assert(Name(7,0,0,"drawn","png")=="Frame-turns-7-0-left-drawn.png");
    assert(Name(7,3,1,"turned","png")=="Frame-turns-7-3-right-turned.png");
    assert(Name(12,1,1,"depth","bin")=="Frame-turns-12-1-right-depth.bin");
    assert(Sidecar(12,1)=="Frame-turns-12-1.txt");
    assert(Frames==4);

    // the game's depth (D24S8: depth in the low 24 bits, stencil above)
    assert(DepthOf(DXGI_FORMAT_R24G8_TYPELESS)==Depth::Unorm24&&DepthOf(DXGI_FORMAT_R24_UNORM_X8_TYPELESS)==Depth::Unorm24);
    assert(DepthOf(DXGI_FORMAT_D32_FLOAT)==Depth::Float32&&DepthOf(DXGI_FORMAT_R32G8X24_TYPELESS)==Depth::Float32);
    assert(DepthOf(DXGI_FORMAT_D16_UNORM)==Depth::Unorm16&&DepthOf(DXGI_FORMAT_R8G8B8A8_UNORM)==Depth::None);
    assert(DepthBytes(Depth::Unorm24)==4&&DepthBytes(Depth::Unorm16)==2&&DepthBytes(Depth::None)==0);
    const uint8_t half24[4]={0x00,0x00,0x80,0xAB};   // 0x800000 with stencil 0xAB
    assert(std::fabs(DepthTexel(Depth::Unorm24,half24)-float(0x800000)/float(0xFFFFFF))<1e-7f);
    const uint8_t full24[4]={0xFF,0xFF,0xFF,0x00};
    assert(DepthTexel(Depth::Unorm24,full24)==1.f);
    const float quarter=.25f;uint8_t f32[4];std::memcpy(f32,&quarter,4);
    assert(DepthTexel(Depth::Float32,f32)==.25f);
    const uint8_t u16[2]={0xFF,0xFF};
    assert(DepthTexel(Depth::Unorm16,u16)==1.f);

    // colour rows with padding: RGBA and BGRA both to BGR, alpha dropped
    assert(Readable(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)&&Readable(DXGI_FORMAT_B8G8R8A8_UNORM)&&!Readable(DXGI_FORMAT_R16G16B16A16_FLOAT));
    const uint8_t rgba[2*8]={1,2,3,4, 5,6,7,8,   // row 0: two texels
                             9,10,11,12, 13,14,15,16};   // row 1, 8 bytes later
    const auto bgr=Bgr(rgba,8,2,2,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    const uint8_t want[12]={3,2,1, 7,6,5, 11,10,9, 15,14,13};
    assert(bgr.size()==12&&!std::memcmp(bgr.data(),want,12));
    const uint8_t padded[2*12]={1,2,3,4, 5,6,7,8, 0,0,0,0,   // a pitch wider than the row
                                9,10,11,12, 13,14,15,16, 0,0,0,0};
    const auto fromBgra=Bgr(padded,12,2,2,DXGI_FORMAT_B8G8R8A8_UNORM);
    const uint8_t same[12]={1,2,3, 5,6,7, 9,10,11, 13,14,15};
    assert(fromBgra.size()==12&&!std::memcmp(fromBgra.data(),same,12));

    // depth rows with padding to tight floats
    uint8_t depth[2*12]{};
    std::memcpy(depth,half24,4);std::memcpy(depth+4,full24,4);
    std::memcpy(depth+12,full24,4);std::memcpy(depth+16,half24,4);
    const auto d=Depths(depth,12,2,2,Depth::Unorm24);
    assert(d.size()==4&&d[1]==1.f&&d[2]==1.f&&std::fabs(d[0]-d[3])<1e-9f&&d[0]>.5f&&d[0]<.5001f);

    // the PNG decodes to the same BGR texels
    wchar_t folder[MAX_PATH];const DWORD n=GetTempPathW(MAX_PATH,folder);assert(n&&n<MAX_PATH);
    const std::wstring path=std::wstring(folder)+L"turn-capture-test-"+std::to_wstring(GetCurrentProcessId())+L".png";
    std::vector<uint8_t> image(3*2*3);for(size_t i=0;i<image.size();++i)image[i]=uint8_t(i*13+7);
    const auto written=image;
    assert(SavePng(path.c_str(),image,3,2));
    assert(!SavePng(path.c_str(),image,4,2));   // the wrong size is refused
    const HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IWICImagingFactory* wic=nullptr;IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* converter=nullptr;
    std::vector<uint8_t> read(written.size());UINT w=0,h=0;
    const bool decoded=SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)))&&
        SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder))&&
        SUCCEEDED(decoder->GetFrame(0,&frame))&&SUCCEEDED(frame->GetSize(&w,&h))&&SUCCEEDED(wic->CreateFormatConverter(&converter))&&
        SUCCEEDED(converter->Initialize(frame,GUID_WICPixelFormat24bppBGR,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))&&
        SUCCEEDED(converter->CopyPixels(nullptr,3*3,UINT(read.size()),read.data()));
    for(IUnknown* p:{static_cast<IUnknown*>(converter),static_cast<IUnknown*>(frame),static_cast<IUnknown*>(decoder),static_cast<IUnknown*>(wic)})if(p)p->Release();
    if(SUCCEEDED(co))CoUninitialize();
    DeleteFileW(path.c_str());
    assert(decoded&&w==3&&h==2&&read==written);
    std::puts("turn capture: names, depth texels, colour rows and the PNG passed");
}
