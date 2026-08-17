#pragma once

#include <Wire.h>
#include <cstdint>

namespace meteo
{

struct BusEndpoint {
    uint8_t addr = 0;
    uint8_t hub_addr = 0;
    int8_t channel = -1;
    bool viaHub() const { return hub_addr != 0 && channel >= 0; }
};

class MeteoBus
{
  public:
    explicit MeteoBus(TwoWire *wire = nullptr);
    bool begin();
    void scan(uint8_t preferred_hub, BusEndpoint *bme, BusEndpoint *scd, uint8_t *hub_out);
    bool ping(const BusEndpoint &ep);
    bool writeto(const BusEndpoint &ep, const uint8_t *buf, size_t len);
    bool readfrom(const BusEndpoint &ep, uint8_t *buf, size_t len);
    bool writetoMem(const BusEndpoint &ep, uint8_t reg, const uint8_t *buf, size_t len);
    bool readfromMem(const BusEndpoint &ep, uint8_t reg, uint8_t *buf, size_t len);
    void noteError();
    void noteOk();
    bool needsRecover() const { return errors_ >= 3; }
    void recover();

  private:
    TwoWire *wire_;
    int selected_hub_ = -1;
    int selected_ch_ = -1;
    int errors_ = 0;
    bool probe(uint8_t addr);
    void select(const BusEndpoint &ep);
    void closeHub(uint8_t hub);
};

} // namespace meteo
