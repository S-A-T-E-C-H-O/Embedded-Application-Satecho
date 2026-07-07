/*
 * SATECHO_SECRETS.example.h — credential template.
 *
 * Copy this file to satecho_secrets.h (same directory) and fill in the real
 * values. satecho_secrets.h is git-ignored; NEVER commit real credentials.
 * satecho_config.h picks it up automatically via __has_include.
 */

#ifndef SATECHO_SECRETS_H
#define SATECHO_SECRETS_H

#define WIFI_SSID            "your-wifi-ssid"
#define WIFI_PASSWORD        "your-wifi-password"

// Must match MQTT_EDGE_API_KEY on the Edge and mqtt.edge.api-key in the backend.
#define MQTT_EDGE_API_KEY    "your-edge-shared-secret"

// One broker per environment — see docs/mqtt-contract.md at the ecosystem root.
#define MQTT_BROKER_HOST     "your-mqtt-broker-host"
#define MQTT_BROKER_PORT     1883

#endif // SATECHO_SECRETS_H
