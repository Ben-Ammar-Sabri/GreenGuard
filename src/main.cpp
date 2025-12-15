#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// --- Configuration Wi-Fi & MQTT ---
// POUR WOKWI : Utilisez "Wokwi-GUEST" et ""
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

const char* MQTT_SERVER = "3.68.37.209"; // IP Fixe HiveMQ (bypass DNS)
const int MQTT_PORT = 1883;

// Topics MQTT
const char* TOPIC_DATA = "greenguard/data";
const char* TOPIC_CONTROL = "greenguard/control";
const char* TOPIC_ALARM = "greenguard/alarm";
const char* TOPIC_SETTINGS = "greenguard/settings";
const char* TOPIC_SIMULATION = "greenguard/simulation";

// --- God Mode Vars ---
bool simMode = false;
float simTemp = 20.0;
float simHum = 50.0;
int simLux = 2000;


// --- Pin Definitions (Doit correspondre à diagram.json) ---
#define PIN_DHT 15
#define PIN_LDR 34     // Analog
#define PIN_PIR 13
#define PIN_SERVO 18
#define PIN_RELAY_HEAT 26
#define PIN_RELAY_PUMP 27
#define PIN_LED_GROW 14

// --- Seuils (Configurables) ---
float TEMP_MIN = 18.0;      // En dessous, chauffage
float TEMP_MAX = 28.0;      // Au dessus, ventilation
float HUM_MIN = 40.0;       // En dessous, arrosage
int LIGHT_THRESHOLD = 2000; // Seuil obscurité (0-4095, inversé sur Wokwi parfois)

// --- Objets ---
DHT dht(PIN_DHT, DHT22);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo myServo;
WiFiClient espClient;
PubSubClient client(espClient);

// --- Variables d'État ---
float temp = 0;
float hum = 0;
int lightLevel = 0;
bool motionDetected = false;

// États des Actionneurs
bool heaterState = false;
bool pumpState = false;
bool lightState = false;
bool fanOpen = false;

// Modes (Auto / Manuel)
bool autoMode = true; 
// En mode manuel, ces flags pilotent les actionneurs :
bool manualFan = false;
bool manualPump = false;
bool manualHeat = false;

// Timers
unsigned long lastTelemetry = 0;
unsigned long lastSensorRead = 0;
unsigned long pumpStartTime = 0;
bool pumpRunning = false;

// --- Prototypes ---
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

void setup() {
  Serial.begin(115200);
  
  // Init Pins
  pinMode(PIN_RELAY_HEAT, OUTPUT);
  pinMode(PIN_RELAY_PUMP, OUTPUT);
  pinMode(PIN_LED_GROW, OUTPUT);
  pinMode(PIN_PIR, INPUT);
  
  // Init Sensors/Actuators
  dht.begin();
  myServo.attach(PIN_SERVO);
  myServo.write(0); // Fermé
  
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("GreenGuard Init");
  
  setupWiFi();
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(mqttCallback);
}

void loop() {
  // 1. Maintain Connection
  if (WiFi.status() != WL_CONNECTED) setupWiFi();
  if (!client.connected()) connectMQTT();
  client.loop();

  unsigned long now = millis();

  // 2. Read Sensors (Every 2s)
  if (now - lastSensorRead > 2000) {
    readSensors();
    lastSensorRead = now;
    
    if (autoMode) {
      logicClimate();
      logicIrrigation();
      logicLighting();
    } else {
      // Manual Mode Logic Application
      digitalWrite(PIN_RELAY_HEAT, manualHeat ? HIGH : LOW);
      digitalWrite(PIN_RELAY_PUMP, manualPump ? HIGH : LOW);
      myServo.write(manualFan ? 90 : 0);
      heaterState = manualHeat;
      pumpState = manualPump;
      fanOpen = manualFan;
    }
  }

  // 3. Security (Toujours actif, même en manuel)
  logicSecurity();

  // 4. Update LCD & Telemetry (Every 1s)
  if (now - lastTelemetry > 1000) {
    updateLCD();
    sendTelemetry();
    lastTelemetry = now;
  }
}


void readSensors() {
  // Mode Simulation (Override)
  if (simMode) {
      temp = simTemp;
      hum = simHum;
      lightLevel = simLux;
      return; // On ignore les vrais capteurs
  }

  // Vrais Capteurs
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  
  if (!isnan(t)) temp = t;
  if (!isnan(h)) hum = h;
  
  lightLevel = analogRead(PIN_LDR);
  motionDetected = digitalRead(PIN_PIR);
}

void logicClimate() {
  // Chauffage
  if (temp < TEMP_MIN) {
    digitalWrite(PIN_RELAY_HEAT, HIGH);
    heaterState = true;
  } else if (temp > TEMP_MIN + 1.0) { // Hysteresis
    digitalWrite(PIN_RELAY_HEAT, LOW);
    heaterState = false;
  }

  // Ventilation (Toit)
  if (temp > TEMP_MAX) {
    myServo.write(90); // Open
    fanOpen = true;
    // Sécurité: Si on ventile, on coupe le chauffage absurdement actif
    digitalWrite(PIN_RELAY_HEAT, LOW);
    heaterState = false; 
  } else if (temp < TEMP_MAX - 1.0) {
    myServo.write(0); // Close
    fanOpen = false;
  }
}

void logicIrrigation() {
  // Arrosage simple : Si trop sec, on active 5s
  if (hum < HUM_MIN && !pumpRunning) {
    Serial.println(">> Arrosage Auto Activé");
    digitalWrite(PIN_RELAY_PUMP, HIGH);
    pumpState = true;
    pumpRunning = true;
    pumpStartTime = millis();
  }
  
  // Arrêt auto après 5s
  if (pumpRunning && millis() - pumpStartTime > 5000) {
    digitalWrite(PIN_RELAY_PUMP, LOW);
    pumpState = false;
    pumpRunning = false;
    Serial.println(">> Arrosage Terminé");
  }
}

void logicLighting() {
  // Wokwi LDR: 0 = Dark, 4095 = Bright
  // Constraint: LIGHT ON only if Dark AND Temp is safe (Lights add heat)
  if (lightLevel < LIGHT_THRESHOLD && temp < TEMP_MAX) { 
    // Sombre + Pas trop chaud => Allumage
    digitalWrite(PIN_LED_GROW, HIGH);
    lightState = true;
  } else {
    // Jour OU Trop chaud => Extinction
    digitalWrite(PIN_LED_GROW, LOW);
    lightState = false;
  }
}

void logicSecurity() {
  static bool lastPIR = false;
  if (digitalRead(PIN_PIR) && !lastPIR) {
    Serial.println("!!! INTRUSION DETECTEE !!!");
    client.publish(TOPIC_ALARM, "Mouvement détecté dans la serre !");
    
    // Flash LCD
    lcd.clear();
    lcd.print("!! ALERTE !!");
    lcd.setCursor(0,1);
    lcd.print("INTRUSION");
    delay(2000); // Bloquant court acceptable pour alerte
    lastPIR = true;
  } else if (!digitalRead(PIN_PIR)) {
    lastPIR = false;
  }
}

void updateLCD() {
  lcd.setCursor(0, 0);
  lcd.printf("T:%.1fC H:%.0f%%", temp, hum);
  lcd.setCursor(0, 1);
  lcd.printf("L:%d %s", lightLevel, fanOpen ? "Open" : "Clsd");
}

void sendTelemetry() {
  StaticJsonDocument<300> doc;
  doc["temp"] = temp;
  doc["hum"] = hum;
  doc["lux"] = lightLevel;
  doc["heat"] = heaterState;
  doc["pump"] = pumpState;
  doc["fan"] = fanOpen;
  doc["light"] = lightState;
  doc["auto"] = autoMode;
  
  char buffer[512];
  serializeJson(doc, buffer);
  client.publish(TOPIC_DATA, buffer);
}

void setupWiFi() {
  if(WiFi.status() == WL_CONNECTED) return;
  
  Serial.print("WiFi Connecting to ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " OK" : " Fail");
}

void connectMQTT() {
  while (!client.connected()) {
    String clientId = "GreenGuard_" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("MQTT Connected");
      client.subscribe(TOPIC_CONTROL);
      client.subscribe(TOPIC_SETTINGS);
      client.subscribe(TOPIC_SIMULATION);
    } else {
      Serial.print("."); // Feedback visuel d'attente
      Serial.print("Echec MQTT, rc=");
      Serial.print(client.state());
      Serial.println(" (Retrying in 2s)");
      delay(2000);
    }
  }
}





// ... inside mqttCallback ...
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.print("Msg ["); Serial.print(topic); Serial.print("]: "); Serial.println(msg);

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
      }
  }
  else if (String(topic) == TOPIC_SETTINGS) {
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, msg);
      if (!error) {
          if (doc.containsKey("min_t")) TEMP_MIN = doc["min_t"];
          if (doc.containsKey("max_t")) TEMP_MAX = doc["max_t"];
          if (doc.containsKey("min_h")) HUM_MIN = doc["min_h"];
          if (doc.containsKey("min_l")) LIGHT_THRESHOLD = doc["min_l"];
          
          Serial.println("Settings Updated!");
          
          // Feedback LCD
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("CONFIG UPDATE!");
          lcd.setCursor(0, 1);
          lcd.print("T:" + String(TEMP_MIN, 1) + "-" + String(TEMP_MAX, 1));
          
          // Force apply logic immediately
          if(autoMode) {
             logicClimate();
             logicIrrigation();
             logicLighting();
          }
          
          delay(2000); // Show config for 2s
          lcd.clear(); // Clear before returning to loop
      }
  }
  else if (String(topic) == TOPIC_SIMULATION) {
      StaticJsonDocument<200> doc;
      deserializeJson(doc, msg);
      
      if (doc.containsKey("active")) simMode = doc["active"];
      if (simMode) {
          if (doc.containsKey("t")) simTemp = doc["t"];
          if (doc.containsKey("h")) simHum = doc["h"];
          if (doc.containsKey("l")) simLux = doc["l"];

          
          // Force Update logic with new fake values
          lcd.clear();
          lcd.setCursor(0,0); 
          lcd.print("SIMULATION MODE");
          
          // Apply logic immediately
          if(doc.containsKey("t")) temp = simTemp;
          if(doc.containsKey("h")) hum = simHum;
          if(doc.containsKey("l")) lightLevel = simLux;
          
          logicClimate();
          logicIrrigation();
          logicLighting();
          
          updateLCD();
          sendTelemetry(); // FORCE IMMEDIATE UPDATE
      }
  }
}
