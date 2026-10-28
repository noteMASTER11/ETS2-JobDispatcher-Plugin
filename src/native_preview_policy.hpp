#pragma once
#include "dispatch.hpp"
namespace jd::preview {
inline bool SameNativePreview(const Request& a,const Request& b){
    // Company selects the paint job. Destination does not affect appearance.
    return a.cargo==b.cargo&&a.trailer==b.trailer&&a.source==b.source;
}
struct NativePreviewPolicy {
    Request rendered;
    unsigned world=0;
    bool valid=false;
    bool NeedsVehicle(const Request& selection,unsigned revision)const{
        return !valid||world!=revision||!SameNativePreview(rendered,selection);
    }
    void Accept(const Request& selection,unsigned revision){rendered=selection;world=revision;valid=true;}
    void Reset(){valid=false;world=0;rendered={};}
};
}
