#include "coop_protocol.h"
#include <cassert>

int main() {
    using namespace framevr::coop;
    const Message hello{Kind::Hello,0xfedcba9876543210ULL,3,BuildHash("0.3.20")};
    auto bytes=Encode(hello);Message decoded{};
    assert(Decode(bytes,decoded)&&decoded.nonce==hello.nonce&&decoded.rules==3&&decoded.build==hello.build);
    assert(bytes[8]==0x10&&bytes[15]==0xfe); // fixed little-endian wire layout
    assert(!Decode(std::span(bytes).first(31),decoded));
    auto bad=bytes;bad[0]^=1;assert(!Decode(bad,decoded));
    bad=bytes;bad[4]=2;assert(!Decode(bad,decoded));
    bad=bytes;bad[6]=5;assert(!Decode(bad,decoded));
    bad=Encode({Kind::Hello,0,3,hello.build});assert(!Decode(bad,decoded));
    assert(Reply(hello,3,hello.build)==Kind::Ready);
    assert(Reply(hello,2,hello.build)==Kind::RulesMismatch);
    assert(Reply(hello,3,BuildHash("other"))==Kind::BuildMismatch);
    Receipt receipt;receipt.Reset(hello.nonce);
    assert(!receipt.Accept(hello,100));
    assert(!receipt.Accept({Kind::Ready,4,3,hello.build},100)); // previous session
    assert(receipt.Accept({Kind::Ready,hello.nonce,3,hello.build},100));
    assert(receipt.Ready(100)&&receipt.Ready(6100)&&!receipt.Ready(6101)&&!receipt.Ready(99));
    assert(receipt.Accept({Kind::RulesMismatch,hello.nonce,2,hello.build},200));
    assert(!receipt.Ready(200));
    receipt.Reset(7);assert(!receipt.Ready(200));
    assert(!receipt.Accept({Kind::Ready,hello.nonce,3,hello.build},300));
}
