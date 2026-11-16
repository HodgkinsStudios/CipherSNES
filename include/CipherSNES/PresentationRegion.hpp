// CipherSNES CP69: purely host-side integer video rectangle / damage tracking.
// The emulated framebuffer, PPU state, and frame pacing are not changed.
#pragma once
#include <algorithm>

namespace snes {
struct VideoRegion {
    int x=0,y=0,w=0,h=0;
    unsigned scale=0;
    bool valid()const{return w>0&&h>0&&scale>0;}
    bool operator==(const VideoRegion& r)const{
        return x==r.x&&y==r.y&&w==r.w&&h==r.h&&scale==r.scale;
    }
    bool operator!=(const VideoRegion& r)const{return !(*this==r);}
};
inline VideoRegion calculateVideoRegion(int windowWidth,int windowHeight,
                    int menuHeight,unsigned sourceWidth,unsigned sourceHeight){
    if(!sourceWidth||!sourceHeight||windowWidth<=0||windowHeight<=menuHeight)return {};
    const int usableW=windowWidth-8,usableH=windowHeight-menuHeight-8;
    const int scale=std::max(1,std::min(usableW/static_cast<int>(sourceWidth),
                                         usableH/static_cast<int>(sourceHeight)));
    const int w=static_cast<int>(sourceWidth)*scale;
    const int h=static_cast<int>(sourceHeight)*scale;
    if(w<=0||h<=0)return {};
    return {(windowWidth-w)/2,menuHeight+(usableH-h)/2,w,h,
            static_cast<unsigned>(scale)};
}
inline bool canPresentVideoOnly(bool staticLayerDirty,bool hasPrevious,
                        const VideoRegion& previous,const VideoRegion& current){
    return !staticLayerDirty&&hasPrevious&&current.valid()&&previous==current;
}
}
