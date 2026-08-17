#include "MeteoBus.h"
#include "MeteoTypes.h"
#include "configuration.h"
#include <Wire.h>

namespace meteo
{

MeteoBus::MeteoBus(TwoWire *wire) : wire_(wire) {}

bool MeteoBus::begin()
{
#ifdef I2C_SDA1
    if (!wire_) {
#ifdef ARCH_RP2040
        Wire1.setSDA(I2C_SDA1);
        Wire1.setSCL(I2C_SCL1);
        Wire1.begin();
#else
        Wire1.begin(I2C_SDA1, I2C_SCL1);
        Wire1.setTimeout(50);
#endif
        wire_ = &Wire1;
    }
    if (wire_)
        wire_->setTimeout(50);
#endif
    return wire_ != nullptr;
}

void MeteoBus::select(const BusEndpoint &ep)
{
    if (!ep.viaHub())
        return;
    if (selected_hub_ == ep.hub_addr && selected_ch_ == ep.channel)
        return;
    uint8_t mask = 1 << ep.channel;
    wire_->beginTransmission(ep.hub_addr);
    wire_->write(mask);
    wire_->endTransmission();
    selected_hub_ = ep.hub_addr;
    selected_ch_ = ep.channel;
}

void MeteoBus::closeHub(uint8_t hub)
{
    if (!hub)
        return;
    wire_->beginTransmission(hub);
    wire_->write((uint8_t)0);
    wire_->endTransmission();
    selected_hub_ = -1;
    selected_ch_ = -1;
}

bool MeteoBus::probe(uint8_t addr)
{
    if (!wire_)
        return false;
    wire_->beginTransmission(addr);
    return wire_->endTransmission() == 0;
}

bool MeteoBus::ping(const BusEndpoint &ep)
{
    select(ep);
    return probe(ep.addr);
}

bool MeteoBus::writeto(const BusEndpoint &ep, const uint8_t *buf, size_t len)
{
    select(ep);
    wire_->beginTransmission(ep.addr);
    wire_->write(buf, len);
    return wire_->endTransmission() == 0;
}

bool MeteoBus::readfrom(const BusEndpoint &ep, uint8_t *buf, size_t len)
{
    select(ep);
    return wire_->requestFrom(ep.addr, len) == len && wire_->readBytes(buf, len) == len;
}

bool MeteoBus::writetoMem(const BusEndpoint &ep, uint8_t reg, const uint8_t *buf, size_t len)
{
    select(ep);
    wire_->beginTransmission(ep.addr);
    wire_->write(reg);
    wire_->write(buf, len);
    return wire_->endTransmission() == 0;
}

bool MeteoBus::readfromMem(const BusEndpoint &ep, uint8_t reg, uint8_t *buf, size_t len)
{
    select(ep);
    wire_->beginTransmission(ep.addr);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0)
        return false;
    return wire_->requestFrom(ep.addr, len) == len && wire_->readBytes(buf, len) == len;
}

void MeteoBus::scan(uint8_t preferred_hub, BusEndpoint *bme, BusEndpoint *scd, uint8_t *hub_out)
{
    *bme = {};
    *scd = {};
    *hub_out = 0;
    uint8_t found[16];
    int n = 0;
    uint8_t addrs[] = {METEO_BME_ADDR, METEO_SCD_ADDR};
    for (uint8_t a : addrs) {
        if (probe(a))
            found[n++] = a;
    }
    for (uint8_t a = METEO_PAHUB_MIN; a <= METEO_PAHUB_MAX; a++) {
        if (probe(a))
            found[n++] = a;
    }
    uint8_t hub = 0;
    if (preferred_hub >= METEO_PAHUB_MIN && preferred_hub <= METEO_PAHUB_MAX) {
        for (int i = 0; i < n; i++) {
            if (found[i] == preferred_hub) {
                hub = preferred_hub;
                break;
            }
        }
    }
    if (!hub) {
        for (int i = 0; i < n; i++) {
            if (found[i] >= METEO_PAHUB_MIN && found[i] <= METEO_PAHUB_MAX) {
                hub = found[i];
                break;
            }
        }
    }
    *hub_out = hub;
    if (hub) {
        for (int ch = 0; ch < METEO_PAHUB_CHANNELS; ch++) {
            uint8_t mask = 1 << ch;
            wire_->beginTransmission(hub);
            wire_->write(mask);
            wire_->endTransmission();
            selected_hub_ = hub;
            selected_ch_ = ch;
            if (!bme->addr && probe(METEO_BME_ADDR))
                *bme = {METEO_BME_ADDR, hub, int8_t(ch)};
            if (!scd->addr && probe(METEO_SCD_ADDR))
                *scd = {METEO_SCD_ADDR, hub, int8_t(ch)};
            if (bme->addr && scd->addr)
                break;
        }
    }
    for (int i = 0; i < n; i++) {
        if (!bme->addr && found[i] == METEO_BME_ADDR)
            *bme = {METEO_BME_ADDR};
        if (!scd->addr && found[i] == METEO_SCD_ADDR)
            *scd = {METEO_SCD_ADDR};
    }
}

void MeteoBus::noteError()
{
    errors_++;
}
void MeteoBus::noteOk()
{
    errors_ = 0;
}

void MeteoBus::recover()
{
    selected_hub_ = -1;
    selected_ch_ = -1;
    errors_ = 0;
#ifdef I2C_SDA1
    wire_->end();
    wire_->begin(I2C_SDA1, I2C_SCL1);
#endif
}

} // namespace meteo
