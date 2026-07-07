/*
 * SATECHO_ACTUATORS.h — Blue LED on GPIO2 (irrigation actuator, Act 2)
 * 0% loops — recursiveBlink replaces for(;;)
 */
#ifndef SATECHO_ACTUATORS_H
#define SATECHO_ACTUATORS_H
#include <Arduino.h>
#include "satecho_config.h"

bool riegoActivo = false;

void actuatorsBegin() {
  pinMode(PIN_LED_RIEGO, OUTPUT);
  digitalWrite(PIN_LED_RIEGO, LOW);
}

void activarRiego() { digitalWrite(PIN_LED_RIEGO, HIGH); riegoActivo = true; }
void desactivarRiego() { digitalWrite(PIN_LED_RIEGO, LOW); riegoActivo = false; }
bool isRiegoActivo() { return riegoActivo; }

// Recursive blinking — replaces for(int i=0;i<3;i++)
void parpadeoRecursivo(int count, int msOn, int msOff) {
  if (count <= 0) return;
  digitalWrite(PIN_LED_RIEGO, HIGH); vTaskDelay(pdMS_TO_TICKS(msOn));
  digitalWrite(PIN_LED_RIEGO, LOW);  vTaskDelay(pdMS_TO_TICKS(msOff));
  parpadeoRecursivo(count - 1, msOn, msOff);
}
#endif