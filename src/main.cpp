#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "FS.h"
#include "SPIFFS.h"
#include "secrets.h"

// --- Pin definitions (AI Thinker) ---
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

// --- LED Pin ---
#define LED_PIN 4
#define LED_MODE LOW

// --- HTTP-Server ---
WiFiServer server(80);

// --- Camera Init (unchanged) ---
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
  config.xclk_freq_hz = 23000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");
    return false;
  }
  return true;
}

// --- Hilfsfunktionen zum Serven ---
void serveIndex(WiFiClient *client) {
  File file = SPIFFS.open("/index.html");
  if (!file) {
    client->println("HTTP/1.1 500 Internal Server Error\r\n\r\n");
    return;
  }

  client->println("HTTP/1.1 200 OK");
  client->println("Content-Type: text/html\r\n\r\n");

  while (file.available()) {
    uint8_t buf[256];
    size_t len = file.read(buf, sizeof(buf));
    client->write(buf, len);
  }
  file.close();
}

void serveCSS(WiFiClient *client) {
  File css = SPIFFS.open("/style.css");
  if (!css) {
    client->println("HTTP/1.1 404 Not Found\r\n\r\n");
    return;
  }
  client->println("HTTP/1.1 200 OK");
  client->println("Content-Type: text/css\r\n\r\n");
  while (css.available()) {
    uint8_t buf[256];
    size_t len = css.read(buf, sizeof(buf));
    client->write(buf, len);
  }
  css.close();
}

// --- LED toggle (HTTP) ---
void ledToggleResponse(WiFiClient *client) {
  // Toggle LED
  digitalWrite(LED_PIN, digitalRead(LED_PIN) == LOW ? HIGH : LOW);

  // Send minimal response
  client->println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nLED toggled");
}

// --- Stream senden (läuft im Client-Task) ---
void handleStreamClient(WiFiClient *client) {
  const char* boundary = "frame";
  String header = "HTTP/1.1 200 OK\r\n"
                  "Content-Type: multipart/x-mixed-replace; boundary=" + String(boundary) + "\r\n\r\n";
  client->print(header);

  while (client->connected()) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera frame failed");
      break;
    }

    // multipart boundary + headers
    client->printf("--%s\r\n", boundary);
    client->println("Content-Type: image/jpeg");
    client->printf("Content-Length: %u\r\n\r\n", (unsigned)fb->len);
    client->write(fb->buf, fb->len);
    client->print("\r\n");

    esp_camera_fb_return(fb);

    // sehr kurzes delay um FreeRTOS anderen Tasks CPU zu geben
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// --- Client-Task: behandelt genau einen Client-Request ---
void clientTask(void * parameter) {
  WiFiClient *client = (WiFiClient*) parameter;
  if (!client) {
    vTaskDelete(NULL);
    return;
  }

  // Wir lesen Header nicht-blockierend bis zum Header-Ende "\r\n\r\n" oder Timeout
  String req = "";
  unsigned long start = millis();
  while (millis() - start < 2000) { // max 2s zum Empfang der Anfrage
    while (client->available()) {
      char c = client->read();
      req += c;
      // End of headers?
      if (req.endsWith("\r\n\r\n")) break;
    }
    if (req.endsWith("\r\n\r\n")) break;
    vTaskDelay(5 / portTICK_PERIOD_MS); // CPU freigeben
  }

  // Fallback: falls komplett leer, beenden
  if (req.length() == 0) {
    client->stop();
    delete client;
    vTaskDelete(NULL);
    return;
  }

  // Bestimme Pfad (sehr einfaches Parsen)
  String firstLine = req.substring(0, req.indexOf("\r\n"));
  // Beispiel: "GET /stream HTTP/1.1"
  String method = firstLine.substring(0, firstLine.indexOf(' '));
  String path = "/";
  int firstSpace = firstLine.indexOf(' ');
  if (firstSpace >= 0) {
    int secondSpace = firstLine.indexOf(' ', firstSpace + 1);
    if (secondSpace > firstSpace) {
      path = firstLine.substring(firstSpace + 1, secondSpace);
    }
  }

  Serial.printf("Client request: %s %s\n", method.c_str(), path.c_str());

  // Route
  if (method == "GET" && (path == "/" || path == "/index.html")) {
    serveIndex(client);
    client->stop();
  }
  else if (method == "GET" && path == "/style.css") {
    serveCSS(client);
    client->stop();
  }
  else if (method == "GET" && path == "/led/toggle") {
    ledToggleResponse(client);
    client->stop();
  }
  else if (method == "GET" && path == "/stream") {
    // Stream bleibt solange verbunden wie der Client will
    handleStreamClient(client);
    client->stop();
  }
  else {
    // 404
    client->println("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot found");
    client->stop();
  }

  // Aufräumen und Task beenden
  delete client;
  vTaskDelete(NULL);
}

// --- Server-Task: akzeptiert Clients und startet je Client eine Task ---
void serverTask(void * parameter) {
  server.begin();
  Serial.println("HTTP-Server is running on port 80 (serverTask)");

  for (;;) {
    WiFiClient client = server.available();
    if (client) {
      // neuen Heap-Client anlegen für Task-Parameter
      WiFiClient *clientPtr = new WiFiClient(client);
      // Erstelle Client-Task (Stack-Size größer für Kamera-Streaming)
      // 8192 ist ein sicherer Wert für Stream-Tasks; ggf. anpassen wenn Speichermangel
      BaseType_t ok = xTaskCreate(
        clientTask,          // Funktion
        "clientTask",        // Name
        8192,                // Stack size
        (void*)clientPtr,    // Parameter
        1,                   // Priority
        NULL                 // Handle
      );
      if (ok != pdPASS) {
        Serial.println("Failed to create clientTask - rejecting client");
        client.stop();
        delete clientPtr;
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS); // kurz warten, damit nicht busy-loop
  }
}

// ----------------------------- Setup / Loop -----------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nStart ESP32-CAM...");

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS couldn't be started!");
    while (true) delay(1000);
  }

  if (!initCamera()) {
    Serial.println("Camera couldn't be initialised!");
    while (true) delay(1000);
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi.");
  Serial.println(ssid);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("\nConnected to WIFI!");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_MODE);

  // Server-Task starten (nicht auf spezifischen Core pinnen)
  xTaskCreate(
    serverTask,
    "serverTask",
    4096,    // Server benötigt weniger Stack
    NULL,
    1,
    NULL
  );

  // loop() kann leer bleiben - FreeRTOS Tasks übernehmen die Arbeit
}

void loop() {
  // leer
}
