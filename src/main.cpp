#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include "secrets.h"

// Onboard LED (ESP32 meist GPIO 2)
const int ledPin = 2;
bool ledState = LOW;

AsyncWebServer server(80);

void setup() {
  Serial.begin(9600);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, ledState);

  // SPIFFS starten
  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS Mount failed");
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

  server.begin();
}

void loop() {
  // Nichts nötig, AsyncWebServer läuft im Hintergrund
}
