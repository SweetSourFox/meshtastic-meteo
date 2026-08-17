#include "MeteoStore.h"
#include "FSCommon.h"
#include "RTC.h"
#include <Preferences.h>
#include <SD.h>
#include <time.h>

namespace meteo
{

MeteoClock syncClock()
{
    MeteoClock c;
    uint32_t gps = getValidTime(RTCQualityGPS);
    uint32_t ntp = getValidTime(RTCQualityNTP);
    uint32_t dev = getValidTime(RTCQualityDevice);
    if (gps > 0) {
        c.unix_sec = gps;
        c.gps = true;
        c.valid = true;
    } else if (ntp > 0) {
        c.unix_sec = ntp;
        c.valid = true;
    } else if (dev > 0) {
        c.unix_sec = dev;
        c.valid = true;
    }
    if (c.valid) {
        time_t t = c.unix_sec;
        struct tm tm;
        gmtime_r(&t, &tm);
        c.year = tm.tm_year + 1900;
        c.month = tm.tm_mon + 1;
        c.day = tm.tm_mday;
        c.hour = tm.tm_hour;
        c.minute = tm.tm_min;
        c.second = tm.tm_sec;
    }
    return c;
}

bool clockIso(const MeteoClock &clk, char *buf, size_t len)
{
    if (!clk.valid || len < 20)
        return false;
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d", clk.year, clk.month, clk.day, clk.hour, clk.minute, clk.second);
    return true;
}

void MeteoStore::loadSettings(MeteoSettings &s)
{
    Preferences prefs;
    if (!prefs.begin(SETTINGS_NS, true))
        return;
    s.schema = prefs.getInt("schema", 1);
    s.altitude_m = prefs.getFloat("alt", 0);
    s.unit_f = prefs.getBool("unit_f", false);
    s.mode = prefs.getInt("mode", 0);
    s.chart_s = prefs.getInt("chart_s", 60);
    s.log_s = prefs.getInt("log_s", 0);
    s.theme = prefs.getInt("theme", 0);
    s.t_off = prefs.getFloat("t_off", 0);
    s.h_off = prefs.getFloat("h_off", 0);
    s.p_off = prefs.getFloat("p_off", 0);
    s.co2_warn = prefs.getInt("co2_w", 800);
    s.co2_high = prefs.getInt("co2_h", 1200);
    s.hub_addr = prefs.getUChar("hub", 0);
    s.blank_s = prefs.getInt("blank", 0);
    s.southern = prefs.getBool("south", false);
    s.logging_requested = prefs.getBool("log_req", false);
    s.chart_mask = prefs.getUChar("ch_mask", 63);
    prefs.end();
}

void MeteoStore::saveSettings(const MeteoSettings &s)
{
    Preferences prefs;
    if (!prefs.begin(SETTINGS_NS, false))
        return;
    prefs.putInt("schema", s.schema);
    prefs.putFloat("alt", s.altitude_m);
    prefs.putBool("unit_f", s.unit_f);
    prefs.putInt("mode", s.mode);
    prefs.putInt("chart_s", s.chart_s);
    prefs.putInt("log_s", s.log_s);
    prefs.putInt("theme", s.theme);
    prefs.putFloat("t_off", s.t_off);
    prefs.putFloat("h_off", s.h_off);
    prefs.putFloat("p_off", s.p_off);
    prefs.putInt("co2_w", s.co2_warn);
    prefs.putInt("co2_h", s.co2_high);
    prefs.putUChar("hub", s.hub_addr);
    prefs.putInt("blank", s.blank_s);
    prefs.putBool("south", s.southern);
    prefs.putBool("log_req", s.logging_requested);
    prefs.putUChar("ch_mask", s.chart_mask);
    prefs.end();
}

void MeteoStore::refreshSd()
{
#ifdef HAS_SDCARD
    if (SD.cardType() == CARD_NONE) {
        sd_state_ = sd_state_ == 1 ? 2 : 0;
        return;
    }
    sd_state_ = 1;
#else
    sd_state_ = FSCom.totalBytes() > 0 ? 1 : 0;
#endif
}

bool MeteoStore::maybeWriteCsv(uint32_t now_unix, const MeteoClock &clk, const MeteoSettings &s, const char *line_fields)
{
    if (s.log_s <= 0 || !s.logging_requested || !line_fields)
        return false;
    uint32_t now_ms = millis();
    if (last_log_ms_ && (now_ms - last_log_ms_) < uint32_t(s.log_s) * 1000UL)
        return false;
    last_log_ms_ = now_ms;

    if (!FSCom.exists("/meteo"))
        FSCom.mkdir("/meteo");
    if (!FSCom.exists("/meteo/logs"))
        FSCom.mkdir("/meteo/logs");

    char path[48];
    if (clk.valid)
        snprintf(path, sizeof(path), "/meteo/logs/%04d-%02d-%02d.csv", clk.year, clk.month, clk.day);
    else
        snprintf(path, sizeof(path), "/meteo/logs/UNSYNCED.csv");

    bool header = !FSCom.exists(path);
    File f = FSCom.open(path, FILE_APPEND);
    if (!f)
        return false;
    if (header)
        f.println("iso_time,uptime_s,temp_c,rh_pct,pressure_hpa,slp_hpa,gas_ohm,iaq,eco2_ppm,bvoc_ppm,co2_ppm,scd_temp_c,scd_rh_pct,status");
    f.println(line_fields);
    f.close();
    strncpy(log_path_, path, sizeof(log_path_) - 1);
    (void)now_unix;
    return true;
}

} // namespace meteo
