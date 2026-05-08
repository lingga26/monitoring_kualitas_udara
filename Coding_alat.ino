#include <Wire.h>
#include <WiFi.h>
#include "ThingSpeak.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "bsec.h"

// =========================================================
//                  DEKLARASI VARIABEL GLOBAL
// =========================================================

// ===== Konfigurasi WiFi =====
const char *ssid = "sandi";
const char *password = "12345678";

// ===== Konfigurasi ThingSpeak =====
unsigned long channelID = 2988837;
const char *writeAPIKey = "28TJRL0YAOZT1YPO";
WiFiClient client;

// ===== OLED =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
// Objek display harus dideklarasikan di scope global
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1); 
int oledAddr = 0x3C;
#define SSD1306_WHITE 1
#define SSD1306_BLACK 0

// ===== BME680 (BSEC2) =====
// Objek sensor harus dideklarasikan di scope global
Bsec iaqSensor; 
int bmeAddr = 0x76;

// ===== Buzzer =====
#define BUZZER 5 // Deklarasi pin BUZZER

// ===== Interval & Timer =====
unsigned long lastSample = 0;
unsigned long lastUpload = 0;
const unsigned long intervalSample = 3000;    // 3 detik update OLED
const unsigned long intervalUpload = 300000;  // 5 menit upload ThingSpeak

// Variabel untuk menyimpan status kalibrasi BSEC
bool iaqSensorCalibrated = false;


// =========================================================
//                       FUNGSI TAMBAHAN
// =========================================================

// Fungsi cek status BSEC harus berada di luar loop
void checkIaqSensorStatus(void) {
  if (iaqSensor.bsecStatus != BSEC_OK) {
    if (iaqSensor.bsecStatus < BSEC_OK) {
      Serial.print("BSEC error code : ");
      Serial.println(iaqSensor.bsecStatus);
      for (;;) delay(10);
    }
  }

  if (iaqSensor.bme68xStatus != BME68X_OK) {
    if (iaqSensor.bme68xStatus < BME68X_OK) {
      Serial.print("BME680 error code : ");
      Serial.println(iaqSensor.bme68xStatus);
      for (;;) delay(10);
    }
  }
}


// =========================================================
//                           SETUP
// =========================================================

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); // SDA=21, SCL=22
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);

  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, oledAddr)) {
    Serial.println("OLED tidak terdeteksi!");
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 0);
  display.println("Air Quality IoT");
  display.display();
  delay(2000);

  // WiFi connect
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();
  
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
  }
  Serial.println("\nWiFi terhubung!");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.display();
  delay(1000);
  
  ThingSpeak.begin(client);

  // BSEC Init
  iaqSensor.begin(bmeAddr, Wire);
  checkIaqSensorStatus();

  bsec_virtual_sensor_t sensorList[5] = {
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_GAS
  };

  iaqSensor.updateSubscription(sensorList, 5, BSEC_SAMPLE_RATE_LP);
  checkIaqSensorStatus();
}


// =========================================================
//                            LOOP
// =========================================================

void loop() {
  unsigned long currentMillis = millis();

  if (iaqSensor.run()) {
    float temperature = iaqSensor.temperature;
    float humidity = iaqSensor.humidity;
    float pressure = iaqSensor.pressure / 100.0;
    float gas_res = iaqSensor.gasResistance / 1000.0;
    int iaq_index = iaqSensor.iaq;
    String aqi_status;
    bool isWarning = false;

    // Cek status kalibrasi
    if (iaqSensor.iaqAccuracy > 0 && !iaqSensorCalibrated) {
      iaqSensorCalibrated = true;
      Serial.println("\nIAQ Sensor Successfully Calibrated!");
    } else if (iaqSensor.iaqAccuracy == 0) {
      iaqSensorCalibrated = false;
    }

    // Kategori IAQ (Pemicu Peringatan saat IAQ >= 151)
    if (iaq_index <= 50) aqi_status = "Good";
    else if (iaq_index <= 100) aqi_status = "Moderate";
    else if (iaq_index <= 150) aqi_status = "Unhealthy SG";
    else if (iaq_index <= 200) { aqi_status = "Unhealthy"; isWarning = true; } // Peringatan dimulai di sini
    else if (iaq_index <= 300) { aqi_status = "Very Unhealthy"; isWarning = true; }
    else { aqi_status = "Hazardous"; isWarning = true; }

    // === Update OLED setiap 3 detik ===
    if (currentMillis - lastSample >= intervalSample) {
      lastSample = currentMillis;

      // Output ke Serial Monitor
      Serial.println("------------------------------------");
      Serial.printf("Temp: %.2f C\n", temperature);
      Serial.printf("Hum : %.2f %%\n", humidity);
      Serial.printf("Pres: %.2f hPa\n", pressure);
      Serial.printf("Gas : %.2f kOhm\n", gas_res);
      Serial.printf("IAQ : %d (%s)\n", iaq_index, aqi_status.c_str());
      if (!iaqSensorCalibrated) Serial.println("Status: Waiting for calibration...");
      if (isWarning) Serial.println("!!! PERINGATAN: KUALITAS UDARA TIDAK SEHAT !!!");
      
      // Update Tampilan OLED
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);

      // Judul Area
      display.setCursor(20, 0);
      display.println("Air Quality IoT");
      
      // Data Sensor
      display.setCursor(0, 16); display.printf("Temp: %.1f C", temperature);
      display.setCursor(0, 26); display.printf("Hum : %.1f %%", humidity);
      display.setCursor(0, 36); display.printf("Pres: %.0f hPa", pressure);
      display.setCursor(0, 46); display.printf("Gas : %.1f kOhm", gas_res);
      
      // Status & Peringatan
      display.setCursor(0, 56); 
      display.printf("IAQ: %d %s", iaq_index, aqi_status.c_str());
      
      // Tampilkan Peringatan di OLED jika Unhealthy atau lebih buruk
      if (isWarning) {
          display.setCursor(80, 56);
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // Invert warna
          display.print("WARNING!");
      }
      
      display.display();

      // Kontrol Buzzer (diaktifkan saat Warning)
      if (isWarning) digitalWrite(BUZZER, HIGH);
      else digitalWrite(BUZZER, LOW);
    }

    // === Upload ke ThingSpeak setiap 5 menit ===
    if (currentMillis - lastUpload >= intervalUpload) {
      lastUpload = currentMillis;

      // Hanya upload jika WiFi terhubung
      if (WiFi.status() == WL_CONNECTED) {
          ThingSpeak.setField(1, temperature);
          ThingSpeak.setField(2, humidity);
          ThingSpeak.setField(3, pressure);
          ThingSpeak.setField(4, gas_res);
          ThingSpeak.setField(5, iaq_index);

          int x = ThingSpeak.writeFields(channelID, writeAPIKey);
          if (x == 200) {
            Serial.println("Update ke ThingSpeak sukses!");
          } else {
            Serial.println("Gagal update ke ThingSpeak. Code error: " + String(x));
          }
      } else {
          Serial.println("WiFi terputus, tidak bisa upload.");
      }
    }
  } else {
    checkIaqSensorStatus();
  }
}