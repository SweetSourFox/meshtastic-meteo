#pragma once

#include <cmath>

namespace meteo
{

float updateIaqBaseline(float baseline, float gas_ohm, bool warming);
void iaqScore(float gas_ohm, float humidity, float baseline, float *iaq, float *eco2, float *bvoc, float *aq_percent);
const char *iaqBand(float iaq);
int iaqColorIndex(float iaq);

} // namespace meteo
