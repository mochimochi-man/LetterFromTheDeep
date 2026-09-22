// Letter from The Deep - an undersea exploration for the ESP32-S3 and a 320x240
// ST7789 panel. Pin assignment and controls are in README.md.
//
// Copyright 2026 mochimochi-man / Uh
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "lgfx_setup.h"
#include "renderer.h"
#include "journal.h"
#include "pilot.h"
#include "finale.h"
#include "survey_map.h"
#include "zone_title.h"
#include "title_data.h"
#include "monument_shots.h"
#include "species_shots.h"
#include "gamepad.h"
#include "hud.h"
#include <Preferences.h>
#include <cstring>

// Scene regeneration is also called from the running control loop.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

using namespace abyss;
static LGFX tft;
static Scene scene;
static Renderer renderer;
static Journal journal;
static Pilot pilot;
static Finale finale;
static SurveyMap survey;
static ZoneTitle zoneTitle;
static bool mapDirty=false;
static uint32_t lastMapSave=0;
static float surfaceTourTime=0;
static PilotInput keyboard;
static uint32_t keyboardUntil=0;
static bool padWasConnected=false;
static float lifeTime=0;
static Preferences journalStorage;
static bool journalStorageReady=false,journalDirty=false;
static uint32_t lastJournalSave=0;
static TaskHandle_t worker=nullptr, mainTask=nullptr;
static std::atomic<int> tileCursor{0};
static uint16_t* zBuffer[2]={nullptr,nullptr};
static lgfx::swap565_t* dmaBuffer[2]={nullptr,nullptr};
static constexpr int TransferRows=24;
static float tourTime=0;
static bool paused=false;
static uint8_t panel=0;
// The title runs over the same twelve minute tour; play itself is always hand flown.
static bool titleScreen=true;
// With nothing plugged into the USB port there is no way to press anything, so the
// machine runs itself: the title carries no menu, and after a few seconds the tour
// takes over and keeps going. It touches no records and never opens the map or the
// city, so an unattended board is a window rather than a save file being edited.
static bool demoMode=false;
static float titleClock=0;
constexpr float DemoTitleSeconds=8.0f;
static bool canPlay() { return usbDeviceCount()>0; }
static uint8_t titleItem=0;                       // 0 START, 1 RESET
static uint8_t monumentCursor=0;                  // row inside the discovered list
static uint8_t speciesCursor=0;                   // row inside the recorded life list
static bool haveSave=false;                       // a spot to continue from
static Camera savedView;
static bool savedCity=false;
// A first dive always opens the same way: in from the far side of the stone arch, through
// its opening, then the area card, then control. Heading up the map, so +Z.
static constexpr Vec3 IntroFrom{-12,-20,-30},IntroThrough{-12,-20,14},IntroTo{-12,-14,26};
static constexpr float IntroSeconds=8.0f,IntroBend=0.76f;   // where the path starts to rise
static float introClock=-1;                       // negative when the opening is not running
enum class Ask : uint8_t { None, Reset, Jump };
static Ask asking=Ask::None;
static float savedNotice=0;                       // seconds left on the SAVED banner
static void saveSpot();
// Opening the map is the save point: position, records and explored ground all land in NVS.
static void nextPanel() {
  if(demoMode) return;                 // no panels and no map while unattended
  panel=(panel+1)%4; survey.expanded=panel!=0;
  pilot.stop(); monumentCursor=0; speciesCursor=0; asking=Ask::None;
  if(panel==1 && !titleScreen && !demoMode) saveSpot();
}
static uint32_t frames=0, reportStart=0;
static uint64_t simulationUs=0,prepareUs=0,drawUs=0,transferUs=0;

static uint32_t workerUs=0,mainUs=0,lastDrawUs=0;
static bool transferPending=false;
static void transferFrame();
// Every on-screen word goes through the HUD font; the panel font is only a fallback
// for failures that happen before the frame buffer exists.
static void showMessage(const char* message,uint16_t ink,uint16_t paper) {
  if(transferPending) { ulTaskNotifyTake(pdTRUE,portMAX_DELAY); transferPending=false; }
  if(!renderer.pixels) {
    tft.fillScreen(paper); tft.setTextColor(ink,paper); tft.setTextSize(1);
    tft.setCursor(8,PanelH/2); tft.println(message); return;
  }
  int length=0; while(message[length]) ++length;
  hudRect(renderer.pixels,0,0,PanelW,PanelH,paper);
  hudText(renderer.pixels,std::max(2,(PanelW-length*7)/2),PanelH/2-6,message,ink);
  transferFrame();
}
static void fatal(const char* message) {
  Serial.printf("FATAL: %s\n",message);
  showMessage(message,0xf800,0);
  while(true) delay(1000);
}
static void drawWorker(void*) {
  for(;;) {
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    uint32_t begin=micros();
    renderer.renderRows(0,Height,zBuffer[0],&tileCursor);
    workerUs=micros()-begin;
    xTaskNotifyGive(mainTask);
    // Second round: the halo pass covers the whole frame, so both cores take half.
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    renderer.bloomApply(0,Height/2);
    xTaskNotifyGive(mainTask);
    // Third round: the panel transfer runs here while the main core prepares the next frame.
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    transferFrame();
    xTaskNotifyGive(mainTask);
  }
}
static void transferFrame() {
  // DMA only reads internal memory. Never change a source until its DMA finishes.
  // Each band is <32 KB. Copy/conversion overlaps the preceding band's SPI transfer.
  tft.startWrite();
  for(int y=0,band=0;y<PanelH;y+=TransferRows,++band) {
    auto* output=dmaBuffer[band&1];
    for(int row=0;row<TransferRows;++row) {
      auto* dest=reinterpret_cast<uint16_t*>(output)+row*PanelW;
      if(UpScale==1) {
        const uint16_t* input=renderer.pixels+(y+row)*Width;
        for(int x=0;x<PanelW;++x) { uint16_t c=input[x]; dest[x]=uint16_t((c<<8)|(c>>8)); }
      } else {
        // Average the square of render pixels behind each panel pixel.
        const uint16_t* upper=renderer.pixels+((y+row)*UpScale)*Width;
        const uint16_t* lower=upper+Width;
        for(int x=0;x<PanelW;++x) {
          uint16_t a=upper[x*2],b=upper[x*2+1],c=lower[x*2],d=lower[x*2+1];
          unsigned r=((a>>11)&31)+((b>>11)&31)+((c>>11)&31)+((d>>11)&31);
          unsigned g=((a>>5)&63)+((b>>5)&63)+((c>>5)&63)+((d>>5)&63);
          unsigned e=(a&31)+(b&31)+(c&31)+(d&31);
          uint16_t mix=uint16_t(((r>>2)<<11)|((g>>2)<<5)|(e>>2));
          dest[x]=uint16_t((mix<<8)|(mix>>8));
        }
      }
    }
    tft.waitDMA();
    tft.pushImageDMA(0,y,PanelW,TransferRows,output);
  }
  tft.waitDMA();
  tft.endWrite();
}
static void changeRegion(bool enter) {
  bool manual=pilot.manual;
  showMessage(enter?"DESCENDING TO THE UNDERSEA CITY":"RETURNING TO THE STONE GATE",0xffff,0x0841);
  if(enter) {
    surfaceTourTime=tourTime;tourTime=0;
    if(!scene.buildCity(scene.triangles,scene.staticIndices)) fatal("City capacity exceeded");
    journal.finalFlags|=3;journalDirty=true;
    journal.category="FINAL SEA DISCOVERED";journal.notice="UNDERSEA CITY";journal.noticeSeconds=7;
  } else {
    scene.gateOpen=true;
    if(!scene.build(scene.triangles,scene.staticIndices)) fatal("Surface capacity exceeded");
    finale.returned();tourTime=surfaceTourTime;
    journal.category="RETURN PASSAGE";journal.notice="THE STONE GATE";journal.noticeSeconds=4;
  }
  if(manual) {
    Camera c=scene.tour(tourTime);
    if(!enter) c.lookAt({-12,-12,-30},{-12,-30,4});
    pilot.enter(c);
  }
  survey.build(scene);lastMapSave=millis()-15001;
  keyboard={};keyboardUntil=0;
  Serial.printf("FINALE: %s | static=%d | stack_min_free=%u\n",enter?"CITY ENTERED":"SURFACE RETURN",scene.staticCount,unsigned(uxTaskGetStackHighWaterMark(nullptr)));
}
static int speciesList(uint8_t* rows);
static constexpr uint8_t CityRow=Journal::MonumentCount;
static constexpr int MaxRows=Journal::MonumentCount+1;
static int discoveredList(uint8_t* rows);
static const char* rowName(uint8_t row);
static const uint16_t* rowShot(uint8_t row);
// Interface text is white or a neutral grey; no hue anywhere in it.
static constexpr uint16_t InkBright=0xffff,InkDim=0xbdd7;
static void centredText(int y,const char* text,uint16_t colour) {
  int length=0; while(text[length]) ++length;
  hudText(renderer.pixels,std::max(2,(PanelW-length*7)/2),y,text,colour);
}
// One dialog shape for both irreversible answers.
static void drawAsk(const char* question,const char* detail,int top) {
  hudRect(renderer.pixels,26,top,PanelW-52,66,0x0000);
  hudRect(renderer.pixels,28,top+2,PanelW-56,62,0x1086);
  centredText(top+12,question,InkBright);
  if(detail) centredText(top+28,detail,InkDim);
  centredText(top+48,"A: YES    B: NO",InkBright);
}
static void drawTitleScreen() {
  drawTitleMask(renderer.pixels,PanelW,PanelH,TitleCard,TitleCardWidth,TitleCardHeight,255,82);
  // No menu when there is nothing to work it with: the logo and the credit, nothing else.
  if(canPlay()) {
    const char* items[]={haveSave?"CONTINUE":"START","RESET"};
    for(int i=0;i<2;++i) {
      bool on=titleItem==i && asking==Ask::None;
      if(on) hudRect(renderer.pixels,PanelW/2-46,154+i*20-3,92,17,0x39e7);
      centredText(154+i*20,items[i],on?InkBright:InkDim);
    }
  }
  centredText(PanelH-29,"Copyright 2026 mochimochi-man / Uh",InkBright);
  if(asking==Ask::Reset) drawAsk("ERASE ALL RECORDS?","MONUMENTS, LIFE AND MAP",144);
}
static void drawMonumentPanel() {
  hudRect(renderer.pixels,0,0,PanelW,PanelH,0x0843);
  hudText(renderer.pixels,6,6,"DISCOVERED MONUMENTS",InkDim);
  uint8_t rows[MaxRows];
  int count=discoveredList(rows);
  if(monumentCursor>=count) monumentCursor=0;
  // The list outgrew the panel once there were more than fourteen places to list, so it
  // scrolls with the cursor the way the life list does.
  constexpr int Visible=13;
  int first=std::max(0,std::min(count-Visible,int(monumentCursor)-Visible/2));
  if(count<=Visible) first=0;
  for(int i=first;i<std::min(count,first+Visible);++i) {
    int y=28+(i-first)*14;
    bool on=i==monumentCursor;
    if(on) hudRect(renderer.pixels,2,y-2,154,14,0x39e7);
    hudText(renderer.pixels,6,y,rowName(rows[i]),on?InkBright:InkDim);
  }
  if(!count) hudText(renderer.pixels,6,40,"NOTHING FOUND YET",InkBright);
  else {
    // The thumbnail is the exact viewpoint the A button travels to.
    const uint16_t* shot=rowShot(rows[monumentCursor]);
    hudRect(renderer.pixels,158,44,ShotWidth+2,ShotHeight+2,0x52aa);
    hudBlit(renderer.pixels,159,45,shot,ShotWidth,ShotHeight);
    if(count>Visible) {
      char tally[16];
      std::snprintf(tally,sizeof(tally),"%d / %d",monumentCursor+1,count);
      hudText(renderer.pixels,159,PanelH-22,tally,InkDim);
    }
  }
  if(asking==Ask::Jump && count) drawAsk("TRAVEL TO THIS PLACE?",rowName(rows[monumentCursor]),86);
}
static void drawSpeciesPanel() {
  hudRect(renderer.pixels,0,0,PanelW,PanelH,0x0843);
  hudText(renderer.pixels,6,6,"RECORDED LIFE",InkDim);
  uint8_t rows[SpeciesShotCount];
  int count=speciesList(rows);
  if(speciesCursor>=count) speciesCursor=0;
  constexpr int Visible=13;
  int first=std::max(0,std::min(count-Visible,int(speciesCursor)-Visible/2));
  if(count<=Visible) first=0;
  for(int i=first;i<std::min(count,first+Visible);++i) {
    int y=26+(i-first)*14;
    bool on=i==speciesCursor;
    if(on) hudRect(renderer.pixels,2,y-2,178,14,0x39e7);
    hudText(renderer.pixels,6,y,SpeciesShotName[rows[i]],on?InkBright:InkDim);
  }
  if(!count) hudText(renderer.pixels,6,40,"NOTHING RECORDED YET",InkBright);
  else {
    const uint16_t* shot=SpeciesShots[rows[speciesCursor]];
    hudRect(renderer.pixels,184,66,SpeciesShotWidth+2,SpeciesShotHeight+2,0x52aa);
    hudBlit(renderer.pixels,185,67,shot,SpeciesShotWidth,SpeciesShotHeight);
    if(count>Visible) {
      char tally[16];
      std::snprintf(tally,sizeof(tally),"%d / %d",speciesCursor+1,count);
      hudText(renderer.pixels,185,PanelH-24,tally,InkDim);
    }
  }
}
static void manualMode(bool enabled) {
  if(finale.cinematic()) return;
  if(enabled && !pilot.manual) pilot.enter(scene.tour(tourTime));
  else if(!enabled) {pilot.manual=false;pilot.stop();}
  keyboard={};
  Serial.println(pilot.manual?"CONTROL: MANUAL":"CONTROL: AUTO");
}
// Only found monuments are listed, so the panel never hints at what is still out there.
// The undersea city takes the row after them, once it has been reached.
static int discoveredList(uint8_t* rows) {
  int count=0;
  for(const auto& entry:Journal::catalog) if(journal.monuments&(1u<<entry.id)) rows[count++]=entry.id;
  if(journal.finalFlags&2) rows[count++]=CityRow;
  return count;
}
static const char* rowName(uint8_t row) {
  return row==CityRow?"UNDERSEA CITY":Journal::catalog[row].name;
}
static const uint16_t* rowShot(uint8_t row) {
  return row==CityRow?CityShot:MonumentShots[row];
}
// Recorded life, in save-id order. No travel from here; the picture is the point.
static int speciesList(uint8_t* rows) {
  int count=0;
  for(int id=0;id<SpeciesShotCount;++id) if(journal.species&(uint64_t(1)<<id)) rows[count++]=uint8_t(id);
  return count;
}
// Seven floats: eye, the point it looks at, and whether that spot is inside the city.
static void saveSpot() {
  if(!journalStorageReady) return;
  float spot[7]={pilot.camera.position.x,pilot.camera.position.y,pilot.camera.position.z,
                 pilot.camera.position.x+pilot.camera.forward.x,
                 pilot.camera.position.y+pilot.camera.forward.y,
                 pilot.camera.position.z+pilot.camera.forward.z,scene.city?1.f:0.f};
  journalStorage.putBytes("spot",spot,sizeof(spot));
  savedView.lookAt({spot[0],spot[1],spot[2]},{spot[3],spot[4],spot[5]});
  savedCity=scene.city; haveSave=true;
  auto records=journal.encode();
  if(journalStorage.putBytes("v1",records.data(),records.size())==records.size()) journalDirty=false;
  auto map=survey.encode();
  if(journalStorage.putBytes("map1",map.data(),map.size())==map.size()) mapDirty=false;
  lastJournalSave=lastMapSave=millis();
  Serial.printf("SAVE: %.0f %.0f %.0f%s\n",double(spot[0]),double(spot[1]),double(spot[2]),
    scene.city?" (city)":"");
  // Opening the map is the only save point, and nothing on screen ever said so.
  savedNotice=3.5f;
}
static void loadSpot() {
  if(!journalStorageReady || journalStorage.getBytesLength("spot")!=sizeof(float)*7) return;
  float spot[7]={};
  if(journalStorage.getBytes("spot",spot,sizeof(spot))!=sizeof(spot)) return;
  for(float v:spot) if(!std::isfinite(v)) return;
  savedView.lookAt({spot[0],spot[1],spot[2]},{spot[3],spot[4],spot[5]});
  savedCity=spot[6]>0.5f; haveSave=true;
}
static void beginPlay() {
  titleScreen=false; asking=Ask::None; panel=0; survey.expanded=false;
  manualMode(true);
  if(haveSave) {
    if(savedCity!=scene.city) changeRegion(savedCity);
    pilot.enter(savedView);
    Serial.println("TITLE: CONTINUE");
  } else {
    introClock=0;
    Camera start;
    start.lookAt(IntroFrom,IntroFrom+Vec3{0,-.18f,1});
    pilot.enter(start);
    Serial.println("TITLE: START, running the arch approach");
  }
}
static void eraseRecords() {
  Serial.println("TITLE: RESET, erasing saved records");
  if(journalStorageReady) journalStorage.clear();
  haveSave=false;
  showMessage("RECORDS ERASED",0xffff,0);
  delay(1200);
  ESP.restart();
}
// The jump lands on the very viewpoint the thumbnail was shot from.
static void jumpToMonument(int id) {
  if(id<0 || id>MaxRows-1) return;
  if(id==CityRow) {
    if(!scene.city) changeRegion(true);
    Camera framed;
    framed.lookAt({CityView[0],CityView[1],CityView[2]},{CityView[3],CityView[4],CityView[5]});
    pilot.enter(framed);
    panel=0; survey.expanded=false; asking=Ask::None;
    journal.category="TRAVELLED TO"; journal.notice="UNDERSEA CITY"; journal.noticeSeconds=4;
    Serial.println("JUMP: UNDERSEA CITY");
    return;
  }
  if(scene.city) changeRegion(false);
  const float* view=MonumentView[id];
  Camera framed;
  framed.lookAt({view[0],view[1],view[2]},{view[3],view[4],view[5]});
  pilot.enter(framed);
  panel=0; survey.expanded=false; asking=Ask::None;
  journal.category="TRAVELLED TO"; journal.notice=Journal::catalog[id].name; journal.noticeSeconds=4;
  Serial.printf("JUMP: %s at %.0f %.0f %.0f\n",Journal::catalog[id].name,double(view[0]),double(view[1]),double(view[2]));
}
// The title runs the tour as a backdrop, so it jumps over every stretch that would put a
// monument the player has not found yet on screen.
static void skipUnfoundMonuments() {
  for(int guard=0;guard<2*MonumentWindowCount;++guard) {
    if(tourTime>=TourSeconds) tourTime-=TourSeconds;
    bool jumped=false;
    for(int i=0;i<MonumentWindowCount;++i) {
      if(journal.monuments&(1u<<MonumentWindowId[i])) continue;
      if(tourTime>=MonumentWindowSpan[i][0] && tourTime<MonumentWindowSpan[i][1]) {
        tourTime=MonumentWindowSpan[i][1]; jumped=true;
      }
    }
    if(!jumped) break;
  }
}
// Hand the machine over to itself. The journal is wiped in memory only, so whatever is
// on the card is neither read from here on nor written back.
static void beginDemo() {
  demoMode=true; titleScreen=false; asking=Ask::None; panel=0; survey.expanded=false;
  introClock=-1;
  journal=Journal{};
  if(scene.city) changeRegion(false);
  pilot.stop();
  Serial.println("DEMO: nothing on the USB port, running the tour unattended");
}
// Somebody plugged a controller in. Put the title back, with the records as they were.
static void endDemo() {
  demoMode=false; titleScreen=true; titleClock=0; asking=Ask::None;
  panel=0; survey.expanded=false;
  uint8_t saved[Journal::SaveBytes];
  journal=Journal{};
  if(journalStorageReady && journalStorage.getBytes("v1",saved,sizeof(saved))==sizeof(saved))
    journal.decode(saved,sizeof(saved));
  loadSpot();
  Serial.println("DEMO: controller attached, back to the title");
}
// Up on the stick or the hat, as a single press rather than a held axis.
static int menuStep(const abyss::PadState& pad,bool& latched) {
  int step=0;
  if(pad.hat==0 || pad.axes[1]<-.6f) step=-1;
  else if(pad.hat==4 || pad.axes[1]>.6f) step=1;
  if(!step) { latched=false; return 0; }
  if(latched) return 0;
  latched=true; return step;
}
static abyss::PilotInput controls() {
  PadState pad=gamepadSnapshot();
  if(padWasConnected && !pad.connected) pilot.stop();
  padWasConnected=pad.connected;
  static bool stepLatched=false;
  if(introClock>=0) {
    if(pad.pressed&1u) introClock=IntroSeconds;      // A cuts the opening short
    return {};
  }
  if(titleScreen) {
    if(asking==Ask::Reset) {
      if(pad.pressed&1u) eraseRecords();
      else if(pad.pressed&(1u<<1)) asking=Ask::None;
    } else {
      int step=menuStep(pad,stepLatched);
      if(step) titleItem=uint8_t((titleItem+2+step)%2);
      if(pad.pressed&1u) { if(titleItem==0) beginPlay(); else asking=Ask::Reset; }
    }
    return {};
  }
  if(pad.pressed&(1u<<9)) {nextPanel();}
  if(panel==2) {
    uint8_t rows[MaxRows];
    int count=discoveredList(rows);
    if(asking==Ask::Jump) {
      if(pad.pressed&1u) { if(count) jumpToMonument(rows[monumentCursor]); asking=Ask::None; }
      else if(pad.pressed&(1u<<1)) asking=Ask::None;
    } else {
      int step=menuStep(pad,stepLatched);
      if(step && count) monumentCursor=uint8_t((monumentCursor+count+step)%count);
      if((pad.pressed&1u) && count) asking=Ask::Jump;
    }
    return {};
  }
  if(panel==3) {
    uint8_t rows[SpeciesShotCount];
    int count=speciesList(rows);
    int step=menuStep(pad,stepLatched);
    if(step && count) speciesCursor=uint8_t((speciesCursor+count+step)%count);
    return {};
  }
  if(scene.city && survey.expanded && (pad.pressed&(1u<<1))) {panel=0;survey.expanded=false;changeRegion(false);}
  PilotInput input;
  if(pad.connected) {
    input.forward=-pad.axes[1];input.strafe=pad.axes[0];
    if((pad.axisMask&0x18)==0x18) {input.yaw=pad.axes[3];input.pitch=-pad.axes[4];}
    else if((pad.axisMask&0x24)==0x24) {input.yaw=pad.axes[2];input.pitch=-pad.axes[5];}
    if(pad.hat>=0) {
      static const int8_t x[]={0,1,1,1,0,-1,-1,-1},y[]={1,1,0,-1,-1,-1,0,1};
      input.strafe=x[pad.hat];input.forward=y[pad.hat];
    }
    input.rise=((pad.buttons&(1u<<4))?1.f:0)-((pad.buttons&(1u<<5))?1.f:0);
    input.boost=pad.buttons&1;
  } else if(int32_t(keyboardUntil-millis())>0) input=keyboard;
  return input;
}
static void commands() {
  while(Serial.available()) {
    int c=Serial.read();
    if(c==' ') paused=!paused;
    else if(c=='r' && !finale.cinematic()) {if(scene.city) changeRegion(false);tourTime=0;introClock=-1;if(!titleScreen) pilot.enter(scene.tour(tourTime));}
    else if(c=='n' && !finale.cinematic()) {tourTime=std::fmod(tourTime+20.0f,TourSeconds);introClock=-1;if(!titleScreen) pilot.enter(scene.tour(tourTime));}
    else if(c>='1' && c<='5' && !finale.cinematic()) {if(scene.city) changeRegion(false);tourTime=world::zoneTourTime(c-'1');introClock=-1;if(!titleScreen) pilot.enter(scene.tour(tourTime));}
    else if(c=='v') {nextPanel();}
    else if(c=='p') {
      if(titleScreen && asking==Ask::None) titleItem=uint8_t((titleItem+1)%2);
      else if(panel==2 && asking==Ask::None) {uint8_t rows[MaxRows];int n=discoveredList(rows);if(n) monumentCursor=uint8_t((monumentCursor+1)%n);}
      else if(panel==3) {uint8_t rows[SpeciesShotCount];int n=speciesList(rows);if(n) speciesCursor=uint8_t((speciesCursor+1)%n);}
    }
    else if(c=='g') {
      if(titleScreen && asking==Ask::Reset) eraseRecords();
      else if(titleScreen) {if(titleItem==0) beginPlay(); else asking=Ask::Reset;}
      else if(panel==2 && asking==Ask::Jump) {uint8_t rows[MaxRows];int n=discoveredList(rows);if(n) jumpToMonument(rows[monumentCursor]);asking=Ask::None;}
      else if(panel==2) {uint8_t rows[MaxRows];if(discoveredList(rows)) asking=Ask::Jump;}
    }
    else if(c=='q') asking=Ask::None;
    else if(c=='f') {
      // Diagnostic: stand the save up as a completed run without flying the whole map,
      // and wind the ending back so the announcement and the descent play again.
      if(scene.city) changeRegion(false);
      journal.monuments=(1u<<Journal::MonumentCount)-1; journal.finalFlags=0; journalDirty=true;
      if(scene.gateOpen) {
        scene.gateOpen=false;
        if(!scene.build(scene.triangles,scene.staticIndices)) fatal("Gate reset capacity exceeded");
        survey.build(scene);
      }
      finale.restore(false,false);
      journal.noticeSeconds=0;   // the gate announces itself now
      Serial.printf("DIAG: all %d monuments found, ending rewound\n",Journal::MonumentCount);
    }
    else if(c=='b' && scene.city) {panel=0;survey.expanded=false;changeRegion(false);}
    else if(c=='y' && !scene.city && !finale.cinematic()) {
      // Diagnostic counterpart to b: drop into the city without flying the ending first.
      journal.finalFlags|=3; journalDirty=true; jumpToMonument(CityRow);
    }
    else if(c=='m' || c=='t') manualMode(true);
    else if(c=='x') {keyboard={};keyboardUntil=0;pilot.stop();}
    else if(c=='w' || c=='s' || c=='a' || c=='d' || c=='u' || c=='o' || c=='h' || c=='l' || c=='i' || c=='k') {
      if(!pilot.manual) manualMode(true);
      keyboard={};keyboardUntil=millis()+350;
      if(c=='w') keyboard.forward=1;
      if(c=='s') keyboard.forward=-1;
      if(c=='a') keyboard.strafe=-1;
      if(c=='d') keyboard.strafe=1;
      if(c=='u') keyboard.rise=1;
      if(c=='o') keyboard.rise=-1;
      if(c=='h') keyboard.yaw=-1;
      if(c=='l') keyboard.yaw=1;
      if(c=='i') keyboard.pitch=1;
      if(c=='k') keyboard.pitch=-1;
    }
    else if(c=='F') {
      // Development aid: mark every monument, vent, animal and machine as found so the
      // whole catalogue can be looked over. Saved like any other discovery, so RESET on
      // the title is the way back to an empty book.
      for(const auto& entry:Journal::catalog) journal.monuments|=1u<<entry.id;
      journal.species=~uint64_t(0)>>(64-SpeciesShotCount);
      journal.nature=0xffffffffu;
      journal.finalFlags|=3;
      journalDirty=true; lastJournalSave=0;
      Serial.printf("JOURNAL: all %d monuments and %d creatures marked found\n",
                    int(Journal::MonumentCount),int(SpeciesShotCount));
    }
    else if(c=='j') {
      Serial.printf("JOURNAL monuments=%08lx species=%016llx nature=%08lx final_ready=%d\n",
        (unsigned long)journal.monuments,(unsigned long long)journal.species,(unsigned long)journal.nature,journal.allMonumentsFound());
      if(journal.species&(uint64_t(1)<<26)) Serial.println("whale");
      for(const auto& entry:Journal::catalog) if(journal.monuments&(1u<<entry.id)) Serial.println(entry.name);
      for(int i=0;i<int(ecology::Species::Count);++i) {
        auto species=ecology::Species(i);
        if(journal.species&(uint64_t(1)<<Journal::speciesId(species))) Serial.println(ecology::name(species));
      }
    }
    else if(c=='?') Serial.println("F: unlock the whole catalogue | v: panels | y/b: enter/leave city | m: take control | wasd: thrust | u/o: rise/dive | h/l i/k: look | x: stop | space: pause | r: restart | n: skip | 1-5: zones | j: journal | c: capture");
    else if(c=='c') {
      // Let the frame in flight land first, so the dump matches the panel exactly, and
      // mute the USB task: its status line would land inside the binary and shift it.
      if(transferPending) { ulTaskNotifyTake(pdTRUE,portMAX_DELAY); transferPending=false; }
      serialQuiet=true;
      // Diagnostic snapshot of what the panel shows, little-endian RGB565.
      Serial.printf("ABYSS_FRAME %d %d %d\n",PanelW,PanelH,PanelW*PanelH*2);
      for(int y=0;y<PanelH;++y) {
        uint16_t row[PanelW];
        const uint16_t* upper=renderer.pixels+(y*UpScale)*Width;
        const uint16_t* lower=upper+(UpScale>1?Width:0);
        for(int x=0;x<PanelW;++x) {
          if(UpScale==1) { row[x]=upper[x]; continue; }
          uint16_t a=upper[x*2],b=upper[x*2+1],c2=lower[x*2],d=lower[x*2+1];
          unsigned r=((a>>11)&31)+((b>>11)&31)+((c2>>11)&31)+((d>>11)&31);
          unsigned g=((a>>5)&63)+((b>>5)&63)+((c2>>5)&63)+((d>>5)&63);
          unsigned e=(a&31)+(b&31)+(c2&31)+(d&31);
          row[x]=uint16_t(((r>>2)<<11)|((g>>2)<<5)|(e>>2));
        }
        Serial.write(reinterpret_cast<const uint8_t*>(row),sizeof(row));
      }
      Serial.println("\nABYSS_END");
      serialQuiet=false;
    }
  }
}
void setup() {
  Serial.begin(115200);
  delay(250);
  tft.init(); tft.setRotation(1); tft.setColorDepth(16); tft.fillScreen(0);
#if PIN_TFT_BLK >= 0
  pinMode(PIN_TFT_BLK,OUTPUT); digitalWrite(PIN_TFT_BLK,HIGH);
#endif
  Serial.printf("\nABYSSAL S3 | render %dx%d | ST7789 320x240 SPI %u MHz\n",
                Width,Height,unsigned(TFT_SPI_FREQ/1000000));
  if(!psramFound()) fatal("OPI PSRAM missing: select PSRAM = OPI");
  auto* triangles=static_cast<Triangle*>(heap_caps_malloc(sizeof(Triangle)*MaxTriangles,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  auto* projected=static_cast<Projected*>(heap_caps_malloc(sizeof(Projected)*MaxProjected,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  // The frame buffer is the hottest surface in the renderer, so internal RAM is worth
  // having - but only if what comes after it still fits. At 320x240 the frame is 150 KB
  // and would be granted, then the render task, the DMA bands and the USB host would
  // have nothing left and the board would hang before it could say so. Measure the
  // headroom rather than assuming from the resolution.
  const size_t frameBytes=size_t(Width)*Height*sizeof(uint16_t);
  const size_t internalFree=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const bool frameFitsInternally=internalFree>frameBytes+140*1024;
  auto* pixels=static_cast<uint16_t*>(heap_caps_calloc(Width*Height,sizeof(uint16_t),
      frameFitsInternally?(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT):(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
  if(!pixels) pixels=static_cast<uint16_t*>(heap_caps_calloc(Width*Height,sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  Serial.printf("FRAME buffer %u KB in %s (internal free was %u KB)\n",
      unsigned(frameBytes/1024),frameFitsInternally?"internal":"PSRAM",unsigned(internalFree/1024));
  Serial.printf("FRAME: render %dx%d, panel %dx%d\n",Width,Height,PanelW,PanelH);
  for(int i=0;i<2;++i) {
    zBuffer[i]=static_cast<uint16_t*>(heap_caps_malloc(Width*TileRows*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    dmaBuffer[i]=static_cast<lgfx::swap565_t*>(heap_caps_malloc(PanelW*TransferRows*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));
    if(!zBuffer[i] || !dmaBuffer[i]) fatal("Internal render/DMA buffer allocation failed");
  }
  auto* bins=static_cast<uint16_t*>(heap_caps_malloc(TileCount*MaxProjected*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  auto* indices=static_cast<uint16_t*>(heap_caps_malloc(MaxTriangles*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!triangles || !projected || !pixels || !bins || !indices) fatal("Scene/frame allocation failed");
  renderer.attach(projected,pixels,bins);
  showMessage("LOADING",0xffff,0);
  if(!scene.build(triangles,indices)) fatal("Scene capacity exceeded");
  mainTask=xTaskGetCurrentTaskHandle();
  BaseType_t ok=xTaskCreatePinnedToCore(drawWorker,"abyss-draw",6144,nullptr,1,&worker,0);
  if(ok!=pdPASS) fatal("Render worker creation failed");
  Serial.printf("static triangles=%d | PSRAM=%u free=%u | internal free=%u\n",
    scene.staticCount,unsigned(ESP.getPsramSize()),
    unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  Serial.println("MAP: 640m x 640m | 5 basins | 12-minute tour | serial 1-5: zone jump");
  journalStorageReady=journalStorage.begin("abyss-journal",false);
  if(journalStorageReady && journalStorage.getBytesLength("v1")==Journal::SaveBytes) {
    uint8_t saved[Journal::SaveBytes];
    if(journalStorage.getBytes("v1",saved,sizeof(saved))==sizeof(saved)) journal.decode(saved,sizeof(saved));
  }
  // One-time restart of monument progression requested for the exploration release.
  // Keep a backup and preserve species/nature records and explored terrain.
  if(journalStorageReady && !journalStorage.getBool("cityReset2",false)) {
    auto backup=journal.encode();journalStorage.putBytes("preCity2",backup.data(),backup.size());
    journal.monuments=0;journal.finalFlags=0;
    auto fresh=journal.encode();
    if(journalStorage.putBytes("v1",fresh.data(),fresh.size())==fresh.size()) journalStorage.putBool("cityReset2",true);
  }
  Serial.printf("JOURNAL loaded monuments=%08lx species=%016llx nature=%08lx storage=%d\n",
    (unsigned long)journal.monuments,(unsigned long long)journal.species,(unsigned long)journal.nature,journalStorageReady);
  finale.restore(journal.allMonumentsFound(),journal.finalFlags&1);
  if(journal.allMonumentsFound()) {
    journal.category="ALL MONUMENTS DISCOVERED";journal.notice="RETURN TO THE STONE GATE";journal.noticeSeconds=8;
  }
  if(journal.finalFlags&1) {
    scene.gateOpen=true;
    if(!scene.build(scene.triangles,scene.staticIndices)) fatal("Open gate capacity exceeded");
  }
  if(journalStorageReady && journalStorage.getBytesLength("map1")==SurveyMap::SaveBytes) {
    uint8_t saved[SurveyMap::SaveBytes];
    if(journalStorage.getBytes("map1",saved,sizeof(saved))==sizeof(saved)) survey.decode(saved,sizeof(saved));
  }
  loadSpot();
  survey.build(scene);
  skipUnfoundMonuments();
  pilot.enter(scene.tour(tourTime));
  pilot.manual=false;
  startGamepadHost();
  reportStart=millis();
}
void loop() {
  static uint32_t last=micros();
  uint32_t start=micros();
  float dt=clampf((start-last)*.000001f,0,.20f); last=start;
  savedNotice=std::max(0.0f,savedNotice-dt);   // runs on wall time, map open or not
  commands();
  uint32_t t0=micros();
  PilotInput input=controls();
  if(!paused && !survey.expanded) {
    lifeTime=std::fmod(lifeTime+dt,86400.f);
    if(!finale.cinematic()) {
      if(introClock>=0) {
        introClock+=dt;
        float t=clampf(introClock/IntroSeconds,0,1);
        float e=t*t*(3-2*t);                       // one curve, standing still at both ends
        // Past the arch the line bends up and the view lifts with it, so the boat glides
        // to a halt instead of stopping dead.
        float tail=e<IntroBend?0:(e-IntroBend)/(1-IntroBend);
        Vec3 p=e<IntroBend?mix(IntroFrom,IntroThrough,e/IntroBend):mix(IntroThrough,IntroTo,tail);
        Camera opening;
        opening.lookAt(p,p+Vec3{0,-.18f+tail*.28f,1});
        pilot.enter(opening);
        if(t>=1) { introClock=-1; Serial.println("INTRO: through the arch"); }
      }
      else if(pilot.manual) pilot.update(scene,input,dt);
      else {
        tourTime=std::fmod(tourTime+dt,scene.city?86400.f:TourSeconds);
        if(titleScreen) skipUnfoundMonuments();
      }
    }
  } else pilot.stop();
  if(titleScreen) {
    if(canPlay()) titleClock=0;
    else if((titleClock+=dt)>=DemoTitleSeconds) beginDemo();
  } else if(demoMode && canPlay()) endDemo();
  Camera camera=pilot.manual?pilot.camera:scene.tour(tourTime);
  if(!titleScreen && !demoMode && !finale.cinematic() && survey.visit(camera.position,scene.city)) mapDirty=true;
  auto event=(paused || survey.expanded || titleScreen || demoMode || introClock>=0)?Finale::Event::None:finale.update(journal.allMonumentsFound(),camera,dt,pilot.manual);
  if(event==Finale::Event::GateOpens) {
    // No cutscene and no teleport: the picture shakes, one line lands in the middle of
    // the screen, and the boat is left exactly where the pilot had it.
    journal.noticeSeconds=0;
    Serial.println("FINALE: A NEW GATE HAS OPENED");
  }
  if(finale.announcing()) scene.sinkGate(finale.clock*4.6f);
  if(event==Finale::Event::OpenGate) {
    scene.gateOpen=true;
    if(!scene.build(scene.triangles,scene.staticIndices)) fatal("Open gate capacity exceeded");
    survey.build(scene);
    if(pilot.manual) pilot.enter(finale.start);
    journal.finalFlags|=1;journalDirty=true;
    journal.category="PASSAGE OPEN";journal.notice="DIVE BELOW THE STONE GATE";journal.noticeSeconds=7;
    Serial.println("FINALE: GATE OPEN");
  }
  if(event==Finale::Event::BeginDive) {
    // The shaft has the boat now: hands off the controls until the city is reached.
    pilot.stop();
    journal.category="THE PASSAGE TAKES HOLD";journal.notice="DESCENDING";journal.noticeSeconds=5;
    Serial.println("FINALE: DIVE BEGINS");
  }
  if(event==Finale::Event::EnterCity || event==Finale::Event::ReturnSurface) {
    changeRegion(event==Finale::Event::EnterCity);
    camera=pilot.manual?pilot.camera:scene.tour(tourTime);
  } else if(event==Finale::Event::OpenGate && pilot.manual) camera=pilot.camera;
  camera=finale.view(camera);
  if(!titleScreen && !finale.cinematic() && survey.visit(camera.position,scene.city)) mapDirty=true;
  scene.animate(lifeTime,camera);
  uint32_t ts=micros();
  renderer.prepare(scene,camera,lifeTime);
  if(scene.overflow || renderer.stats.overflow) fatal("Triangle capacity exceeded");
  uint32_t t1=micros();
  // The previous frame is still going out over SPI; nothing may touch pixels until it lands.
  if(transferPending) { ulTaskNotifyTake(pdTRUE,portMAX_DELAY); transferPending=false; }
  uint32_t tw=micros();
  tileCursor.store(0,std::memory_order_relaxed);
  xTaskNotifyGive(worker);
  uint32_t mainBegin=micros();
  renderer.renderRows(0,Height,zBuffer[1],&tileCursor);
  mainUs=micros()-mainBegin;
  ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
  renderer.bloomSeed();
  xTaskNotifyGive(worker);
  renderer.bloomApply(Height/2,Height);
  ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
  uint32_t t2=micros(); lastDrawUs=t2-t1;
  if(!demoMode && journal.update(scene,camera,(paused || survey.expanded || titleScreen)?0:dt)) {
    journalDirty=true;
    if(journal.allMonumentsFound() && finale.phase==Finale::Phase::Locked) journal.noticeSeconds=0;
    Serial.printf("DISCOVERY %s: %s\n",journal.category,journal.notice);
  }
  // Whatever the opening run passes is recorded, but never announced over it.
  if(introClock>=0) journal.noticeSeconds=0;
  if(demoMode) { journalDirty=false; mapDirty=false; }
  if(journalDirty && journalStorageReady && millis()-lastJournalSave>1500) {
    auto saved=journal.encode();lastJournalSave=millis();
    if(journalStorage.putBytes("v1",saved.data(),saved.size())==saved.size()) journalDirty=false;
  }
  if(mapDirty && journalStorageReady && millis()-lastMapSave>15000) {
    auto saved=survey.encode();lastMapSave=millis();
    if(journalStorage.putBytes("map1",saved.data(),saved.size())==saved.size()) mapDirty=false;
  }
  if(titleScreen) {
    drawTitleScreen();
  } else {
  if(!demoMode) survey.draw(renderer.pixels,PanelW,PanelH,camera,scene.city,journal.monuments);
  if(!survey.expanded) {
    int zone=scene.environment(camera.position).zone;
    // The area card waits for the opening to finish, so it lands as the arch is cleared.
    if(introClock<0 && zoneTitle.update(zone,paused?0:dt)) Serial.printf("AREA: %s\n",scene.city?"UNDERSEA CITY":world::zoneName(zone));
    zoneTitle.draw(renderer.pixels,PanelW,PanelH);
  }
  if(panel==3) {
    drawSpeciesPanel();
  } else if(panel==2) {
    drawMonumentPanel();
  } else if(panel==1) {
    hudText(renderer.pixels,12,2,scene.city?"UNDERSEA CITY / MAP":"FIVE SEAS / MAP",InkDim);
    if(savedNotice>0) {
      hudRect(renderer.pixels,4,PanelH-31,PanelW-8,29,0x0843);
      hudText(renderer.pixels,9,PanelH-29,"SAVED",InkDim);
      hudText(renderer.pixels,9,PanelH-15,"POSITION, RECORDS AND MAP");
    }
  } else {
    if(finale.announcing()) {
      hudRect(renderer.pixels,18,PanelH/2-15,PanelW-36,30,0x0000);
      hudRect(renderer.pixels,20,PanelH/2-13,PanelW-40,26,0x2124);
      centredText(PanelH/2-5,"A NEW GATE HAS OPENED",InkBright);
    }
    if(journal.noticeSeconds>0 && journal.notice) {
      hudRect(renderer.pixels,4,PanelH-31,PanelW-8,29,0x0843);
      hudText(renderer.pixels,9,PanelH-29,journal.category,InkDim);
      hudText(renderer.pixels,9,PanelH-15,journal.notice);
    }
  }
  }
  uint32_t td=micros();
  xTaskNotifyGive(worker); transferPending=true;
  simulationUs+=(ts-t0)+(td-t2); prepareUs+=t1-ts; drawUs+=t2-tw; transferUs+=tw-t1; ++frames;
  uint32_t elapsed=millis()-reportStart;
  if(elapsed>=5000) {
    const Landmark* place=scene.nearby(camera.position);
    Serial.printf("%.1f fps | sim %.2f prep %.2f draw %.2f lcd %.2f ms | tri %d/%d/%d chunks %d | emit %u ras %u halo %u us | fish %d | tour %.1fs | depth %.1fm | lamp %d%% | %s | %s\n",
      frames*1000.0f/elapsed,simulationUs/(1000.0f*frames),prepareUs/(1000.0f*frames),
      drawUs/(1000.0f*frames),transferUs/(1000.0f*frames),
      renderer.stats.visible,renderer.stats.submitted,scene.count,renderer.stats.chunks,
      unsigned(renderer.stats.emitUs),unsigned(renderer.stats.rasterUs),
      unsigned(lastDrawUs-std::max(mainUs,workerUs)),scene.ecosystem.count,tourTime,-camera.position.y,int(renderer.headlightStrength*100),
      pilot.manual?"MANUAL":"AUTO",place?place->name:world::zoneName(world::environment(camera.position.x,camera.position.z).zone));
    frames=0; simulationUs=prepareUs=drawUs=transferUs=0; reportStart=millis();
  }
  // Give idle tasks time even when a frame takes longer than the 30 Hz target.
  uint32_t used=micros()-start;
  delay(used<33000 ? std::max<uint32_t>(1,(33000-used)/1000) : 1);
}
