#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
namespace jd::units {
inline double Distance(double metres,bool miles){return metres/(miles?1609.344:1000.0);}
inline double Mass(double kg,const std::string& unit){
    if(unit=="lb")return kg/0.45359237;
    if(unit=="t")return kg/1000.0;
    if(unit=="st"||unit=="ton"||unit=="short_ton")return kg/907.18474;
    if(unit=="lt"||unit=="long_ton")return kg/1016.0469088;
    return kg;
}
inline std::string Number(double value,bool decimal_comma,int precision=0){
    char buffer[80]{};std::snprintf(buffer,sizeof(buffer),"%.*f",precision,value);
    std::string text=buffer;if(decimal_comma)std::replace(text.begin(),text.end(),'.',',');return text;
}
inline bool DecimalComma(const std::string& language){
    const auto prefix=language.substr(0,2);
    return prefix!="en"&&prefix!="ja"&&prefix!="zh"&&prefix!="ko"&&prefix!="vi";
}
inline int RealMinutes(double road_metres,double scale){
    if(!std::isfinite(road_metres)||road_metres<0||!std::isfinite(scale)||scale<=0)return -1;
    return road_metres>0?std::max(1,static_cast<int>(std::ceil(road_metres/(1500.0*scale)))):0;
}
}
