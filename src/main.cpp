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

// persistent current value of output characteristic
static uint16_t numberOfSteps;

// output characteristic to send output back to client
BLECharacteristic *pOutputChar;

// create encoder
ESP32Encoder wheelEncoder;
// set initial encoder position on program boot
int encoderPosition = 0;

// class InputReceivedCallbacks:
//     public BLECharacteristicCallbacks {

//         void onWrite(BLECharacteristic *pCharWriteState) {

//             pOutputChar->setValue(numberOfSteps);
//             pOutputChar->notify();
//     }
// };

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

void setup() {
    Serial.begin(115200);

    BLEDevice::init(PERIPHERAL_NAME);
    BLEServer *pServer = BLEDevice::createServer();
    BLEService *pService = pServer->createService(SERVICE_UUID);

    pOutputChar = pService->createCharacteristic(
                        CHARACTERISTIC_OUTPUT_UUID,
                        BLECharacteristic::PROPERTY_READ |
                        BLECharacteristic::PROPERTY_NOTIFY);

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

    wheelEncoder.attachFullQuad(SENSOR_1, SENSOR_2);
}

void loop() {
    static int pacer = 0;
    int newPosition = wheelEncoder.getCount();
    if (newPosition != encoderPosition) {
        Serial.printf("Encoder position = %02d\r\n", newPosition);
        encoderPosition = newPosition;
    }

    if (pacer++ > PACER_MAX) {
        pacer = 0;

        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

        pOutputChar->setValue(encoderPosition);
        pOutputChar->notify();
    }
}