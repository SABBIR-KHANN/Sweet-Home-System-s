#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
// ---------------- WIFI + MQTT CONFIG ----------------
const char* ssid = "************";
const char* password = "*************";
const char* mqtt_server = "*************";
const int mqtt_port = 1883;
const char* mqtt_control_topic = "home/relay/cmd";
WiFiClient espClient;
PubSubClient client(espClient);
// ---------------- TIME CONFIG ----------------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 6 * 3600);
// ---------------- SENSOR PINS ----------------
#define ONE_WIRE_BUS D4
#define PIR_PIN D5
#define PIR_PIN2 D6
#define RELAY_PIN D7
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
// ---------------- VARIABLES ----------------
bool motionDetected = false;
bool relayState = false;
unsigned long relayOffTime = 0;
int extendMode = 0;
unsigned long lastPublish = 0;
unsigned long lastMotionTime = 0; // Track last motion time
unsigned long lastMqttCheck = 0;  // Track last MQTT attempt
const unsigned long mqttCheckInterval = 5000;  // Check every 5s
// ---------------- MQTT CALLBACK ----------------
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  Serial.println(message);

  if (message == "1") {
    relayState = true;
    digitalWrite(RELAY_PIN, LOW);
    relayOffTime = 0; // No auto off for manual control
    extendMode = 0;
    client.publish("home/light", "Light turned ON (Manual)");
    Serial.println("Manual: Relay ON");
  } else if (message == "0") {
    relayState = false;
    digitalWrite(RELAY_PIN, HIGH);
    relayOffTime = 0;
    extendMode = 0;
    client.publish("home/light", "Light turned OFF (Manual)");
    Serial.println("Manual: Relay OFF");
  }
}
// ---------------- WIFI CONNECT ----------------
void setup_wifi() {
  delay(10);
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}
// ---------------- MQTT CHECK ----------------
void checkMqtt() {
  Serial.print("MQTT check...");
  if (client.connect("ESP8266_PIR_Temp")) {
    Serial.println("connected");
    client.subscribe(mqtt_control_topic);  // Re-subscribe for "1"/"0" control
  } else {
    Serial.print("failed, rc=");
    Serial.println(client.state());
  }
}
// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  sensors.begin();
  pinMode(PIR_PIN, INPUT);
  pinMode(PIR_PIN2, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  Serial.println("System started - Relay OFF (Active LOW logic)");
  timeClient.begin();
  checkMqtt();  // Initial connect
}
// ---------------- MAIN LOOP ----------------
void loop() {
  // Non-blocking MQTT check every 5s
  if (millis() - lastMqttCheck > mqttCheckInterval) {
    lastMqttCheck = millis();
    if (!client.connected()) {
      checkMqtt();
    }
    client.loop();  // Process messages if connected
  }
  timeClient.update();
  int pirValue = digitalRead(PIR_PIN);
  int pirValue2 = digitalRead(PIR_PIN2);
  sensors.requestTemperatures();
  float temperatureC = sensors.getTempCByIndex(0);
  // publish every 10 sec (only if connected)
  if (millis() - lastPublish > 10000 && client.connected()) {
    lastPublish = millis();
     client.publish("home/pir", pirValue ? "Motion" : "No Motion");
     client.publish("home/pir2", pirValue2 ? "Motion" : "No Motion");
    char tempString[10];
    dtostrf(temperatureC, 1, 2, tempString);
    client.publish("home/temperature", tempString);
  }
  // -------- TIME CHECK (Night Only) --------
  int currentHour = timeClient.getHours();
  bool isNight = (currentHour >= 17 || currentHour < 6);  // 5 PM to 6 AM
  // -------- MOTION HANDLING --------
  if (isNight && (pirValue == HIGH || pirValue2==HIGH)) {
    // Only consider new motion if it's been at least 10 seconds since last motion
    if (millis() - lastMotionTime > 10000) {
      motionDetected = true;
      lastMotionTime = millis();
     
      if (!relayState) {
        // First motion - turn ON for 30 seconds
        relayState = true;
        digitalWrite(RELAY_PIN, LOW);
        relayOffTime = millis() + (30UL * 1000UL); // 30 seconds
        extendMode = 0;
        if (client.connected()) client.publish("home/light", "Light turned ON (30 sec)");
        Serial.println("✅ Relay ON - Motion Detected (30 sec)");
      } else {
        // Additional motion while already ON - extend time
        if (extendMode == 0) {
          // First extension: 60 seconds total
          relayOffTime = millis() + (60UL * 1000UL); // 60 seconds
          extendMode = 1;
          if (client.connected()) client.publish("home/light", "Extended to 60 seconds");
          Serial.println("⏰ Extended to 60 seconds");
        } else {
          // Subsequent extensions: +30 seconds each time
          relayOffTime += (30UL * 1000UL); // +30 seconds
          extendMode++;
          if (client.connected()) client.publish("home/light", "Extended +30 seconds");
          Serial.println("⏰ Extended +30 seconds");
        }
      }
    }
  }
  // -------- RELAY OFF TIMER --------
  if (relayState && relayOffTime != 0 && millis() > relayOffTime) {
    relayState = false;
    digitalWrite(RELAY_PIN, HIGH);
    extendMode = 0; // Reset extension counter
    if (client.connected()) client.publish("home/light", "Light turned OFF");
    Serial.println("❌ Relay OFF - Timer expired");
  }
}