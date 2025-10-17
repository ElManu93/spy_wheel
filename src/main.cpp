#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "FS.h"
#include "SPIFFS.h"
#include "secrets.h"

// --- Camera pin definitions (AI Thinker) ---
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

#define LED_PIN 4

// --- HTTP-Server ---
WiFiServer server(80);

// --- Kamera-Initialisierung ---
bool initCamera() {
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
  config.xclk_freq_hz = 22000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_VGA;  // 320x240
  config.jpeg_quality = 15;
  config.fb_count = 1;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Kamera-Initialisierung fehlgeschlagen");
    return false;
  }

  return true;
}

// --- Stream senden ---
void handleStream(WiFiClient client) {
  const char* boundary = "frame";
  String response = "HTTP/1.1 200 OK\r\n"
                    "Content-Type: multipart/x-mixed-replace; boundary=" + String(boundary) + "\r\n\r\n";
  client.print(response);

  while (client.connected()) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) break;

    client.printf("--%s\r\n", boundary);
    client.println("Content-Type: image/jpeg");
    client.printf("Content-Length: %d\r\n\r\n", fb->len);
    client.write(fb->buf, fb->len);
    client.print("\r\n");

    esp_camera_fb_return(fb);
    delay(10);
  }

  client.stop();
}

// --- Einzelbild ---
void handleCapture(WiFiClient client) {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    client.println("HTTP/1.1 500 Internal Server Error\r\n\r\nFehler bei Kamera");
    return;
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/jpeg");
  client.printf("Content-Length: %d\r\n\r\n", fb->len);
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// --- HTML aus SPIFFS laden ---
void serveIndex(WiFiClient client) {
  File file = SPIFFS.open("/index.html");
  if (!file) {
    client.println("HTTP/1.1 500 Internal Server Error\r\n\r\nindex.html nicht gefunden");
    return;
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html\r\n\r\n");

  while (file.available()) {
    client.write(file.read());
  }
  file.close();
  client.stop();
}

// --- Server starten ---
void startCameraServer() {
  server.begin();
  Serial.println("📡 HTTP-Server läuft auf Port 80");

  while (true) {
    WiFiClient client = server.available();
    if (!client) continue;

    String request = client.readStringUntil('\r');
    client.flush();

    if (request.indexOf("GET / ") >= 0) serveIndex(client);
        else if (request.indexOf("GET /style.css") >= 0) {
          File css = SPIFFS.open("/style.css");
          if (css) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/css\r\n\r\n");
            while (css.available()) client.write(css.read());
            css.close();
          } else {
            client.println("HTTP/1.1 404 Not Found\r\n\r\n");
          }
          client.stop();
        }
        else if (request.indexOf("GET /stream") >= 0) handleStream(client);
        else if (request.indexOf("GET /capture") >= 0) handleCapture(client);
    }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nStarte ESP32-CAM...");

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS konnte nicht gestartet werden!");
    while (true) delay(1000);
  }

  if (!initCamera()) {
    Serial.println("Kamera konnte nicht initialisiert werden!");
    while (true) delay(1000);
  }

  WiFi.begin(ssid, password);
  Serial.print("Verbinde mit WLAN ");
  Serial.println(ssid);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("\nWLAN verbunden!");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());

  startCameraServer();
}

void loop() {}