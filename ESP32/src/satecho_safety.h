/*
 * SATECHO_SAFETY.h — Watchdog + threshold alerts, 0% loops (recursive)
 */
#ifndef SATECHO_SAFETY_H
#define SATECHO_SAFETY_H
#include <Arduino.h>
#include "satecho_config.h"
#include "satecho_sensors.h"

DeviceHealth systemHealth = HEALTH_HEALTHY;
int failedReadings = 0;
unsigned long lastPIRTime = 0;

bool pirDebounce() {
  unsigned long now = xTaskGetTickCount() * portTICK_PERIOD_MS;
  if (now - lastPIRTime < PIR_DEBOUNCE_MS) return false;
  lastPIRTime = now; return true;
}

// Evaluates ONE reading against business thresholds (without for)
int evaluateOneReading(const SensorReading& r) {
  if (!r.valid) return 0;
  if (r.type == METRIC_HUMIDITY_FC28 && r.value < 20.0f) return 1;
  if (r.type == METRIC_SALINITY_HR202L && r.value > 5.0f) return 2;
  return 0;
}

int checkThresholdsRecursive(SensorReading r[], int idx, int accum) {
  if (idx < 0) return accum;
  return checkThresholdsRecursive(r, idx-1, accum | evaluateOneReading(r[idx]));
}
int checkAllThresholds(SensorReading r[], int count) { return checkThresholdsRecursive(r, count-1, 0); }

DeviceHealth updateHealth(int failed) {
  if (failed == 0) { failedReadings = 0; return systemHealth = HEALTH_HEALTHY; }
  failedReadings += failed;
  if (failedReadings >= 6) return systemHealth = HEALTH_CRITICAL;
  if (failedReadings >= 3) return systemHealth = HEALTH_DEGRADED;
  return systemHealth;
}
#endif