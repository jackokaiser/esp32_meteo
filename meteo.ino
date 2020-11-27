#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_CCS811.h>

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

typedef struct {
    float temperature;
    float humidity;
} dht_data;

typedef struct {
    dht_data dhts[4];
    float eCO2;
    float TVOC;
} meteo_data;

meteo_data meteo;

#define ROOM 0

#define DHT_ROOM_PIN 17

// D1 mini default IO21=SDA, IO22=SCL
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHT dht(DHT_ROOM_PIN, DHT22);
//Adafruit_CCS811 ccs;

// RTC_DATA_ATTR int bootCount = 0; // persistent after sleep

void initialize() {
  dht.begin();
  //ccs.begin();
  // text display tests
     
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.setFont(&FreeMono9pt7b);
  display.setTextColor(WHITE);   
}

void read_sensors() {
  meteo.dhts[ROOM].temperature = dht.readTemperature();
  meteo.dhts[ROOM].humidity = dht.readHumidity();

  // float cssTemp = ccs.calculateTemperature();
  // ccs.setTempOffset(cssTemp - meteo.dhts[ROOM].temperature);

  // meteo.eCO2 = ccs.geteCO2();
  // meteo.TVOC = ccs.getTVOC();
}

void update_serial() {
  Serial.print("Temp ");
  Serial.print(meteo.dhts[ROOM].temperature, 1);
  Serial.print("\nHum ");
  Serial.print(meteo.dhts[ROOM].humidity, 1);
  Serial.println("");
}

void update_screen() {
  display.clearDisplay();
  display.setCursor(0,20);             
  display.print("Temp s ");
  display.print(meteo.dhts[ROOM].temperature, 1);
  display.print("c\n");
  display.print("Hum   ");
  display.print(meteo.dhts[ROOM].humidity, 1);
  display.print("%\n");
  display.display();
  //display.print(meteo.eCO2);
  //display.print(meteo.TVOC);
}

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

void setup(){
  Serial.begin(115200);
  Serial.println("Hello from ESP32");
  Serial.flush();
  initialize();

  delay(5000); 
  // while(!ccs.available());

  read_sensors();
  update_serial();
  update_screen();

  //Print the wakeup reason for ESP32
  print_wakeup_reason();

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
