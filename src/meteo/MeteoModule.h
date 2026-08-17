#pragma once

#include "MeteoBus.h"
#include "MeteoHistory.h"
#include "MeteoModel.h"
#include "MeteoScreen.h"
#include "MeteoSensors.h"
#include "MeteoStore.h"
#include "MeteoTypes.h"
#include "Observer.h"
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"
#include "mesh/MeshModule.h"
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace meteo
{

class MeteoModule : public MeshModule, public Observable<const UIFrameEvent *>, private concurrency::OSThread
{
  public:
    MeteoModule();
    void setup() override;
    int32_t runOnce() override;
    bool wantUIFrame() override { return true; }
    Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
    void afterUiDisplay(OLEDDisplay *display);
    bool interceptingKeyboardInput() override;
    int handleInputEvent(const InputEvent *event);
    bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }

    static MeteoModule *instance();

  private:
    CallbackObserver<MeteoModule, const InputEvent *> inputObserver =
        CallbackObserver<MeteoModule, const InputEvent *>(this, &MeteoModule::handleInputEvent);

    MeteoBus bus_;
    Bme688Driver bme_;
    Scd41Driver scd_;
    MeteoModel model_;
    MeteoHistory history_{60};
    MeteoStore store_;
    MeteoScreen screen_;
    MeteoSettings settings_;
    MeteoClock clock_;
    BusEndpoint bme_ep_, scd_ep_;
    uint32_t t0_ms_ = 0;
    uint32_t next_bme_ms_ = 0;
    uint32_t next_ingest_ms_ = 0;
    uint32_t next_save_ms_ = 0;
    uint8_t frame_index_ = 255;
    uint32_t last_draw_ms_ = 0;
    bool setup_complete_ = false;
    bool rescan_requested_ = false;

    bool isFrameActive() const;
    void requestRedraw();

    void updateClock();
    void scanSensors();
    void pollSensors();
    void doIngest();
    void checkLowMem();
};

extern MeteoModule *meteoModule;

} // namespace meteo
