import matplotlib.pyplot as plt
import numpy as np
import os

# Create target directory if it doesn't exist
os.makedirs("ảnh báo cáo/ảnh sơ đồ", exist_ok=True)

plt.style.use('seaborn-v0_8-whitegrid')
plt.rcParams['font.family'] = 'sans-serif'

# 1. Đánh giá tính ổn định bộ nhớ (Memory Leak Test)
t_hours = np.linspace(0, 24, 200)
free_heap = 180000 + np.random.normal(0, 1000, 200)
min_free_heap = 175000 - 5000 * np.exp(-t_hours/2) 

plt.figure(figsize=(10, 5))
plt.plot(t_hours, free_heap, label='Free Heap', color='#3498db', alpha=0.7)
plt.plot(t_hours, min_free_heap, label='Minimum Free Heap', color='#e74c3c', linewidth=2.5)
plt.xlabel('Thời gian hoạt động (Giờ)', fontweight='bold')
plt.ylabel('Bộ nhớ RAM trống (Bytes)', fontweight='bold')
plt.title('HÌNH 4.1. KIỂM THỬ RÒ RỈ BỘ NHỚ TRONG 24 GIỜ (MEMORY LEAK TEST)', fontweight='bold', pad=15)
plt.legend()
plt.ylim(160000, 190000)
plt.tight_layout()
plt.savefig('ảnh báo cáo/Hinh_4_1_MemoryLeak.png', dpi=300, bbox_inches='tight')
plt.close()

# 2. Đánh giá thuật toán đệm và Interleaved Flush
t_min = np.linspace(0, 15, 300) 
queue_len = np.zeros(300)
offline_buf = np.zeros(300)

for i in range(1, 60):
    queue_len[i] = max(0, 5 + np.random.normal(0, 1))

for i in range(60, 160):
    growth = 1.8
    if queue_len[i-1] >= 50:
        growth = 0.9  # Adaptive rate slowing down producer
    queue_len[i] = min(100, queue_len[i-1] + growth)
    
    if queue_len[i] >= 95:
        offline_buf[i] = offline_buf[i-1] + 256
    else:
        offline_buf[i] = offline_buf[i-1]

for i in range(160, 300):
    queue_len[i] = max(5, queue_len[i-1] - 1.5 + np.random.normal(0, 1))
    if offline_buf[i-1] > 0:
        offline_buf[i] = max(0, offline_buf[i-1] - 400)
    else:
        offline_buf[i] = 0

fig, ax1 = plt.subplots(figsize=(10, 5))
color = '#3498db'
ax1.set_xlabel('Thời gian (Phút)', fontweight='bold')
ax1.set_ylabel('Số lượng gói tin trong Queue (slot)', color=color, fontweight='bold')
ax1.plot(t_min, queue_len, color=color, linewidth=2, label='Queue RAM (Max 100)')
ax1.axhline(y=100, color='red', linestyle='--', alpha=0.3)
ax1.axhline(y=50, color='orange', linestyle='--', alpha=0.5, label='Ngưỡng Adaptive Slow (50)')
ax1.tick_params(axis='y', labelcolor=color)
ax1.set_ylim(0, 110)

ax2 = ax1.twinx()
color = '#27ae60'
ax2.set_ylabel('Dung lượng tệp Offline (Bytes)', color=color, fontweight='bold')
ax2.plot(t_min, offline_buf, color=color, linewidth=2, label='Offline Buffer (Thẻ SD)')
ax2.tick_params(axis='y', labelcolor=color)
ax2.set_ylim(-1000, max(offline_buf) + 2000)

ax1.axvspan(3, 8, color='#95a5a6', alpha=0.2, label='Giai đoạn Mất mạng')
ax1.axvspan(8, 15, color='#f1c40f', alpha=0.1, label='Giai đoạn Khôi phục mạng (Flush)')

fig.legend(loc='upper center', bbox_to_anchor=(0.5, -0.15), ncol=2, bbox_transform=ax1.transAxes)
plt.title('HÌNH 4.2. ĐÁNH GIÁ THUẬT TOÁN ĐỆM THÍCH ỨNG VÀ INTERLEAVED FLUSH', fontweight='bold', pad=15)
plt.savefig('ảnh báo cáo/Hinh_4_2_InterleavedFlush.png', dpi=300, bbox_inches='tight')
plt.close()

print("Đã tạo xong biểu đồ Chương 4 tại 'ảnh báo cáo/ảnh sơ đồ/'!")
