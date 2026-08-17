#pragma once

#include "MeteoTypes.h"

namespace meteo
{

MeteoClock syncClock();
bool clockIso(const MeteoClock &clk, char *buf, size_t len);

class MeteoStore
{
  public:
    void loadSettings(MeteoSettings &s);
    void saveSettings(const MeteoSettings &s);

    int sdState() const { return sd_state_; }
    void refreshSd();
    bool maybeWriteCsv(uint32_t now_unix, const MeteoClock &clk, const MeteoSettings &s, const char *line_fields);

  private:
    int sd_state_ = 0; // 0 none, 1 ok, 2 lost
    uint32_t last_log_ms_ = 0;
    char log_path_[48] = {0};
};

} // namespace meteo
