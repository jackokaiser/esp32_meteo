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
#include <esp_deep_sleep.h>
#include <esp_wifi.h>
#include <esp_bt.h>

// Define CS pin for the SD card module
#define SD_CS 5

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
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
RTC_DATA_ATTR uint16_t idx_reading = 0;

RTC_DATA_ATTR bool error_led = false;

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

void initialize_sensors(bool initializeCCS) {
  for (int i=0; i<N_DHTS; ++i) {
    dhts[i].begin();
  }
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println("SSD1306 allocation failed");
  }


  if (initializeCCS) {
    initialize_ccs();
  }
}

void initialize_ccs() {
  Serial.println("initializing CCS!");
  ccs.set_i2cdelay(50); // Needed for ESP8266 because it doesn't handle I2C clock stretch correctly
  if(!ccs.begin()) {
    error_led = true;
    Serial.println("CCS881: Failed to begin");
  } 
  // Print CCS811 versions
  Serial.print("setup: hardware    version: "); Serial.println(ccs.hardware_version(),HEX);
  Serial.print("setup: bootloader  version: "); Serial.println(ccs.bootloader_version(),HEX);
  Serial.print("setup: application version: "); Serial.println(ccs.application_version(),HEX);

  if (!ccs.start(CCS_MODE)) {
    error_led = true;
    Serial.println("CCS881: Failed to start sensing");
  }
}

bool initialize_sd_card () {
  if(!SD.begin(SD_CS)) {
    error_led = true;
    Serial.println("Card Mount Failed");
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE) {
    error_led = true;
    Serial.println("No SD card attached");
    return false;
  }
}

bool log_sd_card(String filename) {
  String writeString = "";
  if (!SD.exists(filename)) {
    Serial.println("File does not exists yet, adding headers"); 
    writeString += String("co2, tvoc, temp_room, hum_room, temp_wall, hum_wall, temp_ext, hum_ext, temp_ceiling, hum_ceiling\n");
  }
  for (uint16_t ii = 0; ii < LOG_SD_CARD_INTERVAL; ii++) {
    writeString += format_meteo_data(&(meteo[ii]));
  }

  File file = SD.open(filename, FILE_WRITE);
  if(!file) {
    error_led = true;
    Serial.println("Couldn't open file");  
    return false;
  }
  file.print(writeString);
  file.close();
  return true;
}

String format_meteo_data(const meteo_data *data) {
  String ret = String(data->ccs.eCO2) + "," + String(data->ccs.TVOC);
  for (uint8_t i_dht = 0; i_dht < 4; i_dht++) {
    ret += "," + String(data->dhts[i_dht].temperature) + "," + String(data->dhts[i_dht].humidity);
  }
  ret += "\n";
  return ret;
}

void read_sensors() {
  meteo_data *data = &(meteo[idx_reading]);

  for (int i=0; i<N_DHTS; ++i) {
    data->dhts[i].temperature = dhts[i].readTemperature();
    data->dhts[i].humidity = dhts[i].readHumidity();
    if (isnan(data->dhts[i].temperature)) {
      error_led = true;
      Serial.println("DHT error on idx " + String(i));
    }
  }

  uint16_t errstat, raw;
  ccs.read(&(data->ccs.eCO2), &(data->ccs.TVOC), &errstat, &raw);
  if (errstat == CCS811_ERRSTAT_OK_NODATA) {
    Serial.println("CCS waiting for new data");
  } else if (errstat & CCS811_ERRSTAT_I2CFAIL) {
    Serial.println("CCS i2c error");
    error_led = true;
  } else if (errstat != CCS811_ERRSTAT_OK) {
    Serial.println("CCS unknown error");
    error_led = true;
  }
}

bool display_screen() {
  if (idx_display == 0) {
    return true;
  }

  display.setFont(&FreeMono9pt7b);
  display.setTextColor(WHITE);
  display.clearDisplay();
  display.setCursor(0,20);
  meteo_data data = meteo[idx_reading];
  if (idx_display == 1) {
    display.println("Air quality ");
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

void setup(){
  Serial.begin(115200);
  delay(1000); //Take some time to open up the Serial Monitor

  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  print_wakeup_reason(wakeup_reason);

  if (wakeup_reason == 0) {
    Serial.println("First run: initialize ccs");
    initialize_sensors(true);

    delay(5000);
    read_sensors();
  }
  else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    idx_display = (idx_display + 1) % (2 + 4); // off screen off, ccs and 4 dhts 
    Serial.println("Changing screen to " + String(idx_display));
    if (idx_display == 0) {
      display.clearDisplay();
    } else {
      display_screen();
    }
  }
  else {
    error_led = false;
    idx_reading += 1;
    Serial.println("Reading sensor idx " + String(idx_reading));
    initialize_sensors(false);
    delay(5000);
    read_sensors();
    display_screen();
  }

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_27, 1);

  // shutdown_rf();
  Serial.println("Going to sleep now for " + String(TIME_TO_SLEEP) + "s");
  esp_deep_sleep_start();
}

void shutdown_rf() {
  esp_bt_controller_disable();
  esp_wifi_disconnect();
}

void loop(){
  //This is not going to be called
}
