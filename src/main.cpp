#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <MPU6050.h>
#include <ss_oled.h> 
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <TinyGPS++.h>
#include "secrets.h"

#define BUTTON_PIN 14
#define MOTOR_PIN 13
#define OLED_ADDR 0x3C
#define SDA_PIN 21
#define SCL_PIN 22
#define MIC_PIN 34
#define THRESHOLD 2000

// --- Objects ---
TinyGPSPlus gps;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
Adafruit_BMP280 bmp; 
MPU6050 mpu;
SSOLED ssoled;

// Hardware Serial 2 for GPS
#define SerialGPS Serial2

void setup() {
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);

    // 1. Initialize Display
    oledInit(&ssoled, OLED_128x64, OLED_ADDR, 0, 0, 1, SDA_PIN, SCL_PIN, -1, 400000L);
    oledFill(&ssoled, 0, 1);
    oledWriteString(&ssoled, 0, 0, 0, (char *)"SYSTEM BOOTING...", FONT_8x8, 0, 1);

    // 2. Initialize Sensors & Pins
    if (!bmp.begin(0x76)) Serial.println("BMP280 Missing!");
    mpu.initialize();
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(MOTOR_PIN, OUTPUT);

    // 3. Connect WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");

    // 4. Initialize Firebase
    config.host = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    // 5. Initialize GPS
    SerialGPS.begin(9600, SERIAL_8N1, 16, 17);
    
    oledFill(&ssoled, 0, 1);
    oledWriteString(&ssoled, 0, 0, 0, (char *)"SYSTEM ONLINE", FONT_8x8, 0, 1);
    delay(1000);
}

void loop() {
    // A. Feed GPS Data
    while (SerialGPS.available() > 0) {
        gps.encode(SerialGPS.read());
    }

    // B. Read Sensors
    int buttonState = digitalRead(BUTTON_PIN);
    float temp = bmp.readTemperature();
    float pressure = bmp.readPressure() / 100.0F;
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);

    // C. Emergency Logic (Instant Priority)
    static int lastEmergencyState = -1; 
    int currentEmergency = (buttonState == LOW) ? 1 : 0;

    if (currentEmergency == 1) {
        digitalWrite(MOTOR_PIN, HIGH);
        // Only push if the state actually changed to avoid spamming Firebase
        if (lastEmergencyState != 1) {
            Firebase.setInt(fbdo, "/Watch/Emergency", 1);
            lastEmergencyState = 1;
            
            // Immediate Screen Update
            oledFill(&ssoled, 0, 1);
            oledWriteString(&ssoled, 0, 0, 2, (char *)"!! EMERGENCY !!", FONT_8x8, 0, 1);
        }
    } else {
        digitalWrite(MOTOR_PIN, LOW);
        if (lastEmergencyState == 1) {
            Firebase.setInt(fbdo, "/Watch/Emergency", 0);
            lastEmergencyState = 0;
        }
    }

    // Inside loop()
    int remoteMotor = 0;
    Firebase.getInt(fbdo, "/Watch/MotorRemote");
    remoteMotor = fbdo.intData();

    if (remoteMotor == 1 || currentEmergency == 1) {
        digitalWrite(MOTOR_PIN, HIGH);
    } else {
        digitalWrite(MOTOR_PIN, LOW);
    }

    // D. Normal Display Update (Every 1 Second)
    static unsigned long lastDisplay = 0;
    if (millis() - lastDisplay > 1000 && currentEmergency == 0) {
        lastDisplay = millis();
        oledFill(&ssoled, 0, 1);
        
        char line1[24], line2[24], line3[24];
        sprintf(line1, "T:%.1f C P:%.0f", temp, pressure);
        oledWriteString(&ssoled, 0, 0, 0, (char *)"--- WATCH DATA ---", FONT_8x8, 0, 1);
        oledWriteString(&ssoled, 0, 0, 2, line1, FONT_8x8, 0, 1);

        if (gps.location.isValid()) {
            sprintf(line2, "LAT: %.4f", gps.location.lat());
            sprintf(line3, "LON: %.4f", gps.location.lng());
            oledWriteString(&ssoled, 0, 0, 4, line2, FONT_8x8, 0, 1);
            oledWriteString(&ssoled, 0, 0, 6, line3, FONT_8x8, 0, 1);
        } else {
            sprintf(line2, "GPS Sats: %d", gps.satellites.value());
            oledWriteString(&ssoled, 0, 0, 4, (char *)"Searching GPS...", FONT_8x8, 0, 1);
            oledWriteString(&ssoled, 0, 0, 6, line2, FONT_8x8, 0, 1);
        }
    }

    // E. Cloud Sync (Every 10 Seconds)
    static unsigned long lastFirebasePush = 0;
    if (millis() - lastFirebasePush > 10000) {
        lastFirebasePush = millis();

        Firebase.setFloat(fbdo, "/Watch/Temperature", temp);
        Firebase.setFloat(fbdo, "/Watch/Pressure", pressure);
        Firebase.setInt(fbdo, "/Watch/AccelX", ax);
        Firebase.setInt(fbdo, "/Watch/AccelY", ay);
        Firebase.setInt(fbdo, "/Watch/AccelZ", az);

        if (gps.location.isValid()) {
            Firebase.setFloat(fbdo, "/Watch/Latitude", gps.location.lat());
            Firebase.setFloat(fbdo, "/Watch/Longitude", gps.location.lng());
        }
        Firebase.setInt(fbdo, "/Watch/Satellites", gps.satellites.value());
        Serial.println("Firebase Sync Complete");
    }
}