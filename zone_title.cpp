#include "zone_title.h"
#include "zone_title_data.h"
namespace abyss {
bool ZoneTitle::update(int next,float seconds) {
 if(next<0 || next>=6 || !std::isfinite(seconds)) return false;
 float dt=clampf(seconds,0,.2f);age=std::min(6.f,age+dt);
 if(zone<0) {zone=next;pending_=next;age=stable_=0;return true;}
 if(next==zone) {pending_=next;stable_=0;return false;}
 if(pending_!=next) {pending_=next;stable_=0;}
 stable_+=dt;
 if(stable_<.8f) return false;
 zone=next;age=stable_=0;return true;
}
void drawTitleMask(uint16_t* pixels,int width,int height,const uint8_t* mask,
                   int maskWidth,int maskHeight,int strength,int centreY) {
 if(!pixels || !mask || strength<=0 || width<32 || height<24) return;
 int w=std::min(maskWidth,width-4),h=maskHeight*w/maskWidth,left=(width-w)/2,top=centreY-h/2;
 for(int pass=0;pass<2;++pass) for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
  int source=(y*maskHeight/h)*maskWidth+x*maskWidth/w;
  uint8_t packed=mask[source/2];int alpha=(source&1)?(packed&15):(packed>>4);
  if(!alpha) continue;
  alpha=alpha*strength/15;if(pass==0) alpha=alpha*3/5;
  int px=left+x+(pass==0?1:0),py=top+y+(pass==0?2:0);
  if(px<0 || py<0 || px>=width || py>=height) continue;
  // Panel coordinates in, render pixels out.
  for(int sy=0;sy<UpScale;++sy) for(int sx=0;sx<UpScale;++sx) {
   uint16_t& dst=pixels[(py*UpScale+sy)*Width+px*UpScale+sx];
   int r=(dst>>11)&31,g=(dst>>5)&63,b=dst&31;
   if(pass) {r+=(31-r)*alpha/255;g+=(63-g)*alpha/255;b+=(31-b)*alpha/255;}
   else {r=r*(255-alpha)/255;g=g*(255-alpha)/255;b=b*(255-alpha)/255;}
   dst=uint16_t((r<<11)|(g<<5)|b);
  }
 }
}
void ZoneTitle::draw(uint16_t* pixels,int width,int height) const {
 if(zone<0 || zone>=6 || age>=5) return;
 float opacity=std::min(clampf(age/.65f,0,1),clampf((5-age)/1.0f,0,1));
 drawTitleMask(pixels,width,height,ZoneTitleMasks[zone],TitleWidth,TitleHeight,int(opacity*255),height/2);
}
}
