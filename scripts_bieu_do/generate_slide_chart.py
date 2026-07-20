import matplotlib.pyplot as plt
import numpy as np
import os

# Đảm bảo lưu đúng thư mục hiện tại
output_path = os.path.join(os.path.dirname(__file__), 'Hinh_1_2_DataLoss_Slide.svg')

plt.style.use('seaborn-v0_8-whitegrid')
plt.rcParams['font.family'] = 'sans-serif'

pts_t = np.arange(5, 95, 3)
received_pts = [p for p in pts_t if p < 35 or p > 70]
lost_pts = [p for p in pts_t if 35 <= p <= 70]

# Kích thước khung hình dài và lùn để vừa vặn phía dưới slide
plt.figure(figsize=(12, 2.5))
plt.plot([0, 100], [1, 1], color='black', linewidth=4, zorder=1)

# Chấm tròn to hơn (s=300) để nhìn rõ trên máy chiếu
plt.scatter(received_pts, [1]*len(received_pts), color='#27ae60', s=300, zorder=2, label='Gói dữ liệu thành công')
# Dấu X to và dày hơn (linewidths=5)
plt.scatter(lost_pts, [1]*len(lost_pts), color='#c0392b', s=300, marker='x', linewidths=5, zorder=2, label='Khoảng trống dữ liệu (Thất thoát)')

plt.axvspan(35, 70, color='#95a5a6', alpha=0.3, label='Khu vực lõm sóng (Vào hầm)')
plt.yticks([])

# Font chữ to hơn hẳn (fontsize=18)
plt.xlabel('Thời gian di chuyển', fontsize=18, fontweight='bold')

# Legend to, bỏ viền, đặt phía dưới
plt.legend(loc='upper center', bbox_to_anchor=(0.5, -0.4), ncol=3, fontsize=16, frameon=False)

plt.tight_layout()
# Lưu định dạng Vector (SVG) để chống vỡ nét
plt.savefig(output_path, format='svg', bbox_inches='tight')
plt.close()

print(f"Đã tạo file vector cho Slide tại: {output_path}")
