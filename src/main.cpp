#include <Arduino.h>

void setup() {
    pinMode(33, INPUT_PULLUP);
    pinMode(15, INPUT_PULLUP);
    Serial.begin(9600);
}

void loop() {
    Serial.println("")
}