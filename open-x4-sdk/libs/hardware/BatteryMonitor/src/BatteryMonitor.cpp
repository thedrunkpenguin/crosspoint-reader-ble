#include "BatteryMonitor.h"
#include <esp_idf_version.h>
#include <Arduino.h>
#include <algorithm>
#include <cmath>
#if ESP_IDF_VERSION_MAJOR < 5
#include <esp_adc_cal.h>
#endif

namespace {
constexpr uint8_t batterySampleCount = 8;
constexpr unsigned long percentageRefreshIntervalMs = 5000;
constexpr uint16_t percentageHysteresis = 1;
constexpr uint16_t maxStepPerRefresh = 2;
}

BatteryMonitor::BatteryMonitor(uint8_t adcPin, float dividerMultiplier)
  : _adcPin(adcPin), _dividerMultiplier(dividerMultiplier)
{
}

uint16_t BatteryMonitor::readPercentage() const
{
    const unsigned long now = millis();
    if (_hasCachedPercentage && (now - _lastPercentageSampleMs) < percentageRefreshIntervalMs) {
        return _cachedPercentage;
    }

    const uint16_t measuredPercentage = percentageFromMillivolts(readMillivolts());
    if (!_hasCachedPercentage) {
        _cachedPercentage = measuredPercentage;
        _hasCachedPercentage = true;
        _lastPercentageSampleMs = now;
        return _cachedPercentage;
    }

    const int delta = static_cast<int>(measuredPercentage) - static_cast<int>(_cachedPercentage);
    if (std::abs(delta) <= percentageHysteresis) {
        _lastPercentageSampleMs = now;
        return _cachedPercentage;
    }

    if (delta > 0) {
        const uint16_t step = static_cast<uint16_t>(std::min<int>(delta, maxStepPerRefresh));
        _cachedPercentage = static_cast<uint16_t>(_cachedPercentage + step);
    } else {
        const uint16_t step = static_cast<uint16_t>(std::min<int>(-delta, maxStepPerRefresh));
        _cachedPercentage = static_cast<uint16_t>(_cachedPercentage - step);
    }

    _lastPercentageSampleMs = now;
    return _cachedPercentage;
}

uint16_t BatteryMonitor::readMillivolts() const
{
    return sampleMillivoltsAveraged();
}

uint16_t BatteryMonitor::sampleMillivoltsAveraged() const
{
#if ESP_IDF_VERSION_MAJOR < 5
    // ESP-IDF 4.x doesn't have analogReadMilliVolts, so we need to do the calibration manually
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    uint32_t rawSum = 0;
    for (uint8_t i = 0; i < batterySampleCount; ++i) {
        rawSum += analogRead(_adcPin);
    }
    const uint32_t avgRaw = rawSum / batterySampleCount;
    const uint16_t mv = esp_adc_cal_raw_to_voltage(static_cast<uint32_t>(avgRaw), &adc_chars);
#else
    // ESP-IDF 5.x has analogReadMilliVolts
    uint32_t mvSum = 0;
    for (uint8_t i = 0; i < batterySampleCount; ++i) {
        mvSum += analogReadMilliVolts(_adcPin);
    }
    const uint16_t mv = static_cast<uint16_t>(mvSum / batterySampleCount);
#endif

    return static_cast<uint16_t>(mv * _dividerMultiplier);
}

double BatteryMonitor::readVolts() const
{
    return static_cast<double>(readMillivolts()) / 1000.0;
}

uint16_t BatteryMonitor::percentageFromMillivolts(uint16_t millivolts)
{
    double volts = millivolts / 1000.0;
    // Polynomial derived from LiPo samples
    double y = -144.9390 * volts * volts * volts +
               1655.8629 * volts * volts -
               6158.8520 * volts +
               7501.3202;

    // Clamp to [0,100] and round
    y = std::max(y, 0.0);
    y = std::min(y, 100.0);
    y = round(y);
    return y;
}
