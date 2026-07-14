import paho.mqtt.client as mqtt
import pickle
import json
from datetime import datetime
import os
import warnings

# Suppress sklearn warnings
warnings.filterwarnings('ignore', category=UserWarning)

# ==================== KONFIGURASI ====================
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
TOPIC_SENSOR = "Project/Re202/Sensor"              # Terima dari ESP32
TOPIC_ML_OUTPUT = "Project/Re202/Dataset/OutputML" # Kirim ke ESP32
MODEL_PATH = "D:\SIC\Stage3\Final_Project\model_inkubator.pkl"

# ==================== LOAD MODEL ====================
print("="*60)
print("MQTT PROCESSOR - MACHINE LEARNING ENGINE")
print("="*60)
print(f"MQTT Broker: {MQTT_BROKER}:{MQTT_PORT}")
print(f"Model Path: {MODEL_PATH}")
print("="*60 + "\n")

# Load model
model = None
if os.path.exists(MODEL_PATH):
    try:
        with open(MODEL_PATH, 'rb') as f:
            model = pickle.load(f)
        print(f"✓ Model berhasil di-load dari {MODEL_PATH}\n")
    except Exception as e:
        print(f"✗ Error loading model: {str(e)}\n")
        exit(1)
else:
    print(f"✗ File model tidak ditemukan: {MODEL_PATH}")
    print("  Jalankan train_model.py terlebih dahulu!\n")
    exit(1)

# Counter untuk tracking
prediction_count = 0

# ==================== FUNGSI PREDIKSI ====================
def prediksi_label(suhu, kelembapan):
    """Prediksi label menggunakan model ML"""
    global model
    
    if model is None:
        return "error", {}
    
    try:
        input_data = [[float(suhu), float(kelembapan)]]
        prediksi = model.predict(input_data)[0]
        probabilitas = model.predict_proba(input_data)[0]
        
        class_labels = ['dingin', 'normal', 'panas']
        prob_dict = {label: float(prob) for label, prob in zip(class_labels, probabilitas)}
        
        return prediksi, prob_dict
    except Exception as e:
        print(f"✗ Error prediksi: {str(e)}")
        return "error", {}

# ==================== MQTT CALLBACKS ====================
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("="*60)
        print(f"✓ MQTT CONNECTED: {MQTT_BROKER}")
        print("="*60)
        client.subscribe(TOPIC_SENSOR)
        print(f"✓ Subscribed to: {TOPIC_SENSOR}")
        print(f"✓ Publishing to: {TOPIC_ML_OUTPUT}")
        print("="*60)
        print("\n⚡ SISTEM SIAP - Menunggu data dari ESP32...")
        print("⚡ Prediksi ML UNLIMITED - Tidak ada batasan!\n")
    else:
        print(f"✗ Connection failed with code: {rc}")

def on_message(client, userdata, msg):
    global prediction_count
    
    try:
        if msg.topic != TOPIC_SENSOR:
            return
        
        # Parse data dari ESP32
        data = json.loads(msg.payload.decode())
        
        suhu = float(data.get('suhu_inkubator', 0))
        kelembapan = float(data.get('kelembapan', 0))
        suhu_luar = float(data.get('suhu_luar', 0))
        
        prediction_count += 1
        
        print(f"\n{'='*60}")
        print(f"[{prediction_count}] 📥 DATA SENSOR DITERIMA")
        print(f"{'='*60}")
        print(f"Timestamp   : {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"Suhu Inkub  : {suhu}°C")
        print(f"Kelembapan  : {kelembapan}%")
        print(f"Suhu Luar   : {suhu_luar}°C")
        
        # PREDIKSI ML - UNLIMITED!
        label, probabilitas = prediksi_label(suhu, kelembapan)
        
        if label == "error":
            print(f"✗ Prediksi gagal, gunakan default: normal")
            label = "normal"
            probabilitas = {"dingin": 0.0, "normal": 1.0, "panas": 0.0}
        else:
            print(f"\n✓ PREDIKSI ML BERHASIL:")
            print(f"   Label      : {label.upper()}")
            print(f"   Confidence : {probabilitas[label]*100:.2f}%")
            print(f"   Probabilitas:")
            for lbl, prob in probabilitas.items():
                print(f"     • {lbl.capitalize()}: {prob*100:.2f}%")
        
        # KIRIM HASIL KE ESP32 - UNLIMITED!
        result = {
            "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "suhu_inkubator": suhu,
            "kelembapan": kelembapan,
            "suhu_luar": suhu_luar,
            "label": label,
            "probabilitas": probabilitas
        }
        
        client.publish(TOPIC_ML_OUTPUT, json.dumps(result))
        
        print(f"\n✓ Hasil dikirim ke ESP32")
        print(f"   Topic: {TOPIC_ML_OUTPUT}")
        print(f"   Total prediksi: {prediction_count}")
        print(f"{'='*60}\n")
        
    except json.JSONDecodeError as e:
        print(f"✗ JSON decode error: {str(e)}")
    except Exception as e:
        print(f"✗ Error processing message: {str(e)}")

def on_disconnect(client, userdata, rc):
    if rc != 0:
        print(f"\n⚠️  MQTT Disconnected (code: {rc})")
        print("⚠️  Mencoba reconnect...\n")

def on_publish(client, userdata, mid):
    # Optional: log setiap publish
    pass

# ==================== MAIN PROGRAM ====================
def main():
    # Setup MQTT Client
    client = mqtt.Client(client_id="ML-Processor-Engine")
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    client.on_publish = on_publish
    
    try:
        # Connect to broker
        print("🔌 Menghubungkan ke MQTT Broker...\n")
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        
        # Loop forever - TIDAK AKAN BERHENTI!
        print("⚡ Starting MQTT loop...")
        print("⚡ Press Ctrl+C to stop\n")
        client.loop_forever()
        
    except KeyboardInterrupt:
        print("\n\n" + "="*60)
        print("SISTEM DIHENTIKAN OLEH USER")
        print("="*60)
        print(f"Total prediksi yang dilakukan: {prediction_count}")
        print("="*60 + "\n")
        client.disconnect()
        
    except Exception as e:
        print(f"\n✗ Error: {str(e)}\n")
        client.disconnect()

if __name__ == "__main__":
    main()