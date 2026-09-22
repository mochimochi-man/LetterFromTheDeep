#pragma once
#include "config.h"
#include "math3d.h"
#include "world.h"
#include "ecology.h"
#include "mechanical.h"
// ESP newlib headers expose a legacy quad -> quad_t macro.
// Keep the polygon helper name identical in every translation unit.
#ifdef quad
#undef quad
#endif
namespace abyss {
// Top bit of material marks a face belonging to a closed body, so the renderer may
// throw it away when it is wound away from the camera. Everything else stays two-sided,
// because most of this world is thin plates - fins, fronds, kelp - with no back to hide.
constexpr uint8_t SolidFace=0x80;
struct Triangle { Vec3 a,b,c; Color color; uint8_t material; uint16_t shades[8]; };
struct Chunk { Vec3 minimum,maximum; uint32_t start; uint16_t count; };
struct Landmark { Vec3 position; const char* name; };
class Scene {
 public:
  Triangle* triangles=nullptr;
  int count=0, staticCount=0, solidCount=0;
  ecology::Ecosystem ecosystem;
  struct Colony { Vec3 position; float height; uint8_t type; };
  static constexpr int ColonyLimit=560;
  Colony colonies[ColonyLimit]{};
  int colonyCount=0;
  struct Vent { Vec3 mouth; uint32_t seed; };
  Vent vents[4]{};
  int ventCount=0;
  uint16_t* staticIndices=nullptr;
  Chunk chunks[ChunkCount]{};
  bool overflow=false;
  bool city=false,gateOpen=false;
  int gateStart=0,gateEnd=0;
  float gateSink=0;
  bool whalePresent=false;
  Vec3 whaleLocation{};
  // This frame's city machines, kept so the journal can record them like any animal.
  MachinePose cityMachines[CityMachineCount]{};
  int cityMachineCount=0;
  bool build(Triangle* storage,uint16_t* indices);
  bool buildCity(Triangle* storage,uint16_t* indices);
  void sinkGate(float metres);
  world::Environment environment(Vec3 eye) const;
  Camera cityTour(float time) const;
  void animateCity(float time,const Camera& camera);
  void animate(float time);
  void animate(float time,const Camera& camera);
  Camera tour(float time) const;
  float floor(float x,float z) const;
  float surface(float x,float z,bool solids=false) const;
  bool clearView(Vec3 from,Vec3 to,float allowance=0) const;
  bool clearHull(Vec3 position,float radius) const;
  const Landmark* nearby(Vec3 eye) const;
  void add(Vec3 a,Vec3 b,Vec3 c,Color col,bool twoSided=true);
  // Same, but for a face on a closed body: the winding is corrected against an outward
  // direction first, so a mesh built without caring which way round its faces go still
  // ends up cullable. `outward` only has to point away from the middle of the body.
  void addSolid(Vec3 a,Vec3 b,Vec3 c,Color col,Vec3 outward);
  void quadSolid(Vec3 a,Vec3 b,Vec3 c,Vec3 d,Color col,Vec3 outward);
  // Catalogue pictures: stage one creature on an empty scene, nothing else in the world.
  void poseMachine(const MachinePose& pose);
  void machineMesh(const MachinePose& pose);
  void poseCreature(const ecology::Animal& animal,bool whale);
  // Running out of triangles is fatal, so the creature meshes watch how full the buffer
  // is and give up detail as it fills. A crowded reef then degrades instead of dying.
  // Measured against what is left after the static world, not against the whole buffer.
  // The static world has grown to two thirds of the budget, and a fixed fraction of the
  // total meant the very first animal of the frame already counted as crowded, so every
  // creature was permanently held at reduced detail.
  bool crowded() const { return count>staticCount+(MaxTriangles-staticCount)*7/10; }
  bool jammed() const { return count>staticCount+(MaxTriangles-staticCount)*17/20; }
 private:
  uint8_t material_=0;
  float lighting_=1;
  Vec3 viewer_{};                              // camera position, so a mesh can pick its detail
  void buildMap();
  void buildVents();
  void buildMonuments();
  void buildSeabedLife();
  void reefCoral(Vec3 p,float size,uint32_t seed,int type);
  void seabedLife(Vec3 p,float size,uint32_t seed,int type);
  void buildIndex(uint16_t* indices);
  void quad(Vec3 a,Vec3 b,Vec3 c,Vec3 d,Color col);
  void rock(Vec3 p,Vec3 size,uint32_t seed,int style=0);
  void beam(Vec3 a,Vec3 b,float width,Color col);
  void cliff(Vec3 start,Vec3 end,float width,float height,uint32_t seed);
  void ruins();
  void vanity();
  void arch();
  void wreck();
  void coral(Vec3 p,float size,uint32_t seed);
  void prehistoricMesh(const ecology::Animal& animal);
  void livingRelicMesh(const ecology::Animal& animal);
  void sunfishMesh(const ecology::Animal& animal);
  void turtlePufferMesh(const ecology::Animal& animal);
  void largeMarineMesh(const ecology::Animal& animal);
  void animalMesh(const ecology::Animal& animal);
  void fish(Vec3 p,float yaw,float size,float phase,Color col,bool whale);
};
}
