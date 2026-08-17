#include "MeteoIaq.h"
#include <algorithm>

namespace meteo
{

namespace
{
float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}
} // namespace

float updateIaqBaseline(float baseline, float gas_ohm, bool warming)
{
    if (gas_ohm <= 0 || std::isnan(gas_ohm))
        return baseline;
    if (std::isnan(baseline) || baseline <= 0)
        return gas_ohm;
    if (warming)
        return gas_ohm > baseline ? gas_ohm : baseline;
    if (gas_ohm > baseline)
        return baseline * 0.995f + gas_ohm * 0.005f;
    return baseline * 0.999f + gas_ohm * 0.001f;
}

void iaqScore(float gas_ohm, float humidity, float baseline, float *iaq, float *eco2, float *bvoc, float *aq_percent)
{
    if (gas_ohm <= 0 || std::isnan(gas_ohm) || std::isnan(baseline) || baseline <= 0) {
        *iaq = *eco2 = *bvoc = NAN;
        if (aq_percent)
            *aq_percent = NAN;
        return;
    }
    if (std::isnan(humidity))
        humidity = 40.0f;
    humidity = clampf(humidity, 0.0f, 100.0f);
    constexpr float hum_baseline = 40.0f;
    constexpr float hum_weight = 0.25f;
    float hum_offset = humidity - hum_baseline;
    float hum_score;
    if (hum_offset > 0)
        hum_score = (100.0f - hum_baseline - hum_offset) / (100.0f - hum_baseline) * hum_weight * 100.0f;
    else
        hum_score = (hum_baseline + hum_offset) / hum_baseline * hum_weight * 100.0f;
    hum_score = clampf(hum_score, 0.0f, hum_weight * 100.0f);
    float gas_span = 100.0f - hum_weight * 100.0f;
    float gas_score = gas_ohm > baseline ? (gas_ohm / baseline) * gas_span : gas_span;
    gas_score = clampf(gas_score, 0.0f, gas_span);
    float aq = clampf(hum_score + gas_score, 0.0f, 100.0f);
    if (aq_percent)
        *aq_percent = aq;
    *iaq = clampf((100.0f - aq) * 5.0f, 0.0f, 500.0f);
    float ratio = clampf(gas_ohm / baseline, 0.05f, 4.0f);
    *eco2 = clampf(400.0f * powf(ratio, -1.75f), 400.0f, 5000.0f);
    *bvoc = clampf(0.5f * expf(*iaq / 100.0f), 0.1f, 1000.0f);
}

const char *iaqBand(float iaq)
{
    if (std::isnan(iaq))
        return "---";
    if (iaq <= 50)
        return "EXCEL";
    if (iaq <= 100)
        return "GOOD";
    if (iaq <= 150)
        return "FAIR";
    if (iaq <= 200)
        return "POOR";
    if (iaq <= 300)
        return "BAD";
    if (iaq <= 400)
        return "SEVERE";
    return "EXTREME";
}

int iaqColorIndex(float iaq)
{
    if (std::isnan(iaq))
        return 7;
    if (iaq <= 100)
        return 4;
    if (iaq <= 200)
        return 5;
    return 6;
}

} // namespace meteo
