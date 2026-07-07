/*
 * SATECHO_TELEMETRY.h — JSON serialization and batch aggregation
 * Bounded Context: Data Ingestion Pipeline
 */

#ifndef SATECHO_TELEMETRY_H
#define SATECHO_TELEMETRY_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "satecho_config.h"
#include "satecho_sensors.h"

char jsonBuffer[JSON_BUFFER_SIZE];

// =============================================================================
// HELPER: RAW field name expected by the Edge for each MetricType.
// The Edge (device_ingest_subscriiber.py::_METRIC_MAP) translates:
//   humidity_fc28      -> moisture   (SOIL_MOISTURE in the backend)
//   salinity_hr202l    -> ec         (ELECTRICAL_CONDUCTIVITY)
//   soil_temp_ds18b20  -> temperature (SOIL_TEMPERATURE)
//   ambient_temp_dht11 -> ambient_temperature
// pH and HUMIDITY have no mapping in the Edge -> nullptr is returned and they are omitted.
// =============================================================================
const char* edgeMetricKey(MetricType t) {
  switch (t) {
    case METRIC_HUMIDITY_FC28:      return "humidity_fc28";
    case METRIC_AMBIENT_TEMP_DHT11: return "ambient_temp_dht11";
    case METRIC_SOIL_TEMP_DS18B20:  return "soil_temp_ds18b20";
    case METRIC_SALINITY_HR202L:    return "salinity_hr202l";
    default:                        return nullptr;
  }
}

// =============================================================================
// RAW UNIT SERIALIZATION — One reading into the format consumed by the Edge:
//   { "metricType": "humidity_fc28", "value": 45.2, "timestamp": "...Z" }
// =============================================================================
void serializeOneReading(const SensorReading& reading, JsonObject& obj) {
  obj["metricType"] = edgeMetricKey(reading.type);
  obj["value"]      = reading.value;

  char tsBuf[30];
  time_t now = time(nullptr);
  struct tm* utc = gmtime(&now);
  snprintf(tsBuf, sizeof(tsBuf), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
           utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
           utc->tm_hour, utc->tm_min, utc->tm_sec);
  obj["timestamp"] = tsBuf;
}

// =============================================================================
// RECURSIVE BATCH SERIALIZATION — Raw array for the Edge.
// Only valid readings with a field the Edge knows how to translate (edgeMetricKey).
// The Edge injects api_key + zone_id and republishes flat format to the backend.
// =============================================================================
void serializeBatchRecursive(SensorReading readings[], int idx, JsonDocument& doc) {
  if (idx < 0) {
    serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    return;
  }

  if (readings[idx].valid && edgeMetricKey(readings[idx].type) != nullptr) {
    JsonObject obj = doc.add<JsonObject>();
    serializeOneReading(readings[idx], obj);
  }

  serializeBatchRecursive(readings, idx - 1, doc);
}

// Builds the raw soil payload (JSON array) for MQTT_TOPIC_RAW_SOIL.
const char* buildRawSoilBatch(SensorReading readings[], int count) {
  JsonDocument doc;
  doc.to<JsonArray>();  // forces array even if there are no valid readings
  serializeBatchRecursive(readings, count - 1, doc);
  return jsonBuffer;
}

// =============================================================================
// RAW SECURITY EVENT JSON — raw format consumed by the Edge:
//   { pulse_duration_ms, triggers_per_minute, detectedAt }
// The ESP32 does NOT classify: it sends physical signals and the Edge
// (PirClassificationService, EP-003-TS001) derives PERSON/ANIMAL/WIND,
// injects api_key + zone_id, and republishes flat format to the backend.
// The ESP32 does not measure a real pulse — pulse_duration_ms=0; debounce guarantees
// triggers_per_minute=1 per event.
// =============================================================================
const char* buildRawSecurityPayload(
    float pulseDurationMs,
    int triggersPerMinute) {
  JsonDocument doc;
  doc["pulse_duration_ms"]    = pulseDurationMs;
  doc["triggers_per_minute"]  = triggersPerMinute;

  char tsBuf[30];
  time_t now = time(nullptr);
  struct tm* utc = gmtime(&now);
  snprintf(tsBuf, sizeof(tsBuf), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
           utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
           utc->tm_hour, utc->tm_min, utc->tm_sec);
  doc["detectedAt"] = tsBuf;

  serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
  return jsonBuffer;
}

// =============================================================================
// VALVE STATE JSON
// =============================================================================
const char* buildValveStatePayload(bool isOpen) {
  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID_NUM;
  doc["state"]    = isOpen ? "OPEN" : "CLOSED";

  char tsBuf[30];
  time_t now = time(nullptr);
  struct tm* utc = gmtime(&now);
  snprintf(tsBuf, sizeof(tsBuf), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
           utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
           utc->tm_hour, utc->tm_min, utc->tm_sec);
  doc["timestamp"] = tsBuf;

  serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
  return jsonBuffer;
}

// =============================================================================
// DEVICE STATUS JSON
// =============================================================================
const char* buildStatusPayload(DeviceHealth health, int failedSensors, int uptimeSeconds) {
  JsonDocument doc;
  doc["deviceId"]      = DEVICE_ID_NUM;
  doc["health"]        = (health == HEALTH_HEALTHY)  ? "HEALTHY" :
                         (health == HEALTH_DEGRADED) ? "DEGRADED" : "CRITICAL";
  doc["failedSensors"] = failedSensors;
  doc["uptimeSeconds"] = uptimeSeconds;
  doc["wifiRSSI"]      = WiFi.RSSI();
  doc["freeHeap"]      = ESP.getFreeHeap();

  char tsBuf[30];
  time_t now = time(nullptr);
  struct tm* utc = gmtime(&now);
  snprintf(tsBuf, sizeof(tsBuf), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
           utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
           utc->tm_hour, utc->tm_min, utc->tm_sec);
  doc["timestamp"] = tsBuf;

  serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
  return jsonBuffer;
}

#endif // SATECHO_TELEMETRY_H