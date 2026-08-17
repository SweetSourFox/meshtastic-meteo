#pragma once

#include "MeteoBus.h"
#include <cstdint>

namespace meteo
{

enum BmeState : uint8_t { BME_IDLE = 0, BME_WAIT = 1, BME_READY = 2, BME_ERROR = 3 };

class Bme688Driver
{
  public:
    explicit Bme688Driver(MeteoBus *bus);
    bool init(const BusEndpoint &ep);
    bool trigger(uint32_t now_ms);
    BmeState poll(uint32_t now_ms);

    float temperature = 0;
    float pressure = 0;
    float humidity = 0;
    float gas_ohm = 0;
    bool gas_valid = false;
    bool heat_stab = false;
    uint8_t chip_id = 0;
    uint32_t adc_p = 0;
    uint32_t adc_t = 0;
    float cal_p1 = 0;
    float pres_var1 = 0;
    float pres_pa = 0;

  private:
    MeteoBus *bus_;
    BusEndpoint ep_;
    bool heater_ready_ = false;
    uint8_t variant_ = 1;
    uint32_t deadline_ms_ = 0;
    BmeState state_ = BME_IDLE;
    uint32_t meas_ms_ = 200;
    float t_fine_ = 0;
    int amb_temp_ = 25;
    float cal_[32] = {};
    bool readCal();
    bool configureTph();
    bool configureHeater(int temp_c, int time_ms);
    bool parseField(const uint8_t *data);
    float calcTemperature(uint32_t adc);
    float calcPressure(uint32_t adc);
    float calcHumidity(uint32_t adc);
    float calcGasHigh(uint16_t adc, uint8_t range);
};

enum ScdMode : uint8_t { SCD_OFF = 0, SCD_NORMAL = 1, SCD_ECO = 2 };

class Scd41Driver
{
  public:
    explicit Scd41Driver(MeteoBus *bus);
    bool init(const BusEndpoint &ep, ScdMode mode = SCD_ECO);
    bool poll(uint32_t now_ms);

    uint16_t co2 = 0;
    float temperature = 0;
    float humidity = 0;
    int crc_errors = 0;
    bool out_of_range = false;

  private:
    MeteoBus *bus_;
    BusEndpoint ep_;
    ScdMode mode_ = SCD_OFF;
    uint32_t next_poll_ms_ = 0;
    bool measuring_ = false;
    static uint8_t crc8(const uint8_t *data, size_t len);
    bool cmd(uint16_t c);
    bool cmdU16(uint16_t c, uint16_t v);
    bool readWords(uint16_t cmd, uint8_t *buf, size_t n);
};

} // namespace meteo
