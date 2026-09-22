#include "scene.h"
namespace abyss {
const char* machineName(MachineKind kind) {
 switch(kind) {
  case MachineKind::ArmoredFish: return "armoured swimmer";
  case MachineKind::WingRay: return "winged drifter";
  default: return "walking tank";
 }
}
MachinePose cityMachinePose(int index,float time) {
 int kind=index/2;float speed=kind==0?.024f:kind==1?.018f:.030f;
 float angle=time*speed+(index%2)*Pi+kind*.7f;
 float rx=kind==0?62.f:kind==1?85.f:42.f,rz=kind==0?12.f:kind==1?8.f:10.f;
 float y=kind==0?-198.f:kind==1?-183.f:-216.f,z=kind==0?-58.f:kind==1?-37.f:-55.f;
 return {{std::sin(angle)*rx,y+std::sin(angle*2)*1.2f,z+std::cos(angle)*rz},
  std::atan2(rx*std::cos(angle),-rz*std::sin(angle)),kind==0?6.4f:kind==1?5.8f:3.8f,
  time*(kind==2?1.7f:1.1f)+index*1.9f,MachineKind(kind)};
}
void Scene::poseMachine(const MachinePose& p){count=0;overflow=false;lighting_=1;viewer_=p.position+Vec3{0,0,p.length*1.4f};machineMesh(p);}
void Scene::machineMesh(const MachinePose& a){
 material_=0;bool detail=!crowded() && length(a.position-viewer_)<a.length*5;
 const Color seam{72,86,96};
 auto w=[&](Vec3 p){return a.position+rotateY(p,a.yaw)*a.length;};
 auto face=[&](Vec3 p,Vec3 q,Vec3 r,Color c,Vec3 center){addSolid(w(p),w(q),w(r),c,rotateY((p+q+r)*(1.f/3)-center,a.yaw));};
 auto sheet=[&](Vec3 p,Vec3 q,Vec3 r,Color c){add(w(p),w(q),w(r),c);};
 auto tube=[&](Vec3 p,Vec3 q,float r0,float r1,Color col){
  Vec3 axis=unit(q-p),u=unit(cross(axis,{.2f,1,.3f})),v=cross(axis,u),c=(p+q)*.5f;Vec3 aa[4],bb[4];
  for(int k=0;k<4;++k){float t=k*Pi*.5f;Vec3 d=u*std::cos(t)+v*std::sin(t);aa[k]=p+d*r0;bb[k]=q+d*r1;}
  for(int k=0;k<4;++k){int n=(k+1)%4;face(aa[k],aa[n],bb[n],col,c);face(aa[k],bb[n],bb[k],col,c);}
  face(aa[0],aa[2],aa[1],seam,c);face(aa[0],aa[3],aa[2],seam,c);face(bb[0],bb[1],bb[2],col,c);face(bb[0],bb[2],bb[3],col,c);
 };
 auto hull=[&](float start,float end,float width,float height,Color col){
  const float along[]={0,.18f,.70f,1},radius[]={.28f,1,1,.45f};Vec3 rings[4][8];
  for(int j=0;j<4;++j)for(int k=0;k<8;++k){float t=k*Pi/4;rings[j][k]={std::sin(t)*width*radius[j],std::cos(t)*height*radius[j],start+(end-start)*along[j]};}
  for(int j=0;j<3;++j)for(int k=0;k<8;++k){int n=(k+1)%8;Vec3 c{0,0,(rings[j][0].z+rings[j+1][0].z)*.5f};Color shade=k>=2&&k<6?scale(col,.76f):col;face(rings[j][k],rings[j][n],rings[j+1][n],shade,c);face(rings[j][k],rings[j+1][n],rings[j+1][k],shade,c);}
  for(int k=0;k<8;++k){face({0,0,start},rings[0][(k+1)%8],rings[0][k],col,{0,0,start+.02f});face({0,0,end},rings[3][k],rings[3][(k+1)%8],col,{0,0,end-.02f});}
 };
 // Streamlined pods and armor pieces have five sections rather than blunt tube ends.
 auto pod=[&](Vec3 c,Vec3 dim,Color color){
  const float z[5]={-1,-.70f,.25f,.72f,1},rad[5]={.24f,.88f,1,.72f,.16f};Vec3 v[5][8];
  for(int j=0;j<5;++j)for(int k=0;k<8;++k){float t=k*Pi/4;v[j][k]=c+Vec3{std::sin(t)*dim.x*rad[j],std::cos(t)*dim.y*rad[j],z[j]*dim.z};}
  for(int j=0;j<4;++j)for(int k=0;k<8;++k){int n=(k+1)%8;Vec3 mid=c+Vec3{0,0,(z[j]+z[j+1])*.5f*dim.z};Color col=k>=2&&k<6?scale(color,.73f):color;face(v[j][k],v[j][n],v[j+1][n],col,mid);face(v[j][k],v[j+1][n],v[j+1][k],col,mid);}
  for(int k=0;k<8;++k){face(c+Vec3{0,0,-dim.z},v[0][(k+1)%8],v[0][k],color,c);face(c+Vec3{0,0,dim.z},v[4][k],v[4][(k+1)%8],color,c);}
 };
 auto disc=[&](Vec3 c,Vec3 normal,float radius,Color col,bool lit){Vec3 u=unit(cross(normal,{.2f,1,.3f}))*radius,v=cross(normal,u);material_=lit?7:0;for(int k=0;k<8;++k){float t=k*Pi/4,n=(k+1)*Pi/4;sheet(c,c+u*std::cos(t)+v*std::sin(t),c+u*std::cos(n)+v*std::sin(n),col);}material_=0;};
 // Riveted brass over an iron frame, verdigris where the water has stood, and a lamp
 // behind a barred porthole. Hoops round the hull like a barrel and a saw-toothed
 // crest down the back: these are vessels that happen to be shaped like animals,
 // not animals with machinery in them.
 const Color brass{184,144,80},brassDim{138,104,56},iron{56,48,40},
             verd{84,130,108},verdDim{60,98,80},lamp{250,198,112},ink{20,18,16};
 // The radius the hull helper has at a given station, so a ring can be made to sit
 // on the plating instead of floating off it.
 auto hullR=[&](float z,float start,float end){
  float f=clampf((z-start)/(end-start),0,1);
  const float A[4]={0,.18f,.70f,1},R[4]={.28f,1,1,.45f};
  int i=1; while(i<3 && A[i]<f) ++i;
  float t=(f-A[i-1])/(A[i]-A[i-1]);
  return R[i-1]+(R[i]-R[i-1])*t;
 };
 auto rivets=[&](Vec3 from,Vec3 to,int n,float r){
  if(!detail) return;
  for(int k=0;k<n;++k){
   Vec3 p=mix(from,to,(k+.5f)/n);
   disc(p,unit(p-Vec3{0,-.5f,0}),r,iron,false);
  }
 };
 // A saw-toothed ridge, the one line that says this was made and not grown.
 auto crest=[&](Vec3 from,Vec3 to,int n,float h,Color col){
  for(int k=0;k<n;++k){
   float f=float(k)/n;
   Vec3 p=mix(from,to,f),q=mix(from,to,float(k+1)/n);
   Vec3 tip=mix(p,q,.42f)+Vec3{0,h*(1.f-.42f*f),0};
   sheet(p,tip,q,col);sheet(q,tip,p,scale(col,.68f));
  }
 };
 // Porthole: iron ring, brass bezel, barred glass, rivets round the outside.
 auto porthole=[&](Vec3 c,Vec3 n,float r){
  Vec3 u=unit(cross(n,Vec3{.2f,1,.3f})),v=cross(n,u);
  disc(c,n,r*1.32f,iron,false);
  disc(c+n*.004f,n,r*1.12f,brass,false);
  disc(c+n*.008f,n,r*.88f,iron,false);
  disc(c+n*.011f,n,r*.74f,lamp,true);
  if(detail){
   for(int k=0;k<2;++k){
    float t=k*Pi*.5f+.5f;
    Vec3 d=u*std::cos(t)+v*std::sin(t);
    tube(c+n*.014f-d*(r*.80f),c+n*.014f+d*(r*.80f),r*.055f,r*.055f,iron);
   }
   for(int k=0;k<4;++k){
    float t=k*Pi*.5f+.25f;
    disc(c+n*.006f+u*(std::cos(t)*r*1.20f)+v*(std::sin(t)*r*1.20f),n,r*.11f,iron,false);
   }
  }
 };
 auto lobe=[&](Vec3 root,Vec3 tip,Vec3 wide,Color col){
  Vec3 c=mix(root,tip,.48f);
  Vec3 v[6]={root+wide*.30f,mix(root,tip,.35f)+wide,mix(root,tip,.78f)+wide*.72f,
             tip,mix(root,tip,.78f)-wide*.60f,mix(root,tip,.35f)-wide*.80f};
  for(int k=0;k<6;++k)sheet(c,v[k],v[(k+1)%6],k&1?col:scale(col,.84f));
  if(detail)tube(root,tip,.0045f,.0030f,iron);          // the rib down every blade
 };
 auto canopy=[&](Vec3 c,float width,float height,float halfLength){
  (void)width;(void)halfLength;
  porthole(c+Vec3{0,height*.30f,0},unit(Vec3{0,.88f,.48f}),height*1.30f);
 };

 // Each of the three is its own submarine, so each carries a conning tower standing
 // off the back: a raked fairing with a lit port in either side, a black band round
 // the foot to match the hull, and masts off the deck. The proportions differ - the
 // fish a tall narrow fin, the ray a low broad turtleback, the frog a short stump -
 // so they read as three boats rather than three copies.
 auto sail=[&](Vec3 base,float len,float height,float wide,float rake,int masts,float mastLen,float aft){
  Vec3 top=base+Vec3{0,height,-rake},mid=base+Vec3{0,height*.5f,-rake*.5f};
  float tw=wide*.58f,tl=len*.66f;
  Vec3 b[4]={base+Vec3{-wide,0,len},base+Vec3{wide,0,len},
             base+Vec3{wide,0,-len},base+Vec3{-wide,0,-len}};
  Vec3 d[4]={top+Vec3{-tw,0,tl},top+Vec3{tw,0,tl},top+Vec3{tw,0,-tl},top+Vec3{-tw,0,-tl}};
  for(int k=0;k<4;++k){
   int m=(k+1)%4;Color c=(k&1)?brass:scale(brass,.80f);
   face(b[k],b[m],d[m],c,mid);face(b[k],d[m],d[k],c,mid);
  }
  for(int k=0;k<4;++k)face(d[k],d[(k+1)%4],top,brassDim,mid);
  for(int side=-1;side<=1;side+=2){
   Vec3 n=unit(Vec3{float(side),.14f,.06f});
   Vec3 c=base+Vec3{side*wide*.80f,height*.54f,len*.04f-rake*.54f};
   disc(c,n,wide*.38f,iron,false);
   disc(c+n*.004f,n,wide*.23f,lamp,true);
  }
  if(detail){
   for(int k=0;k<4;++k){int m=(k+1)%4;tube(mix(b[k],d[k],.15f),mix(b[m],d[m],.15f),.005f,.005f,ink);}
   rivets(mix(b[0],b[1],.5f)+Vec3{0,.008f,0},mix(d[0],d[1],.5f),3,.0042f);
  }
  // A short faired casing running aft off the back of the tower, tapering as it goes
  // down into the plating: enough to say submarine, not enough to be a spine.
  {
   float ew=wide*.54f,eh=height*.30f,et=tw*.46f;
   Vec3 q[4]={base+Vec3{-ew,-.004f,-len-aft},base+Vec3{ew,-.004f,-len-aft},
              base+Vec3{et,eh,-len-aft-rake*.40f},base+Vec3{-et,eh,-len-aft-rake*.40f}};
   Vec3 r[4]={b[3],b[2],d[2],d[3]};
   Vec3 c=mix(mix(r[0],r[2],.5f),mix(q[0],q[2],.5f),.5f);
   for(int k=0;k<4;++k){
    int m=(k+1)%4;Color cc=(k&1)?brass:scale(brass,.80f);
    face(r[k],r[m],q[m],cc,c);face(r[k],q[m],q[k],cc,c);
   }
   face(q[0],q[1],q[2],brassDim,c);face(q[0],q[2],q[3],brassDim,c);
  }
  for(int k=0;k<masts;++k){
   float u=masts>1?(-1+2.f*k/(masts-1))*.46f:0.f;
   Vec3 r=top+Vec3{u*tw,-.004f,tl*.22f},tip=r+Vec3{0,mastLen,-mastLen*.24f};
   tube(r,tip,.0070f,.0050f,iron);
   if(!k)tube(tip+Vec3{0,-.008f,0},tip+Vec3{0,-.008f,-.026f},.005f,.005f,iron);
  }
 };
 if(a.kind==MachineKind::ArmoredFish){
  // One hull the whole length of it, with the nose simply where the hull runs out.
  // Everything that makes it a vessel - hoops, crest, rivets, barred portholes and a
  // short iron beak - is laid on that one form rather than built out of it.
  auto bend=[&](float z){float t=clampf((.40f-z)*.96f,0,1);return std::sin(a.phase*1.15f-t*2.4f)*.046f*t*t;};
  hull(-.340f,.400f,.078f,.086f,brass);
  // Five black rings at even spacing down the hull, and a black line along each
  // flank: one graphic device carried by all three of them.
  for(int j=0;j<5;++j){
   float z=.240f-j*.120f,r=hullR(z,-.340f,.400f);
   pod({bend(z),0,z},{.078f*r+.005f,.086f*r+.005f,.012f},ink);
  }
  pod({bend(-.212f),0,-.212f},{.052f,.058f,.024f},verdDim);
  for(int side=-1;side<=1;side+=2){
   Vec3 prev{0,0,0};
   for(int j=0;j<=5;++j){
    float z=.300f-j*.116f,r=hullR(z,-.340f,.400f);
    Vec3 p{bend(z)+side*(.078f*r+.004f),0,z};
    if(j)tube(prev,p,.0055f,.0055f,ink);
    prev=p;
   }
  }
  crest({bend(.040f),.082f,.040f},{bend(-.250f),.040f,-.250f},6,.048f,brassDim);
  // A tall narrow fin of a conning tower, well forward, raked hard aft.
  sail({bend(.140f),.080f,.140f},.056f,.082f,.028f,.026f,1,.062f,.062f);
  {
   // A short iron beak on the point of the nose, in line with the taper.
   Vec3 n{bend(.400f),-.002f,.392f},tip{bend(.440f),-.006f,.448f};
   Vec3 r[4]={n+Vec3{-.024f,.024f,0},n+Vec3{.024f,.024f,0},n+Vec3{.022f,-.024f,0},n+Vec3{-.022f,-.024f,0}};
   for(int k=0;k<4;++k){int m=(k+1)%4;face(r[k],r[m],tip,k&1?iron:scale(iron,1.4f),n);}
  }
  for(int side=-1;side<=1;side+=2)
   porthole({bend(.288f)+side*.058f,.022f,.288f},unit(Vec3{float(side),.18f,.34f}),.022f);
  canopy({bend(.236f),.062f,.236f},.034f,.026f,.040f);
  rivets({bend(.310f)-.046f,.046f,.310f},{bend(.310f)+.046f,.046f,.310f},4,.005f);
  for(int side=-1;side<=1;side+=2){
   for(int pair=0;pair<2;++pair){
    float z=pair?-.120f:.120f,sweep=std::sin(a.phase*1.05f+pair*1.7f)*.22f;
    Vec3 root{bend(z)+side*(pair?.054f:.076f),-.018f,z};
    disc(root,{float(side),0,0},.018f,iron,false);
    Vec3 tip=root+Vec3{side*(pair?.094f:.146f),-.026f+sweep*.10f,-(pair?.074f:.116f)};
    lobe(root,tip,Vec3{0,.004f,pair?.040f:.060f},pair?verd:brass);
   }
   if(detail){
    tube({bend(.250f)+side*.058f,.020f,.250f},{bend(-.070f)+side*.076f,.004f,-.070f},.006f,.005f,iron);
    tube({bend(-.070f)+side*.076f,.004f,-.070f},{bend(-.215f)+side*.050f,-.004f,-.215f},.005f,.004f,iron);
    rivets({bend(.150f)+side*.072f,-.024f,.150f},{bend(-.110f)+side*.068f,-.030f,-.110f},5,.005f);
   }
  }
  lobe({bend(-.100f),-.076f,-.100f},{bend(-.140f),-.144f,-.152f},Vec3{0,0,.032f},brassDim);
  {
   Vec3 root{bend(-.318f),0,-.318f};
   float wag=bend(-.386f);
   Vec3 stem=root+Vec3{wag,0,-.046f};
   tube(root,stem,.019f,.013f,iron);
   lobe(stem,stem+Vec3{wag*.4f,.146f,-.116f},Vec3{0,0,.046f},brass);
   lobe(stem,stem+Vec3{wag*.4f,-.134f,-.108f},Vec3{0,0,.042f},brass);
  }
 }else if(a.kind==MachineKind::WingRay){
  // One cast disc, hooped across the spine, ribbed out to the rim, crest on the back.
  float beat=std::sin(a.phase*.68f);
  pod({0,.002f,.030f},{.190f,.042f,.276f},brass);
  pod({0,.014f,.020f},{.108f,.034f,.186f},brassDim);
  // The same black rings, spaced evenly along the spine, and a black line down it.
  for(int j=0;j<4;++j)pod({0,.006f,.170f-j*.120f},{.156f-j*.020f,.042f,.012f},ink);
  {
   Vec3 prev{0,.046f,.220f};
   for(int j=1;j<=4;++j){Vec3 p{0,.046f-j*.004f,.220f-j*.110f};tube(prev,p,.0055f,.0055f,ink);prev=p;}
  }
  crest({0,.046f,.040f},{0,.030f,-.230f},5,.038f,brassDim);
  // A low broad turtleback of a tower on the spine, with two masts off the deck.
  sail({0,.040f,.130f},.064f,.052f,.040f,.016f,2,.048f,.058f);
  for(int side=-1;side<=1;side+=2){
   float lift=beat*.11f;
   Vec3 rim[8]={{side*.120f,.006f,.268f},{side*.300f,lift*.30f,.186f},{side*.470f,lift*.64f,.052f},
                {side*.560f,lift*.92f,-.070f},{side*.516f,lift*1.00f,-.164f},{side*.340f,lift*.60f,-.212f},
                {side*.176f,lift*.22f,-.234f},{side*.104f,.004f,-.176f}};
   Vec3 mid{side*.250f,lift*.42f,-.006f};
   for(int k=0;k<8;++k){
    int m=(k+1)%8;
    face(mid+Vec3{0,.020f,0},rim[k],rim[m],k%3?brass:brassDim,mid);
    face(mid-Vec3{0,.012f,0},rim[m],rim[k],brassDim,mid);
   }
   for(int k=0;k<4;++k)tube(rim[k],rim[k+1],.010f,.009f,iron);
   rivets(rim[0],rim[3],6,.006f);
   // Ribs fanning from the spine to the rim, the way a frame carries a skin.
   if(detail)for(int k=1;k<5;++k)
    tube({side*.100f,lift*.16f+.020f,.140f-k*.050f},rim[k]+Vec3{0,.018f,0},.006f,.004f,iron);
   lobe({side*.140f,.032f,.140f},{side*.330f,lift*.46f+.016f,-.060f},Vec3{0,0,.060f},verdDim);
   disc({side*.096f,-.040f,-.196f},{0,-.2f,-.96f},.032f,iron,false);
   disc({side*.096f,-.042f,-.200f},{0,-.2f,-.96f},.014f,lamp,true);
  }
  {
   Vec3 h{0,.012f,.286f};
   pod(h,{.086f,.040f,.070f},brass);
   for(int side=-1;side<=1;side+=2){
    porthole(h+Vec3{side*.062f,.006f,.014f},unit(Vec3{float(side),.20f,.30f}),.022f);
    lobe(h+Vec3{side*.048f,-.008f,.054f},h+Vec3{side*.084f,-.014f,.142f},Vec3{0,.004f,.022f},brass);
   }
   canopy(h+Vec3{0,.032f,-.030f},.038f,.026f,.044f);
  }
  {
   Vec3 prev{0,.004f,-.278f};
   for(int j=0;j<3;++j){
    float t=(j+1)/3.f;
    Vec3 next{std::sin(a.phase*.5f-t*1.2f)*.048f*t,.004f,-.278f-t*.186f};
    tube(prev,next,.018f-j*.004f,.014f-j*.004f,j%2?brass:iron);prev=next;
   }
   lobe(prev+Vec3{0,0,.018f},prev+Vec3{0,-.078f,-.066f},Vec3{0,0,.028f},brass);
  }
 }else{
  // Built to swim, not to sit: a long streamlined hull hooped like a boiler, the
  // arms folded in against it, and the hind legs doing a full breaststroke - drawn
  // up and out, then driven back until they trail, with the webs spreading on the
  // power stroke and feathering on the recovery.
  float k=.5f+.5f*std::sin(a.phase*1.05f);           // 0 gathered, 1 driven back
  float spread=.36f+.64f*(std::sin(a.phase*1.05f+1.0f)*.5f+.5f); // never fully shut
  hull(-.250f,.290f,.086f,.078f,brass);
  // Four black rings at even spacing, and a black line along each flank.
  for(int j=0;j<4;++j){
   float z=.190f-j*.120f,r=hullR(z,-.250f,.290f);
   pod({0,0,z},{.086f*r+.005f,.078f*r+.005f,.012f},ink);
  }
  pod({0,0,-.070f},{.074f,.068f,.020f},verdDim);
  for(int side=-1;side<=1;side+=2){
   Vec3 prev{0,0,0};
   for(int j=0;j<=4;++j){
    float z=.230f-j*.114f,r=hullR(z,-.250f,.290f);
    Vec3 p{side*(.086f*r+.004f),-.004f,z};
    if(j)tube(prev,p,.0055f,.0055f,ink);
    prev=p;
   }
  }
  crest({0,.070f,-.010f},{0,.036f,-.190f},4,.034f,brassDim);
  // A short stump of a tower behind the head, with a snorkel elbow on the mast.
  sail({0,.072f,.080f},.048f,.054f,.032f,.014f,1,.044f,.046f);
  {
   Vec3 h{0,.010f,.286f};
   pod(h,{.094f,.062f,.070f},brass);
   for(int side=-1;side<=1;side+=2){
    // The eyes stand on top, as a frog's do, and are portholes like everything else.
    Vec3 e=h+Vec3{side*.054f,.058f,-.006f};
    pod(e,{.032f,.028f,.032f},brass);
    porthole(e+Vec3{0,.026f,0},unit(Vec3{float(side)*.22f,1,.16f}),.024f);
   }
   rivets(h+Vec3{-.076f,-.018f,.044f},h+Vec3{.076f,-.018f,.044f},5,.005f);
   canopy(h+Vec3{0,.024f,-.098f},.040f,.030f,.046f);
  }
  for(int side=-1;side<=1;side+=2){
   // Arms folded back along the hull, out of the water's way.
   Vec3 sh{side*.078f,-.040f,.152f};
   Vec3 el{side*.096f,-.056f,.052f};
   Vec3 hd{side*.086f,-.058f,-.030f};
   tube(sh,el,.022f,.018f,brass);tube(el,hd,.017f,.013f,brassDim);
   lobe(hd,hd+Vec3{side*.016f,-.010f,-.060f},Vec3{side*.024f,0,0},verd);
   // Hind leg: gathered forward and outboard, then driven straight back.
   Vec3 hip{side*.070f,-.036f,-.140f};
   pod({side*.064f,-.024f,-.132f},{.052f,.050f,.056f},brass);   // haunch
   Vec3 knee{side*(.196f-.090f*k),-.008f+.016f*k,-.150f-.108f*k};
   Vec3 ankle{side*(.142f-.074f*k),-.052f-.006f*k,-.244f-.156f*k};
   tube(hip,knee,.042f,.032f,brass);
   disc(knee,{float(side),.2f,0},.034f,iron,false);
   tube(knee,ankle,.034f,.024f,brass);
   disc(ankle,{float(side),0,.3f},.026f,iron,false);
   // The web: four blades off the ankle, opening on the drive.
   for(int t=0;t<4;++t){
    float f=(t-1.5f)/1.5f;
    Vec3 dir{side*(.030f+.104f*spread*(1.f-.3f*std::abs(f))),-.026f-.026f*f*f,
             -.190f-.056f*spread};
    dir.x+=side*f*.036f; dir.z+=f*.044f;
    // Tilted out of the horizontal so the web still has area from the side, which
    // is the view the city tour gives of these.
    lobe(ankle,ankle+dir,Vec3{side*.030f,.026f,.010f},t&1?verd:brass);
   }
  }
  disc({0,.006f,-.206f},{0,.22f,-.97f},.030f,iron,false);
  disc({0,.006f,-.209f},{0,.22f,-.97f},.014f,lamp,true);
 }
 material_=0;
}
}
