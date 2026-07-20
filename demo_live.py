import paho.mqtt.client as mqtt
import json
import ssl
from datetime import datetime
import sys
import os

# Cấu hình Broker giống trong sys_config.h
BROKER = "broker.emqx.io"
PORT = 8883
TOPIC_BASE = "traintrack" # Lắng nghe TẤT CẢ các thiết bị trong mạng lưới

# Kích hoạt hỗ trợ ANSI Color trên Windows Terminal
os.system('')

class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    CYAN = '\033[96m'
    MAGENTA = '\033[95m'
    WHITE = '\033[97m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

print(f"{Colors.CYAN}{Colors.BOLD}========================================================================================{Colors.RESET}")
print(f"{Colors.CYAN}{Colors.BOLD}            HỆ THỐNG TRẠM ĐIỀU HÀNH TRUNG TÂM (MULTI-DEVICE DASHBOARD)                  {Colors.RESET}")
print(f"{Colors.CYAN}{Colors.BOLD}========================================================================================{Colors.RESET}")
print(f"Đang kết nối tới Broker {BROKER}:{PORT}...\n")

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"{Colors.GREEN}[+] KẾT NỐI THÀNH CÔNG! Đang giám sát toàn bộ các Tàu trên mạng lưới...{Colors.RESET}\n")
        print(f"{Colors.BOLD}{'TRẠNG THÁI / LOẠI':<24} | {'TÊN TÀU':<10} | {'THỜI GIAN ĐO / NỘI DUNG':<20} | {'BID':<5} | {'SEQ':<6} | {'ĐỘ TRỄ':<15}{Colors.RESET}")
        print("-" * 105)
        # Subscribe toàn bộ mạng lưới: traintrack/#
        client.subscribe(f"{TOPIC_BASE}/#")
    else:
        print(f"{Colors.RED}[-] KẾT NỐI THẤT BẠI (Mã lỗi: {rc}){Colors.RESET}")

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode('utf-8')
    
    # Bóc tách tên thiết bị từ Topic (VD: traintrack/Bamboo/telemetry -> Bamboo)
    parts = topic.split('/')
    dev = parts[1] if len(parts) >= 2 else "UNKNOWN"
    
    # 1. Xử lý bản tin STATUS (Khi thiết bị on/off)
    if topic.endswith("/status"):
        print(f"{Colors.CYAN}{Colors.BOLD}[STATUS UPDATE]{Colors.RESET:<23} | {Colors.CYAN}{dev:<10}{Colors.RESET} | {Colors.CYAN}{payload:<45}{Colors.RESET} |")
        return
        
    # 2. Xử lý bản tin RESPONSE (Khi thiết bị trả lời lệnh)
    if topic.endswith("/response"):
        print(f"{Colors.MAGENTA}{Colors.BOLD}[COMMAND RESP]{Colors.RESET:<24} | {Colors.MAGENTA}{dev:<10}{Colors.RESET} | {Colors.MAGENTA}{payload:<45}{Colors.RESET} |")
        return

    # 3. Xử lý bản tin COMMAND (Giám sát lệnh điều khiển bắn xuống)
    if topic.endswith("/command"):
        print(f"{Colors.WHITE}{Colors.BOLD}>>> [ADMIN CMD] >>>{Colors.RESET:<20} | {Colors.WHITE}{dev:<10}{Colors.RESET} | {Colors.WHITE}{payload:<45}{Colors.RESET} |")
        return

    # 4. Xử lý bản tin TELEMETRY (Data tọa độ)
    if topic.endswith("/telemetry"):
        try:
            data = json.loads(payload)
            bid = data.get("bid", data.get("boot", 0))
            seq = data.get("seq", 0)
            meas_time_str = data.get("time", "")
            
            try:
                meas_time = datetime.strptime(meas_time_str, "%Y-%m-%dT%H:%M:%S")
                now = datetime.now()
                diff = (now - meas_time).total_seconds()
                
                if diff <= 60 and diff >= -60:
                    status = f"{Colors.GREEN}[LIVE DATA]{Colors.RESET}"
                    latency_str = f"{abs(diff):.1f} giây"
                else:
                    status = f"{Colors.RED}[RECOVERED]{Colors.RESET}"
                    if diff > 86400:
                        latency_str = f"{diff/86400:.1f} ngày"
                    elif diff > 3600:
                        latency_str = f"{diff/3600:.1f} giờ"
                    else:
                        latency_str = f"{diff/60:.1f} phút"
                    
            except Exception as parse_err:
                status = f"{Colors.YELLOW}[UNKNOWN TIME]{Colors.RESET}"
                latency_str = "N/A"
                
            print(f"{status:<33} | {dev:<10} | {meas_time_str:<23} | {bid:<5} | {seq:<6} | {latency_str:<15}")
            
        except Exception as e:
            print(f"{Colors.YELLOW}Bản tin lỗi format từ {dev}: {payload}{Colors.RESET}")

# Khởi tạo MQTT Client (Sử dụng API v1 để tương thích và ẩn cảnh báo)
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
client.on_connect = on_connect
client.on_message = on_message

# Cấu hình TLS (Bảo mật)
client.tls_set(cert_reqs=ssl.CERT_NONE)

try:
    client.connect(BROKER, PORT, 60)
    client.loop_forever()
except KeyboardInterrupt:
    print(f"\n{Colors.YELLOW}[!] Đã dừng hệ thống giám sát.{Colors.RESET}")
except Exception as e:
    print(f"\n{Colors.RED}[-] Lỗi mạng: {e}{Colors.RESET}")
