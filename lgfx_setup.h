#pragma once
// Pin assignment and panel settings follow Aquarium_Espresso/lgfx_setup.h.
#define LGFX_USE_V1
// No filesystem extension is used: match LGFXBase.cpp's base-class definition.
// Let Arduino discover LovyanGFX.hpp before the library include path is known.
#if __has_include(<lgfx/v1/LGFXBase.hpp>)
#include <lgfx/v1/LGFXBase.hpp>
#endif
#include <LovyanGFX.hpp>
#ifndef TFT_SPI_FREQ
#define TFT_SPI_FREQ 80000000
#endif
#define PIN_TFT_SCLK 12
#define PIN_TFT_MOSI 11
#define PIN_TFT_DC 9
#define PIN_TFT_CS 10
#define PIN_TFT_RST -1
#define PIN_TFT_BLK -1
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel_;
  lgfx::Bus_SPI bus_;
 public:
  LGFX() {
    auto bus=bus_.config();
    bus.spi_host=SPI2_HOST; bus.spi_mode=0;
    bus.freq_write=TFT_SPI_FREQ; bus.freq_read=16000000;
    bus.spi_3wire=false; bus.use_lock=true; bus.dma_channel=SPI_DMA_CH_AUTO;
    bus.pin_sclk=PIN_TFT_SCLK; bus.pin_mosi=PIN_TFT_MOSI;
    bus.pin_miso=-1; bus.pin_dc=PIN_TFT_DC;
    bus_.config(bus); panel_.setBus(&bus_);
    auto p=panel_.config();
    p.pin_cs=PIN_TFT_CS; p.pin_rst=PIN_TFT_RST; p.pin_busy=-1;
    p.panel_width=240; p.panel_height=320;
    p.offset_x=0; p.offset_y=0; p.offset_rotation=0;
    p.readable=false; p.invert=true; p.rgb_order=false;
    p.dlen_16bit=false; p.bus_shared=false;
    panel_.config(p); setPanel(&panel_);
  }
};
