#pragma once
#include "interactions.h"
namespace framevr::potions::ice {
inline constexpr unsigned Count=3;
struct Vertex {float p[3],n[3],uv[2],glass;};
static_assert(sizeof(Vertex)==36);
inline uint32_t Code(int piece){return interaction::Code('i','c',char('1'+(piece<0?0:piece>2?2:piece)));}
inline bool IsCode(uint32_t code){code=(code&0xffffff)|(uint32_t(' ')<<24);return code==Code(0)||code==Code(1)||code==Code(2);}
}
