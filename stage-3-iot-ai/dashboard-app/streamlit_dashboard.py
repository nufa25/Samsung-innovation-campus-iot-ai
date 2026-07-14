import streamlit as st
import pandas as pd
import plotly.graph_objects as go
import json
import time
import threading
import queue
from collections import deque
from datetime import datetime
import paho.mqtt.client as mqtt
from streamlit_autorefresh import st_autorefresh

# ==================== KONFIGURASI ====================
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
TOPIC_OUTPUT = "Project/Re202/Output/Sensor"
TOPIC_CONTROL = "Project/Re202/Control"
UPDATE_INTERVAL = 2000  # ms, untuk st_autorefresh

# ==================== GLOBAL VARIABLES ====================
GLOBAL_QUEUE = queue.Queue()
mqtt_client_instance = None
mqtt_thread_started = False

# ==================== MQTT CALLBACKS ====================
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"✓ Connected: {MQTT_BROKER}")
        client.subscribe(TOPIC_OUTPUT)
        GLOBAL_QUEUE.put({"_type": "status", "connected": True})
    else:
        print(f"✗ Failed: {rc}")
        GLOBAL_QUEUE.put({"_type": "status", "connected": False})

def on_message(client, userdata, msg):
    try:
        if msg.topic != TOPIC_OUTPUT:
            return
        data = json.loads(msg.payload.decode())
        GLOBAL_QUEUE.put({"_type": "data", "payload": data})
    except Exception as e:
        print(f"✗ Error: {e}")

def on_disconnect(client, userdata, rc):
    if rc != 0:
        print(f"⚠️ Disconnected (code: {rc}) - Reconnecting...")
    GLOBAL_QUEUE.put({"_type": "status", "connected": False})

# ==================== MQTT WORKER ====================
def mqtt_worker():
    global mqtt_client_instance
    mqtt_client_instance = mqtt.Client(client_id="Dashboard-Fast")
    mqtt_client_instance.on_connect = on_connect
    mqtt_client_instance.on_message = on_message
    mqtt_client_instance.on_disconnect = on_disconnect
    mqtt_client_instance.reconnect_delay_set(min_delay=1, max_delay=10)
    while True:
        try:
            mqtt_client_instance.connect(MQTT_BROKER, MQTT_PORT, 60)
            mqtt_client_instance.loop_forever()
        except Exception as e:
            print(f"✗ MQTT Error: {e}")
            time.sleep(3)

def start_mqtt_thread():
    global mqtt_thread_started
    if not mqtt_thread_started:
        thread = threading.Thread(target=mqtt_worker, daemon=True)
        thread.start()
        mqtt_thread_started = True
        time.sleep(1)

# ==================== SEND CONTROL ====================
def send_control(buzzer, led, oled):
    global mqtt_client_instance
    if mqtt_client_instance is None:
        return False
    try:
        control_data = {"buzzer": buzzer, "led": led, "oled": oled}
        result = mqtt_client_instance.publish(TOPIC_CONTROL, json.dumps(control_data), qos=1)
        return result.rc == mqtt.MQTT_ERR_SUCCESS
    except:
        return False

# ==================== PROCESS QUEUE ====================
def process_queue():
    updated = False
    while not GLOBAL_QUEUE.empty():
        try:
            item = GLOBAL_QUEUE.get_nowait()
            msg_type = item.get("_type")
            if msg_type == "status":
                st.session_state.mqtt_connected = item.get("connected", False)
                updated = True
            elif msg_type == "data":
                payload = item.get("payload", {})
                data_entry = {
                    'timestamp': payload.get('timestamp', datetime.now().strftime("%Y-%m-%d %H:%M:%S")),
                    'suhu_inkubator': float(payload.get('suhu_inkubator', 0)),
                    'kelembapan': float(payload.get('kelembapan', 0)),
                    'suhu_luar': float(payload.get('suhu_luar', 0)),
                    'label': payload.get('label', 'normal'),
                    'buzzer_status': payload.get('buzzer_status', 'ON'),
                    'led_status': payload.get('led_status', 'ON'),
                    'oled_status': payload.get('oled_status', 'ON')
                }
                st.session_state.graph_buffer.append(data_entry)
                st.session_state.all_data.append(data_entry)
                st.session_state.last_update = datetime.now()
                st.session_state.buzzer_enabled = (data_entry['buzzer_status'] == 'ON')
                st.session_state.led_enabled = (data_entry['led_status'] == 'ON')
                st.session_state.oled_enabled = (data_entry['oled_status'] == 'ON')
                updated = True
        except queue.Empty:
            break
        except Exception as e:
            print(f"✗ Process error: {e}")
            break
    return updated

# ==================== MAIN APP ====================
def main():
    st.set_page_config(page_title="Dashboard Inkubator IoT", page_icon="🌡️", layout="wide")
    
    # Refresh otomatis setiap 2 detik
    st_autorefresh(interval=UPDATE_INTERVAL, key="auto_refresh")
    
    # Start MQTT
    start_mqtt_thread()
    
    # Initialize session state
    if 'graph_buffer' not in st.session_state:
        st.session_state.graph_buffer = deque(maxlen=150)
    if 'all_data' not in st.session_state:
        st.session_state.all_data = []
    if 'buzzer_enabled' not in st.session_state:
        st.session_state.buzzer_enabled = True
    if 'led_enabled' not in st.session_state:
        st.session_state.led_enabled = True
    if 'oled_enabled' not in st.session_state:
        st.session_state.oled_enabled = True
    if 'mqtt_connected' not in st.session_state:
        st.session_state.mqtt_connected = False
    if 'last_update' not in st.session_state:
        st.session_state.last_update = datetime.now()
    
    # Process queue
    process_queue()
    
    # ==================== HEADER ====================
    st.title("🌡️ Dashboard Monitoring Inkubator IoT")
    st.markdown("**Real-time Monitoring System** | Update setiap 2 detik")
    st.markdown("---")
    
    # ==================== STATUS ====================
    col1, col2, col3, col4 = st.columns(4)
    with col1:
        if st.session_state.mqtt_connected:
            st.success("✅ MQTT Connected")
        else:
            st.error("❌ Disconnected")
    with col2:
        st.info(f"📈 Grafik: {len(st.session_state.graph_buffer)}/150")
    with col3:
        st.info(f"📋 Tabel: {len(st.session_state.all_data)} data")
    with col4:
        st.info(f"🕒 {st.session_state.last_update.strftime('%H:%M:%S')}")
    
    st.markdown("---")
    
    # ==================== PANEL KONTROL ====================
    st.subheader("⚙️ Panel Kontrol Device ESP32")
    if len(st.session_state.all_data) > 0:
        latest = st.session_state.all_data[-1]
        col1, col2, col3 = st.columns(3)
        with col1:
            st.caption(f"📡 Buzzer: {latest.get('buzzer_status', 'N/A')}")
        with col2:
            st.caption(f"📡 LED: {latest.get('led_status', 'N/A')}")
        with col3:
            st.caption(f"📡 OLED: {latest.get('oled_status', 'N/A')}")
    
    col1, col2, col3 = st.columns(3)
    with col1:
        buzzer = st.toggle("🔊 Buzzer", st.session_state.buzzer_enabled, key="buzz")
        if buzzer != st.session_state.buzzer_enabled:
            if send_control(buzzer, st.session_state.led_enabled, st.session_state.oled_enabled):
                st.session_state.buzzer_enabled = buzzer
                st.success(f"✅ {'ON' if buzzer else 'OFF'}")
    with col2:
        led = st.toggle("💡 LED", st.session_state.led_enabled, key="led")
        if led != st.session_state.led_enabled:
            if send_control(st.session_state.buzzer_enabled, led, st.session_state.oled_enabled):
                st.session_state.led_enabled = led
                st.success(f"✅ {'ON' if led else 'OFF'}")
    with col3:
        oled = st.toggle("📺 OLED", st.session_state.oled_enabled, key="oled")
        if oled != st.session_state.oled_enabled:
            if send_control(st.session_state.buzzer_enabled, st.session_state.led_enabled, oled):
                st.session_state.oled_enabled = oled
                st.success(f"✅ {'ON' if oled else 'OFF'}")
    
    st.markdown("---")
    
    # ==================== DATA DISPLAY ====================
    if len(st.session_state.all_data) > 0:
        latest = st.session_state.all_data[-1]
        
        # METRIK
        st.subheader("📊 Data Real-time Terbaru")
        col1, col2, col3, col4 = st.columns(4)
        with col1:
            st.metric("🌡️ Suhu Inkubator", f"{latest['suhu_inkubator']:.1f} °C")
        with col2:
            st.metric("💧 Kelembapan", f"{latest['kelembapan']:.1f} %")
        with col3:
            st.metric("🌍 Suhu Luar", f"{latest['suhu_luar']:.1f} °C")
        with col4:
            label = latest['label']
            emoji = "🔥" if label == "panas" else ("❄️" if label == "dingin" else "✅")
            st.metric(f"{emoji} Status", label.upper())
        
        st.markdown("---")
        
        # GRAFIK
        st.subheader("📈 Grafik: Suhu & Kelembapan (150 Data)")
        df = pd.DataFrame(list(st.session_state.graph_buffer))
        if not df.empty:
            df['index'] = range(len(df))
            fig = go.Figure()
            fig.add_trace(go.Scatter(
                x=df['index'],
                y=df['suhu_inkubator'],
                mode='lines+markers',
                name='🌡️ Suhu (°C)',
                line=dict(color='#e74c3c', width=2),
                marker=dict(size=4)
            ))
            fig.add_trace(go.Scatter(
                x=df['index'],
                y=df['kelembapan'],
                mode='lines+markers',
                name='💧 Kelembapan (%)',
                line=dict(color='#3498db', width=2),
                marker=dict(size=4),
                yaxis='y2'
            ))
            fig.update_layout(
                height=400,
                margin=dict(l=50, r=50, t=30, b=50),
                xaxis=dict(title="Index"),
                yaxis=dict(title=dict(text="Suhu (°C)", font=dict(color='#e74c3c')), tickfont=dict(color='#e74c3c')),
                yaxis2=dict(title=dict(text="Kelembapan (%)", font=dict(color='#3498db')), tickfont=dict(color='#3498db'), overlaying='y', side='right'),
                hovermode='x unified',
                showlegend=True,
                legend=dict(orientation="h", yanchor="bottom", y=1.02, xanchor="right", x=1)
            )
            st.plotly_chart(fig, use_container_width=True, key=f"graph_{len(df)}")
        
        st.markdown("---")
        
        # TABEL
        # TABEL
        st.subheader("📋 Tabel Data Historis")
        df_display = df[['timestamp', 'suhu_inkubator', 'kelembapan', 'suhu_luar', 'label']].copy()
        df_display.columns = ['Timestamp', 'Suhu (°C)', 'Kelembapan (%)', 'Luar (°C)', 'Label']
        df_display = df_display.iloc[::+1].reset_index(drop=True)
        st.dataframe(df_display, use_container_width=True, height=400)

        
        # DOWNLOAD & CLEAR
        col1, col2 = st.columns([1, 3])
        with col1:
            csv = df_display.to_csv(index=False).encode('utf-8')
            st.download_button(
                "📥 Download CSV",
                data=csv,
                file_name=f"inkubator_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv",
                mime="text/csv"
            )
        with col2:
            if st.button("🗑️ Clear Data"):
                st.session_state.all_data.clear()
                st.session_state.graph_buffer.clear()
                st.success("✅ Data cleared!")
    
    else:
        st.warning("⏳ Menunggu data dari ESP32...")
    
    # Footer
    st.markdown("---")
    st.caption(f"Last: {st.session_state.last_update.strftime('%Y-%m-%d %H:%M:%S')} | {MQTT_BROKER}")

if __name__ == "__main__":
    main()
