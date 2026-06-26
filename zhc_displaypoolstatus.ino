#include "Adafruit_mfGFX.h"
#include "fonts.h"
#include <ArduinoJson.h>
#include "Arduino_ST7789.h"
#include <SPI.h>

// ── Pin definitions (Photon 2) ────────────────────────────────────────────────
// Hardware SPI: S0=MOSI (DIN), S2=SCK (CLK) — fixed pins, no defines needed
// Control lines on free GPIO pins:
#define TFT_CS   D2
#define TFT_DC   D5
#define TFT_RST  D6
#define TFT_BL   D7

// ── Display object ─────────────────────────────────────────────────────────────
Arduino_ST7789 tft = Arduino_ST7789(TFT_DC, TFT_RST, TFT_CS);

// ── Runtime data (received via Particle subscriptions) ────────────────────────
double lastTempF             = 0.0;
double poolTemp              = 0.0;
double systemTemp            = 0.0;
double startupTemp           = 0.0;
double outsideTemp           = 0.0;
double logLight              = 0.0;
unsigned long dailyHeaterOnMinutes = 0;

#define HEATER_UNKNOWN 0
#define HEATER_AUTO    1
#define HEATER_ON      2
#define HEATER_OFF     3
int heaterControlState = HEATER_UNKNOWN;

// ── Timing ─────────────────────────────────────────────────────────────────────
const unsigned long DISPLAY_INTERVAL = 60000;
const unsigned long WEATHER_INTERVAL = 900000;
bool displayReady = false;  // gate to prevent loop() drawing before setup() finishes

// ── Forward declarations ───────────────────────────────────────────────────────
void updateDisplay();
void weatherHandler(const char *event, const char *data);
void poolDataHandler(const char *event, const char *data);

// ── setup() ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(9600);
    Particle.variable("lastTempF",    lastTempF);
    Particle.variable("poolTemp",     poolTemp);
    Particle.variable("systemTemp",   systemTemp);
    Particle.variable("startupTemp",  startupTemp);
    Particle.variable("outsideTemp",  outsideTemp);
    Particle.variable("logLight",     logLight);
    Particle.variable("heaterState",  heaterControlState);
    Particle.variable("heaterOnMins", dailyHeaterOnMinutes);

    Particle.subscribe("hook-response/weather_openmeteo", weatherHandler, MY_DEVICES);
    Particle.subscribe("poolData",                        poolDataHandler, MY_DEVICES);

    // Backlight on
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init(240, 320);
    delay(500);
    tft.setRotation(3);
    tft.fillScreen(BLACK);
    tft.setFont(COMICS_8);
    tft.setTextColor(WHITE);
    tft.setTextSize(2);
    tft.setCursor(0, 0);
    tft.print("Initializing...");

    displayReady = true;
    Serial.println("Photon 2 display initialized");
}


// ── loop() ────────────────────────────────────────────────────────────────────
void loop() {
    if (!displayReady) return;  // don't draw until setup() is done

    static unsigned long lastDisplayUpdate = 0;
    static bool firstDisplayRun = true;

    if (firstDisplayRun || millis() - lastDisplayUpdate >= DISPLAY_INTERVAL) {
        firstDisplayRun = false;
        updateDisplay();
        lastDisplayUpdate = millis();
    }

    static unsigned long lastWeatherRequest = 0;
    static bool firstWeatherRun = true;

    // Triggers instantly on boot, then checks every 15 mins (or handles freezing targets with a 10s backoff pace)
    if (firstWeatherRun || 
        millis() - lastWeatherRequest >= WEATHER_INTERVAL || 
        (outsideTemp < 10.0 && millis() - lastWeatherRequest > 10000)) {
        
        firstWeatherRun = false;
        Serial.println("Requesting weather data from Open-Meteo...");
        Particle.publish("weather_openmeteo", "", PRIVATE);
        lastWeatherRequest = millis();
    }

    delay(1000);
}


// ── Display update ─────────────────────────────────────────────────────────────
void updateDisplay() {
    const char* heaterStr =
        heaterControlState == HEATER_AUTO ? "AUTO"    :
        heaterControlState == HEATER_ON   ? "ON"      :
        heaterControlState == HEATER_OFF  ? "OFF"     : "Unknown";

    tft.fillScreen(BLACK);
    tft.setFont(COMICS_8);
    tft.setTextColor(WHITE);
    tft.setTextSize(4);
    tft.setCursor(0, 0);

    tft.printlnf("Pool Temp: %d F",   (int)poolTemp);
    delay(100);
    // Smaller text for secondary values
    tft.setFont(GLCDFONT);
    tft.setTextSize(3);

    if (lastTempF > 0.0) {
        tft.printlnf("Inside: %d F",  (int)lastTempF);
    } else {
        tft.println("Inside: -- F");
    }
    tft.printlnf("Outside: %d F",     (int)(outsideTemp + 0.4));
    tft.printlnf("Light: %d Lux",     (int)logLight);
    tft.printlnf("Pool Temp: %d F",   (int)poolTemp);
    tft.printlnf("Sys Temp: %d F",    (int)systemTemp);
    tft.printlnf("Startup: %d F",     (int)startupTemp);
    tft.printlnf("Heater: %s",        heaterStr);
    tft.printlnf("On Min: %lu min",   dailyHeaterOnMinutes);

    Serial.printlnf("Display: Inside:%d Outside:%d Light:%d Pool:%d Sys:%d Start:%d Heater:%s Mins:%lu",
        (int)lastTempF, (int)(outsideTemp + 0.4), (int)logLight,
        (int)poolTemp, (int)systemTemp, (int)startupTemp,
        heaterStr, dailyHeaterOnMinutes);
}

// ── Cloud event handlers ───────────────────────────────────────────────────────
void weatherHandler(const char *event, const char *data) {
    if (strncmp(event, "hook-response/weather_openmeteo", 31) == 0) {
        outsideTemp = atof(data);
        Serial.printlnf("Weather update: %.1f F", outsideTemp);
    }
}

void poolDataHandler(const char *event, const char *data) {
    if (strncmp(event, "poolData", 8) != 0) return;

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, data ? data : "{}");
    if (error) {
        Serial.printlnf("JSON parse failed: %s", error.c_str());
        return;
    }

    poolTemp              = doc["poolTemp"]              | 0.0;
    systemTemp            = doc["systemTemp"]            | 0.0;
    startupTemp           = doc["startupTemp"]           | 0.0;
    heaterControlState    = doc["heaterControlState"]    | (int)HEATER_UNKNOWN;
    dailyHeaterOnMinutes  = doc["dailyHeaterOnMinutes"]  | 0UL;
    lastTempF             = doc["lastTempF"]             | lastTempF;
    logLight              = doc["logLight"]              | logLight;

    Serial.printlnf("Pool data: Pool:%.1f Sys:%.1f Start:%.1f State:%d Mins:%lu",
        poolTemp, systemTemp, startupTemp, heaterControlState, dailyHeaterOnMinutes);
}
