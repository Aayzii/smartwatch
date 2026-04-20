#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <MPU6050.h>
#include <ss_oled.h> 
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <TinyGPS++.h>

// GPS Object
TinyGPSPlus gps;
#define SerialGPS Serial2

// WiFi Credentials
#define WIFI_SSID "notToday"
#define WIFI_PASSWORD "only1gbsad"

// Firebase Credentials (We will fill these later)
#define FIREBASE_HOST "" 
#define FIREBASE_AUTH ""

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Pin Definitions
#define BUTTON_PIN 14
#define MOTOR_PIN 13

// Sensor Objects
Adafruit_BMP280 bmp; 
MPU6050 mpu;
SSOLED ssoled;

// Display Settings
#define SDA_PIN 21
#define SCL_PIN 22
#define OLED_ADDR 0x3C

void setup() {
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);

    // Initialize Display
    oledInit(&ssoled, OLED_128x64, OLED_ADDR, 0, 0, 1, SDA_PIN, SCL_PIN, -1, 400000L);
    
    // Initialize Sensors
    bmp.begin(0x76);
    mpu.initialize();
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(MOTOR_PIN, OUTPUT);
    
    // WiFi Connection
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");

    // 2. ACTUAL FIREBASE INITIALIZATION (Crucial!)
    config.host = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true); // Helps keep the connection alive
    
    Serial.println("Firebase Initialized");

    // Initialize GPS
    SerialGPS.begin(9600, SERIAL_8N1, 16, 17);
}

void loop() {
    // A. ALWAYS FEED GPS DATA
    while (SerialGPS.available() > 0) {
        gps.encode(SerialGPS.read());
    }

    // B. READ ALL SENSORS ONCE
    int buttonState = digitalRead(BUTTON_PIN);
    float temp = bmp.readTemperature();
    float pressure = bmp.readPressure() / 100.0F;

    // C. LOGIC & DISPLAY
    if (buttonState == LOW) {
        // EMERGENCY MODE
        digitalWrite(MOTOR_PIN, HIGH);
        oledFill(&ssoled, 0, 1);
        oledWriteString(&ssoled, 0, 0, 2, (char *)"!! EMERGENCY !!", FONT_8x8, 0, 1);
        oledWriteString(&ssoled, 0, 0, 4, (char *)"PUSHING TO CLOUD", FONT_8x8, 0, 1);
    } else {
        digitalWrite(MOTOR_PIN, LOW);
        
        // Only refresh the screen every 1 second to prevent flickering
        static unsigned long lastUpdate = 0;
        if (millis() - lastUpdate > 1000) {
            lastUpdate = millis();
            oledFill(&ssoled, 0, 1);

            // Row 0: Header
            oledWriteString(&ssoled, 0, 0, 0, (char *)"SMART WATCH v1", FONT_8x8, 0, 1);

            // Row 2: Environment
            char envStr[24];
            sprintf(envStr, "T:%.1fC P:%.0f", temp, pressure);
            oledWriteString(&ssoled, 0, 0, 2, envStr, FONT_8x8, 0, 1);

            // Row 4 & 6: GPS Data
            if (gps.location.isValid()) {
                char latStr[24], lonStr[24];
                sprintf(latStr, "LAT:%.4f", gps.location.lat());
                sprintf(lonStr, "LON:%.4f", gps.location.lng());
                oledWriteString(&ssoled, 0, 0, 4, latStr, FONT_8x8, 0, 1);
                oledWriteString(&ssoled, 0, 0, 6, lonStr, FONT_8x8, 0, 1);
            } else {
                char satStr[24];
                sprintf(satStr, "WAITING GPS (Sats:%d)", gps.satellites.value());
                oledWriteString(&ssoled, 0, 0, 4, satStr, FONT_8x8, 0, 1);
            }
        }
    }

    static unsigned long lastFirebasePush = 0;
    if (millis() - lastFirebasePush > 10000) { 
        lastFirebasePush = millis();

        // 1. Always push Environment
        Firebase.setFloat(fbdo, "/Watch/Temperature", temp);
        Firebase.setFloat(fbdo, "/Watch/Pressure", pressure);
        // Add MPU6050 Motion Data (Great for showing a live graph)
        int16_t ax, ay, az;
        mpu.getAcceleration(&ax, &ay, &az);
        Firebase.setInt(fbdo, "/Watch/AccelX", ax);
        Firebase.setInt(fbdo, "/Watch/AccelY", ay);
        Firebase.setInt(fbdo, "/Watch/AccelZ", az);

        // Note: If you have the MAX30100 wired, add its read here as well

        // 2. Handle GPS Logic
        if (gps.location.isValid()) {
            Firebase.setFloat(fbdo, "/Watch/Latitude", gps.location.lat());
            Firebase.setFloat(fbdo, "/Watch/Longitude", gps.location.lng());
            Firebase.setInt(fbdo, "/Watch/Satellites", gps.satellites.value());
        } else {
            // If no lock, we can push "0" or just the sat count to debug
            // This confirms the GPS is at least talking to the ESP32
            Firebase.setInt(fbdo, "/Watch/Satellites", gps.satellites.value());
            Serial.print("GPS waiting... Satellites seen: ");
            Serial.println(gps.satellites.value());
        }
    }

    if (buttonState == LOW) {
        digitalWrite(MOTOR_PIN, HIGH);
        // Push immediately without waiting for the 2-second timer
        Firebase.setInt(fbdo, "/Watch/Emergency", 1); 
        
        oledFill(&ssoled, 0, 1);
        oledWriteString(&ssoled, 0, 0, 2, (char *)"!! EMERGENCY !!", FONT_8x8, 0, 1);
    }
}
