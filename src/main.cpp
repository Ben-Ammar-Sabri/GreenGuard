#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <time.h>

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
const char* MQTT_SERVER = "broker.emqx.io";
const int MQTT_PORT = 1883;

const char* TOPIC_DATA = "greenguard/data";
const char* TOPIC_CONTROL = "greenguard/control";
const char* TOPIC_ALARM = "greenguard/alarm";
const char* TOPIC_SETTINGS = "greenguard/settings";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;
bool demoTimeMode = false;
int demoHour = 12;

#define PIN_DHT 15
#define PIN_LDR 34
#define PIN_PIR 13
#define PIN_SERVO 18
#define PIN_RELAY_HEAT 26
#define PIN_RELAY_PUMP 27
#define PIN_LED_GROW 14
#define PIN_BUZZER 25

float TEMP_MIN = 18.0;
float TEMP_MAX = 28.0;
float HUM_MIN = 40.0;
int LIGHT_THRESHOLD = 500;

DHT dht(PIN_DHT, DHT22);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo myServo;
WiFiClient espClient;
PubSubClient client(espClient);

float temp = 0;
float hum = 0;
int lightLevel = 0;
bool motionDetected = false;

bool heaterState = false;
bool pumpState = false;
bool lightState = false;
bool fanOpen = false;

bool autoMode = true;
bool manualFan = false;
bool manualPump = false;
bool manualHeat = false;
bool manualLight = false;
bool simulationMode = false;

unsigned long lastTelemetry = 0;
unsigned long lastSensorRead = 0;
unsigned long pumpStartTime = 0;
bool pumpRunning = false;
unsigned long lastMqttReconnectAttempt = 0;

void setupWiFi();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void readSensors();
void logicClimate();
void logicIrrigation();
void logicLighting();
void logicSecurity();
void updateLCD();
void sendTelemetry();
void setupTime();
bool isNight();
int rawToLux(int raw);

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);

  pinMode(PIN_RELAY_HEAT, OUTPUT);
  pinMode(PIN_RELAY_PUMP, OUTPUT);
  pinMode(PIN_LED_GROW, OUTPUT);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  dht.begin();
  myServo.attach(PIN_SERVO);
  myServo.write(0);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("GreenGuard Init");

  setupWiFi();
  setupTime();
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(mqttCallback);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) setupWiFi();
  if (!client.connected()) connectMQTT();
  client.loop();

  unsigned long now = millis();

  if (now - lastSensorRead > 1000) {
    readSensors();
    lastSensorRead = now;

    if (autoMode) {
      logicClimate();
      logicIrrigation();
      logicLighting();
    } else {
      digitalWrite(PIN_RELAY_HEAT, manualHeat ? HIGH : LOW);
      digitalWrite(PIN_RELAY_PUMP, manualPump ? HIGH : LOW);
      digitalWrite(PIN_LED_GROW, manualLight ? HIGH : LOW);
      myServo.write(manualFan ? 90 : 0);
      heaterState = manualHeat;
      pumpState = manualPump;
      lightState = manualLight;
      fanOpen = manualFan;
    }
  }

  logicSecurity();

  if (now - lastTelemetry > 1000) {
    updateLCD();
    sendTelemetry();
    lastTelemetry = now;
  }
}

int rawToLux(int raw) {
  raw = 4095 - raw;
  if (raw <= 0) return 0;
  if (raw >= 4095) return 100000;

  float voltage = raw / 4095.0 * 3.3;
  float r_ldr = (3.3 * 10000.0 / voltage) - 10000.0;
  if (r_ldr < 0) r_ldr = 0;

  float lux = 10.0 * pow(r_ldr / 50000.0, -1.0 / 0.7);
  return (int)lux;
}

void readSensors() {
  motionDetected = digitalRead(PIN_PIR);

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int raw = analogRead(PIN_LDR);
  int realLux = rawToLux(raw);

  if (!simulationMode) {
    if (!isnan(t)) temp = t;
    if (!isnan(h)) hum = h;
    lightLevel = realLux;
  } else {
    if (heaterState) temp += 0.1;
    if (fanOpen) temp -= 0.1;
    if (lightState) temp += 0.05;

    if (pumpState) hum += 0.5;
    if (fanOpen) hum -= 0.2;

    if (lightState) lightLevel += 50;
    
    if (temp > 50.0) temp = 50.0;
    if (temp < 0.0) temp = 0.0;
    if (hum > 100.0) hum = 100.0;
    if (hum < 0.0) hum = 0.0;
    if (lightLevel > 50000) lightLevel = 50000;
    if (lightLevel < 0) lightLevel = 0;
  }
}

void logicClimate() {
  if (temp < TEMP_MIN) {
    digitalWrite(PIN_RELAY_HEAT, HIGH);
    heaterState = true;
  } else if (temp > TEMP_MIN + 1.0) {
    digitalWrite(PIN_RELAY_HEAT, LOW);
    heaterState = false;
  }

  if (temp > TEMP_MAX) {
    myServo.write(90);
    fanOpen = true;
    digitalWrite(PIN_RELAY_HEAT, LOW);
    heaterState = false;
  } else if (temp < TEMP_MAX - 1.0) {
    myServo.write(0);
    fanOpen = false;
  }
}

void logicIrrigation() {
  if (hum < HUM_MIN) {
    digitalWrite(PIN_RELAY_PUMP, HIGH);
    pumpState = true;
  } else {
    digitalWrite(PIN_RELAY_PUMP, LOW);
    pumpState = false;
  }
}

void logicLighting() {
  if (!isNight() && lightLevel < LIGHT_THRESHOLD && temp < TEMP_MAX) {
    digitalWrite(PIN_LED_GROW, HIGH);
    lightState = true;
  } else {
    digitalWrite(PIN_LED_GROW, LOW);
    lightState = false;
  }
}

void logicSecurity() {
  static bool alarmActive = false;
  bool currentMotion = digitalRead(PIN_PIR);

  if (isNight() || simulationMode) {
    if (currentMotion && !alarmActive) {
      client.publish(TOPIC_ALARM, "ON");
      tone(PIN_BUZZER, 1000);
      alarmActive = true;
      lcd.clear();
      lcd.print("!! ALERTE !!");
      lcd.setCursor(0, 1);
      lcd.print("INTRUSION");
    } else if (!currentMotion && alarmActive) {
      client.publish(TOPIC_ALARM, "OFF");
      noTone(PIN_BUZZER);
      alarmActive = false;
      lcd.clear();
    }
  } else {
    if (alarmActive) {
      client.publish(TOPIC_ALARM, "OFF");
      noTone(PIN_BUZZER);
      alarmActive = false;
      lcd.clear();
    }
  }
}

void setupTime() {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  time_t now = time(nullptr);
  int retry = 0;
  while (now < 8 * 3600 * 2 && retry < 20) {
    delay(500);
    now = time(nullptr);
    retry++;
  }
}

bool isNight() {
  if (demoTimeMode) {
    return (demoHour >= 22 || demoHour < 6);
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 50)) {
    return false;
  }

  return (timeinfo.tm_hour >= 22 || timeinfo.tm_hour < 6);
}

void updateLCD() {
  lcd.setCursor(0, 0);
  lcd.printf("T:%.1f H:%.0f %s", temp, hum, isNight() ? "Nuit" : "Jour");
  lcd.setCursor(0, 1);
  lcd.printf("Lux:%d %s", lightLevel, fanOpen ? "Opn" : "Cls");
}

void sendTelemetry() {
  StaticJsonDocument<512> doc;
  doc["temp"] = temp;
  doc["hum"] = hum;
  doc["lux"] = lightLevel;
  doc["heat"] = heaterState;
  doc["pump"] = pumpState;
  doc["fan"] = fanOpen;
  doc["light"] = lightState;
  doc["auto"] = autoMode;
  doc["motion"] = motionDetected;

  if (demoTimeMode) {
    char timeStr[16];
    sprintf(timeStr, "%02d:00:00", demoHour);
    doc["time"] = timeStr;
  } else {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
      char timeStr[16];
      strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
      doc["time"] = timeStr;
    } else {
      doc["time"] = "--:--:--";
    }
  }
  doc["isNight"] = isNight();

  char buffer[512];
  serializeJson(doc, buffer);
  client.publish(TOPIC_DATA, buffer);
}

void setupWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed");
  }
}

void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;

  unsigned long now = millis();
  if (now - lastMqttReconnectAttempt > 5000) {
    lastMqttReconnectAttempt = now;
    Serial.print("Attempting MQTT connection...");
    String clientId = "GreenGuard_" + String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(TOPIC_CONTROL);
      client.subscribe(TOPIC_SETTINGS);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) msg += (char)payload[i];

  if (String(topic) == TOPIC_CONTROL) {
    if (msg == "AUTO") autoMode = true;
    if (msg == "MANUAL") autoMode = false;

    if (!autoMode) {
      if (msg == "FAN_ON") manualFan = true;
      if (msg == "FAN_OFF") manualFan = false;
      if (msg == "HEAT_ON") manualHeat = true;
      if (msg == "HEAT_OFF") manualHeat = false;
      if (msg == "PUMP_ON") manualPump = true;
      if (msg == "PUMP_OFF") manualPump = false;
      if (msg == "LIGHT_ON") manualLight = true;
      if (msg == "LIGHT_OFF") manualLight = false;
    }

    if (msg == "TIME_DEMO_ON") demoTimeMode = true;
    if (msg == "TIME_DEMO_OFF") demoTimeMode = false;
    if (msg.startsWith("SET_HOUR:")) {
      demoHour = msg.substring(9).toInt();
    }

    if (msg == "SIM_ON") simulationMode = true;
    if (msg == "SIM_OFF") simulationMode = false;

    if (autoMode) {
      logicClimate();
      logicIrrigation();
      logicLighting();
      logicSecurity();
    }
    sendTelemetry();
  } else if (String(topic) == TOPIC_SETTINGS) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, msg);
    if (!error) {
      if (doc.containsKey("min_t")) TEMP_MIN = doc["min_t"];
      if (doc.containsKey("max_t")) TEMP_MAX = doc["max_t"];
      if (doc.containsKey("min_h")) HUM_MIN = doc["min_h"];
      if (doc.containsKey("min_l")) LIGHT_THRESHOLD = doc["min_l"];

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("CONFIG UPDATE!");
      lcd.setCursor(0, 1);
      lcd.print("T:" + String(TEMP_MIN, 1) + "-" + String(TEMP_MAX, 1));

      if (autoMode) {
        logicClimate();
        logicIrrigation();
        logicLighting();
      }

      delay(2000);
      lcd.clear();
    }
  }
}
