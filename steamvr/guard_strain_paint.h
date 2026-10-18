#pragma once
#include "guard_strain.h"
#include "latest_worker.h"

namespace framevr::strain {
struct PaintJob {
    float edge=1,alpha=0,feather=Feather;
    uint32_t color=White;
    float density=1;
};
struct PaintImage {uint64_t key=0;std::vector<uint8_t> pixels;};

// Let each requested image finish before scheduling another one. Replacing
// the request before Take discarded every finished image below 25 FPS while
// the pulse/breathing changed, making the rim appear only after it settled.
class Painter {
    LatestWorker<PaintJob,PaintImage> worker;
    uint64_t request=0;
public:
    using Work=std::function<PaintImage(const PaintJob&)>;
    explicit Painter(Work work=[](const PaintJob& job){
        return PaintImage{PaintKey(job.edge,job.alpha,job.feather,job.color,job.density),
            Compose(job.edge,job.alpha,job.feather,job.color,job.density)};
    }):worker(std::move(work)){}
    bool Start(){return worker.Start();}
    bool Busy()const{return request!=0;}
    bool Submit(PaintJob job){
        if(request)return false;
        request=worker.Submit(job);return request!=0;
    }
    bool Take(PaintImage& image){
        if(!request||!worker.Take(request,image))return false;
        request=0;return true;
    }
    // Hidden rim or character change: never reuse that character's old image.
    void Discard(){request=0;}
    void Stop(){worker.Stop();Discard();}
};
}
