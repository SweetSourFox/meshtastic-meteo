#include "MeteoForecast.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>

namespace meteo
{

namespace
{
constexpr float STEADY_HPA_H = 0.5f;
constexpr float FAST_HPA_H = 1.0f;
constexpr uint32_t MIN_TREND_SPAN_MS = 30 * 60 * 1000UL;
constexpr int MIN_TREND_POINTS = 6;
constexpr int MIN_LEG_POINTS = 4;

constexpr int LUT_RISE[] = {0, 1, 1, 2, 5, 6, 8, 9, 11, 12, 12, 16, 19, 24};
constexpr int LUT_FALL[] = {1, 3, 7, 14, 17, 20, 21, 23, 23, 25};
constexpr int LUT_STEADY[] = {0, 1, 1, 1, 4, 10, 13, 13, 15, 15, 18, 22, 22, 23, 23, 23, 25};

float linearMaeHpa(const uint32_t *times_ms, const float *values, int count, float slope)
{
    if (count < 2)
        return 0.0f;
    uint32_t t0 = times_ms[0];
    float y0 = values[0];
    float t_first = wrapMs(times_ms[0], t0) / 3600000.0f;
    float intercept = y0 - slope * t_first;
    float acc = 0.0f;
    for (int i = 0; i < count; i++) {
        float hours = wrapMs(times_ms[i], t0) / 3600000.0f;
        float pred = intercept + slope * hours;
        float err = values[i] - pred;
        if (err < 0)
            err = -err;
        acc += err;
    }
    return acc / float(count);
}
} // namespace

float seaLevelHpa(float pressure_hpa, float altitude_m, float temp_c)
{
    if (altitude_m < -400.0f || altitude_m > 9000.0f)
        return NAN;
    if (pressure_hpa < 300.0f || pressure_hpa > 1100.0f)
        return NAN;
    float denom = temp_c + 0.0065f * altitude_m + 273.15f;
    if (denom <= 0.0f)
        return NAN;
    float ratio = 1.0f - (0.0065f * altitude_m) / denom;
    if (ratio <= 0.0f)
        return NAN;
    return pressure_hpa / powf(ratio, 5.257f);
}

float dewpointC(float temp_c, float rh_pct)
{
    if (rh_pct < 1.0f)
        rh_pct = 1.0f;
    if (rh_pct > 100.0f)
        rh_pct = 100.0f;
    float gamma = logf(rh_pct / 100.0f) + (17.62f * temp_c) / (243.12f + temp_c);
    float denom = 17.62f - gamma;
    if (denom == 0.0f)
        return NAN;
    return 243.12f * gamma / denom;
}

float linearSlopeHpaPerHour(const uint32_t *times_ms, const float *values, int count)
{
    if (count < 2)
        return NAN;
    uint32_t t0 = times_ms[0];
    float sum_t = 0, sum_y = 0, sum_tt = 0, sum_ty = 0;
    float n = float(count);
    for (int i = 0; i < count; i++) {
        float hours = wrapMs(times_ms[i], t0) / 3600000.0f;
        float y = values[i];
        sum_t += hours;
        sum_y += y;
        sum_tt += hours * hours;
        sum_ty += hours * y;
    }
    float denom = n * sum_tt - sum_t * sum_t;
    if (denom == 0.0f)
        return 0.0f;
    return (n * sum_ty - sum_t * sum_y) / denom;
}

float pressureTrendRobust(const uint32_t *times_ms, const float *values, int count, const char **method_out)
{
    if (method_out)
        *method_out = "linear";
    if (count < 2)
        return NAN;
    float linear = linearSlopeHpaPerHour(times_ms, values, count);
    if (count < MIN_TREND_POINTS)
        return linear;

    int min_i = 0;
    float min_v = values[0];
    for (int i = 1; i < count; i++) {
        if (values[i] < min_v) {
            min_v = values[i];
            min_i = i;
        }
    }
    int leg_n = count - min_i;
    if (leg_n < MIN_LEG_POINTS || min_i < 2 || min_i > count - 3)
        return linear;

    uint32_t t0 = times_ms[0];
    uint32_t t_last = times_ms[count - 1];
    uint32_t total_span = wrapMs(t_last, t0);
    uint32_t t_leg = times_ms[min_i];
    uint32_t leg_span = wrapMs(t_last, t_leg);
    if (leg_span < MIN_TREND_SPAN_MS || total_span < MIN_TREND_SPAN_MS)
        return linear;

    float leg_slope = linearSlopeHpaPerHour(times_ms + min_i, values + min_i, leg_n);
    if (std::isnan(leg_slope) || std::isnan(linear))
        return linear;

    float mae = linearMaeHpa(times_ms, values, count, linear);
    if (mae >= 0.35f) {
        if (method_out)
            *method_out = "leg";
        return leg_slope;
    }
    if ((linear >= 0.0f) != (leg_slope >= 0.0f) && fabsf(leg_slope) > fabsf(linear) * 0.5f) {
        if (method_out)
            *method_out = "leg";
        return leg_slope;
    }
    return linear;
}

void classifyTrend(int count, uint32_t span_ms, float rate, int *trend_id, int *conf_id)
{
    if (count < MIN_TREND_POINTS || span_ms < MIN_TREND_SPAN_MS || std::isnan(rate)) {
        *trend_id = TREND_COLLECTING;
        *conf_id = CONF_NONE;
        return;
    }
    float ar = rate >= 0 ? rate : -rate;
    int trend;
    if (ar < STEADY_HPA_H)
        trend = TREND_STEADY;
    else if (rate >= FAST_HPA_H)
        trend = TREND_RISING_FAST;
    else if (rate <= -FAST_HPA_H)
        trend = TREND_FALLING_FAST;
    else if (rate > 0)
        trend = TREND_RISING;
    else
        trend = TREND_FALLING;

    float minutes = span_ms / 60000.0f;
    int conf;
    if (minutes < 90)
        conf = CONF_LOW;
    else if (minutes < 180)
        conf = CONF_MED;
    else
        conf = CONF_HIGH;
    *trend_id = trend;
    *conf_id = conf;
}

int zambrettiIndex(float slp_hpa, int trend_id, int month, bool southern, bool has_rtc)
{
    if (std::isnan(slp_hpa) || trend_id == TREND_COLLECTING)
        return -1;
    float p = slp_hpa;
    if (p < 950.0f)
        p = 950.0f;
    if (p > 1050.0f)
        p = 1050.0f;

    bool falling = trend_id == TREND_FALLING || trend_id == TREND_FALLING_FAST;
    bool rising = trend_id == TREND_RISING || trend_id == TREND_RISING_FAST;
    bool north = !southern;
    bool summer = false;
    if (has_rtc) {
        int m = month ? month : 1;
        summer = north == (m >= 4 && m <= 9);
    }

    const int *lut;
    int lut_len;
    float f;
    if (rising) {
        if (summer)
            p += 3.2f;
        f = 0.1740f * (1031.40f - p);
        lut = LUT_RISE;
        lut_len = sizeof(LUT_RISE) / sizeof(LUT_RISE[0]);
    } else if (falling) {
        if (summer)
            p -= 3.2f;
        f = 0.1553f * (1029.95f - p);
        lut = LUT_FALL;
        lut_len = sizeof(LUT_FALL) / sizeof(LUT_FALL[0]);
    } else {
        f = 0.2314f * (1030.81f - p);
        lut = LUT_STEADY;
        lut_len = sizeof(LUT_STEADY) / sizeof(LUT_STEADY[0]);
    }
    int idx = int(f + 0.5f);
    if (idx < 0)
        idx = 0;
    if (idx >= lut_len)
        idx = lut_len - 1;
    return lut[idx] + 1;
}

const char *zambrettiPhrase(int index)
{
    if (index < 1)
        return "--";
    int i = index - 1;
    int n = int(sizeof(ZAMBRETTI_SHORT) / sizeof(ZAMBRETTI_SHORT[0]));
    if (i >= n)
        i = n - 1;
    return ZAMBRETTI_SHORT[i];
}

int zambrettiScore(int index)
{
    if (index < 1)
        return -1;
    int n = index;
    if (n < 1)
        n = 1;
    if (n > 26)
        n = 26;
    return int((n - 1) * 100 / 25.0f + 0.5f);
}

void sailorOutlook(float slp_hpa, float delta_3h, int *score_out, const char **phrase_out)
{
    if (std::isnan(slp_hpa) || std::isnan(delta_3h)) {
        *score_out = -1;
        *phrase_out = "--";
        return;
    }
    float p = slp_hpa;
    float d = delta_3h;
    float ad = d >= 0 ? d : -d;
    float base = 1050.0f - p;
    if (base < 0)
        base = 0;
    if (base > 100)
        base = 100;
    float score;
    const char *phrase;
    if (ad < 1.6f) {
        score = base * 0.7f + 15.0f;
        if (p >= 1020)
            phrase = "Settled";
        else if (p >= 1010)
            phrase = "Little change";
        else
            phrase = "Unsettled hold";
    } else if (d <= -3.0f) {
        score = base + 35.0f;
        phrase = "Rapid fall";
    } else if (d < 0) {
        score = base + 20.0f;
        phrase = "Falling, worse";
    } else if (d >= 3.0f) {
        score = base - 30.0f;
        phrase = "Rapid rise";
    } else {
        score = base - 15.0f;
        phrase = "Rising, better";
    }
    int n = int(score + 0.5f);
    if (n < 0)
        n = 0;
    if (n > 100)
        n = 100;
    *score_out = n;
    *phrase_out = phrase;
}

void hygroOutlook(float temp_c, float rh_pct, float dew_c, int *score_out, const char **phrase_out, float *spread_out)
{
    if (std::isnan(temp_c) || std::isnan(rh_pct) || std::isnan(dew_c)) {
        *score_out = -1;
        *phrase_out = "--";
        if (spread_out)
            *spread_out = NAN;
        return;
    }
    float spread = temp_c - dew_c;
    if (spread_out)
        *spread_out = spread;
    int score;
    const char *phrase;
    if (rh_pct >= 90 && spread <= 2.0f) {
        score = 75;
        phrase = "FOG RISK";
    } else if (rh_pct >= 80 && spread <= 4.0f) {
        score = 55;
        phrase = "MUGGY";
    } else if (rh_pct >= 70) {
        score = 35;
        phrase = "HUMID";
    } else if (rh_pct <= 40) {
        score = 10;
        phrase = "DRY";
    } else {
        score = 20;
        phrase = "FAIR AIR";
    }
    *score_out = score;
    *phrase_out = phrase;
}

void ensembleWx(int z_score, int sailor_score, int hygro_score, int *ens_out, int *agree_out)
{
    float parts[3];
    float weights[3];
    int n = 0;
    if (z_score >= 0) {
        parts[n] = float(z_score);
        weights[n] = 0.5f;
        n++;
    }
    if (sailor_score >= 0) {
        parts[n] = float(sailor_score);
        weights[n] = 0.35f;
        n++;
    }
    if (hygro_score >= 0) {
        parts[n] = float(hygro_score);
        weights[n] = 0.15f;
        n++;
    }
    if (n == 0) {
        *ens_out = -1;
        *agree_out = -1;
        return;
    }
    float acc = 0, sw = 0;
    for (int i = 0; i < n; i++) {
        acc += parts[i] * weights[i];
        sw += weights[i];
    }
    int ens = int(acc / sw + 0.5f);
    if (ens < 0)
        ens = 0;
    if (ens > 100)
        ens = 100;
    float lo = parts[0], hi = parts[0];
    for (int i = 1; i < n; i++) {
        if (parts[i] < lo)
            lo = parts[i];
        if (parts[i] > hi)
            hi = parts[i];
    }
    int agree = int(100.0f - (hi - lo) + 0.5f);
    if (agree < 0)
        agree = 0;
    if (agree > 100)
        agree = 100;
    *ens_out = ens;
    *agree_out = agree;
}

void confidenceFactors(int count, uint32_t span_ms, bool press_ok, bool has_rtc, int agree, bool altitude_ok, bool has_wind,
                       float raw_out[6], float *total_out)
{
    float three_h = 3 * 3600 * 1000.0f;
    float span_part = 0;
    if (span_ms > 0) {
        span_part = span_ms / three_h;
        if (span_part > 1.0f)
            span_part = 1.0f;
    }
    float n_part = 0;
    if (count > 0) {
        n_part = count / 36.0f;
        if (n_part > 1.0f)
            n_part = 1.0f;
    }
    float w_span = span_part * 40.0f;
    float w_n = n_part * 15.0f;
    float w_baro = press_ok ? 15.0f : 0.0f;
    float w_rtc = has_rtc ? 10.0f : 0.0f;
    float w_alt = altitude_ok ? 5.0f : 0.0f;
    float w_agree = agree >= 0 ? float(agree) * 0.15f : 0.0f;
    raw_out[0] = w_span;
    raw_out[1] = w_n;
    raw_out[2] = w_baro;
    raw_out[3] = w_rtc;
    raw_out[4] = w_alt;
    raw_out[5] = w_agree;
    float total = w_span + w_n + w_baro + w_rtc + w_alt + w_agree;
    if (!has_wind)
        total -= 10.0f;
    if (total < 0)
        total = 0;
    *total_out = total;
}

int confidencePct(int count, uint32_t span_ms, bool press_ok, bool has_rtc, int agree, bool altitude_ok, bool has_wind)
{
    float raw[6], total;
    confidenceFactors(count, span_ms, press_ok, has_rtc, agree, altitude_ok, has_wind, raw, &total);
    int n = int(total + 0.5f);
    if (n < 0)
        n = 0;
    if (n > 100)
        n = 100;
    return n;
}

void confReason(int count, uint32_t span_ms, bool press_ok, bool has_rtc, bool has_wind, char *buf, size_t buflen)
{
    if (count < MIN_TREND_POINTS || span_ms < MIN_TREND_SPAN_MS) {
        strncpy(buf, "collecting", buflen);
        buf[buflen - 1] = 0;
        return;
    }
    const char *baro = press_ok ? "baro OK" : "baro stale";
    const char *rtc = has_rtc ? "RTC Y" : "RTC N";
    if (!has_wind)
        snprintf(buf, buflen, "%s  %s  no wind", baro, rtc);
    else
        snprintf(buf, buflen, "%s  %s", baro, rtc);
}

int confFromPct(int pct)
{
    if (pct < 1)
        return CONF_NONE;
    if (pct < 40)
        return CONF_LOW;
    if (pct < 70)
        return CONF_MED;
    return CONF_HIGH;
}

const char *co2Band(int ppm, int warn, int high)
{
    if (ppm < 0)
        return "---";
    if (ppm < warn)
        return "GOOD";
    if (ppm < high)
        return "FAIR";
    return "VENTILATE";
}

} // namespace meteo
