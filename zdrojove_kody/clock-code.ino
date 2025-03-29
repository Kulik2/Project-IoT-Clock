/*
  ESP32 + DS1302 + DHT11 + 4x8x8 MAX7219 (U8g2)
  + Automatická synchronizace RTC z NTP
  + PŘIČTENÍ +1 HODINY PŘI ZOBRAZENÍ
*/

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>      // Pro NTP (getLocalTime, configTime)
#include <U8g2lib.h>
#include <SPI.h>
#include <DS1302.h>    // Knihovna pro RTC (např. msparks/arduino-ds1302)
#include <DHT.h>       // Knihovna pro DHT

// --- Nastavení WiFi ---
const char* WIFI_SSID     = "T-Kulik11";
const char* WIFI_PASSWORD = "T-Kulik11";

// Jednou za tuto dobu (ms) se zkusíme znovu připojit, pokud nejsme online
const unsigned long WIFI_RETRY_INTERVAL = 3600000UL; // 1 hodina

// --- Piny pro DS1302 (RST=CE, DAT=IO, CLK=SCLK) ---
#define DS1302_RST  2
#define DS1302_DAT  4
#define DS1302_CLK  5

// --- DHT11 ---
#define DHTPIN      18
#define DHTTYPE     DHT11

// --- MAX7219 (software SPI pro U8g2) ---
#define LED_CLK     14
#define LED_DIN     13
#define LED_CS      15

// Objekty
DS1302 rtc(DS1302_RST, DS1302_DAT, DS1302_CLK);
DHT dht(DHTPIN, DHTTYPE);

// 4x8x8 LED matrix (32x8) s MAX7219 – SW SPI
U8G2_MAX7219_32X8_F_4W_SW_SPI u8g2(U8G2_R0, LED_CLK, LED_DIN, LED_CS, U8X8_PIN_NONE);

// Přepínání času / teploty
unsigned long previousMillis = 0;
unsigned long interval = 20000; // 20s čas, 5s teplota
bool showTemp = false;

// Pomocná proměnná pro hlídání pokusu o WiFi připojení
unsigned long lastWifiAttempt = 0;

// ----------------------------------------------------------------
// Připojení k WiFi (krátký pokus ~ 10s)
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return; // už jsme připojeni
  }
  Serial.print("Pripojuji se k WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi pripojeno.");
  } else {
    Serial.println("\nWiFi pripojeni selhalo.");
  }
}

// ----------------------------------------------------------------
// Načtení času z NTP a nastavení DS1302
void updateRTCfromNTP() {
  if (WiFi.status() != WL_CONNECTED) return; // Bez WiFi to nejde

  // Nastavení NTP serveru (časová zóna + offsety)
  // Tady 0,0 => GMT. Pro +1h byste mohl dát: configTime(3600, 0, "pool.ntp.org");
  configTime(0, 0, "pool.ntp.org"); 

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10000)) { // 10s timeout
    Serial.println("Nepodarilo se nacist cas z NTP.");
    return;
  }

  // Převedeme tm strukturu na Time (DS1302)
  int year   = timeinfo.tm_year + 1900;  // např. 2025
  int month  = timeinfo.tm_mon + 1;      // 1-12
  int day    = timeinfo.tm_mday;         // 1-31
  int hour   = timeinfo.tm_hour;         // 0-23
  int minute = timeinfo.tm_min;          // 0-59
  int second = timeinfo.tm_sec;          // 0-59
  int wday   = timeinfo.tm_wday;         // 0-6 (0=neděle)...

  Time newT(year, month, day, hour, minute, second, (Time::Day) wday);
  rtc.time(newT);  // Zapiš do DS1302
  Serial.println("DS1302: Cas aktualizovan z NTP.");
}

// ----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  // Spustíme WiFi (vypneme station mode, zapneme)
  WiFi.mode(WIFI_STA);
  connectWiFi();       // První pokus na začátku
  updateRTCfromNTP();  // Pokud vyšlo WiFi, nastav RTC

  // Ostatní inicializace
  u8g2.begin();
  dht.begin();

  // DS1302 – povolit zápis, spustit hodiny
  rtc.writeProtect(false);
  rtc.halt(false);

  Serial.println("Setup hotovo.");
}

// ----------------------------------------------------------------
void loop() {
  unsigned long currentMillis = millis();

  // Každých 20s (čas) / 5s (teplota) se přepneme
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    showTemp = !showTemp; 
    interval = showTemp ? 5000 : 20000;
  }

  // Když nejsme online, po uplynutí hodiny to zkusíme znova
  if (WiFi.status() != WL_CONNECTED) {
    if (currentMillis - lastWifiAttempt >= WIFI_RETRY_INTERVAL) {
      lastWifiAttempt = currentMillis;
      connectWiFi();
      if (WiFi.status() == WL_CONNECTED) {
        updateRTCfromNTP();
      }
    }
  }

  // Čas z DS1302
  Time now = rtc.time();

  // +1 hodina navíc (pokud přesáhne 23, vrátíme na 0)
  int displayHour = now.hr + 1;
  if (displayHour >= 24) {
    displayHour -= 24;
  }

  // Teplota z DHT
  float t = dht.readTemperature();
  int tempRounded = (int)round(t);

  // Příprava textu
  char displayBuffer[10];
  if (!showTemp) {
    // Vložíme displayHour místo now.hr
    sprintf(displayBuffer, "%02d:%02d", displayHour, now.min);
  } else {
    sprintf(displayBuffer, "%dC", tempRounded);
  }

  // Vymazání a font
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x8_mr); // klidně zkuste u8g2_font_5x8_tr

  // Centrovat na 32x8
  uint8_t textWidth = u8g2.getStrWidth(displayBuffer);
  int x = (32 - textWidth) / 2;
  int y = 7;

  u8g2.drawStr(x, y, displayBuffer);
  u8g2.sendBuffer();

  // Vypsat do Serialu - pro kontrolu
  Serial.print("Cas (DS1302 +1h): ");
  Serial.print(displayHour);
  Serial.print(":");
  Serial.print(now.min);
  Serial.print("  Teplota: ");
  Serial.println(tempRounded);

  delay(200);
}
