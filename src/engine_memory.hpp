#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace jd::engine {
// Borrowed game addresses are checked at the boundary. This does not extend an
// object's lifetime; callers still require the current world and frame owner.
inline bool Region(uintptr_t address,size_t bytes,MEMORY_BASIC_INFORMATION& info){
    if(address<0x10000||bytes>16*1024*1024||address+bytes<address)return false;
    if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)))return false;
    if(info.State!=MEM_COMMIT||(info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    return address+bytes<=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
}
inline bool Readable(uintptr_t address,size_t bytes){
    MEMORY_BASIC_INFORMATION info{};return Region(address,bytes,info);
}
template<class T> T Read(uintptr_t address,T fallback={}){
    static_assert(std::is_trivially_copyable_v<T>);
    if(!Readable(address,sizeof(T)))return fallback;
    T value;std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(T));return value;
}
template<class T> bool Write(uintptr_t address,const T& value){
    static_assert(std::is_trivially_copyable_v<T>);
    MEMORY_BASIC_INFORMATION info{};
    if(!Region(address,sizeof(T),info))return false;
    if(!(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
    std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(T));return true;
}
}
