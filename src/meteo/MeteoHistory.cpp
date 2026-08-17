#include "MeteoHistory.h"
#include "MeteoDebug.h"
#include "MeteoForecast.h"
#include "FSCommon.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace meteo
{

namespace
{
constexpr char SLP2_MAGIC[] = "SLP2";
constexpr uint8_t SLP2_VERSION = 1;

bool ensureMeteoDir()
{
    if (!FSCom.exists("/meteo"))
        return FSCom.mkdir("/meteo");
    return true;
}
} // namespace

MeteoHistory::MeteoHistory(int chart_s) : chart_s_(chart_s)
{
    slp_.init(TREND_POINTS);
    slp_bucket_.reset(300);
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        charts_[i].init(CHART_POINTS);
        buckets_[i].reset(chart_s);
    }
}

void MeteoHistory::setChartInterval(int seconds)
{
    chart_s_ = seconds;
    for (int i = 0; i < CHANNEL_COUNT; i++)
        buckets_[i].reset(seconds);
}

void MeteoHistory::ingestSlp(uint32_t now_ms, float slp)
{
    if (std::isnan(slp)) {
        // #region agent log
        static uint32_t skip_logs = 0;
        if ((skip_logs++ % 15) == 0)
            meteoDbg("H-slpnan", "MeteoHistory.cpp:ingestSlp", "skip-nan", "{}");
        // #endregion
        return;
    }
    float smooth = slp_med_.push(slp);
    float mean = slp_bucket_.add(now_ms, smooth);
    if (!std::isnan(mean))
        slp_.push(now_ms, mean);
}

void MeteoHistory::ingest(uint32_t now_ms, float temp, float rh, float press, float co2, float gas_kohm, float slp, float iaq)
{
    float pairs[CHANNEL_COUNT] = {temp, rh, press, co2, gas_kohm, iaq};
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        if (std::isnan(pairs[i]))
            continue;
        float mean = buckets_[i].add(now_ms, pairs[i]);
        if (!std::isnan(mean))
            charts_[i].push(now_ms, mean);
    }
    if (!std::isnan(slp))
        ingestSlp(now_ms, slp);
}

uint32_t MeteoHistory::slpCollectMs(uint32_t now_ms) const
{
    uint32_t best = 0;
    if (slp_.count() >= 1) {
        uint32_t t0;
        float v;
        slp_.at(0, &t0, &v);
        uint32_t since_oldest = wrapMs(now_ms, t0);
        if (since_oldest <= SLP_MAX_AGE_MS)
            best = since_oldest;
        else if (slp_.count() >= 2)
            best = slp_.spanMs();
    }
    if (slp_.count() >= 2) {
        uint32_t span = slp_.spanMs();
        if (span > best)
            best = span;
    }
    if (slp_bucket_.armed()) {
        uint32_t elapsed = wrapMs(now_ms, slp_bucket_.startMs());
        if (elapsed > SLP_MAX_AGE_MS)
            elapsed = SLP_MAX_AGE_MS;
        if (elapsed > best)
            best = elapsed;
    }
    return best;
}

void MeteoHistory::chartRange(int ch, float *lo, float *hi) const
{
    float l, h;
    charts_[ch].minMax(&l, &h);
    if (charts_[ch].count() == 0) {
        *lo = CH_DEFAULT_RANGE[ch][0];
        *hi = CH_DEFAULT_RANGE[ch][1];
        return;
    }
    if (l == h) {
        float dlo = CH_DEFAULT_RANGE[ch][0];
        float dhi = CH_DEFAULT_RANGE[ch][1];
        float mid = l;
        float span = (dhi - dlo) * 0.5f;
        *lo = mid - span;
        *hi = mid + span;
        return;
    }
    float pad = (h - l) * 0.10f;
    if (pad <= 0)
        pad = 0.1f;
    *lo = l - pad;
    *hi = h + pad;
}

void MeteoHistory::slpStats(float *lo, float *hi, float *avg, float *slope, uint32_t *span_ms) const
{
    if (slp_.count() == 0) {
        *lo = *hi = *avg = *slope = NAN;
        *span_ms = 0;
        return;
    }
    slp_.minMax(lo, hi);
    *avg = slp_.average();
    uint32_t times[TREND_POINTS];
    float values[TREND_POINTS];
    int n = slp_.copyTimesValues(times, values);
    const char *method = nullptr;
    *slope = pressureTrendRobust(times, values, n, &method);
    *span_ms = slp_.spanMs();
}

float MeteoHistory::slpDelta() const
{
    if (slp_.count() < 2)
        return NAN;
    uint32_t t0, t1;
    float v0, v1;
    slp_.at(0, &t0, &v0);
    slp_.newest(&t1, &v1);
    return v1 - v0;
}

float MeteoHistory::slpDelta3h() const
{
    float d = slpDelta();
    uint32_t span = slp_.spanMs();
    if (std::isnan(d) || span == 0)
        return NAN;
    return d * float(SLP_MAX_AGE_MS) / float(span);
}

bool MeteoHistory::saveSlp(uint32_t now_unix, bool use_unix_time)
{
    if (slp_.count() <= 0)
        return false;
    if (!ensureMeteoDir())
        return false;

    uint8_t buf[8 + TREND_POINTS * 8];
    memcpy(buf, SLP2_MAGIC, 4);
    buf[4] = SLP2_VERSION;
    uint16_t count = uint16_t(slp_.count());
    buf[5] = count & 0xFF;
    buf[6] = (count >> 8) & 0xFF;
    buf[7] = 0;

    int off = 8;
    for (int i = 0; i < slp_.count(); i++) {
        uint32_t t_ms;
        float v;
        slp_.at(i, &t_ms, &v);
        uint32_t t_store;
        if (use_unix_time && now_unix > 0) {
            uint32_t newest_t;
            float nv;
            slp_.newest(&newest_t, &nv);
            uint32_t age_ms = wrapMs(newest_t, t_ms);
            t_store = now_unix - (age_ms / 1000);
        } else {
            t_store = t_ms / 1000;
        }
        int32_t slp_x100 = int32_t(v * 100.0f);
        memcpy(buf + off, &t_store, 4);
        memcpy(buf + off + 4, &slp_x100, 4);
        off += 8;
    }

    File f = FSCom.open(SLP_TMP_PATH, FILE_O_WRITE);
    if (!f)
        return false;
    f.write(buf, off);
    f.close();
    if (FSCom.exists(SLP_PATH))
        FSCom.remove(SLP_PATH);
    return renameFile(SLP_TMP_PATH, SLP_PATH);
}

int MeteoHistory::loadSlp(uint32_t now_unix, bool has_rtc)
{
    if (!FSCom.exists(SLP_PATH))
        return 0;
    File f = FSCom.open(SLP_PATH, FILE_O_READ);
    if (!f)
        return 0;
    uint8_t hdr[8];
    if (f.read(hdr, 8) != 8) {
        f.close();
        return 0;
    }
    f.close();
    if (memcmp(hdr, SLP2_MAGIC, 4) != 0)
        return 0;
    uint16_t count = hdr[5] | (uint16_t(hdr[6]) << 8);
    if (count == 0 || count > TREND_POINTS)
        return 0;

    f = FSCom.open(SLP_PATH, FILE_O_READ);
    if (!f)
        return 0;
    size_t need = 8 + count * 8;
    uint8_t *raw = new uint8_t[need];
    if (f.read(raw, need) != int(need)) {
        delete[] raw;
        f.close();
        return 0;
    }
    f.close();

    slp_.clear();
    if (!(has_rtc && now_unix > 0)) {
        delete[] raw;
        // #region agent log
        meteoDbg("H-load", "MeteoHistory.cpp:loadSlp", "skip-no-rtc", "{}");
        // #endregion
        return 0;
    }

    int restored = 0;
    uint32_t now_ref = has_rtc && now_unix > 0 ? now_unix : 0;

    for (int i = 0; i < count; i++) {
        uint32_t t_sec;
        int32_t slp_x100;
        memcpy(&t_sec, raw + 8 + i * 8, 4);
        memcpy(&slp_x100, raw + 8 + i * 8 + 4, 4);
        float v = slp_x100 / 100.0f;

        if (has_rtc && now_ref > 0) {
            if (now_ref >= t_sec) {
                uint32_t age = now_ref - t_sec;
                if (age <= SLP_MAX_AGE_SEC) {
                    slp_.push(t_sec * 1000UL, v);
                    restored++;
                }
            }
        } else {
            slp_.push(t_sec * 1000UL, v);
            restored++;
        }
    }
    delete[] raw;
    return restored;
}

} // namespace meteo
