#include <sys/time.h>
#include <WiFi.h>
#include "time.h"

#define DEBUG true  //set to true for debug output, false for no debug ouput
#define Serial if(DEBUG)Serial

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ccs811.h>

#include <Fonts/FreeMono9pt7b.h>

#include <DHT.h>

#include "wifiCredentials.h"

// Define CS pin for the SD card module
#define SD_CS 5
#define ERROR_LED GPIO_NUM_2

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

#define S_TO_uS_FACTOR 1000000  /* Conversion factor from second to micro seconds */
#define TIME_TO_SLEEP  15        /* Time ESP32 will go to sleep (in seconds) */
#define CCS_MODE CCS811_MODE_10SEC  /* 1s, 10s, 60s*/
#define LOG_SD_CARD_INTERVAL 72 /* number of measures before saving in SD card */

typedef struct {
    float temperature;
    float humidity;
} dht_data;

typedef struct {
    uint16_t eCO2;
    uint16_t TVOC;
} ccs_data;

typedef struct {
    dht_data dhts[4];
    ccs_data ccs;
} meteo_data;

RTC_DATA_ATTR meteo_data meteo[LOG_SD_CARD_INTERVAL];
RTC_DATA_ATTR uint16_t idx_display = 1;
RTC_DATA_ATTR uint16_t idx_reading = -1;

RTC_DATA_ATTR bool error_led = false;
RTC_DATA_ATTR timeval sleep_start;
RTC_DATA_ATTR timeval last_btn_push;
RTC_DATA_ATTR timeval last_ntp_sync;
RTC_DATA_ATTR bool is_ntp_sync = false;
RTC_DATA_ATTR int nosync_idx = -1;

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

void initialize_screen() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println("SSD1306 allocation failed");
  }
}

bool initialize_ccs() {
  bool success = true;
  Serial.println("initializing CCS!");
  ccs.set_i2cdelay(50); // Needed for ESP8266 because it doesn't handle I2C clock stretch correctly
  if(!ccs.begin()) {
    Serial.println("CCS881: Failed to begin");
    success = false;
  }
  // Print CCS811 versions
  Serial.print("setup: hardware    version: "); Serial.println(ccs.hardware_version(),HEX);
  Serial.print("setup: bootloader  version: "); Serial.println(ccs.bootloader_version(),HEX);
  Serial.print("setup: application version: "); Serial.println(ccs.application_version(),HEX);

  if (!ccs.start(CCS_MODE)) {
    Serial.println("CCS881: Failed to start sensing");
    success = false;
  }
  return success;
}

bool initialize_sd_card () {
  if(!SD.begin(SD_CS)) {
    Serial.println("Card Mount Failed");
    return false;
 }

  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return false;
  }
  return true;
}

bool initialize(bool initializeCCS) {
  bool success = true;
  for (int i=0; i<N_DHTS; ++i) {
    dhts[i].begin();
  }
  initialize_screen();
  if (initializeCCS) {
    success = success && initialize_ccs();
  }
  return success;
}

String format_meteo_data(const meteo_data *data) {
  String ret = String(data->ccs.eCO2) + "," + String(data->ccs.TVOC);
  for (uint8_t i_dht = 0; i_dht < 4; i_dht++) {
    ret += "," + String(data->dhts[i_dht].temperature) + "," + String(data->dhts[i_dht].humidity);
  }
  ret += "\n";
  return ret;
}

int get_nosync_index() {
  int max_idx = 0;
  File root = SD.open("/");
  while (true) {
    File entry =  root.openNextFile();
    if (!entry) {
      // no more files
      break;
    }
    String name = entry.name();
    if (!entry.isDirectory() && name.startsWith("/nosync_")) {
      int idx = name.substring(8).toInt();
      if (idx > max_idx) {
        max_idx = idx;
      }
    }
    entry.close();
  }
  root.close();
  return max_idx + 1;
}

bool log_sd_card() {
  timeval now;
  gettimeofday(&now, NULL);
  String prefix;
  if (is_ntp_sync) {
    prefix = "sync_";
  } else {
    prefix = "nosync_" + String(nosync_idx) + "_";
  }
  String filename = "/" + prefix + String(now.tv_sec) + ".csv";

  String writeString = String("co2, tvoc, temp_room, hum_room, temp_wall, hum_wall, temp_ext, hum_ext, temp_ceiling, hum_ceiling\n");
  for (uint16_t ii = 0; ii < LOG_SD_CARD_INTERVAL; ii++) {
    writeString += format_meteo_data(&(meteo[ii]));
  }

  File file = SD.open(filename, FILE_WRITE);
  if(!file) {
    Serial.println("Couldn't open file "+filename);
    return false;
  }
  file.print(writeString);
  Serial.println("Wrote data to " + filename);

  file.close();
  return true;
}

bool read_sensors() {
  meteo_data *data = &(meteo[idx_reading]);
  bool success = true;
  for (int i=0; i<N_DHTS; ++i) {
    data->dhts[i].temperature = dhts[i].readTemperature();
    data->dhts[i].humidity = dhts[i].readHumidity();
    if (isnan(data->dhts[i].temperature)) {
      Serial.println("DHT error on idx " + String(i));
      success = false;
    }
  }

  uint16_t errstat, raw;
  ccs.read(&(data->ccs.eCO2), &(data->ccs.TVOC), &errstat, &raw);
  if (errstat == CCS811_ERRSTAT_OK_NODATA) {
    Serial.println("CCS waiting for new data");
  } else if (errstat & CCS811_ERRSTAT_I2CFAIL) {
    Serial.println("CCS i2c error");
    success = false;
  } else if (errstat != CCS811_ERRSTAT_OK) {
    Serial.println("CCS unknown error");
    success = false;
  }
  return success;
}

void display_warning(int tx, int ty) {
  int w = 8;
  int h = 12;
  display.fillTriangle(tx - w, ty + h,
                       tx, ty,
                       tx + w, ty + h, WHITE);
}

void display_screen() {
  if (idx_display == 0) {
    return;
  }
  display.setFont(&FreeMono9pt7b);
  display.setTextColor(WHITE);
  display.clearDisplay();

  if (!is_ntp_sync) {
    display_warning(
        display.width() - 10,
        0);
  }

  display.setCursor(0,20);
  meteo_data data = meteo[idx_reading];
  if (idx_display == 1) {
    display.println("Particles ");
    display.print("CO2 ");
    display.print(data.ccs.eCO2);
    display.print("ppm\n");
    display.print("VOC ");
    display.print(data.ccs.TVOC);
    display.print("ppb\n");
  }
  else {
    uint8_t i_dht = idx_display - 2;
    dht_data cur_dht = data.dhts[i_dht];
    display.println(dht_locations[i_dht]);
    display.print("Temp ");
    display.print(cur_dht.temperature, 1);
    display.print("c\n");
    display.print("Hum  ");
    display.print(cur_dht.humidity, 1);
    display.print("%\n");
  }
  display.display();
}

//Function that prints the reason by which ESP32 has been awaken from sleep
void print_wakeup_reason(esp_sleep_wakeup_cause_t wakeup_reason) {
  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
  Serial.flush();
}

void printLocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void sync_time(bool blink_led) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  bool led = false;
  for (uint8_t trials = 0;
      (WiFi.status() != WL_CONNECTED) && (trials < 10);
      trials++) {
      delay(1000);
      if (blink_led) {
        led = !led;
        digitalWrite(ERROR_LED, led);
      }
      Serial.print(".");
  }
  Serial.flush();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("failed to connect to wifi:");
    Serial.println("SSID: \"" + String(WIFI_SSID) + "\"");
    Serial.println("Password: \"" + String(WIFI_PASSWORD) + "\"");
    if (nosync_idx == -1) {
      nosync_idx = get_nosync_index();
      Serial.print("not syncing time. Using index entry: ");
      Serial.println(nosync_idx);
    }
    return;
  }
  Serial.println("connected to wifi, IP: " + String(WiFi.localIP()) + " RSSI: " + String(WiFi.RSSI()));
  const long gmtOffset_sec = 3600;
  const int daylightOffset_sec = 3600;
  configTime(gmtOffset_sec, daylightOffset_sec, "fr.pool.ntp.org", "time.nist.gov", "time.windows.com");
  gettimeofday(&last_ntp_sync, NULL);
  is_ntp_sync = true;
  printLocalTime();
  WiFi.disconnect();
}

void setup(){
  Serial.begin(115200);
  // delay(1000); //Take some times to open up the Serial Monitor

  pinMode(ERROR_LED, OUTPUT);

  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  print_wakeup_reason(wakeup_reason);

  uint64_t sleep_duration = TIME_TO_SLEEP * S_TO_uS_FACTOR;
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    timeval now, diff;
    gettimeofday(&now, NULL);
    timersub(&now,&last_btn_push,&diff);
    // throttle button push
    if (diff.tv_sec > 1 || (diff.tv_usec / 1000) > 500) {
      // last button push is old enough
      gettimeofday(&last_btn_push, NULL);
      initialize_screen();
      idx_display = (idx_display + 1) % (2 + 4); // off screen off, ccs and 4 dhts
      Serial.println("Changing screen to " + String(idx_display));
      if (idx_display == 0) {
        display.clearDisplay();
        display.display();
      } else {
        display_screen();
      }
    }
    timersub(&now,&sleep_start,&diff);
    sleep_duration = (TIME_TO_SLEEP - diff.tv_sec) * S_TO_uS_FACTOR + diff.tv_usec;
  }
  else {
    bool sd_card_success = initialize_sd_card(); // light up if no sd card at any time
    bool save_success = true;

    idx_reading += 1;
    if (idx_reading == LOG_SD_CARD_INTERVAL) {
      bool save_success = log_sd_card();
      error_led = error_led && save_success;
      idx_reading = 0;
    }
    if (idx_reading == 0) {
      sync_time(wakeup_reason == 0);
    }
    bool init_success = initialize(wakeup_reason == 0);
    Serial.println("Reading sensor idx " + String(idx_reading));
    bool read_success = read_sensors();

    error_led = !(sd_card_success && save_success && init_success && read_success);

    display_screen();
    gettimeofday(&sleep_start, NULL);
  }

  digitalWrite(ERROR_LED, error_led);
  gpio_hold_en(ERROR_LED);

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_27, 1);
  esp_sleep_enable_timer_wakeup(sleep_duration);

  Serial.println("Going to sleep now for " + String(double(sleep_duration) / S_TO_uS_FACTOR, 0) + "s");
  esp_deep_sleep_start();
}

void loop(){
  //This is not going to be called
}
