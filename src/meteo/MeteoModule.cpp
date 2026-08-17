#include "MeteoModule.h"
#include "MeteoDebug.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "RTC.h"
#include "configuration.h"
#include "input/InputBroker.h"
#include <cstdio>
#include <esp_heap_caps.h>

#if defined(USE_ST7789)
#include <ST7789Spi.h>
extern uint16_t TFT_MESH;
#endif

namespace meteo
{

MeteoModule *meteoModule = nullptr;

MeteoModule::MeteoModule() : MeshModule("meteo"), OSThread("Meteo"), bme_(&bus_), scd_(&bus_) {}

MeteoModule *MeteoModule::instance()
{
    return meteoModule;
}

bool MeteoModule::isFrameActive() const
{
    return last_draw_ms_ != 0 && (millis() - last_draw_ms_) < 1000;
}

void MeteoModule::requestRedraw()
{
    UIFrameEvent e;
    e.action = UIFrameEvent::REDRAW_ONLY;
    notifyObservers(&e);
}

bool MeteoModule::interceptingKeyboardInput()
{
    return isFrameActive();
}

void MeteoModule::setup()
{
    store_.loadSettings(settings_);
    history_.setChartInterval(settings_.chart_s);
    if (!bus_.begin()) {
    } else {
        scanSensors();
    }
    updateClock();
    history_.loadSlp(clock_.unix_sec, clock_.valid);
    store_.refreshSd();
    t0_ms_ = millis();
    next_ingest_ms_ = t0_ms_ + 1000;
    next_save_ms_ = t0_ms_ + 60000;
    if (inputBroker)
        inputObserver.observe(inputBroker);
    setIntervalFromNow(200);
}

void MeteoModule::updateClock()
{
    clock_ = meteo::syncClock();
    settings_.has_rtc = clock_.valid;
    if (clock_.valid)
        settings_.month = clock_.month;
}

void MeteoModule::scanSensors()
{
    uint8_t hub = 0;
    bus_.scan(settings_.hub_addr, &bme_ep_, &scd_ep_, &hub);
    settings_.hub_addr = hub;
    bool has_bme = bme_ep_.addr != 0;
    bool has_scd = scd_ep_.addr != 0;
    model_.setPresence(has_bme, has_scd, millis());
    if (has_bme)
        bme_.init(bme_ep_);
    if (has_scd)
        scd_.init(scd_ep_, SCD_ECO);
}

int32_t MeteoModule::runOnce()
{
    if (!setup_complete_) {
        setup();
        setup_complete_ = true;
        return 200;
    }
    pollSensors();
    uint32_t now = millis();
    if (int32_t(now - next_ingest_ms_) >= 0) {
        doIngest();
        next_ingest_ms_ = now + 1000;
    }
    if (int32_t(now - next_save_ms_) >= 0) {
        updateClock();
        history_.saveSlp(clock_.unix_sec, clock_.valid);
        next_save_ms_ = now + 900000;
    }
    if (rescan_requested_) {
        rescan_requested_ = false;
        scanSensors();
    }
    checkLowMem();
    return 200;
}

void MeteoModule::checkLowMem()
{
    int free_heap = esp_get_free_heap_size();
    if (model_.low_mem) {
        if (free_heap > LOW_MEM_RECOVER_BYTES)
            model_.low_mem = false;
    } else if (free_heap < LOW_MEM_TRIP_BYTES) {
        model_.low_mem = true;
    }
}

void MeteoModule::pollSensors()
{
    uint32_t now = millis();
    if (bme_ep_.addr) {
        if (int32_t(now - next_bme_ms_) >= 0) {
            if (bme_.trigger(now) == false)
                model_.onBmeError();
            next_bme_ms_ = now + 3000;
        }
        BmeState st = bme_.poll(now);
        if (st == BME_READY) {
            bool warming = model_.warmup_frac < 1.0f;
            model_.onBme(now, bme_.temperature, bme_.humidity, bme_.pressure, bme_.gas_ohm, bme_.gas_valid, settings_.t_off,
                         settings_.h_off, settings_.p_off, warming);
        } else if (st == BME_ERROR) {
            model_.onBmeError();
        }
    }
    if (scd_ep_.addr && scd_.poll(now))
        model_.onScd(now, scd_.co2, scd_.temperature, scd_.humidity, false);
    model_.refreshDerived(now, settings_, history_);
}

void MeteoModule::doIngest()
{
    uint32_t now = millis();
    // #region agent log
    {
        static uint32_t last = 0;
        if (now - last > 8000) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "{\"slp\":%.1f,\"p\":%.1f,\"t\":%.1f,\"pstale\":%d,\"pvalid\":%d,\"n\":%d,\"armed\":%d}", model_.slp,
                     model_.press.display, model_.temp.display, model_.press.stale ? 1 : 0, model_.press.valid ? 1 : 0,
                     history_.slpRing().count(), history_.slpRing().count() > 0 || !std::isnan(model_.slp) ? 1 : 0);
            meteoDbg("H-slpnan", "MeteoModule.cpp:doIngest", "ingest", buf);
            last = now;
        }
    }
    // #endregion
    if (model_.low_mem) {
        if (!std::isnan(model_.slp))
            history_.ingestSlp(now, model_.slp);
    } else {
        history_.ingest(now, model_.temp.display, model_.rh.display, model_.press.display, model_.co2.display,
                        model_.gas.display / 1000.0f, model_.slp, model_.iaq);
    }
    if (settings_.logging_requested) {
        char line[160];
        char iso[24] = "";
        clockIso(clock_, iso, sizeof(iso));
        snprintf(line, sizeof(line), "%s,%u,%.2f,%.1f,%.2f,%.2f,%.0f,%.0f,%.0f,%.2f,%d,%.2f,%.1f,OK", iso,
                 (unsigned)(now / 1000), model_.temp.display, model_.rh.display, model_.press.display, model_.slp,
                 model_.gas.display, model_.iaq, model_.eco2, model_.bvoc, int(model_.co2.display + 0.5f), model_.scd_temp.display,
                 model_.scd_rh.display);
        store_.maybeWriteCsv(clock_.unix_sec, clock_, settings_, line);
    }
}

void MeteoModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (state->frameState == IN_TRANSITION && state->transitionFrameRelationship == TransitionRelationship_INCOMING)
        frame_index_ = state->transitionFrameTarget;
    else
        frame_index_ = state->currentFrame;
    last_draw_ms_ = millis();
    updateClock();
    screen_.draw(display, x, y, settings_, model_, history_, store_.sdState(), clock_);
}

void MeteoModule::afterUiDisplay(OLEDDisplay *display)
{
    if (!display || !isFrameActive())
        return;
    if (screen_.page == PAGE_CHART)
        screen_.flushChartColors(display, settings_, history_);
#if defined(USE_ST7789)
    else
        static_cast<ST7789Spi *>(display)->setRGB(TFT_MESH, nullptr);
#endif
}

int MeteoModule::handleInputEvent(const InputEvent *event)
{
    if (!isFrameActive())
        return 0;

    bool in_settings = screen_.page == PAGE_SETTINGS || screen_.settings_active;
    // #region agent log
    char ibuf[96];
    snprintf(ibuf, sizeof(ibuf), "{\"ev\":%u,\"ch\":%u,\"page\":%d,\"set\":%d}", unsigned(event->inputEvent),
             unsigned(event->kbchar), screen_.page, in_settings ? 1 : 0);
    meteoDbg("H-setexit", "MeteoModule.cpp:handleInputEvent", "key", ibuf);
    // #endregion

    if (in_settings &&
        (event->inputEvent == INPUT_BROKER_SELECT || event->inputEvent == INPUT_BROKER_SELECT_LONG)) {
        screen_.handleChar('\r', settings_, store_);
        requestRedraw();
        return 1;
    }
    if (in_settings && (event->inputEvent == INPUT_BROKER_CANCEL || event->inputEvent == INPUT_BROKER_BACK)) {
        screen_.handleChar(27, settings_, store_);
        requestRedraw();
        return 1;
    }
    if (event->inputEvent == INPUT_BROKER_CANCEL || event->inputEvent == INPUT_BROKER_BACK)
        return 0;

    if (event->inputEvent == INPUT_BROKER_LEFT) {
        screen_.handleChar('k', settings_, store_);
        history_.setChartInterval(settings_.chart_s);
        requestRedraw();
        return 1;
    }
    if (event->inputEvent == INPUT_BROKER_RIGHT) {
        screen_.handleChar(';', settings_, store_);
        history_.setChartInterval(settings_.chart_s);
        requestRedraw();
        return 1;
    }
    if (event->inputEvent == INPUT_BROKER_UP) {
        screen_.handleChar('u', settings_, store_);
        requestRedraw();
        return 1;
    }
    if (event->inputEvent == INPUT_BROKER_DOWN) {
        screen_.handleChar('d', settings_, store_);
        requestRedraw();
        return 1;
    }
    if (event->inputEvent == INPUT_BROKER_MATRIXKEY || event->inputEvent == INPUT_BROKER_ANYKEY) {
        char c = char(event->kbchar);
        if (c >= 32 && c <= 126) {
            screen_.handleChar(c, settings_, store_);
            if (c == 'r' || c == 'R')
                rescan_requested_ = true;
            if (screen_.page != PAGE_SETTINGS)
                store_.saveSettings(settings_);
            else
                history_.setChartInterval(settings_.chart_s);
            requestRedraw();
            return 1;
        }
    }
    return 1;
}

} // namespace meteo
