/*
  AQI & VOC logger (ESP32)
  Sensors : SGP30 (TVOC, eCO2, I2C) + GP2Y1010 (dust, analog)
  Output  : local web page (port 80) + Google Sheets webhook every 35 s
  Setup   : copy secrets.example.h to secrets.h and fill in your own values
*/
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <time.h>
#include "Adafruit_SGP30.h"
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, SHEET_URL (not committed to git)

// ---------------- PIN DEFINITIONS ----------------
#define DUST_LED_PIN 25   // GP2Y1010 Pin 3 (LED)
#define DUST_VO_PIN  34   // GP2Y1010 Pin 5 (Vo) via 10k/20k divider
// SGP30 uses I2C: SDA = GPIO21, SCL = GPIO22

// ---------------- GP2Y1010 SETTINGS ----------------
const float DIVIDER_FACTOR = 1.5;    // (10k+20k)/20k, undoes the voltage divider
const float DUST_ZERO_V    = 0.213;  // Vo in clean air. CALIBRATE THIS
const float DUST_SLOPE     = 200.0;  // ug/m3 per volt (0.5V per 0.1 mg/m3)
const int   DUST_SAMPLES   = 10;

// ---------------- OBJECTS ----------------
WebServer server(80);
Adafruit_SGP30 sgp;

// ---------------- WIFI / GOOGLE SHEET (from secrets.h) ----------------
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* sheetURL = SHEET_URL;

// ---------------- GLOBALS ----------------
uint16_t sgp30_tvoc = 0;
uint16_t sgp30_eco2 = 0;
bool sgp30_ready = false;

float dust_vo = 0;        // sensor output voltage (V)
float dust_density = 0;   // estimated ug/m3

unsigned long lastLogTime = 0;
const unsigned long logInterval = 35 * 1000;

unsigned long lastSGPTime = 0;   // SGP30 needs a read every 1s

// ---------------- GP2Y1010 READ ----------------
void readDust() {
  float sumMv = 0;
  for (int i = 0; i < DUST_SAMPLES; i++) {
    digitalWrite(DUST_LED_PIN, LOW);      // LED ON (active low)
    delayMicroseconds(280);
    sumMv += analogReadMilliVolts(DUST_VO_PIN);
    delayMicroseconds(40);
    digitalWrite(DUST_LED_PIN, HIGH);     // LED OFF
    delayMicroseconds(9680);
  }
  float adcV = (sumMv / DUST_SAMPLES) / 1000.0;
  dust_vo = adcV * DIVIDER_FACTOR;
  dust_density = (dust_vo - DUST_ZERO_V) * DUST_SLOPE;
  if (dust_density < 0) dust_density = 0;
}

// ---------------- SGP30 READ ----------------
void readSGP() {
  if (sgp.IAQmeasure()) {
    sgp30_tvoc = sgp.TVOC;
    sgp30_eco2 = sgp.eCO2;
    sgp30_ready = true;
  }
}

// ---------------- AQI ----------------
float calculateAQI(float pm25) {
  float aqi = 0;
  if (pm25 <= 12.0) aqi = (50.0 / 12.0) * pm25;
  else if (pm25 <= 35.4) aqi = ((100.0 - 51.0) / (35.4 - 12.1)) * (pm25 - 12.1) + 51.0;
  else if (pm25 <= 55.4) aqi = ((150.0 - 101.0) / (55.4 - 35.5)) * (pm25 - 35.5) + 101.0;
  else aqi = ((200.0 - 151.0) / (150.4 - 55.5)) * (pm25 - 55.5) + 151.0;
  return aqi;
}

// ---------------- TIME ----------------
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Time Error";
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%I:%M:%S %p", &timeinfo);
  return String(buffer);
}

// ---------------- LOG TO GOOGLE SHEET ----------------
void logToSheet(String time, uint16_t tvoc, uint16_t eco2, float vo, float dust) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(sheetURL) + "?time=" + time
                  + "&tvoc=" + String(tvoc)
                  + "&eco2=" + String(eco2)
                  + "&vo=" + String(vo, 3)
                  + "&dust=" + String(dust, 1);
    url.replace(" ", "%20");

    http.begin(url);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int httpCode = http.GET();
    if (httpCode > 0) Serial.println("Logged | HTTP Code: " + String(httpCode));
    else Serial.println("Failed to log. HTTP code: " + String(httpCode));
    http.end();
  }
}

// ---------------- WEBPAGE ----------------
void handleRoot() {
  readDust();
  float aqi = calculateAQI(dust_density);

  String airStatus;
  if (aqi <= 50) airStatus = "Good";
  else if (aqi <= 100) airStatus = "Moderate";
  else if (aqi <= 150) airStatus = "Unhealthy (Sensitive)";
  else airStatus = "Unhealthy";

  String page = "<!DOCTYPE html><html><head>";
  page += "<meta http-equiv='refresh' content='5'>";
  page += "<title>ESP32 Air Monitor</title>";
  page += "</head><body style='font-family:Arial;'>";
  page += "<h1>ESP32 Air Quality Monitor</h1>";

  page += "<h2>TVOC/eCO2 Sensor (SGP30)</h2>";
  if (sgp30_ready) {
    page += "<p>TVOC: " + String(sgp30_tvoc) + " ppb</p>";
    page += "<p>eCO2: " + String(sgp30_eco2) + " ppm</p>";
  } else {
    page += "<p>Waiting for SGP30 data...</p>";
  }

  page += "<h2>Dust Sensor (GP2Y1010)</h2>";
  page += "<p>Vo: " + String(dust_vo, 3) + " V</p>";
  page += "<p>Dust density: " + String(dust_density, 1) + " ug/m3</p>";
  page += "<h2>AQI: " + String(aqi, 0) + " (" + airStatus + ")</h2>";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void setup() {
  Serial.begin(115200);

  pinMode(DUST_LED_PIN, OUTPUT);
  digitalWrite(DUST_LED_PIN, HIGH);          // LED off
  analogSetPinAttenuation(DUST_VO_PIN, ADC_11db);

  Wire.begin();                              // SDA 21, SCL 22
  if (!sgp.begin()) Serial.println("SGP30 not detected. Check wiring.");
  else Serial.println("SGP30 initialized.");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");   // IST = UTC+5:30
  Serial.println("Syncing time...");
  delay(2000);

  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  // SGP30 must be polled every 1s for its baseline algorithm to work
  if (now - lastSGPTime >= 1000) {
    lastSGPTime = now;
    readSGP();
  }

  if (now - lastLogTime >= logInterval || lastLogTime == 0) {
    lastLogTime = now;
    readDust();
    logToSheet(getFormattedTime(), sgp30_tvoc, sgp30_eco2, dust_vo, dust_density);
    Serial.printf("TVOC %u ppb | eCO2 %u ppm | Vo %.3f V | Dust %.1f ug/m3\n",
                  sgp30_tvoc, sgp30_eco2, dust_vo, dust_density);
  }
}
