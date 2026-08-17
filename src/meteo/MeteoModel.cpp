#include "MeteoModel.h"
#include "MeteoDebug.h"
#include "MeteoForecast.h"
#include "MeteoIaq.h"
#include <algorithm>
#include <cstdio>

namespace meteo
{

void Field::setOk(uint32_t now, float raw_v, float corr, int src, float ema, float median3)
{
    raw = raw_v;
    corrected = corr;
    last_good = corr;
    last_ms = now;
    valid = true;
    stale = false;
    source = src;
    errors = 0;
    if (!std::isnan(median3))
        display = median3;
    else if (std::isnan(display))
        display = corr;
    else
        display = ema * corr + (1.0f - ema) * display;
}

void Field::noteError()
{
    errors++;
    valid = false;
}

uint32_t Field::ageMs(uint32_t now) const
{
    if (last_ms == 0)
        return 0;
    return now - last_ms;
}

void Field::markStaleIfOld(uint32_t now, uint32_t timeout_ms)
{
    if (last_ms && (now - last_ms) > timeout_ms) {
        stale = true;
        valid = false;
    }
}

float MeteoModel::median3Co2(float v)
{
    if (co2_med_n_ == 0) {
        co2_med_[0] = v;
        co2_med_n_ = 1;
        return v;
    }
    if (co2_med_n_ == 1) {
        co2_med_[1] = v;
        co2_med_n_ = 2;
        return (co2_med_[0] + co2_med_[1]) / 2.0f;
    }
    co2_med_[2] = co2_med_[1];
    co2_med_[1] = co2_med_[0];
    co2_med_[0] = v;
    float x = co2_med_[0], y = co2_med_[1], z = co2_med_[2];
    if (x > y)
        std::swap(x, y);
    if (y > z)
        std::swap(y, z);
    if (x > y)
        std::swap(x, y);
    return y;
}

void MeteoModel::setPresence(bool bme, bool scd, uint32_t now)
{
    bme_present = bme;
    scd_present = scd;
    if (bme && bme_state == ST_ABSENT)
        bme_state = ST_STARTING;
    if (!bme)
        bme_state = ST_ABSENT;
    if (scd && scd_state == ST_ABSENT)
        scd_state = ST_STARTING;
    if (!scd)
        scd_state = ST_ABSENT;
    if (bme && bme_started_ms_ == 0)
        bme_started_ms_ = now;
    if (started_ms == 0)
        started_ms = now;
    dirty = true;
}

void MeteoModel::refreshIaq(bool warming)
{
    float g = gas.display, h = rh.display;
    iaqScore(g, h, gas_baseline, &iaq, &eco2, &bvoc, nullptr);
    iaq_band = iaqBand(iaq);
    if (warming)
        iaq_warming = true;
}

void MeteoModel::refreshWarmup(uint32_t now)
{
    if (!bme_started_ms_ || !bme_present) {
        warmup_frac = 0;
        warmup_left_s = 0;
        return;
    }
    uint32_t elapsed = now - bme_started_ms_;
    if (elapsed >= WARMUP_MS) {
        warmup_frac = 1.0f;
        warmup_left_s = 0;
        iaq_warming = false;
    } else {
        warmup_frac = elapsed / float(WARMUP_MS);
        warmup_left_s = int((WARMUP_MS - elapsed) / 1000);
        iaq_warming = true;
    }
}

void MeteoModel::onBme(uint32_t now, float t, float h, float p, float gas_ohm, bool gas_valid, float t_off, float h_off,
                       float p_off, bool warming)
{
    t += t_off;
    h += h_off;
    p += p_off;
    if (h < 0)
        h = 0;
    if (h > 100)
        h = 100;
    if (t >= -40 && t <= 85)
        temp.setOk(now, t - t_off, t, SRC_BME);
    if (h >= 0 && h <= 100)
        rh.setOk(now, h - h_off, h, SRC_BME);
    bool p_ok = (p >= 300 && p <= 1100);
    // #region agent log
    {
        static uint32_t last = 0;
        if (now - last > 8000) {
            char buf[160];
            snprintf(buf, sizeof(buf), "{\"p\":%.2f,\"poff\":%.2f,\"pok\":%d,\"t\":%.2f,\"h\":%.1f}", p, p_off, p_ok ? 1 : 0, t,
                     h);
            meteoDbg("H-prange", "MeteoModel.cpp:onBme", "gate", buf);
            last = now;
        }
    }
    // #endregion
    if (p_ok)
        press.setOk(now, p - p_off, p, SRC_BME);
    if (gas_valid && gas_ohm > 0) {
        gas.setOk(now, gas_ohm, gas_ohm, SRC_BME);
        gas_baseline = updateIaqBaseline(gas_baseline, gas_ohm, warming);
        if (!std::isnan(gas_baseline)) {
            if (gas_ohm > gas_baseline * 1.08f)
                gas_trend = "UP";
            else if (gas_ohm < gas_baseline * 0.92f)
                gas_trend = "DOWN";
            else
                gas_trend = "FLAT";
        }
        refreshIaq(warming);
    }
    bme_state = warming ? ST_WARMING : ST_READY;
    refreshWarmup(now);
    dirty = true;
}

void MeteoModel::onBmeError()
{
    temp.noteError();
    rh.noteError();
    press.noteError();
    gas.noteError();
    if (bme_state != ST_ABSENT)
        bme_state = ST_ERROR;
    dirty = true;
}

void MeteoModel::onScd(uint32_t now, uint16_t co2_ppm, float t, float h, bool stabilizing)
{
    float med = median3Co2(float(co2_ppm));
    co2.setOk(now, float(co2_ppm), float(co2_ppm), SRC_SCD, 0.25f, med);
    scd_temp.setOk(now, t, t, SRC_SCD);
    scd_rh.setOk(now, h, h, SRC_SCD);
    if (temp.source != SRC_BME || temp.stale || std::isnan(temp.last_good)) {
        if (temp.source != SRC_BME) {
            temp.setOk(now, t, t, SRC_SCD);
            rh.setOk(now, h, h, SRC_SCD);
        }
    }
    scd_state = stabilizing ? ST_WARMING : ST_READY;
    dirty = true;
}

void MeteoModel::onScdError(bool crc)
{
    co2.noteError();
    if (crc)
        scd_crc++;
    if (scd_state != ST_ABSENT)
        scd_state = ST_ERROR;
    dirty = true;
}

void MeteoModel::refreshDerived(uint32_t now_ms, MeteoSettings &settings, MeteoHistory &history)
{
    uint32_t interval = settings.mode == 0 ? 11000 : 30000;
    uint32_t stale_ms = interval * 3;
    temp.markStaleIfOld(now_ms, stale_ms);
    rh.markStaleIfOld(now_ms, stale_ms);
    press.markStaleIfOld(now_ms, stale_ms * 2);
    co2.markStaleIfOld(now_ms, 90000);
    if (temp.stale && temp.source == SRC_BME && !scd_temp.stale && !std::isnan(scd_temp.display)) {
        temp.display = scd_temp.display;
        temp.source = SRC_SCD;
        rh.display = scd_rh.display;
        rh.source = SRC_SCD;
    }
    float t = temp.display, h = rh.display, p = press.display;
    dew = (!std::isnan(t) && !std::isnan(h)) ? dewpointC(t, h) : NAN;
    slp = (!std::isnan(p) && !std::isnan(t) && !press.stale) ? seaLevelHpa(p, settings.altitude_m, t) : NAN;
    if (std::isnan(slp) && press.valid && !press.stale && !std::isnan(p))
        slp = p;

    float lo, hi, avg, slope;
    uint32_t span;
    history.slpStats(&lo, &hi, &avg, &slope, &span);
    if (history.slpRing().count() >= 2) {
        uint32_t times[TREND_POINTS];
        float values[TREND_POINTS];
        int n = history.slpRing().copyTimesValues(times, values);
        trend_method = "linear";
        pressureTrendRobust(times, values, n, &trend_method);
    }
    classifyTrend(history.slpRing().count(), span, slope, &trend_id, &conf_id);
    slp_d3h = history.slpDelta3h();
    z_index = zambrettiIndex(slp, trend_id, settings.month, settings.southern, settings.has_rtc);
    z_phrase = zambrettiPhrase(z_index);
    int hy_score;
    hygroOutlook(t, h, dew, &hy_score, &hygro_phrase, &dp_spread);
    if (trend_id == TREND_COLLECTING) {
        wx_z = wx_sailor = wx_hygro = wx_ens = wx_agree = -1;
        sailor_phrase = "--";
    } else {
        wx_z = zambrettiScore(z_index);
        sailorOutlook(slp, slp_d3h, &wx_sailor, &sailor_phrase);
        wx_hygro = hy_score;
        ensembleWx(wx_z, wx_sailor, wx_hygro, &wx_ens, &wx_agree);
    }
    bool press_ok = !std::isnan(p) && !press.stale;
    bool alt_ok = settings.altitude_m >= -400 && settings.altitude_m <= 9000;
    float total;
    confidenceFactors(history.slpRing().count(), span, press_ok, settings.has_rtc, wx_agree, alt_ok, false, conf_factors,
                      &total);
    conf_pct = confidencePct(history.slpRing().count(), span, press_ok, settings.has_rtc, wx_agree, alt_ok, false);
    conf_id = confFromPct(conf_pct);
    confReason(history.slpRing().count(), span, press_ok, settings.has_rtc, false, conf_reason_str, sizeof(conf_reason_str));
    if (!std::isnan(gas.display) && !std::isnan(gas_baseline))
        refreshIaq(iaq_warming);
    if (bme_present && press.stale && bme_state != ST_STALE)
        bme_state = ST_STALE;
    dirty = false;
}

} // namespace meteo
