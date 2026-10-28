#pragma once
#include "engine_memory.hpp"

namespace jd::engine {
struct NativeStringAbi { uintptr_t vtable, setter; };
inline bool AssignString(uintptr_t object,const char* value,NativeStringAbi abi){
    // string_t: vtable, char*, uint32 length, uint32 capacity. An interior
    // char** receiver was the cause of the previous real-game preview crash.
    if(!Readable(object,24)||Read<uintptr_t>(object)!=abi.vtable)return false;
    const auto buffer=Read<uintptr_t>(object+8);
    const auto length=Read<uint32_t>(object+0x10),capacity=Read<uint32_t>(object+0x14);
    if(length>65535||capacity>65536||(length&&capacity<=length)||
        !Readable(buffer,static_cast<size_t>(length)+1)||Read<uint8_t>(buffer+length))return false;
    reinterpret_cast<uintptr_t(*)(uintptr_t,const char**)>(abi.setter)(object,&value);
    const auto result=Read<uintptr_t>(object+8);
    const auto size=std::strlen(value);
    return Read<uint32_t>(object+0x10)==size&&Readable(result,size+1)&&
        !std::memcmp(reinterpret_cast<void*>(result),value,size+1);
}
}
