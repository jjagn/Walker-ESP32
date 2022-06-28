#include <Arduino.h>
#include <ESP32Encoder.h>

#define SENSOR_1 33
#define SENSOR_2 15
#define PACER_MAX 20000

ESP32Encoder wheelEncoder;

int64_t encoderPosition = 0;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);

    ESP32Encoder::useInternalWeakPullResistors=UP;

    wheelEncoder.attachFullQuad(SENSOR_1, SENSOR_2);
}

void loop() {
    static int pacer = 0;
    int64_t newPosition = wheelEncoder.getCount();
    if (newPosition != encoderPosition) {
        Serial.printf("Encoder position = %02d\r\n", newPosition);
        encoderPosition = newPosition;
    }

    if (pacer++ > PACER_MAX) {
        pacer = 0;

        // int sensor_1 = digitalRead(SENSOR_1);
        // int sensor_2 = digitalRead(SENSOR_2);

        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

        // Serial.print("Sensor 1: ");
        // Serial.println(sensor_1);
        // Serial.print("Sensor 2: ");
        // Serial.println(sensor_2);
    }
}