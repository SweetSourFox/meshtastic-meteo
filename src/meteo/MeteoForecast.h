#pragma once

#include "MeteoTypes.h"
#include <cstddef>

namespace meteo
{

constexpr const char *ZAMBRETTI_SHORT[] = {
    "Settled fine",       "Fine weather",         "Becoming fine",       "Fine, less settled",   "Fine, poss showers",
    "Fairly fine, better", "Fairly fine, showers", "Fairly fine later rain", "Showery early, better", "Changeable, mending",
    "Fairly fine, showers", "Unsettled, clearing",  "Unsettled, improving", "Showery, bright",      "Showery, unsettled",
    "Changeable, some rain", "Unsettled, fine spells", "Unsettled, rain later", "Unsettled, some rain", "Very unsettled",
    "Rain, worsening",    "Rain, very unsettled", "Rain at times",       "Rain, unsettled",      "Stormy, may improve",
    "Stormy, much rain",
};

float seaLevelHpa(float pressure_hpa, float altitude_m, float temp_c);
float dewpointC(float temp_c, float rh_pct);

float linearSlopeHpaPerHour(const uint32_t *times_ms, const float *values, int count);
float pressureTrendRobust(const uint32_t *times_ms, const float *values, int count, const char **method_out = nullptr);

void classifyTrend(int count, uint32_t span_ms, float rate, int *trend_id, int *conf_id);

int zambrettiIndex(float slp_hpa, int trend_id, int month, bool southern, bool has_rtc);
const char *zambrettiPhrase(int index);
int zambrettiScore(int index);

void sailorOutlook(float slp_hpa, float delta_3h, int *score_out, const char **phrase_out);
void hygroOutlook(float temp_c, float rh_pct, float dew_c, int *score_out, const char **phrase_out, float *spread_out);
void ensembleWx(int z_score, int sailor_score, int hygro_score, int *ens_out, int *agree_out);

void confidenceFactors(int count, uint32_t span_ms, bool press_ok, bool has_rtc, int agree, bool altitude_ok, bool has_wind,
                       float raw_out[6], float *total_out);
int confidencePct(int count, uint32_t span_ms, bool press_ok, bool has_rtc, int agree, bool altitude_ok = true,
                  bool has_wind = false);
void confReason(int count, uint32_t span_ms, bool press_ok, bool has_rtc, bool has_wind, char *buf, size_t buflen);
int confFromPct(int pct);
const char *co2Band(int ppm, int warn = 800, int high = 1200);

} // namespace meteo
