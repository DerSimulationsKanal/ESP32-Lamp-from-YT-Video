#include <FastLED.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <time.h>

// -------------------------------------------------------------------
// --- KONFIGURATION ---
// -------------------------------------------------------------------

#define LED_PIN 27          
#define NUM_LEDS 58         
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

const char* ssid = "W-LAN NAME";
const char* passwort = "Passwort W-LAN";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;      
const int daylightOffset_sec = 3600;  

// UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c2c68cddb290"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Befehle
#define CMD_SET_COLOR 0x01 
#define CMD_SET_EFFECT 0x02 

// Effekt IDs
#define EFFECT_STATIC_COLOR 0x00 
#define EFFECT_RAINBOW 0x01
#define EFFECT_TWINKLE 0x02

// -------------------------------------------------------------------
// --- GLOBALE VARIABLEN ---
// -------------------------------------------------------------------
CRGB leds[NUM_LEDS];
uint8_t currentBrightness = 255;
uint8_t currentEffect = EFFECT_STATIC_COLOR;
uint8_t effectSpeed = 15;  //Standart verzögerung ms (ms = millisekunden)

bool deviceConnected = false;
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL; 

// -------------------------------------------------------------------
// --- EFFEKT FUNKTIONEN ---
// -------------------------------------------------------------------

void rainbowEffect() {
    // WICHTIG: EVERY_N_MILLISECONDS ersetzt delay()!
    // So blockieren wir Bluetooth nicht.
    EVERY_N_MILLISECONDS(20) { 
        static uint8_t hue = 0;
        fill_rainbow(leds, NUM_LEDS, hue, 7); 
        hue++; 
        FastLED.show();
    }
}

void twinkleEffect() {
    EVERY_N_MILLISECONDS(30) {
        fadeToBlackBy(leds, NUM_LEDS, 20); 
        if (random8() < 40) {
            uint16_t pixel = random16(NUM_LEDS);
            leds[pixel] = CHSV(random8(), 255, 255); 
        }
        FastLED.show();
    }
}

// -------------------------------------------------------------------
// HELFER: ERROR / WARN BLINKEN
// -------------------------------------------------------------------
void showYellowError() {
    Serial.println("ACHTUNG: Fehler/Reboot Status");
    uint8_t oldBrightness = FastLED.getBrightness();
    
    // Hier nutzen wir noch delay, da eh gleich rebootet wird -> egal für BLE
    for(int i = 0; i < 3; i++) {
        for(int b = 0; b <= 255; b += 5) {
            fill_solid(leds, NUM_LEDS, CRGB::Yellow);
            FastLED.setBrightness(b);
            FastLED.show();
            delay(5);
        }
        delay(100);
        for(int b = 255; b >= 0; b -= 5) {
            fill_solid(leds, NUM_LEDS, CRGB::Yellow);
            FastLED.setBrightness(b);
            FastLED.show();
            delay(5);
        }
    }
    FastLED.setBrightness(oldBrightness); 
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}

// -------------------------------------------------------------------
// --- BLE SERVER LOGIK ---
// -------------------------------------------------------------------

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("BLE: Verbunden");
      
      // OPTIONAL: Verbindungsparameter für mehr Stabilität anfordern
      // (Min Interval, Max Interval, Latency, Timeout)
      // pServer->updateConnParams(pServer->getConnId(), 0x10, 0x40, 0, 400); 
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("BLE: Getrennt");
      // Kleiner Delay damit der Stack sich erholen kann vor Neustart
      delay(500); 
      pServer->getAdvertising()->start(); 
      Serial.println("BLE: Advertising neu gestartet");
    }
};

class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        uint8_t *data = pCharacteristic->getData();
        size_t len = pCharacteristic->getLength();
        
        if (len > 0) {
            uint8_t cmd = data[0];
            
            if (cmd == CMD_SET_COLOR && len >= 4) {
                currentEffect = EFFECT_STATIC_COLOR;
                uint8_t r = data[1];
                uint8_t g = data[2];
                uint8_t b = data[3];
                fill_solid(leds, NUM_LEDS, CRGB(r, g, b));
                FastLED.setBrightness(currentBrightness); 
                FastLED.show();
            } 
            else if (cmd == CMD_SET_EFFECT && len >= 2) {
                currentEffect = data[1];
            } 
        }
    }
};

// -------------------------------------------------------------------
// --- SETUP ---
// -------------------------------------------------------------------

void setup() {
    Serial.begin(115200);

    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(currentBrightness);
    
    // Kurzes Start-Blinken
    fill_solid(leds, NUM_LEDS, CRGB::Blue); // Blau für Start
    FastLED.show();
    delay(500);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();

    // WiFi verbinden
    Serial.printf("Verbinde mit WLAN: %s ", ssid);
    WiFi.begin(ssid, password);
    
    int wifiRetries = 0;
    while (WiFi.status() != WL_CONNECTED && wifiRetries < 20) {
        delay(500);
        Serial.print(".");
        wifiRetries++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWLAN Verbunden!");
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    } else {
        Serial.println("\nWLAN fehlgeschlagen - Offline Modus.");
    }

    // BLE Init
    Serial.println("Starte BLE...");
    BLEDevice::init("Lumina Flow"); 
    
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    
    pCharacteristic = pService->createCharacteristic(
                                CHARACTERISTIC_UUID,
                                BLECharacteristic::PROPERTY_WRITE
                              );
    pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
    pService->start();
    
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    // WICHTIG: Min/Max Interval für Advertising (iOS mag das)
    pAdvertising->setMinPreferred(0x06);  
    pAdvertising->setMinPreferred(0x12);
    
    BLEDevice::startAdvertising();
    Serial.println("System Bereit!");
}

// -------------------------------------------------------------------
// --- LOOP ---
// -------------------------------------------------------------------

void loop() {
    // 1. REBOOT LOGIK 
    if (WiFi.status() == WL_CONNECTED) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            if (timeinfo.tm_hour == 3 && timeinfo.tm_min == 0 && timeinfo.tm_sec < 5) { // <5 sekunden fenster reicht
                Serial.println("Night-Reset!");
                showYellowError();
                ESP.restart();
            }
        }
    } else {
        if (millis() > 86400000) { //86400000 sind 24h sprich 1 Tag 
             Serial.println("Offline Reset!");
             showYellowError(); 
             ESP.restart();
        }
    }

    // 2. EFFEKTE
    // Das switch-case wird jetzt tausende Male pro Sekunde durchlaufen,
    // aber die LEDs updaten nur alle paar Millisekunden dank EVERY_N_MILLISECONDS.
    // Dadurch hat BLE genug Zeit zu "atmen".
    switch (currentEffect) {
        case EFFECT_STATIC_COLOR: 
            // Hier brauchen wir gar nichts tun, Farbe steht ja schon
            // Ein kleines Delay entlastet die CPU
            delay(10); 
            break;
        case EFFECT_RAINBOW: 
            rainbowEffect(); 
            break;
        case EFFECT_TWINKLE: 
            twinkleEffect(); 
            break;
        default: break;
    }
}