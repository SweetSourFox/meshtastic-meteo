#include "unity.h"
#include "meteo/MeteoForecast.h"

void test_sea_level_hpa()
{
    float slp = meteo::seaLevelHpa(1000.0f, 100.0f, 20.0f);
    TEST_ASSERT_TRUE(slp > 1000.0f);
    TEST_ASSERT_TRUE(slp < 1020.0f);
}

void test_zambretti_steady()
{
    int idx = meteo::zambrettiIndex(1013.0f, meteo::TREND_STEADY, 6, false, true);
    TEST_ASSERT_TRUE(idx >= 1 && idx <= 26);
    const char *phrase = meteo::zambrettiPhrase(idx);
    TEST_ASSERT_NOT_NULL(phrase);
}

void test_classify_trend_collecting()
{
    int tid, cid;
    meteo::classifyTrend(2, 1000, 0.0f, &tid, &cid);
    TEST_ASSERT_EQUAL(meteo::TREND_COLLECTING, tid);
}

void test_slp2_age_filter()
{
    meteo::MeteoHistory h(60);
    uint32_t now = 1700000000UL;
    h.ingestSlp((now - 3600) * 1000UL, 1010.0f);
    h.ingestSlp(now * 1000UL, 1012.0f);
    TEST_ASSERT_EQUAL(2, h.slpRing().count());
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_sea_level_hpa);
    RUN_TEST(test_zambretti_steady);
    RUN_TEST(test_classify_trend_collecting);
    RUN_TEST(test_slp2_age_filter);
    return UNITY_END();
}
