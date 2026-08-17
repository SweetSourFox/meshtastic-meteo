#pragma once

#include "DebugConfiguration.h"
#include <cstdio>

// #region agent log
namespace meteo
{
inline void meteoDbg(const char *hypothesisId, const char *location, const char *message, const char *dataJson)
{
    char line[256];
    snprintf(line, sizeof(line),
             "METEO_DBG {\"sessionId\":\"d53c55\",\"hypothesisId\":\"%s\",\"location\":\"%s\",\"message\":\"%s\",\"data\":%s}",
             hypothesisId, location, message, dataJson ? dataJson : "{}");
    LOG_INFO("%s", line);
}
} // namespace meteo
// #endregion
