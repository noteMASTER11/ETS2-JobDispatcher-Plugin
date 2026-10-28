#pragma once
#include <set>
#include <string>
namespace jd::preview {
inline std::string CacheGeometryKey(const std::string& selection,const std::set<std::string>& visible_loads){
    const auto first=selection.find('|'),last=selection.rfind('|');
    if(first==std::string::npos||first==last)return selection;
    return visible_loads.count(selection.substr(0,last))?selection:selection.substr(0,first)+"|@body";
}
}
