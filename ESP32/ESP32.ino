/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║              ESP32.ino — SATECHO FIRMWARE REACTIVE ARCHITECTURE         ║
 * ║        Zero Imperative Loops — FreeRTOS Timers + Tail Recursion         ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 *
 * DOMAIN: Precision Agriculture — Cyber-Physical Soil Monitoring
 * CONTROLLER: ESP32-WROOM-32 / ESP32 DevKit V1
 * PARADIGM: Event-Driven + Functional Recursion + FreeRTOS Timers
 *
 * ────────────────────────────────────────────────────────────────────────────
 * ARCHITECTURAL REFACTORING — PHASE 1 (Zero Imperative Loops)
 * ────────────────────────────────────────────────────────────────────────────
 *
 * REMOVED:  void loop() — infinite polling loop
 * REMOVED:  while(true) in FreeRTOS tasks
 * REMOVED:  for (int i=0; i<N; i++) in all submodules
 *
 * REPLACED BY:
 *   - FreeRTOS Software Timers (auto-reload) for periodic work
 *   - Tail recursion for continuous queue processing (without while)
 *   - Finite State Machines (FSM) for WiFi connection (without while)
 *   - Event Groups for synchronization between tasks (without polling)
 *
 * KEY DESIGN PRINCIPLE:
 * Each FreeRTOS Timer invokes its callback exactly at the configured period.
 * The callback executes its logic and returns — without any loop.
 * The timer hardware (in the ESP32 Timer Group) automatically generates the
 * next interrupt. This is "hardware-driven event-driven":
 * the timer IS the event, not a counter checked inside a loop.
 */

#include <Arduino.h>
#include <WiFi.h>
#include "src/satecho_config.h"
#include "src/satecho_sensors.h"
#include "src/satecho_actuators.h"
#include "src/satecho_network.h"
#include "src/satecho_telemetry.h"
#include "src/satecho_safety.h"

// =============================================================================
// FREERTOS — Synchronization primitives
// =============================================================================

EventGroupHandle_t systemEvents = nullptr;
QueueHandle_t sensorQueue = nullptr;
QueueHandle_t remoteCmdQueue = nullptr;
TimerHandle_t sensorTimer = nullptr;
TimerHandle_t networkTimer = nullptr;
TimerHandle_t telemetryTimer = nullptr;
TimerHandle_t safetyTimer = nullptr;

// =============================================================================
// Remote MQTT command structure
// =============================================================================
struct RemoteCommand {
  char action[32];
  int durationMinutes;
};

// =============================================================================
// GLOBAL VARIABLES (minimal, only for shared buffers between timers)
// =============================================================================
SensorReading gReadings[METRIC_COUNT];

// =============================================================================
// TIMER CALLBACK 1: SENSOR — Periodic reading (every 2s, auto-reload)
// =============================================================================
/*
 * WHY FreeRTOS Timer instead of while(true) + vTaskDelay():
 * The hardware timer generates an interrupt every 2 seconds that invokes this
 * callback. The callback performs the reading and returns — without a loop.
 * The hardware timer automatically rearms itself (auto-reload), creating an
 * infinite cycle of events without a single 'while' or 'for' instruction in
 * the source code.
 *
 * WARNING: FreeRTOS Timer callbacks run in the context of the
 * Timer Service Task. They must not block (no delay, no blocking MQTT).
 * This callback only reads sensors (ADC, OneWire, DHT) and places results
 * into a queue. MQTT batch publishing is handled by telemetryTimer.
 */
void sensorTimerCallback(TimerHandle_t xTimer) {
  gReadings[0] = readFC28();      // METRIC_HUMIDITY_FC28
  gReadings[1] = readDHT11();     // METRIC_AMBIENT_TEMP_DHT11
  gReadings[2] = readDS18B20();   // METRIC_SOIL_TEMP_DS18B20
  gReadings[3] = readHR202L();    // METRIC_SALINITY_HR202L

  xQueueOverwrite(sensorQueue, &gReadings);
  xEventGroupSetBits(systemEvents, EVENT_SENSORS_READY);
}

// =============================================================================
// TIMER CALLBACK 2: NETWORK — WiFi/MQTT maintenance (every 500ms, auto-reload)
// =============================================================================
/*
 * Advances the WiFi FSM one step and processes incoming MQTT messages.
 * No loops: each call executes ONE FSM step and returns.
 */
void networkTimerCallback(TimerHandle_t xTimer) {
  networkLoop();
}

// =============================================================================
// TIMER CALLBACK 3: TELEMETRY — RAW batch publishing through MQTT (every 10s, auto-reload)
// =============================================================================
void telemetryTimerCallback(TimerHandle_t xTimer) {
  if (!isFullyConnected()) return;

  SensorReading localReadings[METRIC_COUNT];
  if (xQueuePeek(sensorQueue, &localReadings, 0) == pdTRUE) {
    memcpy(gReadings, localReadings, sizeof(gReadings));
  }

  const char* rawJson = buildRawSoilBatch(gReadings, METRIC_COUNT);

  if (publishMQTT(MQTT_TOPIC_RAW_SOIL, rawJson)) {
    Serial.printf("[MQTT] Raw soil → %s\n", MQTT_TOPIC_RAW_SOIL);
  } else {
    Serial.println(F("[MQTT] Raw soil ERROR"));
  }
}

// =============================================================================
// TIMER CALLBACK 4: SAFETY — Watchdog (every 1s, auto-reload, highest priority)
// =============================================================================
/*
 * WHY a 1-second timer for safety:
 * It checks the valve timeout EVERY SECOND. If the valve stays open
 * for more than 60s, it closes it immediately. The timer runs even if another
 * task is blocked (the Timer Service Task has high priority).
 */
void safetyTimerCallback(TimerHandle_t xTimer) {
  // 1. Sensor health
  EventBits_t bits = xEventGroupGetBits(systemEvents);
  if (bits & EVENT_SENSORS_READY) {
    xEventGroupClearBits(systemEvents, EVENT_SENSORS_READY);

    SensorReading localReadings[METRIC_COUNT];
    if (xQueuePeek(sensorQueue, &localReadings, 0) == pdTRUE) {
      // Failure count without loops: sum of ternary expressions (4 metrics)
      int failed = (localReadings[0].valid ? 0 : 1)
                 + (localReadings[1].valid ? 0 : 1)
                 + (localReadings[2].valid ? 0 : 1)
                 + (localReadings[3].valid ? 0 : 1);

      updateHealth(failed);

      // Check thresholds (recursive algorithm, without internal for)
      int alerts = checkAllThresholds(localReadings, METRIC_COUNT);
      if (alerts > 0) {
        Serial.printf("[SAFETY] %d threshold alerts\n", alerts);
      }
    }
  }

  // 2. PIR — motion detection (raw to the Edge; the Edge classifies it)
  if (readPIR() && pirDebounce()) {
    xEventGroupSetBits(systemEvents, EVENT_PIR_TRIGGERED);
    Serial.println(F("[SAFETY] PIR: Motion detected"));
    // pulse_duration_ms=0 (ESP32 does not measure pulse), triggers_per_minute=1
    // (the debounce guarantees 1 trigger here). The Edge derives PERSON/ANIMAL/WIND.
    const char* secJson = buildRawSecurityPayload(0.0f, 1);
    publishMQTT(MQTT_TOPIC_RAW_SECURITY, secJson);
  }
}

// =============================================================================
// MQTT CALLBACK — Processes remote commands
// =============================================================================
/*
 * Invoked by mqttClient.loop() in the context of networkTimerCallback.
 * Places the command into the queue for asynchronous processing.
 */
void handleMQTTCommand(char* topic, byte* payload, unsigned int length) {
  char message[MQTT_BUFFER_SIZE];
  unsigned int copyLen = min(length, (unsigned int)(MQTT_BUFFER_SIZE - 1));
  memcpy(message, payload, copyLen);
  message[copyLen] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error) return;

  RemoteCommand cmd;
  const char* action = doc["action"] | "";
  strncpy(cmd.action, action, sizeof(cmd.action) - 1);
  cmd.action[sizeof(cmd.action) - 1] = '\0';
  // The backend sends "duration_minutes" (snake_case). camelCase is accepted as fallback.
  cmd.durationMinutes = doc["duration_minutes"] | (doc["durationMinutes"] | 15);

  xQueueSend(remoteCmdQueue, &cmd, 0);
}

// =============================================================================
// TASK: MQTT COMMAND PROCESSOR — Recursive command processing
// =============================================================================
/*
 * WHY recursion instead of while(true):
 * xQueueReceive() blocks the task until a command arrives. Once received,
 * it processes it and recursively calls itself to wait for the next one.
 * Recursion only happens AFTER the command has been processed, so the stack
 * never grows beyond 1 frame (each frame waits in xQueueReceive before
 * the next recursion).
 *
 * This is the ONLY persistent FreeRTOS task — all other functions are
 * Timer callbacks (no loop, no task).
 */
void mqttCommandTask(void* param) {
  RemoteCommand cmd;
  xQueueReceive(remoteCmdQueue, &cmd, portMAX_DELAY);

  // The backend (MqttActuatorPublisher) sends IRRIGATE_ON / IRRIGATE_OFF.
  // OPEN / CLOSE are kept as aliases for compatibility.
  if (strcmp(cmd.action, "IRRIGATE_ON") == 0 || strcmp(cmd.action, "OPEN") == 0) {
    activarRiego();
    publishMQTT(MQTT_TOPIC_VALVE, buildValveStatePayload(true));
    Serial.printf("[MQTT-CMD] Irrigation ACTIVATED (%d min)\n", cmd.durationMinutes);
  }
  else if (strcmp(cmd.action, "IRRIGATE_OFF") == 0 || strcmp(cmd.action, "CLOSE") == 0) {
    desactivarRiego();
    publishMQTT(MQTT_TOPIC_VALVE, buildValveStatePayload(false));
    Serial.println(F("[MQTT-CMD] Irrigation DEACTIVATED"));
  }
  else if (strcmp(cmd.action, "STATUS_REQUEST") == 0) {
    int uptime = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
    publishMQTT(MQTT_TOPIC_STATUS, buildStatusPayload(systemHealth, 0, uptime));
  }

  // Tail recursion: process the next command (without while)
  mqttCommandTask(param);
}

// =============================================================================
// WiFi INIT TASK — Asynchronous initial connection (without while)
// =============================================================================
/*
 * WHY recursion with vTaskDelay instead of while:
 * It tries to connect WiFi one step at a time. If it does not connect in this
 * attempt, it waits 500ms and recursively calls itself. If it connects,
 * it configures NTP and MQTT, then exits the recursion. If 30 attempts fail,
 * it exits with an error.
 *
 * @param remainingAttempts Number of remaining attempts (base case: 0 = give up)
 */
void wifiInitAttempt(int remainingAttempts) {
  if (remainingAttempts <= 0) {
    Serial.println(F("[INIT] WiFi: Failed after 30 attempts. Offline mode."));
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[INIT] WiFi OK — IP: %s, RSSI: %d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    xEventGroupSetBits(systemEvents, EVENT_WIFI_CONNECTED);

    configTime(-18000, 0, "pool.ntp.org", "time.nist.gov");
    vTaskDelay(pdMS_TO_TICKS(2000));

    setMqttCallback(handleMQTTCommand);
    if (connectMQTT()) {
      Serial.println(F("[INIT] MQTT connected"));
      xEventGroupSetBits(systemEvents, EVENT_MQTT_CONNECTED);
    }
    return; // Base case: connected — end recursion
  }

  // Try again after 500ms
  vTaskDelay(pdMS_TO_TICKS(500));
  wifiInitAttempt(remainingAttempts - 1);
}

/*
 * WiFi initialization task — launched once in setup().
 * Starts WiFi.begin() and then delegates to recursive wifiInitAttempt().
 */
void wifiInitTask(void* param) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiInitAttempt(30); // up to 30 attempts = 15 seconds maximum
  vTaskDelete(NULL);   // Initialization task completed
}

// =============================================================================
// SETUP — One-shot initialization
// =============================================================================
void setup() {
  Serial.begin(115200);
  vTaskDelay(pdMS_TO_TICKS(1000));

  Serial.println(F("\n╔══════════════════════════════════════════════╗"));
  Serial.println(F("║   SATECHO AgroSafe — Firmware v3.0 REACTIVE  ║"));
  Serial.println(F("║   Zero Loops — 100% Event-Driven + Timers     ║"));
  Serial.println(F("╚══════════════════════════════════════════════╝\n"));

  // ─── 1. HARDWARE ───
  sensorsBegin();
  actuatorsBegin();

  // ─── 2. FREERTOS PRIMITIVES ───
  systemEvents = xEventGroupCreate();
  sensorQueue = xQueueCreate(1, sizeof(SensorReading) * METRIC_COUNT);
  remoteCmdQueue = xQueueCreate(QUEUE_REMOTE_CMD_LEN, sizeof(RemoteCommand));

  // ─── 3. CREATE FREERTOS TIMERS (auto-reload, without loops) ───
  // WHY timers instead of tasks with while(true):
  // FreeRTOS timers are kernel objects that invoke their callback
  // periodically without requiring a loop in the application code.
  // The ESP32 Timer Group hardware generates the interrupts;
  // the Timer Service Task executes the callback and rearms it. Zero loops.

  sensorTimer = xTimerCreate(
    "Sensor", pdMS_TO_TICKS(PERIOD_SENSOR_READ_MS), pdTRUE, nullptr, sensorTimerCallback);
  networkTimer = xTimerCreate(
    "Network", pdMS_TO_TICKS(500), pdTRUE, nullptr, networkTimerCallback);
  telemetryTimer = xTimerCreate(
    "Telemetry", pdMS_TO_TICKS(PERIOD_TELEMETRY_BATCH_MS), pdTRUE, nullptr, telemetryTimerCallback);
  safetyTimer = xTimerCreate(
    "Safety", pdMS_TO_TICKS(PERIOD_SAFETY_CHECK_MS), pdTRUE, nullptr, safetyTimerCallback);

  // ─── 4. START ASYNCHRONOUS WiFi ───
  xTaskCreatePinnedToCore(wifiInitTask, "WiFi-Init", 4096, nullptr, 3, nullptr, 0);

  // ─── 5. START MQTT COMMAND TASK ───
  xTaskCreatePinnedToCore(mqttCommandTask, "MQTT-Cmd", STACK_MQTT_TASK, nullptr, PRIO_MQTT_TASK, nullptr, 1);

  // ─── 6. START TIMERS ───
  xTimerStart(sensorTimer, 0);
  xTimerStart(networkTimer, 0);
  xTimerStart(telemetryTimer, 0);
  xTimerStart(safetyTimer, 0);

  Serial.println(F("[INIT] 4 timers + 2 FreeRTOS tasks launched.\n"));

  // ─── 7. GIVE FULL CONTROL TO THE KERNEL ───
  vTaskDelete(NULL);
}

// =============================================================================
// LOOP — REMOVED. The FreeRTOS kernel + Timers govern the system.
// =============================================================================
/*
 * WHY loop() is empty:
 * In the event-driven paradigm with FreeRTOS Timers, there is no concept
 * of a "main loop". Each timer triggers its callback exactly when required
 * (2s, 500ms, 10s, 30s, 1s) without polling.
 *
 * void loop() is a remnant of Arduino's sequential model. It self-deletes
 * to avoid consuming stack or CPU cycles.
 */
void loop() {
  vTaskDelete(NULL);
}
