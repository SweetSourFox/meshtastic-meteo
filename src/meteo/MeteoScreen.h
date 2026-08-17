#pragma once

#include "configuration.h"
#include "MeteoHistory.h"
#include "MeteoModel.h"
#include "MeteoStore.h"
#include "MeteoTypes.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

#if defined(USE_ST7789)
#include <ST7789Spi.h>
#endif

namespace meteo
{

class MeteoScreen
{
  public:
    int page = PAGE_OVERVIEW;
    int forecast_sub = 0;
    int stats_sub = 0;
    int raw_sub = 0;
    int chart_ch = CH_TEMP;
    bool settings_active = false;
    int settings_line = 0;

    void draw(OLEDDisplay *display, int16_t x, int16_t y, const MeteoSettings &settings, const MeteoModel &model,
              MeteoHistory &history, int sd_state, const MeteoClock &clk);
    bool handleChar(char c, MeteoSettings &settings, MeteoStore &store);
    void flushChartColors(OLEDDisplay *d, const MeteoSettings &s, MeteoHistory &h);

  private:
    int16_t off_x_ = 0;
    int16_t off_y_ = 0;
#if defined(USE_ST7789)
    TFTColorRegion color_regions_[8] = {};
    int color_region_count_ = 0;
#endif

    int16_t sx(int x) const { return off_x_ + x; }
    int16_t sy(int y) const { return off_y_ + y; }

    uint16_t themeCol(const MeteoSettings &s, int idx) const;
    static uint16_t rgbBe(uint16_t native);
    void clearRegions();
    void addRegion(int x, int y, int w, int h, uint16_t nativeRgb);
    void applyRegions(OLEDDisplay *d);
    void drawHeader(OLEDDisplay *d, const MeteoSettings &s, int sd_state, const MeteoClock &clk);
    void drawFooter(OLEDDisplay *d, const MeteoSettings &s);
    void drawOverview(OLEDDisplay *d, const MeteoSettings &s, const MeteoModel &m);
    void drawAir(OLEDDisplay *d, const MeteoSettings &s, const MeteoModel &m);
    void drawForecast(OLEDDisplay *d, const MeteoSettings &s, const MeteoModel &m, MeteoHistory &h);
    void drawChart(OLEDDisplay *d, const MeteoSettings &s, MeteoHistory &h);
    void drawStats(OLEDDisplay *d, const MeteoModel &m, MeteoHistory &h);
    void drawRaw(OLEDDisplay *d, const MeteoModel &m, const MeteoSettings &s);
    void drawSettings(OLEDDisplay *d, MeteoSettings &s);
    void drawSparkline(OLEDDisplay *d, int x, int y, int w, int h, RingBuffer<float> &ring, float lo, float hi);
    void plotRing(OLEDDisplay *d, RingBuffer<float> &ring, float lo, float hi, int x0, int y0, int x1, int y1);
#if defined(USE_ST7789)
    void plotRingRgb(ST7789Spi *st, RingBuffer<float> &ring, float lo, float hi, int x0, int y0, int x1, int y1,
                     uint16_t nativeRgb);
    uint32_t chart_blit_hash_ = 0;
#endif
    void drawBar(OLEDDisplay *d, int x, int y, int w, int h, float frac, OLEDDISPLAY_COLOR fg, OLEDDISPLAY_COLOR bg);
    void drawFracBar(OLEDDisplay *d, int x, int y, int w, int h, float frac);
    void drawPagePips(OLEDDisplay *d, int x, int y, int active);
    void drawCoverageBar(OLEDDisplay *d, int x, int y, int w, int h, float frac, const MeteoSettings &s);
    void drawBulletBar(OLEDDisplay *d, int x, int y, int w, int h, int score, bool invert = false);
    OLEDDISPLAY_COLOR fgColor(const MeteoSettings &s) const;
    OLEDDISPLAY_COLOR bgColor(const MeteoSettings &s) const;
    OLEDDISPLAY_COLOR invColor(const MeteoSettings &s) const;
    const char *co2Band(const MeteoModel &m, const MeteoSettings &s) const;
    uint16_t co2BorderColor(const MeteoModel &m, const MeteoSettings &s) const;
    void fmtVal(char *buf, size_t n, float v, int digits) const;
};

} // namespace meteo
