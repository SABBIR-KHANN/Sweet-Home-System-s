#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// WiFi credentials - replace with your own
const char* ssid = "*********";
const char* password = "*******";

// MQTT settings - replace broker if needed (this is a public test broker)
const char* mqtt_server = "*********";
const int mqtt_port = 1883;
const char* mqtt_client_id = "ESP8266_Relay";
const char* mqtt_topic = "home/relay";  // Topic to subscribe to for commands (0 or 1)

// Pins
#define RELAY_PIN D7   // GPIO13
#define GND_SENSE_PIN D3  // GPIO0

// MQTT client
WiFiClient espClient;
PubSubClient client(espClient);

// State variables
bool last_mqtt_on = false;
bool mqtt_received = false;
bool relay_on = false;
bool prev_grounded = false;

void setup() {
  Serial.begin(115200);  // For debugging, optional
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(GND_SENSE_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, HIGH);  // Start with relay OFF (HIGH = OFF)
  relay_on = false;

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected - IP: ");
  Serial.println(WiFi.localIP());

  // Setup MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
}

void loop() {
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();

  // Read sensor
  int sensorValue = digitalRead(GND_SENSE_PIN);
  bool now_grounded = (sensorValue == LOW);

  // Calculate desired state based on grounded status (before checking transition)
  bool desired_on;
  if (now_grounded) {
    desired_on = mqtt_received ? last_mqtt_on : true;  // Default ON if grounded and no MQTT
  } else {
    desired_on = mqtt_received ? last_mqtt_on : false;  // Default OFF if not grounded and no MQTT
  }

  // Check for transition and toggle if changed
  if (now_grounded != prev_grounded) {
    desired_on = !relay_on;  // Toggle the current relay state
    last_mqtt_on = desired_on;  // Update last MQTT to reflect the toggled state
    prev_grounded = now_grounded;
  }

  // Apply desired state if changed
  if (desired_on != relay_on) {
    relay_on = desired_on;
    digitalWrite(RELAY_PIN, relay_on ? LOW : HIGH);  // LOW = ON, HIGH = OFF
    Serial.print("Relay state changed to: ");
    Serial.println(relay_on ? "ON" : "OFF");  // Debug
  }

  delay(50);  // Debounce delay
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("MQTT message arrived: ");
  Serial.println(message);

  if (message == "1") {
    last_mqtt_on = true;
  } else if (message == "0") {
    last_mqtt_on = false;
  }
  mqtt_received = true;
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(mqtt_client_id)) {
      Serial.println("connected");
      client.subscribe(mqtt_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}