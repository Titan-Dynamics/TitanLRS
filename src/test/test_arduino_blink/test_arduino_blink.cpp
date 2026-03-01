/**
 * Arduino Blink Test for WeAct Mini H743
 * Gate 1 validation - confirms toolchain, clock, and GPIO work
 *
 * LED on PE3 (active-low on the WeAct Mini H743)
 */
#include <Arduino.h>

#ifndef GPIO_PIN_LED
#define GPIO_PIN_LED PE3
#endif

void setup()
{
    pinMode(GPIO_PIN_LED, OUTPUT);
}

void loop()
{
    digitalWrite(GPIO_PIN_LED, LOW);   // LED on (active-low)
    delay(1000);
    digitalWrite(GPIO_PIN_LED, HIGH);  // LED off
    delay(1000);
}
