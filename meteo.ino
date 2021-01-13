#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ccs811.h>

#include <Fonts/FreeMono9pt7b.h>

#include <DHT.h>
#include <esp_deep_sleep.h>
#include <esp_wifi.h>
#include <esp_bt.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  15        /* Time ESP32 will go to sleep (in seconds) */
#define CCS_MODE CCS811_MODE_10SEC  /* 1s, 10s, 60s*/         

typedef struct {
    float temperature;
    float humidity;
    bool error;
} dht_data;

typedef struct {
    uint16_t eCO2;
    uint16_t TVOC;
    bool error;
} ccs_data;

typedef struct {
    dht_data dhts[4];
    ccs_data ccs;
} meteo_data;

meteo_data meteo;

#define DHT_ROOM_PIN 16
#define DHT_WALL_PIN 17
#define DHT_OUTSIDE_PIN 33
#define DHT_CEILING_PIN 26

#define ROOM_IDX 0
#define WALL_IDX 1
#define OUTSIDE_IDX 2
#define CEILING_IDX 3
#define N_DHTS 4

DHT dhts [N_DHTS] = {
  DHT(DHT_ROOM_PIN, DHT22),
  DHT(DHT_WALL_PIN, DHT22),
  DHT(DHT_OUTSIDE_PIN, DHT22),
  DHT(DHT_CEILING_PIN, DHT22)
};

String dht_locations [N_DHTS] = {
  "Room",
  "Wall",
  "Outside",
  "Ceiling"
};

// D1 mini default IO21=SDA, IO22=SCL
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

CCS811 ccs;

// RTC_DATA_ATTR int bootCount = 0; // persistent after sleep

void initialize(bool initializeCCS) {
  for (int i=0; i<N_DHTS; ++i) {
    dhts[i].begin();
  }

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println("SSD1306 allocation failed");
  }

  if (initializeCCS) {
    Serial.println("initializing CCS!");
    ccs.set_i2cdelay(50); // Needed for ESP8266 because it doesn't handle I2C clock stretch correctly


    if(!ccs.begin()) {
      Serial.println("CCS881: Failed to begin");
    } 

    // Print CCS811 versions
    Serial.print("setup: hardware    version: "); Serial.println(ccs.hardware_version(),HEX);
    Serial.print("setup: bootloader  version: "); Serial.println(ccs.bootloader_version(),HEX);
    Serial.print("setup: application version: "); Serial.println(ccs.application_version(),HEX);
  
    if (!ccs.start(CCS_MODE)) {
      Serial.println("CCS881: Failed to start sensing");
    }
  }
  
  display.setFont(&FreeMono9pt7b);
  display.setTextColor(WHITE);
}

void read_sensors() {
  for (int i=0; i<N_DHTS; ++i) {
    meteo.dhts[i].temperature = dhts[i].readTemperature();
    meteo.dhts[i].humidity = dhts[i].readHumidity();
    meteo.dhts[i].error = isnan(meteo.dhts[i].temperature);
  }

  uint16_t errstat, raw;
  ccs.read(&meteo.ccs.eCO2, &meteo.ccs.TVOC, &errstat, &raw);
  meteo.ccs.error = (errstat != CCS811_ERRSTAT_OK);
  if (errstat == CCS811_ERRSTAT_OK_NODATA) {
    Serial.println("CCS waiting for new data");
  } else if (errstat & CCS811_ERRSTAT_I2CFAIL) {
    Serial.println("CCS i2c error");
  } else if (meteo.ccs.error) {
    Serial.println("CCS unknown error");
  }
}


void update_screen() {
  int screen_delay = 3000;

  if (!meteo.ccs.error) {
    display.clearDisplay();
    display.setCursor(0,20);
    
    display.println("Air quality ");
    display.print("CO2 ");
    display.print(meteo.ccs.eCO2);
    display.print("ppm\n");
    display.print("VOC ");
    display.print(meteo.ccs.TVOC);
    display.print("ppb\n");
    
    display.display();
    delay(screen_delay);
  }

  for (int i=0; i<N_DHTS; ++i) {
    dht_data cur_dht = meteo.dhts[i];

    if (!cur_dht.error) {
      display.clearDisplay();
      display.setCursor(0,20);
  
      display.println(dht_locations[i]);
      display.print("Temp ");
      display.print(cur_dht.temperature, 1);
      display.print("c\n");
      display.print("Hum  ");
      display.print(cur_dht.humidity, 1);
      display.print("%\n");
      display.display();
      delay(screen_delay);
    }
  }
  display.clearDisplay();
  display.display();
}

void setup(){
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  bool initializeCCS = (wakeup_reason != ESP_SLEEP_WAKEUP_TIMER);
  
  Serial.begin(115200);
  Serial.println("Hello from ESP32");
  Serial.flush();
  initialize(initializeCCS);

  delay(5000);

  read_sensors();
  update_screen();

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  // shutdown_rf();
  Serial.println("Going to sleep now for " + String(TIME_TO_SLEEP) + "s");
  Serial.flush();
  esp_deep_sleep_start();
}

void shutdown_rf() {
  esp_bt_controller_disable();
  esp_wifi_disconnect();
}

void loop(){
  //This is not going to be called
}
