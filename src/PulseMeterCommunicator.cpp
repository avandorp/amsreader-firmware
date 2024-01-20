#include "PulseMeterCommunicator.h"
#include "Uptime.h"

PulseMeterCommunicator::PulseMeterCommunicator(RemoteDebug* debugger) {
    this->debugger = debugger;
}

void PulseMeterCommunicator::configure(MeterConfig& meterConfig, Timezone* tz) {
    this->meterConfig = meterConfig;
    this->configChanged = false;
    this->tz = tz;
    setupGpio();
}

bool PulseMeterCommunicator::loop() {
    return updated || !initialized;
}

AmsData* PulseMeterCommunicator::getData(AmsData& meterState) {
    if(!initialized) {
        if(debugger->isActive(RemoteDebug::INFO)) debugger->printf_P(PSTR("Initializing pulse meter state\n"));
        state.apply(meterState);
        initialized = true;
        return NULL;
    }
    updated = false;

    AmsData* ret = new AmsData();
    ret->apply(state);
    if(debugger->isActive(RemoteDebug::VERBOSE)) debugger->printf_P(PSTR("Returning AMS data, list type: %d\n"), ret->getListType());
    return ret;
}

int PulseMeterCommunicator::getLastError() {
    return 0;
}

bool PulseMeterCommunicator::isConfigChanged() {
    return this->configChanged;
}

void PulseMeterCommunicator::getCurrentConfig(MeterConfig& meterConfig) {
    meterConfig = this->meterConfig;
}

void PulseMeterCommunicator::setupGpio() {
    if(debugger->isActive(RemoteDebug::DEBUG)) debugger->printf_P(PSTR("Setting up Pulse Meter GPIO, rx: %d, tx: %d\n"), meterConfig.rxPin, meterConfig.txPin);
    if(meterConfig.rxPin != NOT_A_PIN) {
        pinMode(meterConfig.rxPin, meterConfig.rxPinPullup ? INPUT_PULLUP : INPUT_PULLDOWN);
    }
    if(meterConfig.txPin != NOT_A_PIN) {
        pinMode(meterConfig.txPin, OUTPUT);
        digitalWrite(meterConfig.txPin, HIGH);
    }
}

void PulseMeterCommunicator::onPulse(uint8_t pulses) {
    uint64_t now = millis64();
    if(initialized && pulses == 0) {
        if(now - lastUpdate > 10000) {
            ImpulseAmsData update(state, meterConfig.baud, pulses);
            state.apply(update);
            updated = true;
            lastUpdate = now;
        }
        return;
    }
    if(debugger->isActive(RemoteDebug::VERBOSE)) debugger->printf_P(PSTR("PULSE\n"));
    if(!initialized) {
        if(debugger->isActive(RemoteDebug::WARNING)) debugger->printf_P(PSTR("Pulse communicator not initialized\n"));
        return;
    }

    ImpulseAmsData update(state, meterConfig.baud, pulses);
    state.apply(update);
    updated = true;
    lastUpdate = now;
}