#pragma once
#include <cstdint>
namespace framevr::transparentfog {
// Recording is insufficient: the native command list must actually reach the
// presentation queue before this eye can sample it. Never reuse a prior eye.
class EyeCapture {
    uint64_t epoch=1,submitted=0;
public:
    uint64_t Record()const{return epoch;}
    void Submit(uint64_t token,bool presentationQueue) {
        if(presentationQueue&&token==epoch)submitted=epoch;
    }
    bool Fresh()const{return submitted==epoch;}
    void Invalidate(){submitted=0;++epoch;}
};
}
