#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

//SENSOR
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

//LED & BUZZER
#define RED_PIN   25
#define GREEN_PIN 26
#define BLUE_PIN  27
#define BUZZER    14

//OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//WIFI
const char* ssid = "Wifi";
const char* pass = "25mei2005oke";

//MQTT
const char* BROKER = "broker.hivemq.com";
const char* TOPIK_SENSOR   = "Project/Re202/Sensor";              // ESP32 → Python (data sensor)
const char* TOPIK_ML_RESULT = "Project/Re202/Dataset/OutputML";   // Python → ESP32 (hasil ML)
const char* TOPIK_OUTPUT   = "Project/Re202/Output/Sensor";       // ESP32 → Python (output lengkap)
const char* TOPIK_CONTROL  = "Project/Re202/Control";             // Python → ESP32 (kontrol buzzer/led/oled)

WiFiClient esp;
PubSubClient client(esp);

//VARIABEL DATA
float suhu = 0;
float lembap = 0;
float suhuLuar = 0;
String labelML = "-";
String timestampML = "-";

//KONTROL ON/OFF
bool buzzerEnabled = true;
bool ledEnabled = true;
bool oledEnabled = true;

unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 3000;


//FUNGSI AMBIL SUHU LUAR DARI OPENWEATHER API
float getOutdoorTemp() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q=Samarinda&appid=95be204bd0ec0a72aefbb5d0f298c9d2&units=metric";

    http.begin(url);
    http.setTimeout(5000);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);

      float temp = doc["main"]["temp"];
      http.end();
      return temp;
    } else {
      Serial.println("API gagal diakses!");
      http.end();
      return NAN;
    }
  }
  return NAN;
}


//RGB + BUZZER Berdasarkan LABEL dari ML
void setRGBandBuzzer(String label) {
  // LED Control
  if (ledEnabled) {
    if (label == "panas") {
      digitalWrite(RED_PIN, HIGH);
      digitalWrite(GREEN_PIN, LOW);
      digitalWrite(BLUE_PIN, LOW);
    }
    else if (label == "dingin") {
      digitalWrite(RED_PIN, LOW);
      digitalWrite(GREEN_PIN, LOW);
      digitalWrite(BLUE_PIN, HIGH);
    }
    else { // normal
      digitalWrite(RED_PIN, LOW);
      digitalWrite(GREEN_PIN, HIGH);
      digitalWrite(BLUE_PIN, LOW);
    }
  } else {
    // LED OFF
    digitalWrite(RED_PIN, LOW);
    digitalWrite(GREEN_PIN, LOW);
    digitalWrite(BLUE_PIN, LOW);
  }

  // Buzzer Control
  if (buzzerEnabled) {
    if (label == "panas") {
      tone(BUZZER, 900);
    }
    else if (label == "dingin") {
      tone(BUZZER, 1200);
    }
    else {
      noTone(BUZZER);
    }
  } else {
    noTone(BUZZER);
  }
}


//TAMPILKAN OLED
void tampilOLED() {
  if (!oledEnabled) {
    display.clearDisplay();
    display.display();
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setCursor(10, 0);
  display.setTextSize(1);
  display.println("PEMANTAUAN INKUBATOR");
  
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // Data
  display.setTextSize(1);
  display.setCursor(0, 15);
  display.print("Suhu Ink : ");
  display.print(suhu, 1);
  display.println(" C");
  
  display.setCursor(0, 27);
  display.print("Lembap   : ");
  display.print(lembap, 1);
  display.println(" %");
  
  display.setCursor(0, 39);
  display.print("Suhu Luar: ");
  display.print(suhuLuar, 1);
  display.println(" C");
  
  display.setCursor(0, 51);
  display.print("Label    : ");
  display.println(labelML);

  display.display();
}


//KIRIM DATA OUTPUT LENGKAP KE PYTHON (STREAMLIT)
void kirimOutputData() {
  StaticJsonDocument<400> doc;
  doc["timestamp"] = timestampML;
  doc["suhu_inkubator"] = suhu;
  doc["kelembapan"] = lembap;
  doc["suhu_luar"] = suhuLuar;
  doc["label"] = labelML;
  doc["buzzer_status"] = buzzerEnabled ? "ON" : "OFF";
  doc["led_status"] = ledEnabled ? "ON" : "OFF";
  doc["oled_status"] = oledEnabled ? "ON" : "OFF";

  char buffer[400];
  serializeJson(doc, buffer);

  bool published = client.publish(TOPIK_OUTPUT, buffer, true);  // retained = true

  Serial.println("\n=== OUTPUT DIKIRIM KE STREAMLIT ===");
  Serial.println(buffer);
  Serial.print("Status: ");
  Serial.println(published ? "SUCCESS" : "FAILED");
  Serial.println("====================================");
}


//CALLBACK - TERIMA HASIL ML & KONTROL DARI PYTHON
void callback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  
  String jsonStr;
  for (int i = 0; i < length; i++) {
    jsonStr += (char)payload[i];
  }

  Serial.println("\n=== PESAN MQTT DITERIMA ===");
  Serial.print("Topic: ");
  Serial.println(topicStr);
  Serial.print("Payload: ");
  Serial.println(jsonStr);
  Serial.println("===========================");

  //TERIMA HASIL ML DARI PYTHON
  if (topicStr == TOPIK_ML_RESULT) {
    Serial.println("\n=== HASIL ML DITERIMA ===");
    Serial.println(jsonStr);
    Serial.println("=========================");

    StaticJsonDocument<400> doc;
    deserializeJson(doc, jsonStr);

    suhu = doc["suhu_inkubator"];
    lembap = doc["kelembapan"];
    suhuLuar = doc["suhu_luar"];
    labelML = String((const char*)doc["label"]);
    timestampML = String((const char*)doc["timestamp"]);

    setRGBandBuzzer(labelML);
    tampilOLED();

    // Kirim output lengkap ke Streamlit SETIAP KALI ADA UPDATE
    kirimOutputData();
  }

  //TERIMA KONTROL ON/OFF DARI STREAMLIT
  else if (topicStr == TOPIK_CONTROL) {
    Serial.println("\n=== KONTROL DITERIMA ===");
    Serial.println(jsonStr);
    Serial.println("========================");

    StaticJsonDocument<200> doc;
    deserializeJson(doc, jsonStr);

    if (doc.containsKey("buzzer")) {
      buzzerEnabled = doc["buzzer"];
      Serial.print("Buzzer: ");
      Serial.println(buzzerEnabled ? "ON" : "OFF");
    }

    if (doc.containsKey("led")) {
      ledEnabled = doc["led"];
      Serial.print("LED: ");
      Serial.println(ledEnabled ? "ON" : "OFF");
    }

    if (doc.containsKey("oled")) {
      oledEnabled = doc["oled"];
      Serial.print("OLED: ");
      Serial.println(oledEnabled ? "ON" : "OFF");
    }

    // Update display sesuai kontrol
    setRGBandBuzzer(labelML);
    tampilOLED();
    
    // KIRIM KONFIRMASI STATUS KEMBALI KE STREAMLIT
    kirimOutputData();
  }
}


//RECONNECT MQTT
void reconnect() {
  int attempts = 0;
  while (!client.connected() && attempts < 3) {
    Serial.print("Menghubungkan MQTT...");
    
    String clientId = "ESP32-Inkubator-" + String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println(" Tersambung!");
      
      // Subscribe ke topic hasil ML dan kontrol
      client.subscribe(TOPIK_ML_RESULT);
      client.subscribe(TOPIK_CONTROL);
      
      Serial.println("Subscribe ke:");
      Serial.print("  - ");
      Serial.println(TOPIK_ML_RESULT);
      Serial.print("  - ");
      Serial.println(TOPIK_CONTROL);
      
      Serial.println("\nMenunggu data...");
      break;
    } else {
      Serial.print(" Gagal, rc=");
      Serial.print(client.state());
      Serial.println(" Retry...");
      attempts++;
      delay(1000);
    }
  }
}


//SETUP

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n========================================");
  Serial.println("    ESP32 OUTPUT - SISTEM INKUBATOR    ");
  Serial.println("========================================");

  // Setup Pin
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // Matikan semua di awal
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BLUE_PIN, LOW);
  noTone(BUZZER);

  // Setup OLED
  Wire.begin(23, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED gagal diinisialisasi!");
  } else {
    Serial.println("✓ OLED berhasil diinisialisasi");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Sistem Inkubator");
    display.setCursor(0, 20);
    display.println("Menghubungkan WiFi...");
    display.display();
  }

  // Setup WiFi
  WiFi.begin(ssid, pass);
  Serial.print("Menghubungkan WiFi");
  
  int wifiAttempt = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempt < 20) {
    delay(500);
    Serial.print(".");
    wifiAttempt++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi terhubung!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n✗ WiFi gagal terhubung!");
  }

  // Setup MQTT
  client.setServer(BROKER, 1883);
  client.setCallback(callback);
  client.setBufferSize(512);
  client.setKeepAlive(15); 
  client.setSocketTimeout(5);  // Timeout 5 detik

  // Setup DHT
  dht.begin();
  Serial.println("✓ Sensor DHT11 diinisialisasi");

  Serial.println("========================================");
  Serial.println("Sistem siap!\n");
}


//LOOP
void loop() {
  // Reconnect MQTT jika terputus
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Update display dan device sesuai status kontrol
  setRGBandBuzzer(labelML);
  
  // Kirim data sensor setiap 3 detik
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = millis();

    suhu = dht.readTemperature();
    lembap = dht.readHumidity();
    suhuLuar = getOutdoorTemp();

    if (isnan(suhu) || isnan(lembap)) {
      Serial.println("✗ Error membaca sensor DHT11!");
      suhu = 0;
      lembap = 0;
    }

    if (isnan(suhuLuar)) {
      Serial.println("✗ Error mendapatkan suhu luar dari API!");
      suhuLuar = 0;
    }

    // Kirim data sensor ke Python untuk prediksi ML
    StaticJsonDocument<300> doc;
    doc["suhu_inkubator"] = suhu;
    doc["kelembapan"] = lembap;
    doc["suhu_luar"] = suhuLuar;
    doc["timestamp"] = String(millis());

    char buffer[300];
    serializeJson(doc, buffer);

    bool published = client.publish(TOPIK_SENSOR, buffer);

    Serial.println("\n=== DATA SENSOR DIKIRIM ===");
    Serial.println(buffer);
    Serial.print("Status: ");
    Serial.println(published ? "SUCCESS" : "FAILED");
    Serial.println("===========================");
    
    // Kirim output saat ini untuk update real-time di Streamlit
    if (labelML != "-") {
      kirimOutputData();
    }
  }
}