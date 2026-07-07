/*
 * SATECHO_CONFIG.h — Pin map and calibration constants
 * Bounded Context: CyberPhysical Domain (IoT Hardware Layer)
 */

#ifndef SATECHO_CONFIG_H
#define SATECHO_CONFIG_H

#include <Arduino.h>

// =============================================================================
// DEVICE IDENTITY
// =============================================================================
#define DEVICE_SERIAL       "ESP32-SATECHO-001"   // for MQTT client-id only
#define DEVICE_TYPE         "SOIL_SENSOR"

// Numeric IDs — must match the ones registered in the backend IoT BC
#define FARM_ID_STR         "1"
#define DEVICE_ID_STR       "5"
#define DEVICE_ID_NUM       5

// =============================================================================
// GPIO PIN MAP
// =============================================================================
#define PIN_PIR             27   // HC-SR501 PIR — interrupt-capable (RISING edge)
#define PIN_SOIL_MOISTURE   33   // YL-69 (FC-28) — ADC1, WiFi-safe
#define PIN_DS18B20         25   // DS18B20 — OneWire (4.7k pull-up to 3.3V)
#define PIN_DHT             26   // DHT11 — safe GPIO with OUTPUT/INPUT
#define PIN_HR202L_DATA     32   // HR202L analog output — ADC1, WiFi-safe
#define PIN_HR202L_VCC      14   // HR202L power-gating — HIGH=powers the sensor
#define PIN_LED_RIEGO        2   // Onboard blue LED — irrigation actuator (Act 2)

// =============================================================================
// SENSOR CALIBRATION
// =============================================================================
#define SOIL_DRY_ADC        4095
#define SOIL_WET_ADC        1400
#define HR202L_DRY_ADC      3000
#define HR202L_WET_ADC      500
#define PH_ACID_ADC         1800
#define PH_ACID_VALUE       4.01f
#define PH_NEUTRAL_ADC      2100
#define PH_NEUTRAL_VALUE    7.00f

// =============================================================================
// BUSINESS THRESHOLDS (ODS-aligned)
// =============================================================================
#define THRESHOLD_SOIL_MOISTURE_LOW   30.0f
#define THRESHOLD_TEMP_CRITICAL       40.0f
#define THRESHOLD_PH_LOW              5.5f
#define THRESHOLD_PH_HIGH             8.0f

// =============================================================================
// FREERTOS TIMING
// =============================================================================
#define pdMS_TO_TICKS(ms)  ((TickType_t)(((uint32_t)(ms) * configTICK_RATE_HZ) / 1000))

#define PERIOD_SENSOR_READ_MS      2000
#define PERIOD_TELEMETRY_BATCH_MS  10000
#define PERIOD_STATUS_REPORT_MS    60000
#define PERIOD_WIFI_RETRY_MS       5000
#define PERIOD_MQTT_RETRY_MS       3000
#define PERIOD_SAFETY_CHECK_MS     1000

// =============================================================================
// FREERTOS STACK SIZES AND PRIORITIES
// =============================================================================
#define STACK_SENSOR_TASK       4096
#define STACK_NETWORK_TASK      6144
#define STACK_MQTT_TASK         4096
#define STACK_TELEMETRY_TASK    4096
#define STACK_SAFETY_TASK       2048
#define STACK_LED_TASK          1536

#define PRIO_SAFETY_TASK        5
#define PRIO_NETWORK_TASK       4
#define PRIO_MQTT_TASK          3
#define PRIO_SENSOR_TASK        2
#define PRIO_TELEMETRY_TASK     2
#define PRIO_LED_TASK           1

// =============================================================================
// SAFETY / WATCHDOG
// =============================================================================
#define VALVE_AUTOCLOSE_MS      60000
#define PIR_DEBOUNCE_MS         5000
#define SENSOR_MAX_INVALID      3
#define WIFI_MAX_RETRIES        5

// =============================================================================
// NETWORK
// =============================================================================
#define WIFI_SSID            "SATECHO_Field"
#define WIFI_PASSWORD        "AgroSafe2026!"

// The ESP32 communicates ONLY through MQTT with the Edge (no direct REST/JWT to the backend).
// The Edge validates/injects this shared secret; it must match mqtt.edge.api-key in the backend.
#define MQTT_EDGE_API_KEY    "edge-shared-secret-change-me"

// MQTT broker (same IP as the backend in the prototype)
#define MQTT_BROKER_HOST     "satecho-mqtt-demo-cientifica.eastus.azurecontainer.io"
#define MQTT_BROKER_PORT     1883
#define MQTT_KEEPALIVE       60
#define MQTT_QOS             1

// Outgoing topics to the backend (valve/status) — numeric IDs, Long.parseLong()
#define MQTT_TOPIC_VALVE     "agrosafe/" FARM_ID_STR "/devices/" DEVICE_ID_STR "/valve/state"
#define MQTT_TOPIC_STATUS    "agrosafe/" FARM_ID_STR "/devices/" DEVICE_ID_STR "/status"
// Incoming command topic from the backend (via broker; edge buffers if offline)
#define MQTT_TOPIC_COMMAND   "agrosafe/" FARM_ID_STR "/devices/" DEVICE_ID_STR "/actuator/command"

// -----------------------------------------------------------------------------
// RAW topics — the ESP32 publishes raw format through MQTT; the Edge consumes it,
// translates field names, classifies the PIR, injects api_key + zone_id, and
// republishes in flat format to agrosafe/{farm}/devices/{device}/... for the backend.
// Exact field contract in edge/shared/infrastructure/device_ingest_subscriiber.py
// -----------------------------------------------------------------------------
#define MQTT_TOPIC_RAW_SOIL      "agrosafe/raw/" FARM_ID_STR "/" DEVICE_ID_STR "/soil/reading"
#define MQTT_TOPIC_RAW_SECURITY  "agrosafe/raw/" FARM_ID_STR "/" DEVICE_ID_STR "/security/event"

// =============================================================================
// BUFFERS
// =============================================================================
#define JSON_BUFFER_SIZE      1024
#define MQTT_BUFFER_SIZE      512
#define SENSOR_SAMPLES_AVG    5

// =============================================================================
// DOMAIN ENUMERATIONS
// =============================================================================
// Names aligned 1:1 with the keys expected by the Edge (_METRIC_MAP):
//   FC28->humidity_fc28, DHT11->ambient_temp_dht11,
//   DS18B20->soil_temp_ds18b20, HR202L->salinity_hr202l
enum MetricType {
  METRIC_HUMIDITY_FC28 = 0,     // FC-28 soil moisture
  METRIC_AMBIENT_TEMP_DHT11,    // DHT11 ambient temperature
  METRIC_SOIL_TEMP_DS18B20,     // DS18B20 soil temperature
  METRIC_SALINITY_HR202L,       // HR202L salinity / EC
  METRIC_COUNT
};

enum DeviceHealth {
  HEALTH_HEALTHY = 0,
  HEALTH_DEGRADED,
  HEALTH_CRITICAL
};

enum NetworkState {
  NET_DISCONNECTED = 0,
  NET_WIFI_CONNECTING,
  NET_WIFI_CONNECTED,
  NET_MQTT_CONNECTING,
  NET_MQTT_CONNECTED,
  NET_ERROR
};

// =============================================================================
// FREERTOS QUEUES
// =============================================================================
#define QUEUE_SENSOR_READINGS_LEN 10
#define QUEUE_ACTUATOR_CMD_LEN    5
#define QUEUE_REMOTE_CMD_LEN      5

// Event Group bits
#define EVENT_SENSORS_READY     (1 << 0)
#define EVENT_WIFI_CONNECTED    (1 << 1)
#define EVENT_MQTT_CONNECTED    (1 << 2)
#define EVENT_PIR_TRIGGERED     (1 << 3)
#define EVENT_VALVE_TIMEOUT     (1 << 4)
#define EVENT_SENSOR_DEGRADED   (1 << 5)

#endif // SATECHO_CONFIG_H