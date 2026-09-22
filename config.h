#pragma once
#ifndef ABYSS_ENABLE_USB_GAMEPAD
#define ABYSS_ENABLE_USB_GAMEPAD 1
#endif
// Render pixels per panel pixel. The world is drawn at UpScale times the panel and box
// filtered on the way out, so a higher resolution panel only changes PanelW/PanelH.
//
// Moving this project to a 640x480 panel is this one line and nothing else: set
// ABYSS_UPSCALE to 1 and PanelW/PanelH to 640/480, and every asset comes with it. All
// the art - title card, zone cards, HUD font, monument and species thumbnails - is
// authored in panel coordinates and drawn through hudBlit/hudText, which multiply by
// UpScale; the baked thumbnails in monument_shots.h and species_shots.h were rendered
// from a 640x480 frame and so have the detail for it already.
//
// Set to 2 instead to supersample this 320x240 panel: sharper, and four times the pixels
// to fill, which on this board costs most of the frame rate.
#ifndef ABYSS_UPSCALE
#define ABYSS_UPSCALE 1
#endif
static_assert(ABYSS_UPSCALE == 1 || ABYSS_UPSCALE == 2,
              "ABYSS_UPSCALE must be 1 or 2");
namespace abyss {
constexpr int PanelW = 320, PanelH = 240;           // the ST7789 in front of us
constexpr int UpScale = ABYSS_UPSCALE;
constexpr int Width = PanelW * UpScale;             // the frame the renderer works in
constexpr int Height = PanelH * UpScale;
constexpr int TileRows = 12;                        // band height, in render rows
constexpr int TileCount = Height / TileRows;
constexpr float Near = 0.4f, Far = 90.0f;
constexpr float Focal = Width * 0.80f;
constexpr int MaxTriangles = 80000, MaxProjected = 13000;
constexpr int WorldSize = 640, WorldMin = -320;
constexpr int ChunkSize = 32, ChunksAcross = WorldSize / ChunkSize;
constexpr int ChunkCount = ChunksAcross * ChunksAcross;
constexpr float TourSeconds = 866.0f;
}
