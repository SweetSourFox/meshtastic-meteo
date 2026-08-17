#include "MeteoSensors.h"
#include "MeteoDebug.h"
#include <Arduino.h>
#include <cmath>
#include <cstring>

namespace meteo
{

namespace
{
constexpr uint8_t REG_CHIPID = 0xD0;
constexpr uint8_t REG_VARIANT = 0xF0;
constexpr uint8_t REG_SOFTRESET = 0xE0;
constexpr uint8_t REG_CONFIG = 0x75;
constexpr uint8_t REG_CTRL_MEAS = 0x74;
constexpr uint8_t REG_CTRL_HUM = 0x72;
constexpr uint8_t REG_CTRL_GAS_0 = 0x70;
constexpr uint8_t REG_CTRL_GAS_1 = 0x71;
constexpr uint8_t REG_GAS_WAIT_0 = 0x64;
constexpr uint8_t REG_RES_HEAT_0 = 0x5A;
constexpr uint8_t REG_MEAS_STATUS = 0x1D;
constexpr uint8_t REG_COEFF1 = 0x8A;
constexpr uint8_t REG_COEFF2 = 0xE1;
constexpr uint8_t CHIP_ID = 0x61;

// Bosch oversampling register indices (not literal sample counts)
constexpr uint8_t OS_T = 3; // OS_4
constexpr uint8_t OS_P = 4; // OS_8
constexpr uint8_t OS_H = 2; // OS_2
constexpr uint8_t FILTER = 2;

uint32_t read24(const uint8_t *p)
{
    return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
}

int8_t rdS8(const uint8_t *p)
{
    return int8_t(p[0]);
}

int16_t rdS16(const uint8_t *p)
{
    return int16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}

uint16_t rdU16(const uint8_t *p)
{
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

uint32_t estimateMeasMs()
{
    const uint8_t cycles[] = {0, 1, 2, 4, 8, 16};
    uint32_t t = cycles[OS_T] + cycles[OS_P] + cycles[OS_H];
    uint32_t dur_us = t * 1963 + 477 * 4 + 477 * 5 + 1000;
    return (dur_us / 1000) + 170;
}
} // namespace

Bme688Driver::Bme688Driver(MeteoBus *bus) : bus_(bus) {}

bool Bme688Driver::init(const BusEndpoint &ep)
{
    ep_ = ep;
    uint8_t rst = 0xB6;
    if (!bus_->writetoMem(ep_, REG_SOFTRESET, &rst, 1))
        return false;
    delay(10);
    if (!bus_->readfromMem(ep_, REG_CHIPID, &chip_id, 1) || chip_id != CHIP_ID)
        return false;
    if (!bus_->readfromMem(ep_, REG_VARIANT, &variant_, 1))
        return false;
    if (!readCal())
        return false;
    configureTph();
    configureHeater(320, 150);
    meas_ms_ = estimateMeasMs();
    state_ = BME_IDLE;
    return true;
}

bool Bme688Driver::readCal()
{
    uint8_t c1[24], c2[14];
    if (!bus_->readfromMem(ep_, REG_COEFF1, c1, 24) || !bus_->readfromMem(ep_, REG_COEFF2, c2, 14))
        return false;
    uint8_t coeff[38];
    memcpy(coeff, c1, 24);
    memcpy(coeff + 24, c2, 14);

    // struct.unpack("<hbBHhbBhhbbBBhhBBBBBbbbBbHhbb", coeff)
    const uint8_t *p = coeff;
    int16_t u0 = rdS16(p);
    p += 2;
    int8_t u1 = rdS8(p);
    p += 1;
    p += 1; // B u2
    uint16_t u3 = rdU16(p);
    p += 2;
    int16_t u4 = rdS16(p);
    p += 2;
    int8_t u5 = rdS8(p);
    p += 1;
    p += 1; // B u6
    int16_t u7 = rdS16(p);
    p += 2;
    int16_t u8 = rdS16(p);
    p += 2;
    int8_t u9 = rdS8(p);
    p += 1;
    int8_t u10 = rdS8(p);
    p += 1;
    p += 1; // B u11
    p += 1; // B u12 @17
    int16_t u13 = rdS16(p);
    p += 2;
    int16_t u14 = rdS16(p);
    p += 2;
    uint8_t u15 = p[0];
    p += 1;
    p += 1; // B u16
    uint8_t u17 = p[0];
    p += 1;
    uint8_t u18 = p[0];
    p += 1;
    uint8_t u19 = p[0];
    p += 1;
    int8_t u20 = rdS8(p);
    p += 1;
    int8_t u21 = rdS8(p);
    p += 1;
    int8_t u22 = rdS8(p);
    p += 1;
    uint8_t u23 = p[0]; // B
    p += 1;
    int8_t u24 = rdS8(p);
    p += 1;
    uint16_t u25 = rdU16(p); // H par_t1
    p += 2;
    int16_t u26 = rdS16(p); // h
    p += 2;
    int8_t u27 = rdS8(p); // b
    p += 1;
    int8_t u28 = rdS8(p); // b

    int par_h2 = (int(u17) << 4) | (u18 >> 4);
    int par_h1 = (int(u19) << 4) | (u18 & 0x0F);

    cal_[0] = float(par_h1);
    cal_[1] = float(par_h2);
    cal_[2] = float(u20);
    cal_[3] = float(u21);
    cal_[4] = float(u22);
    cal_[5] = float(u23);
    cal_[6] = float(u24);
    cal_[7] = float(u27);
    cal_[8] = float(u26);
    cal_[9] = float(u28);
    cal_[10] = float(u25);
    cal_[11] = float(u0);
    cal_[12] = float(u1);
    cal_[13] = float(u3);
    cal_[14] = float(u4);
    cal_[15] = float(u5);
    cal_[16] = float(u7);
    cal_[17] = float(u8);
    cal_[18] = float(u10);
    cal_[19] = float(u9);
    cal_[20] = float(u13);
    cal_[21] = float(u14);
    cal_[22] = float(u15);

    uint8_t heat[1];
    if (bus_->readfromMem(ep_, 0x02, heat, 1))
        cal_[23] = float(heat[0] & 0x30) / 16.0f;
    if (bus_->readfromMem(ep_, 0x00, heat, 1))
        cal_[24] = float(heat[0]);
    if (bus_->readfromMem(ep_, 0x04, heat, 1))
        cal_[25] = float(heat[0] & 0xF0) / 16.0f;
    return true;
}

bool Bme688Driver::configureTph()
{
    uint8_t os_h = OS_H & 7;
    uint8_t filt = (FILTER & 7) << 2;
    uint8_t ctrl = ((OS_T & 7) << 5) | ((OS_P & 7) << 2);
    return bus_->writetoMem(ep_, REG_CTRL_HUM, &os_h, 1) && bus_->writetoMem(ep_, REG_CONFIG, &filt, 1) &&
           bus_->writetoMem(ep_, REG_CTRL_MEAS, &ctrl, 1);
}

bool Bme688Driver::configureHeater(int temp_c, int time_ms)
{
    float gh1 = cal_[7], gh2 = cal_[8], gh3 = cal_[9];
    float htr = cal_[23], htv = cal_[24];
    if (temp_c > 400)
        temp_c = 400;
    float var1 = (gh1 / 16.0f) + 49.0f;
    float var2 = ((gh2 / 32768.0f) * 0.0005f) + 0.00235f;
    float var3 = gh3 / 1024.0f;
    float var4 = var1 * (1.0f + (var2 * float(temp_c)));
    float var5 = var4 + (var3 * float(amb_temp_));
    int rh = int(3.4f * ((var5 * (4.0f / (4.0f + htr)) * (1.0f / (1.0f + (htv * 0.002f))))) - 25.0f) & 0xFF;
    int dur = time_ms;
    int factor = 0;
    if (dur >= 0xFC0)
        dur = 0xFF;
    else {
        while (dur > 0x3F) {
            dur /= 4;
            factor++;
        }
        dur = int(dur + factor * 64);
    }
    uint8_t gw = dur & 0xFF;
    if (!bus_->writetoMem(ep_, REG_RES_HEAT_0, (uint8_t *)&rh, 1))
        return false;
    if (!bus_->writetoMem(ep_, REG_GAS_WAIT_0, (uint8_t *)&gw, 1))
        return false;
    uint8_t g0, g1;
    bus_->readfromMem(ep_, REG_CTRL_GAS_0, &g0, 1);
    bus_->readfromMem(ep_, REG_CTRL_GAS_1, &g1, 1);
    g0 &= ~0x08;
    g1 = (g1 & ~0x0F) | 0x00;
    g1 = (g1 & ~0x30) | (0x02 << 4);
    heater_ready_ = bus_->writetoMem(ep_, REG_CTRL_GAS_0, &g0, 1) && bus_->writetoMem(ep_, REG_CTRL_GAS_1, &g1, 1);
    return heater_ready_;
}

bool Bme688Driver::trigger(uint32_t now_ms)
{
    configureTph();
    if (!heater_ready_)
        configureHeater(320, 150);
    uint8_t ctrl = ((OS_T & 7) << 5) | ((OS_P & 7) << 2) | 0x01;
    if (!bus_->writetoMem(ep_, REG_CTRL_MEAS, &ctrl, 1)) {
        state_ = BME_ERROR;
        return false;
    }
    deadline_ms_ = now_ms + meas_ms_;
    state_ = BME_WAIT;
    return true;
}

BmeState Bme688Driver::poll(uint32_t now_ms)
{
    if (state_ != BME_WAIT)
        return state_;
    if (int32_t(now_ms - deadline_ms_) < 0)
        return BME_WAIT;
    uint8_t data[17];
    if (!bus_->readfromMem(ep_, REG_MEAS_STATUS, data, 17)) {
        state_ = BME_ERROR;
        return BME_ERROR;
    }
    if ((data[0] & 0x80) == 0) {
        if (int32_t(now_ms - (deadline_ms_ + 80)) < 0)
            return BME_WAIT;
        state_ = BME_ERROR;
        return BME_ERROR;
    }
    if (!parseField(data)) {
        state_ = BME_ERROR;
        return BME_ERROR;
    }
    state_ = BME_READY;
    amb_temp_ = int(temperature);
    return BME_READY;
}

bool Bme688Driver::parseField(const uint8_t *data)
{
    adc_p = read24(data + 2) >> 4;
    adc_t = read24(data + 5) >> 4;
    uint16_t adc_h = (data[8] << 8) | data[9];
    uint16_t adc_gas = (data[15] << 2) | (data[16] >> 6);
    uint8_t gas_range = data[16] & 0x0F;
    bool raw_gas_valid = (data[16] & 0x20) != 0;
    heat_stab = (data[16] & 0x10) != 0;
    temperature = calcTemperature(adc_t);
    pres_pa = calcPressure(adc_p);
    pressure = pres_pa / 100.0f;
    humidity = calcHumidity(adc_h);
    gas_ohm = calcGasHigh(adc_gas, gas_range);
    gas_valid = raw_gas_valid && gas_ohm > 0;
    cal_p1 = cal_[13];
    // #region agent log
    {
        static uint32_t last = 0;
        uint32_t now = millis();
        if (now - last > 8000) {
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "{\"adcp\":%lu,\"adct\":%lu,\"hpa\":%.1f,\"t1\":%.0f,\"p1\":%.0f,\"t\":%.1f,\"h\":%.1f,\"pok\":%d}",
                     (unsigned long)adc_p, (unsigned long)adc_t, pressure, cal_[10], cal_p1, temperature, humidity,
                     (pressure >= 300.0f && pressure <= 1100.0f) ? 1 : 0);
            meteoDbg("H-adcp", "MeteoSensors.cpp:parseField", "raw", buf);
            last = now;
        }
    }
    // #endregion
    return true;
}

float Bme688Driver::calcTemperature(uint32_t adc)
{
    float par_t1 = cal_[10], par_t2 = cal_[11], par_t3 = cal_[12];
    float var1 = ((adc / 16384.0f) - (par_t1 / 1024.0f)) * par_t2;
    float var2 = ((adc / 131072.0f) - (par_t1 / 8192.0f)) * ((adc / 131072.0f) - (par_t1 / 8192.0f)) * (par_t3 * 16.0f);
    t_fine_ = var1 + var2;
    return t_fine_ / 5120.0f;
}

float Bme688Driver::calcPressure(uint32_t adc)
{
    float p1 = cal_[13], p2 = cal_[14], p3 = cal_[15], p4 = cal_[16], p5 = cal_[17], p6 = cal_[18], p7 = cal_[19], p8 = cal_[20],
          p9 = cal_[21], p10 = cal_[22];
    float var1 = (t_fine_ / 2.0f) - 64000.0f;
    float var2 = var1 * var1 * (p6 / 131072.0f);
    var2 += var1 * p5 * 2.0f;
    var2 = (var2 / 4.0f) + (p4 * 65536.0f);
    var1 = (((p3 * var1 * var1) / 16384.0f) + (p2 * var1)) / 524288.0f;
    var1 = (1.0f + (var1 / 32768.0f)) * p1;
    pres_var1 = var1;
    float calc_pres = 1048576.0f - adc;
    if (int(var1) == 0)
        return 0;
    calc_pres = (calc_pres - (var2 / 4096.0f)) * 6250.0f / var1;
    var1 = (p9 * calc_pres * calc_pres) / 2147483648.0f;
    var2 = calc_pres * (p8 / 32768.0f);
    float var3 = powf(calc_pres / 256.0f, 3.0f) * (p10 / 131072.0f);
    calc_pres += (var1 + var2 + var3 + (p7 * 128.0f)) / 16.0f;
    return calc_pres;
}

float Bme688Driver::calcHumidity(uint32_t adc)
{
    float h1 = cal_[0], h2 = cal_[1], h3 = cal_[2], h4 = cal_[3], h5 = cal_[4], h6 = cal_[5], h7 = cal_[6];
    float temp_comp = t_fine_ / 5120.0f;
    float var1 = float(adc) - ((h1 * 16.0f) + ((h3 / 2.0f) * temp_comp));
    float var2 = var1 * ((h2 / 262144.0f) * (1.0f + ((h4 / 16384.0f) * temp_comp) + ((h5 / 1048576.0f) * temp_comp * temp_comp)));
    float var3 = h6 / 16384.0f;
    float var4 = h7 / 2097152.0f;
    float calc_hum = var2 + ((var3 + (var4 * temp_comp)) * var2 * var2);
    if (calc_hum > 100.0f)
        return 100.0f;
    if (calc_hum < 0.0f)
        return 0.0f;
    return calc_hum;
}

float Bme688Driver::calcGasHigh(uint16_t adc, uint8_t range)
{
    uint32_t var1 = 262144u >> range;
    int32_t var2 = (int32_t(adc) - 512) * 3 + 4096;
    if (var2 == 0)
        return 0;
    return 1000000.0f * float(var1) / float(var2);
}

// --- SCD41 ---
Scd41Driver::Scd41Driver(MeteoBus *bus) : bus_(bus) {}

uint8_t Scd41Driver::crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
    }
    return crc;
}

bool Scd41Driver::cmd(uint16_t c)
{
    uint8_t buf[2] = {uint8_t(c >> 8), uint8_t(c & 0xFF)};
    return bus_->writeto(ep_, buf, 2);
}

bool Scd41Driver::cmdU16(uint16_t c, uint16_t v)
{
    uint8_t buf[5];
    buf[0] = c >> 8;
    buf[1] = c & 0xFF;
    buf[2] = v >> 8;
    buf[3] = v & 0xFF;
    buf[4] = crc8(buf + 2, 2);
    return bus_->writeto(ep_, buf, 5);
}

bool Scd41Driver::readWords(uint16_t c, uint8_t *buf, size_t n)
{
    if (!cmd(c))
        return false;
    delay(1);
    return bus_->readfrom(ep_, buf, n);
}

bool Scd41Driver::init(const BusEndpoint &ep, ScdMode mode)
{
    ep_ = ep;
    cmd(0x3F86); // stop
    delay(500);
    uint16_t start = mode == SCD_ECO ? 0x21AC : 0x21B1;
    if (!cmd(start))
        return false;
    delay(1000);
    mode_ = mode;
    measuring_ = true;
    next_poll_ms_ = millis() + 5000;
    return true;
}

bool Scd41Driver::poll(uint32_t now_ms)
{
    if (!measuring_ || int32_t(now_ms - next_poll_ms_) < 0)
        return false;
    uint8_t ready[3];
    if (!readWords(0xE4B8, ready, 3) || crc8(ready, 2) != ready[2])
        return false;
    if ((ready[0] & 0x07) == 0 && ready[1] == 0) {
        next_poll_ms_ = now_ms + 1000;
        return false;
    }
    uint8_t meas[9];
    if (!readWords(0xEC05, meas, 9)) {
        crc_errors++;
        return false;
    }
    if (crc8(meas, 2) != meas[2] || crc8(meas + 3, 2) != meas[5] || crc8(meas + 6, 2) != meas[8]) {
        crc_errors++;
        return false;
    }
    co2 = (meas[0] << 8) | meas[1];
    uint16_t raw_t = (meas[3] << 8) | meas[4];
    uint16_t raw_h = (meas[6] << 8) | meas[7];
    temperature = -45.0f + 175.0f * raw_t / 65535.0f;
    humidity = 100.0f * raw_h / 65535.0f;
    out_of_range = co2 > 40000;
    next_poll_ms_ = now_ms + (mode_ == SCD_ECO ? 30000 : 5000);
    return true;
}

} // namespace meteo
