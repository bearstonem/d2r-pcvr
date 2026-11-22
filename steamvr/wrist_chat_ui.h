#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace framevr::wristchat {
std::vector<uint8_t> Paint(bool detached,bool typing,bool supported,bool captured,const std::string& draft);
}
