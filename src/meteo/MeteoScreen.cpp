#include "MeteoScreen.h"
#include "MeteoDebug.h"
#include "MeteoForecast.h"
#include "MeteoIaq.h"
#include "MeteoModel.h"
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(USE_ST7789)
extern uint16_t TFT_MESH;
#endif

namespace meteo
{

namespace
{
constexpr int HEADER_H = 18;
constexpr int FOOTER_Y = 116;
constexpr int FOOTER_H = 19;

const char *stateName(int st)
{
    static const char *names[] = {"ABSENT", "STARTING", "WARMING", "READY", "STALE", "ERROR"};
    if (st >= 0 && st < 6)
        return names[st];
    return "?";
}

const char *sdLabel(int sd_state)
{
    if (sd_state == 1)
        return "SD+";
    if (sd_state == 2)
        return "SDL";
    return "SD-";
}

void fmtMmss(char *buf, size_t n, int sec)
{
    if (sec < 0)
        sec = 0;
    snprintf(buf, n, "%d:%02d", sec / 60, sec % 60);
}
} // namespace

uint16_t MeteoScreen::themeCol(const MeteoSettings &s, int idx) const
{
    return THEMES[s.theme % 4][idx];
}

uint16_t MeteoScreen::rgbBe(uint16_t native)
{
    return uint16_t((native >> 8) | (native << 8));
}

void MeteoScreen::clearRegions()
{
#if defined(USE_ST7789)
    color_region_count_ = 0;
    for (int i = 0; i < 8; i++)
        color_regions_[i].enabled = false;
#endif
}

void MeteoScreen::addRegion(int x, int y, int w, int h, uint16_t nativeRgb)
{
#if defined(USE_ST7789)
    if (color_region_count_ >= 7 || w < 2 || h < 2)
        return;
    auto &r = color_regions_[color_region_count_++];
    r.x = sx(x);
    r.y = sy(y);
    r.width = w;
    r.height = h;
    r.onColorBe = rgbBe(nativeRgb);
    r.offColorBe = 0;
    r.enabled = true;
    color_regions_[color_region_count_].enabled = false;
#endif
}

void MeteoScreen::applyRegions(OLEDDisplay *d)
{
#if defined(USE_ST7789)
    auto *st = static_cast<ST7789Spi *>(d);
    // White fallback (not TFT_MESH yellow) so Meteo text stays white; regions supply accents.
    if (color_region_count_ > 0)
        st->setRGB(0xFFFF, color_regions_);
    else
        st->setRGB(0xFFFF, nullptr);
    // #region agent log
    static int last_n = -99;
    static uint16_t last_c0 = 0;
    uint16_t c0 = color_region_count_ > 0 ? color_regions_[0].onColorBe : 0;
    if (color_region_count_ != last_n || c0 != last_c0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"n\":%d,\"c0be\":%u,\"native0\":%u,\"page\":%d}", color_region_count_, unsigned(c0),
                 color_region_count_ > 0 ? unsigned(uint16_t((c0 >> 8) | (c0 << 8))) : 0, page);
        meteoDbg("H-color", "MeteoScreen.cpp:applyRegions", "regions", buf);
        last_n = color_region_count_;
        last_c0 = c0;
    }
    // #endregion
#endif
}

OLEDDISPLAY_COLOR MeteoScreen::fgColor(const MeteoSettings &s) const
{
    return (s.theme % 4) == 3 ? BLACK : WHITE;
}

OLEDDISPLAY_COLOR MeteoScreen::bgColor(const MeteoSettings &s) const
{
    return (s.theme % 4) == 3 ? WHITE : BLACK;
}

OLEDDISPLAY_COLOR MeteoScreen::invColor(const MeteoSettings &s) const
{
    return fgColor(s) == WHITE ? BLACK : WHITE;
}

void MeteoScreen::fmtVal(char *buf, size_t n, float v, int digits) const
{
    if (std::isnan(v) || !std::isfinite(v)) {
        snprintf(buf, n, "--");
        return;
    }
    if (digits == 0)
        snprintf(buf, n, "%d", int(v + 0.5f));
    else
        snprintf(buf, n, "%.*f", digits, v);
}

const char *MeteoScreen::co2Band(const MeteoModel &m, const MeteoSettings &s) const
{
    if (!m.co2.valid)
        return "";
    float v = m.co2.display;
    if (v < s.co2_warn)
        return "GOOD";
    if (v < s.co2_high)
        return "FAIR";
    return "VENTILATE";
}

uint16_t MeteoScreen::co2BorderColor(const MeteoModel &m, const MeteoSettings &s) const
{
    const char *band = co2Band(m, s);
    if (!band[0])
        return themeCol(s, 7);
    if (strcmp(band, "GOOD") == 0)
        return themeCol(s, 4);
    if (strcmp(band, "FAIR") == 0)
        return themeCol(s, 5);
    return themeCol(s, 6);
}

void MeteoScreen::drawHeader(OLEDDisplay *d, const MeteoSettings &s, int sd_state, const MeteoClock &clk)
{
    d->setColor(bgColor(s));
    d->fillRect(sx(0), sy(0), W, HEADER_H);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->setFont(ArialMT_Plain_10);
    d->setColor(fgColor(s));
    d->drawString(sx(4), sy(4), "METEO");
    char right[32];
    if (clk.valid)
        snprintf(right, sizeof(right), "%s %02d:%02d", sdLabel(sd_state), clk.hour, clk.minute);
    else
        snprintf(right, sizeof(right), "%s", sdLabel(sd_state));
    d->setTextAlignment(TEXT_ALIGN_RIGHT);
    d->drawString(sx(W - 4), sy(4), right);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->drawHorizontalLine(sx(0), sy(HEADER_H - 1), W);
}

void MeteoScreen::drawFooter(OLEDDisplay *d, const MeteoSettings &s)
{
    d->setColor(bgColor(s));
    d->fillRect(sx(0), sy(FOOTER_Y), W, FOOTER_H);
    d->setFont(ArialMT_Plain_10);
    d->setColor(fgColor(s));
    const char *msg;
    if (page == PAGE_SETTINGS)
        msg = "UD SEL  LR CHG  ENT SAVE";
    else if (page == PAGE_CHART)
        msg = "UD FOCUS  THPCGQ  <> PAGE";
    else if (page == PAGE_FORECAST || page == PAGE_RAW || page == PAGE_STATS)
        msg = "UD MORE  <> PAGE  ESC";
    else
        msg = "<> PAGE  M MENU  ESC EXIT";
    d->setTextAlignment(TEXT_ALIGN_CENTER);
    d->drawString(sx(W / 2), sy(120), msg);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
}

void MeteoScreen::drawBar(OLEDDisplay *d, int x, int y, int w, int h, float frac, OLEDDISPLAY_COLOR fg, OLEDDISPLAY_COLOR bg)
{
    if (frac < 0)
        frac = 0;
    if (frac > 1)
        frac = 1;
    d->setColor(bg);
    d->fillRect(sx(x), sy(y), w, h);
    d->setColor(fg);
    d->fillRect(sx(x), sy(y), int(w * frac), h);
}

void MeteoScreen::drawFracBar(OLEDDisplay *d, int x, int y, int w, int h, float frac)
{
    d->drawRect(sx(x), sy(y), w, h);
    if (frac < 0)
        frac = 0;
    if (frac > 1)
        frac = 1;
    int fw = int(w * frac);
    if (fw > 2)
        d->fillRect(sx(x + 1), sy(y + 1), fw - 2, h - 2);
}

void MeteoScreen::drawBulletBar(OLEDDisplay *d, int x, int y, int w, int h, int score, bool invert)
{
    d->drawRect(sx(x), sy(y), w, h);
    if (score < 0)
        return;
    float frac = score / 100.0f;
    if (frac < 0)
        frac = 0;
    if (frac > 1)
        frac = 1;
    int fw = int((w - 2) * frac);
    if (fw < 1)
        return;
    d->fillRect(sx(x + 1), sy(y + 1), fw, h - 2);
}

void MeteoScreen::drawPagePips(OLEDDisplay *d, int x, int y, int active)
{
    for (int i = 0; i < 3; i++) {
        if (i == active)
            d->fillRect(sx(x + i * 6), sy(y), 2, 2);
        else
            d->drawRect(sx(x + i * 6), sy(y), 2, 2);
    }
}

void MeteoScreen::drawCoverageBar(OLEDDisplay *d, int x, int y, int w, int h, float frac, const MeteoSettings &s)
{
    (void)s;
    d->drawRect(sx(x), sy(y), w, h);
    int tick1 = x + w / 3;
    int tick2 = x + (2 * w) / 3;
    d->drawVerticalLine(sx(tick1), sy(y), h);
    d->drawVerticalLine(sx(tick2), sy(y), h);
    if (frac > 1)
        frac = 1;
    int fw = int(w * frac);
    if (fw > 2)
        d->fillRect(sx(x + 1), sy(y + 1), fw - 2, h - 2);
}

void MeteoScreen::plotRing(OLEDDisplay *d, RingBuffer<float> &ring, float lo, float hi, int x0, int y0, int x1, int y1)
{
    float span = hi - lo;
    if (span <= 0)
        span = 1.0f;
    int n = ring.count();
    if (n < 2)
        return;
    d->setColor(WHITE);
    int px = -1, py = -1;
    for (int i = 0; i < n; i++) {
        uint32_t t;
        float v;
        ring.at(i, &t, &v);
        int cx = x0 + (i * (x1 - x0 - 1)) / (n - 1);
        int cy = y1 - 1 - int((v - lo) / span * (y1 - y0 - 1));
        if (cy < y0)
            cy = y0;
        if (cy > y1 - 1)
            cy = y1 - 1;
        if (px >= 0)
            d->drawLine(sx(px), sy(py), sx(cx), sy(cy));
        px = cx;
        py = cy;
    }
}

#if defined(USE_ST7789)
static void rgbLine(ST7789Spi *st, int x0, int y0, int x1, int y1, uint16_t nativeRgb)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        st->pushRGBPixel(x0, y0, nativeRgb);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void MeteoScreen::plotRingRgb(ST7789Spi *st, RingBuffer<float> &ring, float lo, float hi, int x0, int y0, int x1, int y1,
                              uint16_t nativeRgb)
{
    float span = hi - lo;
    if (span <= 0)
        span = 1.0f;
    int n = ring.count();
    if (n < 2)
        return;
    int px = -1, py = -1;
    for (int i = 0; i < n; i++) {
        uint32_t t;
        float v;
        ring.at(i, &t, &v);
        int cx = x0 + (i * (x1 - x0 - 1)) / (n - 1);
        int cy = y1 - 1 - int((v - lo) / span * (y1 - y0 - 1));
        if (cy < y0)
            cy = y0;
        if (cy > y1 - 1)
            cy = y1 - 1;
        if (px >= 0)
            rgbLine(st, px, py, cx, cy, nativeRgb);
        px = cx;
        py = cy;
    }
}
#endif

void MeteoScreen::drawSparkline(OLEDDisplay *d, int x, int y, int w, int h, RingBuffer<float> &ring, float lo, float hi)
{
    d->drawRect(sx(x), sy(y), w, h);
    int n = ring.count();
    if (n < 2) {
        d->setTextAlignment(TEXT_ALIGN_CENTER);
        d->drawString(sx(x + w / 2), sy(y + h / 2 - 4), "---");
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        return;
    }
    float span = hi - lo;
    if (span <= 0)
        span = 1.0f;
    d->setColor(WHITE);
    int px = -1, py = -1;
    for (int i = 0; i < n; i++) {
        uint32_t t;
        float v;
        ring.at(i, &t, &v);
        int cx = x + 1 + (i * (w - 2)) / (n - 1);
        int cy = y + h - 1 - int((v - lo) / span * (h - 2));
        if (cy < y + 1)
            cy = y + 1;
        if (cy > y + h - 1)
            cy = y + h - 1;
        if (px >= 0)
            d->drawLine(sx(px), sy(py), sx(cx), sy(cy));
        px = cx;
        py = cy;
    }
}

void MeteoScreen::drawOverview(OLEDDisplay *d, const MeteoSettings &s, const MeteoModel &m)
{
    struct Card {
        int x, y;
        const char *lbl;
        char val[16];
        uint16_t border;
    };
    char tbuf[16], hbuf[16], pbuf[16], cbuf[16];
    fmtVal(tbuf, sizeof(tbuf), m.temp.valid ? m.temp.display : NAN, 1);
    fmtVal(hbuf, sizeof(hbuf), m.rh.valid ? m.rh.display : NAN, 0);
    fmtVal(pbuf, sizeof(pbuf), m.press.valid ? m.press.display : NAN, 1);
    fmtVal(cbuf, sizeof(cbuf), m.co2.valid ? m.co2.display : NAN, 0);

    Card cards[] = {
        {5, 22, "TEMP", {0}, themeCol(s, 2)},
        {123, 22, "HUM", {0}, themeCol(s, 2)},
        {5, 69, "PRESS", {0}, themeCol(s, 2)},
        {123, 69, "CO2", {0}, co2BorderColor(m, s)},
    };
    snprintf(cards[0].val, sizeof(cards[0].val), "%s", tbuf);
    snprintf(cards[1].val, sizeof(cards[1].val), "%s", hbuf);
    snprintf(cards[2].val, sizeof(cards[2].val), "%s", pbuf);
    snprintf(cards[3].val, sizeof(cards[3].val), "%s", cbuf);

    for (int i = 0; i < 4; i++) {
        auto &c = cards[i];
        d->setColor(fgColor(s));
        d->drawRect(sx(c.x), sy(c.y), 112, 42);
        d->drawString(sx(c.x + 6), sy(c.y + 4), c.lbl);
        if (i == 3) {
            const char *band = co2Band(m, s);
            if (band[0]) {
                d->setTextAlignment(TEXT_ALIGN_RIGHT);
                d->drawString(sx(c.x + 106), sy(c.y + 4), band);
                d->setTextAlignment(TEXT_ALIGN_LEFT);
            }
            addRegion(c.x, c.y, 112, 3, c.border);
            addRegion(c.x, c.y + 39, 112, 3, c.border);
            addRegion(c.x, c.y, 3, 42, c.border);
            addRegion(c.x + 109, c.y, 3, 42, c.border);
            // #region agent log
            {
                static char last_band[16] = "";
                if (strncmp(last_band, band, sizeof(last_band)) != 0) {
                    char cbuf[96];
                    snprintf(cbuf, sizeof(cbuf), "{\"band\":\"%s\",\"native\":%u,\"be\":%u}", band, unsigned(c.border),
                             unsigned(rgbBe(c.border)));
                    meteoDbg("H-color", "MeteoScreen.cpp:drawOverview", "co2-border", cbuf);
                    strncpy(last_band, band, sizeof(last_band) - 1);
                }
            }
            // #endregion
        }
        d->setFont(ArialMT_Plain_16);
        d->drawString(sx(c.x + 6), sy(c.y + 18), c.val);
        d->setFont(ArialMT_Plain_10);
    }
    applyRegions(d);
}

void MeteoScreen::drawAir(OLEDDisplay *d, const MeteoSettings &s, const MeteoModel &m)
{
    bool warming = m.iaq_warming;
    const char *band = warming ? "WARM" : m.iaq_band;
    char big[8];
    if (std::isnan(m.iaq))
        snprintf(big, sizeof(big), "--");
    else
        snprintf(big, sizeof(big), "%.0f", m.iaq);

    d->setFont(ArialMT_Plain_10);
    d->setColor(fgColor(s));
    d->drawString(sx(6), sy(22), "IAQ  BME688 est");
    d->setFont(ArialMT_Plain_24);
    d->drawString(sx(6), sy(34), big);
    d->setFont(ArialMT_Plain_10);
    d->setTextAlignment(TEXT_ALIGN_RIGHT);
    d->drawString(sx(W - 8), sy(40), band);
    d->setTextAlignment(TEXT_ALIGN_LEFT);

    if (warming) {
        drawFracBar(d, 6, 58, 228, 8, m.warmup_frac);
        char left[12];
        fmtMmss(left, sizeof(left), m.warmup_left_s);
        char line[40];
        snprintf(line, sizeof(line), "VOC heater %s / 5:00  %d%%", left, int(m.warmup_frac * 100.0f + 0.5f));
        d->drawString(sx(6), sy(70), line);
        if (m.warmup_frac > 0.01f)
            addRegion(7, 59, int(226 * m.warmup_frac), 6, themeCol(s, 5));
    } else {
        float iaqVal = std::isnan(m.iaq) ? 0 : m.iaq;
        drawBar(d, 6, 58, 228, 8, iaqVal / 500.0f, fgColor(s), bgColor(s));
        char eco2[8], bvoc[8];
        fmtVal(eco2, sizeof(eco2), m.eco2, 0);
        fmtVal(bvoc, sizeof(bvoc), m.bvoc, 1);
        char line[40];
        snprintf(line, sizeof(line), "eCO2 %s ppm  bVOC %s", eco2, bvoc);
        d->drawString(sx(6), sy(70), line);
        if (!std::isnan(m.iaq)) {
            int bw = int(226 * (iaqVal / 500.0f));
            addRegion(7, 59, bw < 4 ? 4 : bw, 6, themeCol(s, iaqColorIndex(m.iaq)));
        }
    }

    char scdLine[48];
    if (!m.co2.valid) {
        snprintf(scdLine, sizeof(scdLine), "SCD CO2  --");
    } else {
        snprintf(scdLine, sizeof(scdLine), "SCD CO2  %d ppm  %s", int(m.co2.display + 0.5f), co2Band(m, s));
    }
    d->drawString(sx(6), sy(84), scdLine);

    char gasLine[48];
    if (m.bme_state == ST_ABSENT) {
        snprintf(gasLine, sizeof(gasLine), "VOC RAW  --");
    } else if (!m.gas.valid) {
        snprintf(gasLine, sizeof(gasLine), warming ? "VOC WARMING" : "VOC RAW  --");
    } else {
        snprintf(gasLine, sizeof(gasLine), "VOC  %.0f kOhm  %s", m.gas.display / 1000.0f, m.gas_trend);
    }
    d->drawString(sx(6), sy(98), gasLine);
    applyRegions(d);
}

void MeteoScreen::drawForecast(OLEDDisplay *d, const MeteoSettings &s, const MeteoModel &m, MeteoHistory &h)
{
    auto fg = fgColor(s);
    char buf[48];

    if (forecast_sub == 1) {
        d->setFont(ArialMT_Plain_10);
        d->setColor(fg);
        d->drawString(sx(6), sy(22), "MODELS");
        drawPagePips(d, 210, 24, 1);
        bool collecting = m.trend_id == TREND_COLLECTING;
        const char *zph = collecting ? "COLLECTING" : m.z_phrase;
        auto wxRow = [&](int ry, const char *tag, int score, const char *phrase) {
            d->drawString(sx(6), sy(ry), tag);
            drawBulletBar(d, 38, ry, 190, 8, score >= 0 ? score : -1);
            if (score >= 0) {
                char b[8];
                snprintf(b, sizeof(b), "%3d", score);
                d->setTextAlignment(TEXT_ALIGN_RIGHT);
                d->drawString(sx(38 + 190), sy(ry), b);
                d->setTextAlignment(TEXT_ALIGN_LEFT);
            }
            d->drawString(sx(6), sy(ry + 10), phrase ? phrase : "--");
        };
        wxRow(36, "ZAM", m.wx_z, zph);
        wxRow(56, "SLR", m.wx_sailor, m.sailor_phrase);
        wxRow(76, "HYG", m.wx_hygro, m.hygro_phrase);
        char ensLine[32];
        snprintf(ensLine, sizeof(ensLine), "ENS %3d  AGREE %d%%", m.wx_ens >= 0 ? m.wx_ens : 0,
                 m.wx_agree >= 0 ? m.wx_agree : 0);
        d->drawString(sx(6), sy(96), ensLine);
        drawBulletBar(d, 6, 107, 228, 6, m.wx_ens >= 0 ? m.wx_ens : -1);
        return;
    }

    if (forecast_sub == 2) {
        d->setFont(ArialMT_Plain_10);
        d->setColor(fg);
        d->drawString(sx(6), sy(22), "CONFIDENCE");
        drawPagePips(d, 210, 24, 2);
        int n = h.slpRing().count();
        uint32_t span = 0;
        float lo, hi, avg, slope;
        h.slpStats(&lo, &hi, &avg, &slope, &span);
        uint32_t collect_ms = h.slpCollectMs(millis());
        if (collect_ms == 0 && (!std::isnan(m.slp) || m.press.valid) && m.started_ms) {
            collect_ms = wrapMs(millis(), m.started_ms);
            if (collect_ms > SLP_MAX_AGE_MS)
                collect_ms = SLP_MAX_AGE_MS;
        }
        if (collect_ms > span)
            span = collect_ms;
        snprintf(buf, sizeof(buf), "hist %d/36  %uh%02um", n, unsigned(span / 3600000), unsigned((span / 60000) % 60));
        d->drawString(sx(6), sy(40), buf);
        drawBulletBar(d, 170, 40, 64, 5, int((n / 36.0f) * 100.0f));
        d->drawString(sx(6), sy(52), m.conf_reason_str);
        snprintf(buf, sizeof(buf), "agree %d%%", m.wx_agree >= 0 ? m.wx_agree : 0);
        d->drawString(sx(6), sy(64), m.wx_agree >= 0 ? buf : "agree --");
        if (m.wx_agree >= 0)
            drawBulletBar(d, 170, 64, 64, 5, m.wx_agree);
        d->drawString(sx(6), sy(76), "CO2/IAQ not used");
        char td[8], rh[8], spr[8];
        fmtVal(td, sizeof(td), m.dew, 1);
        fmtVal(rh, sizeof(rh), m.rh.valid ? m.rh.display : NAN, 0);
        fmtVal(spr, sizeof(spr), m.dp_spread, 1);
        snprintf(buf, sizeof(buf), "Td %s  RH %s  T-Td %s", td, rh, spr);
        d->drawString(sx(6), sy(90), buf);
        int x = 6;
        float total = 0;
        for (int i = 0; i < 6; i++)
            total += m.conf_factors[i];
        d->setColor(fg);
        for (int i = 0; i < 6; i++) {
            int w = total > 0 ? int(226 * m.conf_factors[i] / total) : 0;
            if (w > 0)
                d->fillRect(sx(x), sy(97), w, 6);
            x += w;
        }
        snprintf(buf, sizeof(buf), "CONF %d%% %s", m.conf_pct, CONF_LABEL[m.conf_id]);
        d->drawString(sx(6), sy(106), buf);
        drawBulletBar(d, 6, 112, 228, 4, m.conf_pct);
        return;
    }

    // Outlook sub-page 0
    d->setFont(ArialMT_Plain_10);
    d->setColor(fg);
    d->drawString(sx(6), sy(22), "BARO OUTLOOK");
    d->setTextAlignment(TEXT_ALIGN_RIGHT);
    d->drawString(sx(W - 8), sy(24), TREND_LABEL[m.trend_id]);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
    drawPagePips(d, 210, 24, 0);

    if (!m.bme_present && !m.press.valid) {
        d->drawString(sx(6), sy(52), "PRESSURE SENSOR");
        d->drawString(sx(6), sy(70), "REQUIRED");
        return;
    }

    char slpBuf[24];
    if (std::isnan(m.slp))
        snprintf(slpBuf, sizeof(slpBuf), "SLP --");
    else
        snprintf(slpBuf, sizeof(slpBuf), "SLP %.1f hPa", m.slp);
    d->drawString(sx(6), sy(38), slpBuf);

    float d3 = m.slp_d3h;
    if (std::isnan(d3))
        d3 = NAN;
    char dtxt[16];
    if (std::isnan(d3))
        snprintf(dtxt, sizeof(dtxt), "3h --");
    else
        snprintf(dtxt, sizeof(dtxt), "3h%+.1f", d3);

    float lo, hi, avg, slope;
    uint32_t span;
    h.slpStats(&lo, &hi, &avg, &slope, &span);
    char rate[16];
    if (std::isnan(slope))
        snprintf(rate, sizeof(rate), "--");
    else
        snprintf(rate, sizeof(rate), "%+.2f/h", slope);
    char right[32];
    snprintf(right, sizeof(right), "%s %s", dtxt, rate);
    d->setTextAlignment(TEXT_ALIGN_RIGHT);
    d->drawString(sx(W - 8), sy(40), right);
    d->setTextAlignment(TEXT_ALIGN_LEFT);

    snprintf(buf, sizeof(buf), "CONF %s", CONF_LABEL[m.conf_id]);
    d->drawString(sx(6), sy(54), buf);
    if (!s.has_rtc) {
        d->setTextAlignment(TEXT_ALIGN_RIGHT);
        d->drawString(sx(W - 8), sy(54), "NO RTC");
        d->setTextAlignment(TEXT_ALIGN_LEFT);
    }

    const char *phrase = m.trend_id == TREND_COLLECTING ? "COLLECTING" : m.z_phrase;
    d->drawString(sx(6), sy(66), phrase);

    float slo, shi;
    h.chartRange(CH_PRESS, &slo, &shi);
    drawSparkline(d, 6, 82, 228, 10, h.slpRing(), slo, shi);

    uint32_t coll = h.slpCollectMs(millis());
    if (coll == 0 && (!std::isnan(m.slp) || m.press.valid) && m.started_ms) {
        coll = wrapMs(millis(), m.started_ms);
        if (coll > SLP_MAX_AGE_MS)
            coll = SLP_MAX_AGE_MS;
    }
    if (coll > span)
        span = coll;
    // #region agent log
    {
        static uint32_t last_span = 0xFFFFFFFFu;
        if (span != last_span) {
            char fbuf[128];
            snprintf(fbuf, sizeof(fbuf), "{\"n\":%d,\"coll\":%u,\"span\":%u,\"slp\":%.1f,\"pvalid\":%d}", h.slpRing().count(),
                     unsigned(coll), unsigned(span), m.slp, m.press.valid ? 1 : 0);
            meteoDbg("H-bucket", "MeteoScreen.cpp:drawForecast", "collect", fbuf);
            last_span = span;
        }
    }
    // #endregion
    snprintf(buf, sizeof(buf), "%uh%02um of 3h", unsigned(span / 3600000), unsigned((span / 60000) % 60));
    d->drawString(sx(6), sy(102), buf);
    drawCoverageBar(d, 6, 92, 228, 6, float(span) / float(SLP_MAX_AGE_MS), s);
}

void MeteoScreen::drawChart(OLEDDisplay *d, const MeteoSettings &s, MeteoHistory &h)
{
    const int x0 = 18, y0 = 34, x1 = 232, y1 = 104;
    const int focus = chart_ch;
    float lo, hi;
    h.chartRange(focus, &lo, &hi);

    d->setFont(ArialMT_Plain_10);
    d->setColor(fgColor(s));
    char title[32];
    snprintf(title, sizeof(title), "%s  last %dm", CH_LABEL[focus], s.chart_s);
    d->drawString(sx(6), sy(22), title);
    char rng[24];
    snprintf(rng, sizeof(rng), "%.1f..%.1f %s", lo, hi, CH_UNIT[focus]);
    d->setTextAlignment(TEXT_ALIGN_RIGHT);
    d->drawString(sx(W - 6), sy(22), rng);
    d->setTextAlignment(TEXT_ALIGN_LEFT);

    d->setColor(fgColor(s));
    d->drawRect(sx(x0), sy(y0), x1 - x0, y1 - y0);

    int drawn = 0;
    for (int ch = 0; ch < CHANNEL_COUNT; ch++) {
        if (ch == focus || !s.chartLineOn(ch))
            continue;
        if (h.chart(ch).count() >= 2)
            drawn++;
    }
    if (s.chartLineOn(focus) && h.chart(focus).count() >= 2)
        drawn++;

    if (drawn == 0) {
        d->setTextAlignment(TEXT_ALIGN_CENTER);
        const char *msg = (s.chart_mask & 63) == 0 ? "NO LINES" : "COLLECTING";
        if ((s.chart_mask & 63) != 0) {
            char coll[24];
            snprintf(coll, sizeof(coll), "COLLECTING %d/60", h.chart(focus).count());
            d->drawString(sx(W / 2), sy(60), coll);
        } else {
            d->drawString(sx(W / 2), sy(60), msg);
        }
        d->setTextAlignment(TEXT_ALIGN_LEFT);
    }

    int lx = 6;
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        char mark[8];
        if (i == focus && s.chartLineOn(i))
            snprintf(mark, sizeof(mark), "[%c]", CH_TICK[i]);
        else
            snprintf(mark, sizeof(mark), " %c ", CH_TICK[i]);
        d->drawString(sx(lx), sy(106), mark);
        lx += d->getStringWidth(mark);
    }
}

void MeteoScreen::flushChartColors(OLEDDisplay *d, const MeteoSettings &s, MeteoHistory &h)
{
#if defined(USE_ST7789)
    const int x0 = 18, y0 = 34, x1 = 232, y1 = 104;
    auto *st = static_cast<ST7789Spi *>(d);
    uint32_t hash = uint32_t(chart_ch) * 2654435761u;
    for (int ch = 0; ch < CHANNEL_COUNT; ch++) {
        auto &ring = h.chart(ch);
        int n = ring.count();
        hash ^= uint32_t(n + 1) * uint32_t(ch + 3);
        if (s.chartLineOn(ch) && n >= 2) {
            uint32_t t;
            float v = 0;
            ring.at(n - 1, &t, &v);
            uint32_t bits;
            memcpy(&bits, &v, sizeof(bits));
            hash ^= bits ^ t;
        }
    }
    if (hash == chart_blit_hash_) {
        // #region agent log
        static uint32_t skip_logs = 0;
        if ((skip_logs++ % 20) == 0)
            meteoDbg("H-bbox", "MeteoScreen.cpp:flushChartColors", "skip-unchanged", "{}");
        // #endregion
        return;
    }
    chart_blit_hash_ = hash;

    const int ax0 = sx(x0 + 1);
    const int ay0 = sy(y0 + 1);
    const int ax1 = sx(x1 - 1);
    const int ay1 = sy(y1 - 1);
    st->beginPushRGB();
    st->pushRGBFill(ax0, ay0, ax1 - ax0 + 1, ay1 - ay0 + 1, 0x0000);
    int flushed = 0;
    uint16_t used[CHANNEL_COUNT] = {};
    for (int ch = 0; ch < CHANNEL_COUNT; ch++) {
        if (ch == chart_ch || !s.chartLineOn(ch) || h.chart(ch).count() < 2)
            continue;
        float clo, chi;
        h.chartRange(ch, &clo, &chi);
        plotRingRgb(st, h.chart(ch), clo, chi, ax0, ay0, ax1, ay1, CH_COLOR[ch]);
        used[ch] = CH_COLOR[ch];
        flushed++;
    }
    if (s.chartLineOn(chart_ch) && h.chart(chart_ch).count() >= 2) {
        float clo, chi;
        h.chartRange(chart_ch, &clo, &chi);
        plotRingRgb(st, h.chart(chart_ch), clo, chi, ax0, ay0, ax1, ay1, CH_COLOR[chart_ch]);
        used[chart_ch] = CH_COLOR[chart_ch];
        flushed++;
    }
    int lx = sx(6);
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        if (!s.chartLineOn(i))
            continue;
        st->pushRGBFill(lx, sy(107), 8, 6, CH_COLOR[i]);
        lx += 18;
    }
    st->endPushRGB();
    st->setRGB(TFT_MESH, nullptr);
    // #region agent log
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"path\":\"spi\",\"flushed\":%d,\"focus\":%d,\"c0\":%u,\"c1\":%u,\"c2\":%u,\"c3\":%u,\"c4\":%u,\"c5\":%u}", flushed,
             chart_ch, unsigned(used[0]), unsigned(used[1]), unsigned(used[2]), unsigned(used[3]), unsigned(used[4]),
             unsigned(used[5]));
    meteoDbg("H-bbox", "MeteoScreen.cpp:flushChartColors", "spi-blit", buf);
    // #endregion
#else
    (void)d;
    (void)s;
    (void)h;
#endif
}

void MeteoScreen::drawStats(OLEDDisplay *d, const MeteoModel &m, MeteoHistory &h)
{
    char buf[48];
    d->setFont(ArialMT_Plain_10);
    int y = 22;
    if (stats_sub == 0) {
        snprintf(buf, sizeof(buf), "BME %s", stateName(m.bme_state));
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        snprintf(buf, sizeof(buf), "SCD %s crc=%d", stateName(m.scd_state), m.scd_crc);
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        float lo, hi, avg, slope;
        uint32_t span;
        h.slpStats(&lo, &hi, &avg, &slope, &span);
        if (std::isnan(slope))
            snprintf(buf, sizeof(buf), "slope --");
        else
            snprintf(buf, sizeof(buf), "slope %.2f hPa/h", slope);
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        if (std::isnan(lo))
            snprintf(buf, sizeof(buf), "P min/max --");
        else
            snprintf(buf, sizeof(buf), "P %.1f..%.1f avg %.1f", lo, hi, avg);
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        char iaq[8], eco2[8], gas[16];
        fmtVal(iaq, sizeof(iaq), m.iaq, 0);
        fmtVal(eco2, sizeof(eco2), m.eco2, 0);
        fmtVal(gas, sizeof(gas), m.gas.valid ? m.gas.display : NAN, 0);
        snprintf(buf, sizeof(buf), "IAQ %s %s  eCO2 %s", iaq, m.iaq_band, eco2);
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        char co2[8];
        fmtVal(co2, sizeof(co2), m.co2.valid ? m.co2.display : NAN, 0);
        snprintf(buf, sizeof(buf), "CO2 %s  gas %s Ohm", co2, gas);
        d->drawString(sx(6), sy(y), buf);
    } else {
        snprintf(buf, sizeof(buf), "method %s", m.trend_method);
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        float lo, hi, avg, slope;
        uint32_t span;
        h.slpStats(&lo, &hi, &avg, &slope, &span);
        snprintf(buf, sizeof(buf), "span %uh%02um n %d", unsigned(span / 3600000), unsigned((span / 60000) % 60),
                 h.slpRing().count());
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        fmtVal(buf, sizeof(buf), m.slp_d3h, 1);
        char line[32];
        snprintf(line, sizeof(line), "d3h %s", buf);
        d->drawString(sx(6), sy(y), line);
        y += 14;
        snprintf(buf, sizeof(buf), "agree %d", m.wx_agree >= 0 ? m.wx_agree : 0);
        d->drawString(sx(6), sy(y), buf);
    }
}

void MeteoScreen::drawRaw(OLEDDisplay *d, const MeteoModel &m, const MeteoSettings &s)
{
    char buf[48];
    d->setFont(ArialMT_Plain_10);
    snprintf(buf, sizeof(buf), "RAW %d/3", raw_sub + 1);
    d->drawString(sx(6), sy(22), buf);
    int y = 36;
    if (raw_sub == 0) {
        char t[8], tr[8], h[8], hr[8], p[8], slp[8], gas[8], iaq[8], eco2[8], bvoc[8];
        fmtVal(t, sizeof(t), m.temp.valid ? m.temp.display : NAN, 2);
        fmtVal(tr, sizeof(tr), m.temp.valid ? m.temp.raw : NAN, 2);
        fmtVal(h, sizeof(h), m.rh.valid ? m.rh.display : NAN, 1);
        fmtVal(hr, sizeof(hr), m.rh.valid ? m.rh.raw : NAN, 1);
        fmtVal(p, sizeof(p), m.press.valid ? m.press.display : NAN, 2);
        fmtVal(slp, sizeof(slp), m.slp, 1);
        fmtVal(gas, sizeof(gas), m.gas.valid ? m.gas.display : NAN, 0);
        fmtVal(iaq, sizeof(iaq), m.iaq, 0);
        fmtVal(eco2, sizeof(eco2), m.eco2, 0);
        fmtVal(bvoc, sizeof(bvoc), m.bvoc, 2);
        snprintf(buf, sizeof(buf), "T %sC raw %s off %.1f", t, tr, s.t_off);
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        snprintf(buf, sizeof(buf), "H %s%% raw %s off %.1f", h, hr, s.h_off);
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        snprintf(buf, sizeof(buf), "P %s SLP %s", p, slp);
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        snprintf(buf, sizeof(buf), "GAS %s Ohm  IAQ %s", gas, iaq);
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        snprintf(buf, sizeof(buf), "eCO2 %s  bVOC %s", eco2, bvoc);
        d->drawString(sx(6), sy(y), buf);
    } else if (raw_sub == 1) {
        snprintf(buf, sizeof(buf), "BME id=%d var=%d", m.bme_present ? 0x61 : 0, m.bme_present ? 1 : 0);
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        snprintf(buf, sizeof(buf), "st=%d heat=%d gasV=%d", m.bme_state, m.iaq_warming ? 0 : 1, m.gas.valid ? 1 : 0);
        d->drawString(sx(6), sy(y), buf);
        y += 14;
        fmtVal(buf, sizeof(buf), m.temp.raw, 2);
        char line[32];
        snprintf(line, sizeof(line), "ADC T raw %s", buf);
        d->drawString(sx(6), sy(y), line);
        y += 14;
        fmtVal(buf, sizeof(buf), m.gas.raw, 0);
        snprintf(line, sizeof(line), "GAS raw %s Ohm", buf);
        d->drawString(sx(6), sy(y), line);
    } else {
        fmtVal(buf, sizeof(buf), m.co2.valid ? m.co2.display : NAN, 0);
        char line[32];
        snprintf(line, sizeof(line), "SCD CO2 %s ppm", buf);
        d->drawString(sx(6), sy(y), line);
        y += 14;
        fmtVal(buf, sizeof(buf), m.scd_temp.display, 2);
        char h[8];
        fmtVal(h, sizeof(h), m.scd_rh.display, 0);
        snprintf(line, sizeof(line), "T %sC H %s%%", buf, h);
        d->drawString(sx(6), sy(y), line);
        y += 14;
        snprintf(buf, sizeof(buf), "crc %d  OOR %d", m.scd_crc, m.co2.stale ? 1 : 0);
        d->drawString(sx(6), sy(y), buf);
    }
}

void MeteoScreen::drawSettings(OLEDDisplay *d, MeteoSettings &s)
{
    d->setFont(ArialMT_Plain_10);
    char buf[32];
    const char *lines[] = {"ALT m", "THEME", "CHART s", "LOG s", "SOUTH", "HUB"};
    for (int i = 0; i < 6; i++) {
        int y = 22 + i * 15;
        if (i == settings_line) {
            d->setColor(fgColor(s));
            d->fillRect(sx(4), sy(y - 1), 232, 14);
            d->setColor(invColor(s));
        } else {
            d->setColor(fgColor(s));
        }
        switch (i) {
        case 0:
            snprintf(buf, sizeof(buf), "%s %.0f", lines[i], s.altitude_m);
            break;
        case 1:
            snprintf(buf, sizeof(buf), "%s %s", lines[i], THEME_NAME[s.theme % 4]);
            break;
        case 2:
            snprintf(buf, sizeof(buf), "%s %d", lines[i], s.chart_s);
            break;
        case 3:
            snprintf(buf, sizeof(buf), "%s %d", lines[i], s.log_s);
            break;
        case 4:
            snprintf(buf, sizeof(buf), "%s %s", lines[i], s.southern ? "Y" : "N");
            break;
        default:
            snprintf(buf, sizeof(buf), "%s 0x%02X", lines[i], s.hub_addr);
            break;
        }
        d->drawString(sx(8), sy(y), buf);
    }
}

void MeteoScreen::draw(OLEDDisplay *display, int16_t x, int16_t y, const MeteoSettings &settings, const MeteoModel &model,
                       MeteoHistory &history, int sd_state, const MeteoClock &clk)
{
#if defined(USE_ST7789)
    static_cast<ST7789Spi *>(display)->setRGB(0xFFFF, nullptr);
#endif
    clearRegions();
    off_x_ = x;
    off_y_ = y;
#if defined(USE_ST7789)
    if (page != PAGE_CHART)
        chart_blit_hash_ = 0;
#endif
    // #region agent log
    if (x != 0 || y != 0) {
        char obuf[64];
        snprintf(obuf, sizeof(obuf), "{\"x\":%d,\"y\":%d,\"page\":%d}", int(x), int(y), page);
        meteoDbg("H-offset", "MeteoScreen.cpp:draw", "nonzero-offset", obuf);
    }
    // #endregion

    display->setColor(bgColor(settings));
    display->fillRect(sx(0), sy(0), W, H);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    drawHeader(display, settings, sd_state, clk);
    display->setColor(fgColor(settings));

    switch (page) {
    case PAGE_OVERVIEW:
        drawOverview(display, settings, model);
        break;
    case PAGE_AIR:
        drawAir(display, settings, model);
        break;
    case PAGE_FORECAST:
        drawForecast(display, settings, model, history);
        break;
    case PAGE_CHART:
        drawChart(display, settings, history);
        break;
    case PAGE_STATS:
        drawStats(display, model, history);
        break;
    case PAGE_RAW:
        drawRaw(display, model, settings);
        break;
    case PAGE_SETTINGS:
        drawSettings(display, const_cast<MeteoSettings &>(settings));
        break;
    }

    drawFooter(display, settings);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
}

bool MeteoScreen::handleChar(char c, MeteoSettings &settings, MeteoStore &store)
{
    if (settings_active || page == PAGE_SETTINGS) {
        if (c == 27 || c == '`' || c == '\r' || c == '\n') {
            settings_active = false;
            page = PAGE_OVERVIEW;
            if (c == '\r' || c == '\n')
                store.saveSettings(settings);
            // #region agent log
            {
                char sbuf[64];
                snprintf(sbuf, sizeof(sbuf), "{\"c\":%d,\"saved\":%d,\"page\":%d}", int((unsigned char)c),
                         (c == '\r' || c == '\n') ? 1 : 0, page);
                meteoDbg("H-setexit", "MeteoScreen.cpp:handleChar", "leave", sbuf);
            }
            // #endregion
            return true;
        }
        if (c == 'u' || c == 'U') {
            settings_line = (settings_line + 5) % 6;
            return true;
        }
        if (c == 'd' || c == 'D') {
            settings_line = (settings_line + 1) % 6;
            return true;
        }
        auto bumpChart = [](int cur, int dir) {
            static const int opts[] = {15, 30, 60, 120, 300};
            int idx = 2;
            for (int i = 0; i < 5; i++)
                if (opts[i] == cur)
                    idx = i;
            idx = (idx + dir + 5) % 5;
            return opts[idx];
        };
        auto bumpLog = [](int cur, int dir) {
            static const int opts[] = {0, 10, 30, 60, 300};
            int idx = 0;
            for (int i = 0; i < 5; i++)
                if (opts[i] == cur)
                    idx = i;
            idx = (idx + dir + 5) % 5;
            return opts[idx];
        };
        if (c == ';' || c == 'j') {
            switch (settings_line) {
            case 0:
                settings.altitude_m += 10;
                break;
            case 1:
                settings.theme = (settings.theme + 1) % 4;
                break;
            case 2:
                settings.chart_s = bumpChart(settings.chart_s, 1);
                break;
            case 3:
                settings.log_s = bumpLog(settings.log_s, 1);
                break;
            case 4:
                settings.southern = !settings.southern;
                break;
            default:
                break;
            }
            return true;
        }
        if (c == 'k') {
            switch (settings_line) {
            case 0:
                settings.altitude_m -= 10;
                break;
            case 1:
                settings.theme = (settings.theme + 3) % 4;
                break;
            case 2:
                settings.chart_s = bumpChart(settings.chart_s, -1);
                break;
            case 3:
                settings.log_s = bumpLog(settings.log_s, -1);
                break;
            case 4:
                settings.southern = !settings.southern;
                break;
            default:
                break;
            }
            return true;
        }
        if (c == 'h')
            settings.altitude_m += 10;
        if (c == 'n')
            settings.altitude_m -= 10;
        if (c == 't')
            settings.theme = (settings.theme + 1) % 4;
        if (c == 'l') {
            settings.logging_requested = !settings.logging_requested;
            store.saveSettings(settings);
        }
        return true;
    }
    if (c == 'm' || c == 'M') {
        page = PAGE_SETTINGS;
        settings_active = true;
        return true;
    }
    if (c == 'o' || c == 'O') {
        page = PAGE_OVERVIEW;
        return true;
    }
    if (c == 'a' || c == 'A') {
        page = PAGE_AIR;
        return true;
    }
    if (c == 'f' || c == 'F') {
        page = PAGE_FORECAST;
        return true;
    }
    if (c == 'i' || c == 'I') {
        page = PAGE_STATS;
        return true;
    }
    if (c == 'x' || c == 'X') {
        page = PAGE_RAW;
        return true;
    }
    if (c == 't' || c == 'T') {
        page = PAGE_CHART;
        chart_ch = CH_TEMP;
        return true;
    }
    if (c == 'h' || c == 'H') {
        page = PAGE_CHART;
        chart_ch = CH_HUM;
        return true;
    }
    if (c == 'p' || c == 'P') {
        page = PAGE_CHART;
        chart_ch = CH_PRESS;
        return true;
    }
    if (c == 'c' || c == 'C') {
        page = PAGE_CHART;
        chart_ch = CH_CO2;
        return true;
    }
    if (c == 'g' || c == 'G') {
        page = PAGE_CHART;
        chart_ch = CH_GAS;
        return true;
    }
    if (c == 'q' || c == 'Q') {
        page = PAGE_CHART;
        chart_ch = CH_IAQ;
        return true;
    }
    if (c == 'l' || c == 'L') {
        settings.logging_requested = !settings.logging_requested;
        store.saveSettings(settings);
        return true;
    }
    if (c == 'r' || c == 'R')
        return true;
    if (c == ';' || c == 'j' || c == 0x06) {
        page = (page + 1) % NAV_COUNT;
        return true;
    }
    if (c == 'k' || c == 0x08) {
        page = (page + NAV_COUNT - 1) % NAV_COUNT;
        return true;
    }
    if (page == PAGE_FORECAST && (c == 'u' || c == 'U')) {
        forecast_sub = (forecast_sub + 2) % 3;
        return true;
    }
    if (page == PAGE_FORECAST && (c == 'd' || c == 'D')) {
        forecast_sub = (forecast_sub + 1) % 3;
        return true;
    }
    if (page == PAGE_STATS && (c == 'u' || c == 'U' || c == 'd' || c == 'D')) {
        stats_sub = 1 - stats_sub;
        return true;
    }
    if (page == PAGE_RAW && (c == 'u' || c == 'U')) {
        raw_sub = (raw_sub + 2) % 3;
        return true;
    }
    if (page == PAGE_RAW && (c == 'd' || c == 'D')) {
        raw_sub = (raw_sub + 1) % 3;
        return true;
    }
    if (page == PAGE_CHART && (c == 'u' || c == 'U')) {
        chart_ch = (chart_ch + CHANNEL_COUNT - 1) % CHANNEL_COUNT;
        return true;
    }
    if (page == PAGE_CHART && (c == 'd' || c == 'D')) {
        chart_ch = (chart_ch + 1) % CHANNEL_COUNT;
        return true;
    }
    return true;
}

} // namespace meteo
