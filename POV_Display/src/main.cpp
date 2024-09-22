#include <Arduino.h>
#include <Wire.h>
#include <Wifi.h>
#include <AS5600.h>
#include <Adafruit_DotStar.h>
#include <ESPAsyncWebServer.h>

#include "image.h"

#define SSID "ESP"
#define PASSWORD "12345678"

#define NUM_LEDS 72
#define BRIGHTNESS 50
#define RES 3
#define OFFSET 210
#define MOTOR_MAX_SPEED 70
#define MOTOR_MIN_SPEED 50
#define ANGLE_OFFSET 95.0

#define MOTOR_IN_1  22
#define MOTOR_IN_2  4

#define PIN_OUT 32
#define PIN_DIR 33
#define PIN_SCL 25
#define PIN_SDA 26
#define PIN_GPO 27

#define DATAPIN  23
#define CLOCKPIN 18

#define MAX_IMAGES 4

AS5600 as5600;
AsyncWebServer server(80);
Adafruit_DotStar strip(NUM_LEDS,  DOTSTAR_BRG, &SPI);

uint16_t counter1, counter2;

const int n_segm = 360 / RES;
const byte offset = (float)OFFSET / 360 * n_segm;

bool multipleImage = false;
bool receivedFirst = false;

uint16_t cur_seg = 0;
uint32_t sum = 0;

unsigned long startTime = 0;
uint16_t packetCounter = 0;
uint16_t rotation = 0;
uint8_t image_count = 0;

int curr_image = 0;

enum {
  IDLE_STATE,
  START_STATE,
  DRAW_STATE
};

int state = IDLE_STATE;

void animation() {
  strip.clear();
  counter1 = cur_seg;
  counter2 = (counter1 + (n_segm/2))%n_segm;
  for(int i = NUM_LEDS/2 - 1; i >= 0; i--) {
    strip.setPixelColor(NUM_LEDS/2 - i - 1,strip.gamma32(image[curr_image][n_segm -1 - counter2][i]));
    strip.setPixelColor(NUM_LEDS/2 + i, strip.gamma32(image[curr_image][n_segm -1 - counter1][i]));
  }
  strip.show();
}

void motor_stop() {
  for(int i = MOTOR_MAX_SPEED; i >= 0; i--) {
    analogWrite(MOTOR_IN_1, i);
    delay(20);
  }
}

void handle_led_on() {
  digitalWrite(BUILTIN_LED, HIGH);
  Serial.println("Led on");
}

void handle_led_off() {
  digitalWrite(BUILTIN_LED, LOW);
  Serial.println("Led off");
}

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void as5600_task(void* parameter) {
  while(1){
    cur_seg = as5600.rawAngle()*AS5600_RAW_TO_DEGREES/RES;
    vTaskDelay(1);
  }
}

void strip_start() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
}

void server_start() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(SSID, PASSWORD);
  packetCounter = 0;
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  server.begin();

  server.on("/ledOn", HTTP_POST, [](AsyncWebServerRequest *request){
    handle_led_on();
    request->send(200, "/ledOn", "Hello, POST: " );
  });

  server.on("/ledOff", HTTP_POST, [](AsyncWebServerRequest *request){
    handle_led_off();
    request->send(200, "/ledOff", "Hello, POST: " );
  });

  server.on("/motorStop", HTTP_POST, [](AsyncWebServerRequest *request){
    motor_stop();
    request->send(200, "/motorStop", "Hello, POST: " );
  });

  server.on("/start-end", HTTP_POST, [](AsyncWebServerRequest *request){
    String st_end = request->getParam(0)->value();
    if(st_end == "start"){
      startTime  = millis();
      Serial.println("start");
      state = IDLE_STATE;
    }else{
      if(st_end == "start_without_stop"){
        startTime  = millis();
        Serial.println("start_without_stop");
      } else {
        Serial.print("end ");
        Serial.println(millis() - startTime);
        state = START_STATE;
        if(multipleImage){
          if(image_count >= MAX_IMAGES){
            Serial.print("max limit is ");
            Serial.println(MAX_IMAGES);
          } else {
            if(receivedFirst){
              image_count++;
            } else {
              receivedFirst = true;
              image_count++;
            }
            Serial.print("cur image count is ");
            Serial.println(image_count);
          }
        } else {
          receivedFirst = false;
          image_count = 0;
        }
      }
    }
    request->send(200, "/start-end", "Hello, POST: " );
  });

  server.on("/multi-image", HTTP_POST, [](AsyncWebServerRequest *request){
    String st_end = request->getParam(0)->value();
    if(st_end == "multi"){
      multipleImage = true;
      receivedFirst = false;
      image_count = 0;
    }else{
      multipleImage = false;
      receivedFirst = false;
      image_count = 0;
    }
    Serial.println(multipleImage);
    request->send(200, "/start-end", "Hello, POST: " );
  });

 
  server.on("/post-data", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Data Received");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    int col = 0;
    int row = 0;
    String temp = "";

    for (int i = 1; i < len; i++) {
      char c = data[i];
      if (c == ',' || c == ']') {
        if(col == 0){
          row = temp.toInt();
        } else {
          image[image_count][col-1][row] = temp.toInt();
        }
        col++;
        temp = "";
      } else {
        temp += c;
      }
    }
  });
}

void as5600_start() {
  pinMode(PIN_OUT,INPUT);

  Wire.begin(PIN_SDA,PIN_SCL);
  as5600.begin(PIN_DIR);
  as5600.setOffset(ANGLE_OFFSET);
  as5600.setDirection(AS5600_COUNTERCLOCK_WISE);
  as5600.setHysteresis(AS5600_HYST_OFF);
  as5600.setOutputMode(AS5600_OUTMODE_ANALOG_100);

  xTaskCreate(as5600_task, "AS6500", 2048, NULL, 1, NULL);
}

void motor_start() {
  digitalWrite(MOTOR_IN_2, LOW);
  analogWrite(MOTOR_IN_1, 0);
  for(int i = MOTOR_MIN_SPEED; i < MOTOR_MAX_SPEED; i++) {
    analogWrite(MOTOR_IN_1, i);
    delay(20);
  }
}

void main_task(void* parameter) {
  while(1){
    if(state == IDLE_STATE){
      analogWrite(MOTOR_IN_1, 0);
    }
    if(state == START_STATE){
      motor_start();
      state = DRAW_STATE;
    }
    if(state == DRAW_STATE){
      if(cur_seg != counter1){
        if(cur_seg == 0){
          rotation++;
          if(multipleImage && receivedFirst && (image_count != 0)) {
            curr_image = rotation%(image_count);
          } else{
            curr_image = rotation%(image_count+1);
          }
        }
        animation();
      } 
    }
    vTaskDelay(1);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("POV Display");

  Serial.println("CPU Frequency: " + String(ESP.getCpuFreqMHz()) + " MHz");
  Serial.println("Wifi power: " + String(WiFi.getTxPower()));

  pinMode(BUILTIN_LED, OUTPUT);
  pinMode(MOTOR_IN_1, OUTPUT);
  pinMode(MOTOR_IN_2, OUTPUT);

  strip_start();
  server_start();
  as5600_start();

  xTaskCreate(main_task, "Main", 2048, NULL, 1, NULL);
}

void loop() {

}