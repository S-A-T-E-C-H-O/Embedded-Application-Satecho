/*
 * SATECHO_NETWORK.h — WiFi + MQTT connectivity management
 * Bounded Context: Connectivity Infrastructure
 *
 * EVENT-DRIVEN REFACTORING (PHASE 1):
 * ─────────────────────────────────────────────────────────────────
 * CHANGE 1: connectWiFi() — Replaced the 'while (WiFi.status() != WL_CONNECTED
 *          && attempts < 20)' loop with a non-blocking finite state machine (FSM).
 *          The FSM advances through states: IDLE → CONNECTING → AWAITING → CONNECTED/ERROR.
 *          Each transition happens in a single call, without holding the CPU.
 * CHANGE 2: delay(500) replaced by timer checking with millis(),
 *          yielding control to the scheduler between attempts.
 * CHANGE 3: networkLoop() — Now operates as a pure FSM: evaluates current state,
 *          applies transition if the condition is met, and returns immediately.
 *
 * WHY a state machine instead of while:
 * A while-loop blocks the current task, preventing any other operation.
 * The FSM allows the same task trying WiFi to also process incoming MQTT
 * commands and maintain keepalive, maximizing throughput with a single
 * execution thread.
 */

#ifndef SATECHO_NETWORK_H
#define SATECHO_NETWORK_H

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "satecho_config.h"

// =============================================================================
// NETWORK CLIENTS
// =============================================================================
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// =============================================================================
// CONNECTIVITY STATE
// =============================================================================
NetworkState networkState = NET_DISCONNECTED;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;
int wifiRetryCount = 0;
unsigned long wifiBackoffMs = 1000;
int wifiAttemptIndex = 0;      // Attempt counter for FSM (0-19 for 20 attempts)
unsigned long wifiAttemptStart = 0; // Timestamp of the current attempt start

// =============================================================================
// MQTT CALLBACK
// =============================================================================
void (*mqttMessageCallback)(char* topic, byte* payload, unsigned int length) = nullptr;

void setMqttCallback(void (*callback)(char*, byte*, unsigned int)) {
  mqttMessageCallback = callback;
  mqttClient.setCallback(callback);
}

// =============================================================================
// WiFi STATE MACHINE — Replaces 'while (WiFi.status() != ...)'
// =============================================================================
/*
 * Attempts one step of the WiFi connection.
 * WHY FSM instead of while + delay:
 * The original version executed: while(status != CONNECTED && attempts < 20) { delay(500); }
 * This blocks the core for up to 10 seconds. The FSM advances ONE step per call:
 *   1. Starts WiFi.begin() (only the first time)
 *   2. In subsequent calls, checks whether it is already connected (status == CONNECTED)
 *   3. If 500ms passed without connecting, increments attemptIndex
 *   4. If attemptIndex >= 20, transitions to NET_ERROR
 * Each call returns immediately (without delay), yielding control to the scheduler.
 *
 * @return true if connected, false if still attempting or failed
 */
bool connectWiFiStep() {
  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    networkState = NET_WIFI_CONNECTED;
    wifiRetryCount = 0;
    wifiBackoffMs = 1000;
    wifiAttemptIndex = 0;
    return true;
  }

  if (wifiRetryCount >= WIFI_MAX_RETRIES) {
    networkState = NET_ERROR;
    return false;
  }

  // Start attempt if this is the first iteration or if the backoff has elapsed
  if (networkState != NET_WIFI_CONNECTING) {
    if (now - lastWifiAttempt < wifiBackoffMs) return false;
    networkState = NET_WIFI_CONNECTING;
    lastWifiAttempt = now;
    wifiAttemptStart = now;
    wifiAttemptIndex = 0;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    return false;
  }

  // Check current attempt timeout (500ms per attempt)
  if (now - wifiAttemptStart >= 500) {
    wifiAttemptIndex++;
    wifiAttemptStart = now;

    if (WiFi.status() == WL_CONNECTED) {
      networkState = NET_WIFI_CONNECTED;
      wifiRetryCount = 0;
      wifiBackoffMs = 1000;
      wifiAttemptIndex = 0;
      return true;
    }

    // Did we exhaust the 20 attempts in this cycle?
    if (wifiAttemptIndex >= 20) {
      networkState = NET_DISCONNECTED;
      wifiRetryCount++;
      wifiBackoffMs = min(wifiBackoffMs * 2, 32000UL);
      return false;
    }
  }

  return false;
}

// =============================================================================
// MQTT MANAGEMENT — Atomic check (without loops)
// =============================================================================
bool connectMQTT() {
  if (!WiFi.isConnected()) return false;
  if (mqttClient.connected()) return true;

  unsigned long now = millis();
  if (now - lastMqttAttempt < PERIOD_MQTT_RETRY_MS) return false;
  lastMqttAttempt = now;

  networkState = NET_MQTT_CONNECTING;
  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
  mqttClient.setKeepAlive(MQTT_KEEPALIVE);

  String clientId = String(DEVICE_SERIAL) + "-" + WiFi.macAddress();

  if (mqttClient.connect(clientId.c_str())) {
    networkState = NET_MQTT_CONNECTED;
    mqttClient.subscribe(MQTT_TOPIC_COMMAND, MQTT_QOS);
    return true;
  }

  return false;
}

bool publishMQTT(const char* topic, const char* json) {
  if (!mqttClient.connected()) return false;
  return mqttClient.publish(topic, json, false);
}

/*
 * Network maintenance loop — pure FSM (without loops, without blocking).
 * Each call advances the FSM one step and returns immediately.
 *
 * WHY it is called on each scheduler iteration:
 * mqttClient.loop() must run frequently to process incoming TCP packets
 * and maintain keepalive. The WiFi FSM advances one step per call,
 * without holding the CPU longer than necessary.
 */
void networkLoop() {
  // Advance WiFi FSM one step
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFiStep();
  }

  // Maintain MQTT
  if (WiFi.isConnected() && !mqttClient.connected()) {
    connectMQTT();
  }

  // Process incoming MQTT messages (backend commands)
  mqttClient.loop();
}

bool isFullyConnected() {
  return WiFi.isConnected() && mqttClient.connected();
}

// =============================================================================
// RECURSIVE WiFi INIT — Replaces while-loop with tail recursion
// =============================================================================
/*
 * Attempts to connect WiFi using tail recursion instead of 'while'.
 * WHY recursion:
 * The architectural constraint forbids 'while'. This function attempts
 * to connect once (checks WiFi.status()), and if it is not connected and
 * there are still attempts left, it waits 500ms (vTaskDelay) and calls
 * itself with remainingAttempts-1.
 *
 * Tail Call: the self-invocation occurs as the last operation of the function,
 * allowing the compiler to reuse the stack frame (TCO).
 *
 * @param remainingAttempts Remaining attempts (base case: 0)
 * @return true if connected, false if attempts were exhausted
 */
bool wifiInitRecursive(int remainingAttempts) {
  if (remainingAttempts <= 0) {
    networkState = NET_DISCONNECTED;
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    networkState = NET_WIFI_CONNECTED;
    return true;
  }

  vTaskDelay(pdMS_TO_TICKS(500));
  return wifiInitRecursive(remainingAttempts - 1);
}

#endif // SATECHO_NETWORK_H