#define NOMINMAX
#include <cwchar>
#include "ally_health_art.h"
#include <cassert>
#include <limits>
int main(int argc,char** argv){
    using namespace framevr::allyhealth;
    List list;list.session=1;list.at=100;list.count=3;
    for(unsigned i=0;i<3;++i){auto& e=list.entries[i];e.id=i+1;e.maxHealth=100;std::strcpy(e.name.data(),i?"Party member":"Mercenary");}
    list.entries[0].health=100;list.entries[1].health=50;list.entries[2].health=0;
    assert(list.Current(1,1100)&&!list.Current(1,1101)&&!list.Current(2,100));
    assert(!list.Current(1,99));
    std::vector<uint8_t> pixels;assert(Paint(list,pixels));
    const auto green=[&](int row,int x){return pixels[((row*RowHeight+50)*Width+x)*4+1];};
    assert(green(0,450)==175);assert(green(1,200)==175&&green(1,450)==25);assert(green(2,20)==25);
    for(size_t i=size_t(3)*RowHeight*Width*4;i<pixels.size();++i)assert(pixels[i]==0);
    if(argc>1){FILE* out=std::fopen(argv[1],"wb");assert(out);assert(std::fwrite(pixels.data(),1,pixels.size(),out)==pixels.size());std::fclose(out);}
    list.entries[0].health=std::numeric_limits<float>::quiet_NaN();assert(!list.entries[0].Valid());
    assert(Paint(list,pixels));for(int i=0;i<Width*RowHeight*4;++i)assert(pixels[i]==0);
    list.count=Capacity+1;assert(!Paint(list,pixels));
    std::puts("ally health art: full/half/dead bars, unknown transparency, capacity and freshness passed");
}
