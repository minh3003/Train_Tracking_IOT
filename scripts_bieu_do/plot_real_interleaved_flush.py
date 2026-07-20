import pandas as pd
import matplotlib.pyplot as plt
import os

# Create target directory
os.makedirs("ảnh báo cáo/ảnh sơ đồ", exist_ok=True)

# 1. Đọc dữ liệu từ file CSV
try:
    df = pd.read_csv('../telemetry_data.csv')
except FileNotFoundError:
    print("Không tìm thấy file telemetry_data.csv!")
    exit(1)

# Xử lý dữ liệu: chuyển đổi thời gian nhận (time) thành datetime
df['time'] = pd.to_datetime(df['time'])

# Sắp xếp dữ liệu theo thời gian nhận được ở Broker
df = df.sort_values(by='time')

# 2. Phân tích Dữ liệu thời gian thực (Realtime) vs Dữ liệu bù (Buffered)
# Bằng cách so sánh Sequence Number (seq) hiện tại với seq trước đó
df['seq'] = df['seq'].astype(float)

# Dữ liệu realtime thường có seq tăng dần đều và là seq mới nhất
# Dữ liệu buffered là các seq cũ bị sót lại được gửi bù
# Thuật toán đơn giản: Nút ESP gửi interleaved, nên có sự chênh lệch lớn về seq
df['is_buffered'] = df['seq'].diff() < -100 # Nếu seq đột ngột giảm sâu, đó là gói tin cũ được gửi bù

plt.figure(figsize=(12, 6))

# Vẽ các gói Realtime
realtime_df = df[~df['is_buffered']]
plt.scatter(realtime_df['time'], realtime_df['seq'], color='#27ae60', s=10, label='Realtime Data', alpha=0.7)

# Vẽ các gói Buffered
buffered_df = df[df['is_buffered']]
plt.scatter(buffered_df['time'], buffered_df['seq'], color='#e74c3c', s=15, label='Buffered Data (SD Card)', alpha=0.9)

plt.xlabel('Thời gian nhận tại Server')
plt.ylabel('Sequence Number (seq)')
plt.title('HÌNH 4.X. CHỨNG MINH CƠ CHẾ INTERLEAVED FLUSH TỪ DỮ LIỆU THỰC TẾ')
plt.legend()
plt.grid(True)
plt.xticks(rotation=45)
plt.tight_layout()

# Lưu ảnh
plt.savefig('ảnh báo cáo/Hinh_4_X_RealData_Interleaved.png', dpi=300)
print("Đã tạo biểu đồ từ dữ liệu thật tại: ảnh báo cáo/Hinh_4_X_RealData_Interleaved.png")
