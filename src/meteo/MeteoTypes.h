#pragma once

#include <cstdint>

namespace meteo
{

constexpr int W = 240;
constexpr int H = 135;

constexpr int CH_TEMP = 0;
constexpr int CH_HUM = 1;
constexpr int CH_PRESS = 2;
constexpr int CH_CO2 = 3;
constexpr int CH_GAS = 4;
constexpr int CH_IAQ = 5;
constexpr int CHANNEL_COUNT = 6;

constexpr int CHART_POINTS = 60;
constexpr int TREND_POINTS = 36;
constexpr uint32_t SLP_MAX_AGE_SEC = 3 * 60 * 60;
constexpr uint32_t SLP_MAX_AGE_MS = SLP_MAX_AGE_SEC * 1000UL;

constexpr int PAGE_OVERVIEW = 0;
constexpr int PAGE_AIR = 1;
constexpr int PAGE_FORECAST = 2;
constexpr int PAGE_CHART = 3;
constexpr int PAGE_STATS = 4;
constexpr int PAGE_RAW = 5;
constexpr int PAGE_SETTINGS = 6;
constexpr int PAGE_COUNT = 7;
constexpr int NAV_COUNT = 6;

constexpr int TREND_COLLECTING = 0;
constexpr int TREND_STEADY = 1;
constexpr int TREND_RISING = 2;
constexpr int TREND_FALLING = 3;
constexpr int TREND_RISING_FAST = 4;
constexpr int TREND_FALLING_FAST = 5;

constexpr int CONF_NONE = 0;
constexpr int CONF_LOW = 1;
constexpr int CONF_MED = 2;
constexpr int CONF_HIGH = 3;

constexpr int ST_ABSENT = 0;
constexpr int ST_STARTING = 1;
constexpr int ST_WARMING = 2;
constexpr int ST_READY = 3;
constexpr int ST_STALE = 4;
constexpr int ST_ERROR = 5;

constexpr int SRC_NONE = 0;
constexpr int SRC_BME = 1;
constexpr int SRC_SCD = 2;

constexpr uint32_t WARMUP_MS = 5 * 60 * 1000UL;
constexpr int LOW_MEM_RECOVER_BYTES = 48 * 1024;
constexpr int LOW_MEM_TRIP_BYTES = 40 * 1024;

constexpr uint8_t METEO_BME_ADDR = 0x77;
constexpr uint8_t METEO_SCD_ADDR = 0x62;
constexpr uint8_t METEO_PAHUB_MIN = 0x70;
constexpr uint8_t METEO_PAHUB_MAX = 0x76;
constexpr int METEO_PAHUB_CHANNELS = 6;

constexpr const char *SLP_PATH = "/meteo/slp.bin";
constexpr const char *SLP_TMP_PATH = "/meteo/slp.tmp";
constexpr const char *SETTINGS_NS = "meteo";
constexpr const char *LOG_DIR = "/meteo/logs";

constexpr float CH_DEFAULT_RANGE[CHANNEL_COUNT][2] = {
    {15.0f, 30.0f}, {20.0f, 80.0f}, {990.0f, 1030.0f}, {400.0f, 1600.0f}, {10.0f, 200.0f}, {0.0f, 200.0f}};

constexpr const char *TREND_LABEL[] = {"COLLECTING", "STEADY", "RISING", "FALLING", "RISING FAST", "FALLING FAST"};
constexpr const char *CONF_LABEL[] = {"NONE", "LOW", "MED", "HIGH"};
constexpr const char *PAGE_NAME[] = {"OVERVIEW", "AIR", "FORECAST", "CHART", "STATS", "RAW", "SETTINGS"};
constexpr const char *CH_LABEL[] = {"TEMP", "HUM", "PRESS", "CO2", "GAS", "IAQ"};
constexpr const char *CH_TICK = "THPCGQ";
constexpr const char *CH_UNIT[] = {"C", "%", "hPa", "ppm", "kOhm", ""};
constexpr const char *THEME_NAME[] = {"MIDNIGHT", "OCEAN", "AMBER", "PAPER"};

// RGB565 palettes: bg, fg, accent, card, good, warn, bad, muted
constexpr uint16_t THEMES[4][8] = {
    {0x0000, 0xFFFF, 0x5D7F, 0x18C3, 0x07E0, 0xFFE0, 0xF800, 0x7BEF},
    {0x0010, 0xFFFF, 0x07FF, 0x0210, 0x07E0, 0xFFE0, 0xF800, 0x4A69},
    {0x0000, 0xFFE0, 0xFD20, 0x2104, 0x07E0, 0xFFE0, 0xF800, 0x8410},
    {0xFFFF, 0x0000, 0x001F, 0xC618, 0x0320, 0xFD20, 0xF800, 0x8410},
};

constexpr uint16_t CH_COLOR[CHANNEL_COUNT] = {0xF800, 0x07FF, 0xFFE0, 0x07E0, 0xF81F, 0xFD20};

inline uint32_t wrapMs(uint32_t later, uint32_t earlier)
{
    return (later - earlier) & 0xFFFFFFFFu;
}

struct MeteoSettings {
    int schema = 1;
    float altitude_m = 0.0f;
    bool unit_f = false;
    int mode = 0;
    int chart_s = 60;
    int log_s = 0;
    int theme = 0;
    float t_off = 0.0f;
    float h_off = 0.0f;
    float p_off = 0.0f;
    int co2_warn = 800;
    int co2_high = 1200;
    uint8_t hub_addr = 0;
    int blank_s = 0;
    bool southern = false;
    bool logging_requested = false;
    bool has_rtc = false;
    int month = 1;
    uint8_t chart_mask = 63;

    bool chartLineOn(int ch) const { return (chart_mask & (1 << ch)) != 0; }
};

struct MeteoClock {
    bool valid = false;
    bool gps = false;
    uint32_t unix_sec = 0;
    int year = 0;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

} // namespace meteo
