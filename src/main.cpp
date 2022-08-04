#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <ESP32Encoder.h>

#define PERIPHERAL_NAME "ESP32"
#define SERVICE_UUID "97108D83-8D51-46F2-93B1-678D129C1E8B"
#define CHARACTERISTIC_OUTPUT_UUID "ED01FEB8-A2A6-45F0-A70D-3B328414514C"
#define CHARACTERISTIC_INPUT_UUID "7104D9CC-83C8-422C-97FE-C353C7B9D322"

#define SENSOR_1 12
#define SENSOR_2 27
#define PACER_MAX 1000
#define LED_PACER_MAX 50
#define TIMEOUT_NO_CHECKIN_UNITS 10000
#define SLEEP_PACER_MAX 2000

// variable for setting whether
// #define DEBUG

#ifdef DEBUG
    // defines for generating random steps for debug
    #define RANDOM_STEP_RANGE 100
    #define RANDOM_STEP_START 0

    // debug 6 second hour for rapid testing 
    #define MILLIS_UNIT (60*1000/10) 
#else
    // 1 unit currently initialised to 5 minutes
    #define UNIT_MINUTES 5
    #define MILLIS_UNIT (UNIT_MINUTES*60*1000) 
#endif

// output characteristic to send output back to client
BLECharacteristic *pOutputChar;

bool overrideSend = false;
bool connected = false;
bool prevConnected = connected;

// create encoder
ESP32Encoder wheelEncoder;

// set initial encoder position on program boot
int32_t encoderPosition = 0;

uint32_t stepCountCode = 0xFFFFFFFF;
uint32_t hourlyDataCode = 0xFFFFFFFE;

// accumulator for step diff over time
// should be a circular buffer in future
int stepsOverTime[TIMEOUT_NO_CHECKIN_UNITS] = { 0 };

// variable for holding the current number of steps
uint16_t stepsThisUnit = 0;

unsigned long unit = 0;
unsigned long sentUnit = 0;

unsigned long prevStepsThisUnit = 0;

unsigned long millisToNextUnit = MILLIS_UNIT; // assume we start exactly on a unit
unsigned long nextUnit = millisToNextUnit;

// Class defines methods called when a device connects and disconnects from the service
class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        // Serial.println("BLE Client Connected");
        overrideSend = true;
        connected = true;
    }
    void onDisconnect(BLEServer* pServer) {
        BLEDevice::startAdvertising();
        // Serial.println("BLE Client Disconnected");
        connected = false;
    }
};

class InputReceivedCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharWriteState) {
        uint8_t *inputValues = pCharWriteState->getData();
        size_t dataSize = pCharWriteState->getLength();
        // Serial.print("Length of input data: ");
        // Serial.println(dataSize);

        millisToNextUnit = 0;

        int j = 0;
        for (size_t i = dataSize; i > 0; i--)
        {
            unsigned long temp = (inputValues[i-1] << (8*(j++)));
            millisToNextUnit += temp;
        }
        // Serial.print("ms to next Unit: ");
        // Serial.println(millisToNextUnit);

        nextUnit = millis() + millisToNextUnit;
        // Serial.print("next Unit: ");
        // Serial.println(nextUnit);
    }
};

void setup() {
    // setCpuFrequencyMhz(40);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_12, 0); // wakeup on encoder event
    // esp_sleep_enable_timer_wakeup(MILLIS_UNIT * 1000 / 2); // wake up twice per unit
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
    // Serial.println("BLE Service Advertising");

    pinMode(LED_BUILTIN, OUTPUT);

    ESP32Encoder::useInternalWeakPullResistors=UP;

    wheelEncoder.attachSingleEdge(SENSOR_1, SENSOR_2);

    Serial.print("Current time: ");
    Serial.println(millis());
}

void loop() {
    static int pacer = 0;
    static int ledPacer = 0;
    static int sleepPacer = 0;

    int32_t newPosition = wheelEncoder.getCount();
    if (newPosition != encoderPosition) {
        Serial.printf("Encoder position = %d\r\n", newPosition);
        encoderPosition = newPosition;

        stepsThisUnit++;
        sleepPacer = 0;
    }

    if (pacer++ > PACER_MAX) {
        pacer = 0;

        if (sleepPacer++ > SLEEP_PACER_MAX) {
            Serial.println("going to sleep");
            digitalWrite(LED_BUILTIN, LOW); // turn off LED in sleep mode
            esp_light_sleep_start();
            Serial.println("waking up");
        }

        unsigned long currentMillis = millis();

        // iterate to next unit if appropriate
            if (currentMillis >= nextUnit) {
                unsigned long late = currentMillis - nextUnit;
                Serial.print("ms late: ");
                Serial.println(late);

                nextUnit = currentMillis + MILLIS_UNIT - (late % MILLIS_UNIT);
                Serial.print("next Unit: ");
                Serial.println(nextUnit);
                
                // not sure if this is yucky or not
                #ifdef DEBUG
                    int random = rand() % RANDOM_STEP_RANGE + RANDOM_STEP_START;
                    stepsThisUnit += random;
                #else
                #endif

                stepsOverTime[unit] = stepsThisUnit;

                stepsThisUnit = 0;

                unit += (late / MILLIS_UNIT) + 1;
            }  

        if (!connected) {
            if(ledPacer++ > LED_PACER_MAX) {
                // seperate LED pacer to slow down blinking while allowing BT transmit speed to increased
                digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // LED blinks when not connected
                ledPacer = 0;
            }
        } else {
            sleepPacer = 0;
            if (connected != prevConnected) { // i.e. app has just connected
                delay(1000); // second delay to allow app BLE to get its shit together before sending
            }
            digitalWrite(LED_BUILTIN, HIGH); // set LED to solid when connected

            // Serial.printf("Current time (ms) = %ld\r\n", currentMillis);
            // Serial.printf("Current time (s) = %ld\r\n", (currentMillis)/1000);
            // Serial.printf("Current Unit = %d\r\n", unit);

            if (unit > sentUnit) {
                // Serial.println("new unit, sending through data for this unit");
                // Serial.print("steps this unit: ");
                // Serial.println(stepsOverTime[sentUnit]);
                int stepsForUnit = -stepsOverTime[sentUnit];
                
                // zeros are sent as -65535 to allow distinguishing between '0 steps for the sent hour' and '0 steps right now'
                if (stepsForUnit == 0) {
                    stepsForUnit = -65535;
                }

                // Serial.print("sending steps for this unit as: ");
                // Serial.println(stepsForUnit);
                pOutputChar->setValue(stepsForUnit);
                pOutputChar->notify();
                sentUnit++;
            }
            
            if (prevStepsThisUnit != stepsThisUnit) {
                // Serial.print("sending step data for this unit: ");
                // Serial.println(stepsThisUnit);
                pOutputChar->setValue(stepsThisUnit);
                pOutputChar->notify();
                prevStepsThisUnit = stepsThisUnit;
            }
        }
        prevConnected = connected;
    }
}