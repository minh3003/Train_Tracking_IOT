import paho.mqtt.client as mqtt
import json
import csv
import os
import ssl

BROKER = "broker.emqx.io"
PORT = 8883
TOPIC = "traintrack/Bamboo/telemetry"
CSV_FILE = "telemetry_data.csv"

# Tạo header cho file CSV nếu chưa có
if not os.path.exists(CSV_FILE):
    with open(CSV_FILE, mode='w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(["dev", "boot", "seq", "ts", "time", "lat", "lon", "speed", "sats", "ax", "ay", "az", "gx", "temp", "gps_ok"])

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[+] Da ket noi thanh cong den {BROKER}:{PORT}")
        client.subscribe(TOPIC)
        print(f"[+] Dang lang nghe tren topic: {TOPIC}")
        print(f"[+] Du lieu se duoc luu tu dong vao file: {CSV_FILE}")
        print(f"[!] An Ctrl+C de dung thu thap.\n" + "-"*50)
    else:
        print(f"[-] Ket noi that bai voi ma loi {rc}")

def on_message(client, userdata, msg):
    payload = msg.payload.decode('utf-8')
    print(f"Nhan: {payload}")
    try:
        data = json.loads(payload)
        with open(CSV_FILE, mode='a', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            writer.writerow([
                data.get("dev", ""),
                data.get("boot", ""),
                data.get("seq", ""),
                data.get("ts", ""),
                data.get("time", ""),
                data.get("lat", ""),
                data.get("lon", ""),
                data.get("speed", ""),
                data.get("sats", ""),
                data.get("ax", ""),
                data.get("ay", ""),
                data.get("az", ""),
                data.get("gx", ""),
                data.get("temp", ""),
                data.get("gps_ok", "")
            ])
    except Exception as e:
        print(f"[-] Loi xu ly JSON: {e}")

# Khoi tao MQTT Client
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

# Cau hinh TLS de ket noi vao cong 8883
client.tls_set(cert_reqs=ssl.CERT_NONE)

try:
    print(f"[*] Dang ket noi den {BROKER}...")
    client.connect(BROKER, PORT, 60)
    client.loop_forever()
except KeyboardInterrupt:
    print("\n[!] Da dung thu thap du lieu.")
except Exception as e:
    print(f"\n[-] Loi: {e}")
