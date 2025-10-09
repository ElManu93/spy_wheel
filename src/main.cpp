#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "esp_camera.h"
#include "SPIFFS.h"
#include "secrets.h"

// Onboard LED
const int ledPin = 4;
bool ledState = LOW;

// Webserver
AsyncWebServer server(80);

// Camera pin configuration (AI Thinker)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void startCameraServer();

void setup() {
  Serial.begin(9600);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, ledState);

  // SPIFFS starten
  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS Mount failed");
    return;
  }

    // Configurate camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // start camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Kamera-Init fehlgeschlagen: 0x%x", err);
    return;
  }

  // WLAN verbinden
  WiFi.begin(ssid, password);
  Serial.print("Verbinde mit WLAN ..");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nVerbunden!");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());

  // index.html & style.css automatisch aus SPIFFS laden
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  // Endpunkte für LED-Steuerung
  server.on("/led/on", HTTP_GET, [](AsyncWebServerRequest *request){
    ledState = HIGH;
    digitalWrite(ledPin, ledState);
    request->send(200, "text/plain", "LED AN");
  });

  server.on("/led/off", HTTP_GET, [](AsyncWebServerRequest *request){
    ledState = LOW;
    digitalWrite(ledPin, ledState);
    request->send(200, "text/plain", "LED AUS");
  });

  // Signle shot camera capture
  server.on("/camera", HTTP_GET, [](AsyncWebServerRequest *request){
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    request->send(500, "text/plain", "Kamerabild konnte nicht abgerufen werden");
    return;
  }
  AsyncWebServerResponse *response = request->beginResponse(200, "image/jpeg", fb->buf, fb->len);
  response->addHeader("Content-Disposition", "inline; filename=capture.jpg");
  request->send(response);
  esp_camera_fb_return(fb);
  });

  // start server
  server.begin();
}

void loop() {
  // Nichts nötig, AsyncWebServer läuft im Hintergrund
}
