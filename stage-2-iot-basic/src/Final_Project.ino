#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <PubSubClient.h>

#define DHTPIN 4
#define DHTTYPE DHT11

#define RED_PIN   25
#define GREEN_PIN 26
#define BLUE_PIN  27
#define BUZZER    14

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(DHTPIN, DHTTYPE);

// WIFI
const char* ssid = "Wifi";
const char* password = "25mei2005oke";

// MQTT
const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
WiFiClient espClient;
PubSubClient client(espClient);

// API Weather
const char* city = "Samarinda";
const char* apiKey = "95be204bd0ec0a72aefbb5d0f298c9d2";

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke MQTT...");
    if (client.connect("ESP32-Inkubator")) {
      Serial.println("Terhubung!");
    } else {
      Serial.print("Gagal. rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

float getOutdoorTemp() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q=Samarinda&appid=95be204bd0ec0a72aefbb5d0f298c9d2&units=metric";
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);
      float temp = doc["main"]["temp"];
      http.end();
      return temp;
    } else {
      Serial.println("Gagal ambil data suhu luar!");
      http.end();
      return NAN;
    }
  }
  return NAN;
}

// --- FUNGSI LABEL SUHU UNTUK DATASET ---
String getLabel(float suhuInkubator, float suhuLuar, float kelembapan) {

  float delta = suhuInkubator - suhuLuar;

  float dinginLimit = 36.8;
  float panasLimit  = 38.2;

  // Penyesuaian suhu luar
  if (suhuLuar >= 32.0) panasLimit -= 0.3;
  else if (suhuLuar <= 25.0) dinginLimit += 0.3;

  // Penyesuaian kelembapan
  if (kelembapan < 45) {        // terlalu kering
    panasLimit -= 0.2;
  } 
  else if (kelembapan > 75) {   // terlalu lembap
    dinginLimit += 0.2;
  }

  if (suhuInkubator < dinginLimit) return "dingin";
  if (suhuInkubator > panasLimit)  return "panas";
  return "normal";
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  Wire.begin(23, 22);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Gagal menampilkan layar!");
    while(true);
  }

  digitalWrite(RED_PIN, HIGH);
  digitalWrite(GREEN_PIN, HIGH);
  digitalWrite(BLUE_PIN, HIGH);

  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nTerhubung!");

  client.setServer(mqttServer, mqttPort);
}

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  float suhuInkubator = dht.readTemperature();
  float kelembapan = dht.readHumidity();
  float suhuLuar = getOutdoorTemp();

  if (isnan(suhuInkubator) || isnan(kelembapan) || isnan(suhuLuar)) {
    Serial.println("Error sensor/API!");
    delay(2000);
    return;
  }

  // Dapatkan label dataset
  String label = getLabel(suhuInkubator, suhuLuar, kelembapan);

  Serial.print("Suhu Inkubator: ");
  Serial.println(suhuInkubator);
  Serial.print("Label: ");
  Serial.println(label);
  Serial.print("Kelembapan: ");
  Serial.println(kelembapan);
  Serial.print("Suhu Luar: ");
  Serial.println(suhuLuar);

  // MQTT JSON
  DynamicJsonDocument dataSend(300);
  dataSend["suhu_inkubator"] = suhuInkubator;
  dataSend["kelembapan"] = kelembapan;
  dataSend["suhu_luar"] = suhuLuar;
  dataSend["label"] = label;

  char buffer[300];
  serializeJson(dataSend, buffer);
  client.publish("Project/Re202/Stage3", buffer);

  // OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Monitoring Suhu Telur");

  display.setCursor(0,15);
  display.print("Inkubator: ");
  display.print(suhuInkubator);
  display.println(" C");

  display.setCursor(0,27);
  display.print("Lembap: ");
  display.print(kelembapan);
  display.println(" %");

  display.setCursor(0,39);
  display.print("Luar: ");
  display.print(suhuLuar);
  display.println(" C");

  display.setCursor(0,51);
  display.print("Label: ");
  display.println(label);

  // LED & BUZZER
  if (suhuInkubator < 36.0) {
    digitalWrite(RED_PIN, LOW);
    digitalWrite(GREEN_PIN, LOW);
    digitalWrite(BLUE_PIN, HIGH);
    tone(BUZZER, 1000, 500);
  }
  else if (suhuInkubator >= 36.0 && suhuInkubator <= 36.9) {
    digitalWrite(RED_PIN, HIGH);
    digitalWrite(GREEN_PIN, HIGH);
    digitalWrite(BLUE_PIN, LOW);
    noTone(BUZZER);
  }
  else if (suhuInkubator > 38.0 && suhuInkubator <= 39.0) {
    digitalWrite(RED_PIN, HIGH);
    digitalWrite(GREEN_PIN, HIGH);
    digitalWrite(BLUE_PIN, LOW);
    noTone(BUZZER);
  }
  else if (suhuInkubator >= 39.0) {
    digitalWrite(RED_PIN, HIGH);
    digitalWrite(GREEN_PIN, LOW);
    digitalWrite(BLUE_PIN, LOW);
    tone(BUZZER, 1000, 500);
  }
  else {
    digitalWrite(RED_PIN, LOW);
    digitalWrite(GREEN_PIN, HIGH);
    digitalWrite(BLUE_PIN, LOW);
    noTone(BUZZER);
  }

  display.display();
  delay(5000);
}
