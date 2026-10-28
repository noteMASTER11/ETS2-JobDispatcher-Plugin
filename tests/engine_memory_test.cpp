#include "engine_memory.hpp"
#include <cassert>
#include <limits>
#include <iostream>
int main(){
    using namespace jd::engine;
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(pages);
    const auto address=reinterpret_cast<uintptr_t>(pages);
    assert(Write(address,uint32_t{42})&&Read<uint32_t>(address)==42);
    assert(!Readable(0,4)&&Read<uint32_t>(0,17)==17);
    assert(!Readable(std::numeric_limits<uintptr_t>::max()-1,4));
    DWORD previous=0;
    assert(VirtualProtect(pages,4096,PAGE_READONLY,&previous));
    assert(Read<uint32_t>(address)==42&&!Write(address,uint32_t{99}));
    assert(VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&previous));
    assert(!Readable(address+4094,4)&&!Write(address+4094,uint32_t{99}));
    assert(!Readable(address,17*1024*1024));
    VirtualFree(pages,0,MEM_RELEASE);
    std::cout<<"Native memory reads, writes, protection and bounds passed\n";
}
