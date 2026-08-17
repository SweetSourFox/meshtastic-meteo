#pragma once

#include "MeteoHistory.h"
#include "MeteoTypes.h"
#include <cmath>
#include <cstring>

namespace meteo
{

struct Field {
    float raw = NAN;
    float corrected = NAN;
    float display = NAN;
    float last_good = NAN;
    uint32_t last_ms = 0;
    bool valid = false;
    bool stale = false;
    int source = SRC_NONE;
    int errors = 0;

    void setOk(uint32_t now, float raw_v, float corr, int src, float ema = 0.25f, float median3 = NAN);
    void noteError();
    uint32_t ageMs(uint32_t now) const;
    void markStaleIfOld(uint32_t now, uint32_t timeout_ms);
};

class MeteoModel
{
  public:
    int bme_state = ST_ABSENT;
    int scd_state = ST_ABSENT;
    Field temp, rh, press, gas, co2, scd_temp, scd_rh;
    float slp = NAN;
    float dew = NAN;
    int trend_id = TREND_COLLECTING;
    const char *trend_method = "linear";
    int conf_id = CONF_NONE;
    int conf_pct = 0;
    float conf_factors[6] = {};
    char conf_reason_str[28] = "collecting";
    int z_index = -1;
    const char *z_phrase = "--";
    int wx_z = -1, wx_sailor = -1, wx_hygro = -1, wx_ens = -1, wx_agree = -1;
    const char *sailor_phrase = "--";
    const char *hygro_phrase = "--";
    float dp_spread = NAN;
    float slp_d3h = NAN;
    float gas_baseline = NAN;
    const char *gas_trend = "FLAT";
    float iaq = NAN, eco2 = NAN, bvoc = NAN;
    const char *iaq_band = "---";
    bool iaq_warming = false;
    float warmup_frac = 0;
    int warmup_left_s = 0;
    bool bme_present = false;
    bool scd_present = false;
    int scd_crc = 0;
    bool low_mem = false;
    bool dirty = true;
    uint32_t started_ms = 0;

    void setPresence(bool bme, bool scd, uint32_t now);
    void onBme(uint32_t now, float t, float h, float p, float gas_ohm, bool gas_valid, float t_off, float h_off, float p_off,
               bool warming);
    void onBmeError();
    void onScd(uint32_t now, uint16_t co2_ppm, float t, float h, bool stabilizing);
    void onScdError(bool crc = false);
    void refreshDerived(uint32_t now_ms, MeteoSettings &settings, MeteoHistory &history);

  private:
    uint32_t bme_started_ms_ = 0;
    float co2_med_[3] = {};
    int co2_med_n_ = 0;
    float median3Co2(float v);
    void refreshIaq(bool warming);
    void refreshWarmup(uint32_t now);
};

} // namespace meteo
