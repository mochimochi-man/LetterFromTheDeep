#pragma once
#include "scene.h"
#include "hud_font.h"
namespace abyss {
// Every interface coordinate is in panel space. Drawing multiplies by UpScale, so the
// layout stays put whatever resolution the world is rendered at.
inline void hudPlot(uint16_t* pixels,int x,int y,uint16_t color) {
  if(x<0 || y<0 || x>=PanelW || y>=PanelH) return;
  for(int j=0;j<UpScale;++j) {
    uint16_t* row=pixels+(y*UpScale+j)*Width+x*UpScale;
    for(int i=0;i<UpScale;++i) row[i]=color;
  }
}
inline void hudText(uint16_t* pixels,int x,int y,const char* text,uint16_t color=0xffff) {
 if(!text) return;
 for(;*text;++text,x+=7) {
  unsigned c=static_cast<unsigned char>(*text);if(c<32 || c>126) continue;
  for(int j=0;j<12;++j) for(int i=0;i<8;++i)
   if(HudFont[(c-32)*12+j]&(1<<i)) hudPlot(pixels,x+i,y+j,color);
 }
}
inline void hudRect(uint16_t* pixels,int x,int y,int w,int h,uint16_t color) {
 for(int j=std::max(0,y);j<std::min(PanelH,y+h);++j) {
  for(int p=0;p<UpScale;++p) {
   uint16_t* row=pixels+(j*UpScale+p)*Width;
   for(int i=std::max(0,x)*UpScale;i<std::min(PanelW,x+w)*UpScale;++i) row[i]=color;
  }
 }
}
// A baked picture, one source pixel per panel pixel.
inline void hudBlit(uint16_t* pixels,int x,int y,const uint16_t* source,int w,int h) {
 for(int j=0;j<h;++j) for(int i=0;i<w;++i) hudPlot(pixels,x+i,y+j,source[j*w+i]);
}
}
