#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <ESP32Encoder.h>

#define PERIPHERAL_NAME "ESP32"
#define SERVICE_UUID "97108D83-8D51-46F2-93B1-678D129C1E8B"
#define CHARACTERISTIC_OUTPUT_UUID "ED01FEB8-A2A6-45F0-A70D-3B328414514C"
#define CHARACTERISTIC_INPUT_UUID "7104D9CC-83C8-422C-97FE-C353C7B9D322"

#define SENSOR_1 33
#define SENSOR_2 15
#define PACER_MAX 20000
#define TIMEOUT_NO_CHECKIN_HOURS 1000

#define MILLIS_HOUR 3600*1000

// function prototypes
unsigned long adjMillis(unsigned long millisSinceLastHour);

// output characteristic to send output back to client
BLECharacteristic *pOutputChar;

// create encoder
ESP32Encoder wheelEncoder;

// set initial encoder position on program boot
int32_t encoderPosition = 0;

// accumulator for step diff over time
uint16_t stepsOverTime[TIMEOUT_NO_CHECKIN_HOURS] = {};

unsigned long hour = 0;

unsigned long millisSinceLastHour = 0;

// bias time forward by the milliseconds in an hour to prevent underflow when
// calibrating time later
unsigned long previousHourMillis = adjMillis(millisSinceLastHour);

time_t currentTime = 0;

// Class defines methods called when a device connects and disconnects from the service
class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        Serial.println("BLE Client Connected");
    }
    void onDisconnect(BLEServer* pServer) {
        BLEDevice::startAdvertising();
        Serial.println("BLE Client Disconnected");
    }
};

class InputReceivedCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharWriteState) {
        uint8_t *inputValues = pCharWriteState->getData();
        size_t dataSize = pCharWriteState->getLength();
        Serial.print("Length of input data: ");
        Serial.println(dataSize);

        millisSinceLastHour = 0;

        int j = 0;
        for (size_t i = dataSize; i > 0; i--)
        {
            unsigned long temp = (inputValues[i-1] << (8*(j++)));
            Serial.print("Value = ");
            Serial.println(inputValues[i-1]);
            Serial.print("temp = ");
            Serial.println(temp);
            millisSinceLastHour += temp;
        }

        Serial.println(millisSinceLastHour);
    }
};

void setup() {
    Serial.begin(115200);

    BLEDevice::init(PERIPHERAL_NAME);
    BLEServer *pServer = BLEDevice::createServer();
    BLEService *pService = pServer->createService(SERVICE_UUID);

    pOutputChar = pService->createCharacteristic(
                        CHARACTERISTIC_OUTPUT_UUID,
                        BLECharacteristic::PROPERTY_READ |
                        BLECharacteristic::PROPERTY_NOTIFY);

    BLECharacteristic *pInputChar = pService->createCharacteristic(
                        CHARACTERISTIC_INPUT_UUID,
                        BLECharacteristic::PROPERTY_WRITE_NR | 
                        BLECharacteristic::PROPERTY_WRITE);

    // Hook callback to report server events
    pServer->setCallbacks(new ServerCallbacks());
    pInputChar->setCallbacks(new InputReceivedCallbacks());

    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);

    // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x06);  
    pAdvertising->setMinPreferred(0x12);

    BLEDevice::startAdvertising();
    Serial.println("BLE Service Advertising");

    pinMode(LED_BUILTIN, OUTPUT);

    ESP32Encoder::useInternalWeakPullResistors=UP;

    wheelEncoder.attachSingleEdge(SENSOR_1, SENSOR_2);

    Serial.print("Current time: ");
    Serial.println(previousHourMillis);
    
}

void loop() {
    static int pacer = 0;

    int32_t newPosition = wheelEncoder.getCount();
    if (newPosition != encoderPosition) {
        Serial.printf("Encoder position = %02d\r\n", newPosition);
        encoderPosition = newPosition;

        unsigned long currentMillis = adjMillis(millisSinceLastHour);

        if (currentMillis >= previousHourMillis + MILLIS_HOUR) {
            Serial.println("More than one hour elapsed");
            Serial.print("Previous hour ms: ");
            Serial.println(previousHourMillis);
            Serial.print("Current ms: ");
            Serial.println(currentMillis);

            unsigned long diff = currentMillis - previousHourMillis;

            Serial.print("Difference: ");
            Serial.println(diff);

            unsigned long elapsed = diff / MILLIS_HOUR;

            Serial.print("Hours elapsed: ");
            Serial.println(elapsed);

            hour += elapsed;
            previousHourMillis = currentMillis;
        }  

        Serial.printf("Current time (ms) = %ld\r\n", currentMillis);
        Serial.printf("Current time (s) = %ld\r\n", (currentMillis)/1000);
        Serial.printf("Current hour = %d\r\n", hour);
    }

    if (pacer++ > PACER_MAX) {
        pacer = 0;

        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

        pOutputChar->setValue(encoderPosition);
        pOutputChar->notify();
    }
}

unsigned long adjMillis(unsigned long millisSinceLastHour) {
    // take current time, add one hour's worth of ms to prevent underflow and then
    // calibrate by the number of milliseconds since the last hour 
    unsigned long timeNow = millis();
    unsigned long timeAdjusted = 0;
    unsigned long timeRemainder = millisSinceLastHour % MILLIS_HOUR;

    if (timeNow > timeRemainder) {
        timeAdjusted = timeNow - timeRemainder;
    } else {
        timeAdjusted = timeNow + MILLIS_HOUR - timeRemainder;
    }

    Serial.println("Fetching current time");
    Serial.print("System time: ");
    Serial.println(timeNow);

    Serial.print("Adjusted time: ");
    Serial.println(timeAdjusted);
    return timeAdjusted;
}