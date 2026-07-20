import matplotlib.pyplot as plt
import numpy as np
import matplotlib.patches as patches

# Thiết lập font chữ và style
plt.style.use('seaborn-v0_8-whitegrid')
plt.rcParams['font.family'] = 'sans-serif'

# ------------------------------------------------------------------
# HÌNH 1.1: BIỂU ĐỒ SUY HAO TÍN HIỆU LTE
# ------------------------------------------------------------------
t = np.linspace(0, 100, 500)
# Tín hiệu bình thường ngoài không gian mở (-60 to -75 dBm)
signal = -65 + np.random.normal(0, 3, len(t))

# Khu vực hầm từ t=35 đến t=70
tunnel_mask = (t >= 35) & (t <= 70)

# Suy hao thêm 45-55 dB trong hầm
signal[tunnel_mask] -= 45 + np.random.normal(0, 5, np.sum(tunnel_mask))

# Làm mượt khúc chuyển giao (đi vào và đi ra khỏi hầm)
window = 15
for i in range(350, 350+window):
    signal[i] = signal[349] - (signal[349] - signal[350+window]) * (i-350)/window
for i in range(700, 700+window):
    if i < len(signal):
        pass # Handle smoothing correctly
signal = np.convolve(signal, np.ones(5)/5, mode='same')

plt.figure(figsize=(10, 5))
plt.plot(t, signal, color='#2c3e50', linewidth=2, label='Cường độ tín hiệu thực tế')
plt.axhline(y=-100, color='#e74c3c', linestyle='--', linewidth=2, label='Ngưỡng mất sóng (-100 dBm)')
plt.axvspan(35, 70, color='#95a5a6', alpha=0.3, label='Khu vực hầm đường sắt')

plt.xlabel('Hành trình (Thời gian / Quãng đường)', fontsize=11, fontweight='bold')
plt.ylabel('Cường độ tín hiệu LTE (dBm)', fontsize=11, fontweight='bold')
plt.title('HÌNH 1.1. MÔ PHỎNG BIẾN THIÊN CƯỜNG ĐỘ TÍN HIỆU LTE QUA HẦM ĐƯỜNG SẮT', fontsize=12, fontweight='bold', pad=15)
plt.legend(loc='lower left', frameon=True, facecolor='white')
plt.ylim(-130, -50)
plt.tight_layout()
plt.savefig('Hinh_1_1_SuyHaoTinHieu.png', dpi=300)
plt.close()

# ------------------------------------------------------------------
# HÌNH 1.2: MINH HOẠ KHOẢNG TRỐNG DỮ LIỆU (DATA LOSS)
# ------------------------------------------------------------------
pts_t = np.arange(5, 95, 3)
received_pts = [p for p in pts_t if p < 35 or p > 70]
lost_pts = [p for p in pts_t if 35 <= p <= 70]

plt.figure(figsize=(11, 3))
plt.plot([0, 100], [1, 1], color='black', linewidth=2, zorder=1)

plt.scatter(received_pts, [1]*len(received_pts), color='#27ae60', s=120, zorder=2, label='Gói dữ liệu truyền thành công lên Server')
plt.scatter(lost_pts, [1]*len(lost_pts), color='#c0392b', s=120, marker='x', linewidths=2, zorder=2, label='Gói dữ liệu bị thất thoát (Mất sóng)')

plt.axvspan(35, 70, color='#95a5a6', alpha=0.3, label='Khu vực hầm (Offline)')
plt.yticks([])
plt.xlabel('Thời gian', fontsize=11, fontweight='bold')
plt.title('HÌNH 1.2. MINH HOẠ KHOẢNG TRỐNG DỮ LIỆU KHI KHÔNG CÓ LƯU TRỮ DỰ PHÒNG', fontsize=12, fontweight='bold', pad=15)
plt.legend(loc='center', bbox_to_anchor=(0.5, -0.4), ncol=3)
plt.tight_layout()
plt.subplots_adjust(bottom=0.3)
plt.savefig('Hinh_1_2_DataLoss.png', dpi=300)
plt.close()

# ------------------------------------------------------------------
# HÌNH 1.3: GIẢI PHÁP LƯU TRỮ VÀ CHUYỂN TIẾP (STORE AND FORWARD)
# ------------------------------------------------------------------
plt.figure(figsize=(11, 4.5))
plt.plot([0, 100], [1, 1], color='black', linewidth=2, zorder=1)

received_pts_realtime = [p for p in pts_t if p < 35]
buffered_pts = [p for p in pts_t if 35 <= p <= 70]
received_pts_forwarded = [p for p in pts_t if p > 70]

# Realtime Data
plt.scatter(received_pts_realtime, [1]*len(received_pts_realtime), color='#27ae60', s=120, zorder=2, label='Dữ liệu truyền thời gian thực')

# Buffered Data (Stored on SD Card)
plt.plot([35, 70], [0.5, 0.5], color='#f39c12', linestyle='--', linewidth=2)
plt.scatter(buffered_pts, [0.5]*len(buffered_pts), color='#f39c12', s=120, marker='s', zorder=2, label='Dữ liệu lưu vào thẻ nhớ (Store)')

# Normal Data after tunnel
plt.scatter(received_pts_forwarded, [1]*len(received_pts_forwarded), color='#27ae60', s=120, zorder=2)

# Forwarding arrows
for bp in buffered_pts:
    plt.annotate('', xy=(73, 1.15), xytext=(bp, 0.55),
                 arrowprops=dict(arrowstyle="->", color="#2980b9", lw=1.5, alpha=0.6))

plt.text(68, 1.25, 'Đồng bộ gửi bù\n(Forward)', color='#2980b9', fontweight='bold', ha='center')
plt.scatter(buffered_pts, [1]*len(buffered_pts), color='#2980b9', s=120, alpha=0.7, zorder=2, label='Dữ liệu gửi bù thành công')

plt.axvspan(35, 70, color='#95a5a6', alpha=0.3, label='Khu vực hầm (Offline)')
plt.ylim(0.2, 1.5)
plt.yticks([])
plt.xlabel('Thời gian', fontsize=11, fontweight='bold')
plt.title('HÌNH 1.3. GIẢI PHÁP LƯU TRỮ VÀ ĐỒNG BỘ DỮ LIỆU (STORE-AND-FORWARD)', fontsize=12, fontweight='bold', pad=15)
plt.legend(loc='center', bbox_to_anchor=(0.5, -0.2), ncol=2)
plt.tight_layout()
plt.subplots_adjust(bottom=0.2)
plt.savefig('Hinh_1_3_StoreAndForward.png', dpi=300)
plt.close()

print("Đã tạo thành công 3 ảnh minh hoạ cho Chương 1!")

# ------------------------------------------------------------------
# HÌNH 2.2: ĐẶC TUYẾN TIÊU THỤ DÒNG ĐIỆN CỦA MODULE A7680C KHI DÒ SÓNG
# ------------------------------------------------------------------
t_burst = np.linspace(0, 1000, 2000) # 0 to 1000ms
# Dòng nền (Idle/Rx mode) khoảng 50mA
i_burst = np.full_like(t_burst, 50.0)

# Hàm tạo xung vọt dòng (Tx Burst) với độ rộng tùy chỉnh
def tx_pulse_ma(t, t0, duration, peak_ma):
    pulse = np.exp(-((t - t0 - duration/2) / (duration/2))**8)  
    return pulse * peak_ma

# Tạo 3 xung dò sóng. Trong mạng LTE, 1 subframe = 1ms. Thời gian phát Tx (như PRACH preamble hoặc RRC setup) 
# thường kéo dài từ vài ms đến mười mấy ms. Ta chọn độ rộng 10-15ms để sát thực tế.
spike1 = tx_pulse_ma(t_burst, 200, 10, 1950) # Từ 200ms, kéo dài 10ms
spike2 = tx_pulse_ma(t_burst, 450, 15, 1850) # Từ 450ms, kéo dài 15ms
spike3 = tx_pulse_ma(t_burst, 780, 12, 1900) # Từ 780ms, kéo dài 12ms

i_burst += spike1 + spike2 + spike3

# Thêm nhiễu nền
noise_base = np.random.normal(0, 2, t_burst.shape)
# Thêm nhiễu lớn khi vọt dòng
noise_spike = np.random.normal(0, 40, t_burst.shape) * ((spike1 + spike2 + spike3) > 100)
i_burst += noise_base + noise_spike

# Thêm nhiễu đuôi (ringing) do tụ xả nạp sau khi dứt xung
for t0 in [200+10, 450+15, 780+12]:
    ringing_mask = (t_burst > t0) & (t_burst < t0 + 50)
    i_burst[ringing_mask] += np.random.normal(20, 10, np.sum(ringing_mask)) * np.exp(-(t_burst[ringing_mask]-t0)/15)

i_burst = np.clip(i_burst, 0, None)

plt.figure(figsize=(12, 6))
plt.plot(t_burst, i_burst, color='#c0392b', linewidth=1.5, label='Mức tiêu thụ dòng điện tức thời')

# Đường giới hạn 2A và vùng Brownout
plt.axhline(y=2000, color='red', linestyle='--', linewidth=2, label='Đỉnh kéo dòng tối đa (2A / 2000mA)')
plt.axhspan(1800, 2100, color='#e74c3c', alpha=0.15, label='Vùng nguy cơ gây sụt áp (Brownout)')

plt.xlabel('Thời gian (ms)', fontsize=11, fontweight='bold')
plt.ylabel('Dòng điện (mA)', fontsize=11, fontweight='bold')
plt.title('HÌNH 2.2: ĐẶC TUYẾN TIÊU THỤ DÒNG ĐIỆN CỦA MODULE A7680C KHI DÒ SÓNG', fontsize=12, fontweight='bold', pad=15)
plt.grid(True, linestyle='-', alpha=0.5)
plt.ylim(0, 2500)
plt.xlim(-50, 1050)

# Annotations (Căn chỉnh để không đè vạch)
plt.annotate('Quá trình dò sóng vô tuyến\n(Tx Burst)', xy=(200, 1950), xytext=(450, 2300),
             arrowprops=dict(facecolor='black', shrink=0.05, width=1.0, headwidth=6, headlength=8, connectionstyle="arc3,rad=0.1"),
             fontsize=11, fontweight='bold', ha='center',
             bbox=dict(boxstyle="round,pad=0.3", fc="white", ec="gray", alpha=0.9))

plt.text(100, 150, 'Trạng thái nghỉ\n(Idle)', color='#2980b9', fontweight='bold', ha='center',
         bbox=dict(boxstyle="round,pad=0.3", fc="white", ec="none", alpha=0.8))

plt.legend(loc='upper right', frameon=True, facecolor='white', edgecolor='black')
plt.tight_layout()
plt.savefig('Hinh_2_2_TxBurstCurrent_v5.png', dpi=300)
plt.close()

print("Đã tạo thành công ảnh minh hoạ Hình 2.2 (Tx Burst Bản Cải Tiến Lần 3) cho Chương 2!")

# ------------------------------------------------------------------
# HÌNH 2.3: KIẾN TRÚC PUBLISH/SUBSCRIBE CỦA GIAO THỨC MQTT
# ------------------------------------------------------------------
fig, ax = plt.subplots(figsize=(10, 5))
ax.set_xlim(0, 10)
ax.set_ylim(0, 5)
ax.axis('off')

# Hộp 1: Publisher
pub_box = patches.FancyBboxPatch((1, 2), 2, 1, boxstyle="round,pad=0.1", fc="#3498db", ec="none", lw=2)
ax.add_patch(pub_box)
ax.text(2, 2.5, "Thiết bị IoT\n(ESP32 Publisher)", ha="center", va="center", color="white", fontweight="bold", fontsize=11)

# Hộp 2: Broker
broker_box = patches.FancyBboxPatch((4.5, 1.5), 2, 2, boxstyle="round,pad=0.1", fc="#e74c3c", ec="none", lw=2)
ax.add_patch(broker_box)
ax.text(5.5, 2.5, "Máy chủ trung gian\n(MQTT Broker)", ha="center", va="center", color="white", fontweight="bold", fontsize=11)

# Hộp 3: Subscriber
sub_box = patches.FancyBboxPatch((8, 2), 2, 1, boxstyle="round,pad=0.1", fc="#27ae60", ec="none", lw=2)
ax.add_patch(sub_box)
ax.text(9, 2.5, "Ứng dụng giám sát\n(Web Subscriber)", ha="center", va="center", color="white", fontweight="bold", fontsize=11)

# Mũi tên 1: Publisher -> Broker
ax.annotate("", xy=(4.5, 3.0), xytext=(3, 3.0), arrowprops=dict(arrowstyle="->", lw=2, color="#2c3e50"))
ax.text(3.75, 3.1, "1. Publish Data\n(Topic: /train/loc)", ha="center", va="bottom", fontsize=10, color="#2c3e50")

# Mũi tên 2: Subscriber -> Broker (Subscribe request)
ax.annotate("", xy=(6.5, 2.0), xytext=(8, 2.0), arrowprops=dict(arrowstyle="->", lw=2, color="#2c3e50"))
ax.text(7.25, 1.9, "2. Subscribe\n(Topic: /train/loc)", ha="center", va="top", fontsize=10, color="#2c3e50")

# Mũi tên 3: Broker -> Subscriber (Forward data)
ax.annotate("", xy=(8, 3.0), xytext=(6.5, 3.0), arrowprops=dict(arrowstyle="->", lw=2, color="#2c3e50"))
ax.text(7.25, 3.1, "3. Forward Data", ha="center", va="bottom", fontsize=10, color="#2c3e50")

plt.title('HÌNH 2.3. KIẾN TRÚC PUBLISH/SUBSCRIBE CỦA GIAO THỨC MQTT', fontsize=12, fontweight='bold', pad=15)
plt.tight_layout()
plt.savefig('Hinh_2_3_MQTT_Architecture.png', dpi=300)
plt.close()

# ------------------------------------------------------------------
# HÌNH 2.4: CƠ CHẾ XÁC NHẬN BẢN TIN TRONG MQTT QOS 1
# ------------------------------------------------------------------
fig, ax = plt.subplots(figsize=(8, 4))
ax.set_xlim(0, 8)
ax.set_ylim(0, 4)
ax.axis('off')

# Hộp 1: Publisher
pub_box = patches.FancyBboxPatch((1, 1.5), 2, 1, boxstyle="round,pad=0.1", fc="#3498db", ec="none", lw=2)
ax.add_patch(pub_box)
ax.text(2, 2, "Thiết bị IoT\n(Publisher)", ha="center", va="center", color="white", fontweight="bold", fontsize=11)

# Hộp 2: Broker
broker_box = patches.FancyBboxPatch((5, 1.5), 2, 1, boxstyle="round,pad=0.1", fc="#e74c3c", ec="none", lw=2)
ax.add_patch(broker_box)
ax.text(6, 2, "MQTT Broker", ha="center", va="center", color="white", fontweight="bold", fontsize=11)

# Mũi tên 1: Publish
ax.annotate("", xy=(5, 2.3), xytext=(3, 2.3), arrowprops=dict(arrowstyle="->", lw=2, color="#2c3e50"))
ax.text(4, 2.4, "PUBLISH (QoS 1, Packet ID: 10)", ha="center", va="bottom", fontsize=10, color="#2c3e50", fontweight='bold')

# Mũi tên 2: Puback
ax.annotate("", xy=(3, 1.7), xytext=(5, 1.7), arrowprops=dict(arrowstyle="->", lw=2, color="#27ae60"))
ax.text(4, 1.6, "PUBACK (Packet ID: 10)", ha="center", va="top", fontsize=10, color="#27ae60", fontweight='bold')

plt.title('HÌNH 2.4. CƠ CHẾ XÁC NHẬN BẢN TIN TRONG MQTT QOS 1', fontsize=12, fontweight='bold', pad=15)
plt.tight_layout()
plt.savefig('Hinh_2_4_MQTT_QoS1.png', dpi=300)
plt.close()

print("Đã tạo thành công ảnh minh hoạ Hình 2.3 và Hình 2.4 cho Chương 2!")
