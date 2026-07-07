/*
 * SATECHO_SENSORS.h — Drivers for the 4 physical sensors
 * Pins: FC-28(GPIO33), DHT11(GPIO26), DS18B20(GPIO25), HR202L(GPIO14+GPIO32)
 * 0% loops — recursive accumulateADC with OOB guard
 */
#ifndef SATECHO_SENSORS_H
#define SATECHO_SENSORS_H
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include "satecho_config.h"

struct SensorReading { MetricType type; float value; const char* unit; bool valid; };
OneWire oneWire(PIN_DS18B20);
DallasTemperature ds18b20(&oneWire);
DHT dht(PIN_DHT, DHT11);
DeviceAddress ds18b20Addr;
bool ds18b20Found = false;

// Recursive moving average with OOB guard — replaces for(;;)
long accumulateADC(int pin, int count, long sum) {
  if (count <= 0) return sum;
  if (pin < 0 || pin > 39) return sum;
  delayMicroseconds(500);
  return accumulateADC(pin, count - 1, sum + analogRead(pin));
}
float movingAverage(int pin, int samples) { return (float)accumulateADC(pin, samples, 0) / samples; }

void sensorsBegin() {
  dht.begin(); vTaskDelay(pdMS_TO_TICKS(1500));
  ds18b20.begin();
  ds18b20Found = ds18b20.getAddress(ds18b20Addr, 0);
  if (ds18b20Found) ds18b20.setResolution(ds18b20Addr, 12);
  pinMode(PIN_SOIL_MOISTURE, INPUT);
  pinMode(PIN_HR202L_DATA, INPUT);
  pinMode(PIN_HR202L_VCC, OUTPUT);
  digitalWrite(PIN_HR202L_VCC, LOW);
  pinMode(PIN_PIR, INPUT);
}

// FC-28 soil moisture (Act 1,2)
SensorReading readFC28() {
  SensorReading r; r.type = METRIC_HUMIDITY_FC28; r.unit = "%";
  float raw = movingAverage(PIN_SOIL_MOISTURE, 5);
  r.value = constrain(map(raw, 4095, 1400, 0, 100), 0, 100);
  r.valid = (raw > 50); return r;
}

// DHT11 ambient temperature (GPIO26, corrected from GPIO34)
SensorReading readDHT11() {
  SensorReading r; r.type = METRIC_AMBIENT_TEMP_DHT11; r.unit = "\u00B0C";
  float t = dht.readTemperature();
  r.value = isnan(t) ? -999.0f : t; r.valid = !isnan(t); return r;
}

// DS18B20 soil temperature
SensorReading readDS18B20() {
  SensorReading r; r.type = METRIC_SOIL_TEMP_DS18B20; r.unit = "\u00B0C";
  if (!ds18b20Found) { r.value = -999; r.valid = false; return r; }
  ds18b20.requestTemperatures();
  float t = ds18b20.getTempC(ds18b20Addr);
  r.value = (t == DEVICE_DISCONNECTED_C) ? -999.0f : t;
  r.valid = (t != DEVICE_DISCONNECTED_C); return r;
}

// HR202L with Power-Gating (Act 3)
SensorReading readHR202L() {
  SensorReading r; r.type = METRIC_SALINITY_HR202L; r.unit = "dS/m";
  digitalWrite(PIN_HR202L_VCC, HIGH);
  vTaskDelay(pdMS_TO_TICKS(50));
  float raw = movingAverage(PIN_HR202L_DATA, 5);
  digitalWrite(PIN_HR202L_VCC, LOW);
  float dsm = map(raw, 2800, 400, 0, 1000);
  r.value = constrain(dsm / 100.0f, 0.0f, 10.0f);
  r.valid = (raw > 20); return r;
}

bool readPIR() { return digitalRead(PIN_PIR) == HIGH; }
#endif