"""
ve_so_do_chuong3.py
Vẽ 3 sơ đồ bổ sung cho Chương 3 báo cáo DATN
Chạy: python ve_so_do_chuong3.py
Output: 3 file PNG trong cùng thư mục
"""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
import matplotlib.gridspec as gridspec
import numpy as np

# ============================================================
# HÌNH 1: BẢNG ÁNH XẠ GPIO (PIN MAPPING TABLE)
# ============================================================
def ve_bang_gpio():
    fig, ax = plt.subplots(figsize=(14, 6))
    ax.axis('off')

    headers = ["Ngoại vi", "Giao tiếp", "Chân ESP32", "Hướng", "Điện áp", "Ghi chú"]
    rows = [
        ["A7680C LTE", "UART1", "TX→GPIO26\nRX→GPIO27", "Bidirectional", "5V", "Baud 115200"],
        ["A7680C LTE\n(Hard Reset)", "GPIO", "GPIO4 (RST)", "Output", "3.3V", "LOW 500ms = Hard Reset"],
        ["GPS NEO-7M", "UART2", "TX2→GPIO17\nRX2→GPIO16", "Input", "3.3V", "Baud 9600"],
        ["MPU6050 IMU", "I2C", "SCL→GPIO33\nSDA→GPIO32", "Bidirectional", "3.3V", "Địa chỉ 0x68"],
        ["MicroSD", "SPI (VSPI)", "CS=5, SCK=18\nMOSI=23, MISO=19", "Bidirectional", "3.3V", "4MHz, FAT32"],
        ["LED Onboard", "GPIO", "GPIO13", "Output", "3.3V", "Báo trạng thái boot"],
        ["Fault Injection\n(Demo/Test)", "GPIO\n(Input-only)", "GPIO34", "Input", "3.3V", "Pull-up; nối GND\n= mô phỏng mất mạng"],
    ]

    col_widths = [0.16, 0.12, 0.18, 0.13, 0.10, 0.25]
    col_positions = [0.01]
    for w in col_widths[:-1]:
        col_positions.append(col_positions[-1] + w)

    # Header
    for i, (h, x, w) in enumerate(zip(headers, col_positions, col_widths)):
        rect = FancyBboxPatch((x, 0.88), w - 0.005, 0.10,
                              boxstyle="round,pad=0.005",
                              facecolor='#1a3a5c', edgecolor='white', linewidth=1.5,
                              transform=ax.transAxes, clip_on=False)
        ax.add_patch(rect)
        ax.text(x + w/2 - 0.003, 0.93, h, transform=ax.transAxes,
                ha='center', va='center', fontsize=9, fontweight='bold',
                color='white', fontfamily='DejaVu Sans')

    row_colors = ['#eaf2fb', '#ffffff', '#eaf2fb', '#ffffff', '#eaf2fb', '#ffffff', '#eaf2fb']
    row_height = 0.11
    for r, (row, bg) in enumerate(zip(rows, row_colors)):
        y = 0.88 - (r + 1) * row_height
        for i, (cell, x, w) in enumerate(zip(row, col_positions, col_widths)):
            rect = FancyBboxPatch((x, y), w - 0.005, row_height - 0.005,
                                  boxstyle="round,pad=0.002",
                                  facecolor=bg, edgecolor='#aaaaaa', linewidth=0.8,
                                  transform=ax.transAxes, clip_on=False)
            ax.add_patch(rect)
            ax.text(x + w/2 - 0.003, y + row_height/2, cell,
                    transform=ax.transAxes,
                    ha='center', va='center', fontsize=8,
                    color='#1a1a1a', fontfamily='DejaVu Sans')

    ax.set_title("Bảng 3.x: Ánh xạ chân tín hiệu phần cứng (Pin Mapping)\n"
                 "Hệ thống Giám sát Hành trình Tàu hỏa IoT",
                 fontsize=12, fontweight='bold', pad=15, color='#1a3a5c')
    plt.tight_layout()
    plt.savefig('bang_gpio_mapping.png', dpi=180, bbox_inches='tight',
                facecolor='white', edgecolor='none')
    plt.close()
    print("[OK] Saved: bang_gpio_mapping.png")


# ============================================================
# HÌNH 2: SƠ ĐỒ KIẾN TRÚC BỘ ĐỆM HAI TẦNG (TWO-TIER BUFFER)
# ============================================================
def ve_two_tier_buffer():
    fig, ax = plt.subplots(figsize=(14, 7))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 7)
    ax.axis('off')

    def box(x, y, w, h, text, fc='#dbeafe', ec='#1d4ed8', fs=9, bold=False):
        rect = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.15",
                              facecolor=fc, edgecolor=ec, linewidth=2)
        ax.add_patch(rect)
        fw = 'bold' if bold else 'normal'
        ax.text(x + w/2, y + h/2, text, ha='center', va='center',
                fontsize=fs, fontweight=fw, color='#1e293b',
                fontfamily='DejaVu Sans', multialignment='center')

    def arrow(x1, y1, x2, y2, label='', color='#475569'):
        ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                    arrowprops=dict(arrowstyle='->', color=color,
                                   lw=2, connectionstyle='arc3,rad=0'))
        if label:
            mx, my = (x1+x2)/2, (y1+y2)/2
            ax.text(mx + 0.15, my, label, fontsize=7.5, color=color,
                    style='italic', ha='left', va='center')

    # --- Sensor Task ---
    box(0.3, 5.2, 2.8, 1.2, "Sensor Task\n(Core 1 — Priority 3)\nChu kỳ: 3s/sample",
        fc='#f0fdf4', ec='#16a34a', bold=True)

    # --- Tier 1: RAM Queue ---
    ax.add_patch(FancyBboxPatch((4.0, 4.2), 4.5, 2.2,
                                boxstyle="round,pad=0.2",
                                facecolor='#fef9c3', edgecolor='#ca8a04', linewidth=2.5))
    ax.text(6.25, 6.7, "Tầng 1: Bộ đệm Sơ cấp (RAM Queue)",
            ha='center', va='center', fontsize=9, fontweight='bold', color='#92400e')
    box(4.2, 4.4, 4.1, 1.7,
        "FreeRTOS Queue\n100 slot × 512B ≈ 50KB\nBuffer ≈ 5 phút\n(Bộ nhớ bay hơi — Volatile)",
        fc='#fef3c7', ec='#f59e0b', fs=8.5)

    # --- Tier 2: SD Card ---
    ax.add_patch(FancyBboxPatch((4.0, 1.2), 4.5, 2.5,
                                boxstyle="round,pad=0.2",
                                facecolor='#e0f2fe', edgecolor='#0369a1', linewidth=2.5))
    ax.text(6.25, 3.95, "Tầng 2: Bộ đệm Thứ cấp (MicroSD)",
            ha='center', va='center', fontsize=9, fontweight='bold', color='#0c4a6e')
    box(4.2, 1.9, 1.8, 1.5, "offline.buf\nStore-and-Forward\nBền vững\nsau sập nguồn",
        fc='#bae6fd', ec='#0284c7', fs=7.5)
    box(6.3, 1.9, 1.8, 1.5, "data.log\nBlackbox Log\n5MB/file\n30 ngày",
        fc='#bae6fd', ec='#0284c7', fs=7.5)

    # --- MQTT Cloud ---
    box(10.5, 4.2, 2.8, 2.2, "☁ MQTT Broker\nTLS Port 8883\n(Cloud)",
        fc='#dcfce7', ec='#15803d', bold=True)

    # --- Transmit Task ---
    box(10.5, 1.2, 2.8, 2.2, "Transmit Task\n(Core 0 — Priority 4)\nFSM điều phối",
        fc='#faf5ff', ec='#7c3aed', bold=True)

    # Arrows
    # Sensor → Queue (normal)
    arrow(3.1, 5.8, 4.0, 5.8, "xQueueSend()\nNormal path", color='#16a34a')
    # Sensor → offline.buf (spill-over)
    ax.annotate('', xy=(5.5, 3.4), xytext=(1.7, 5.2),
                arrowprops=dict(arrowstyle='->', color='#dc2626', lw=1.8,
                                connectionstyle='arc3,rad=0.3'))
    ax.text(2.8, 4.3, "Queue FULL\nPassive Spill-over", fontsize=7.5,
            color='#dc2626', style='italic', ha='center')

    # Queue → MQTT (online)
    arrow(8.5, 5.5, 10.5, 5.5, "PIPE_ONLINE:\nPop → Publish", color='#15803d')
    # Queue → offline.buf (active drain)
    arrow(6.25, 4.2, 6.25, 3.4, "PIPE_OFFLINE:\nActive Drain", color='#b45309')
    # offline.buf → MQTT (flush)
    ax.annotate('', xy=(10.5, 3.0), xytext=(8.3, 2.6),
                arrowprops=dict(arrowstyle='->', color='#0369a1', lw=1.8,
                                connectionstyle='arc3,rad=-0.2'))
    ax.text(9.5, 2.5, "Reconnect:\nInterleaved Flush 4:10", fontsize=7.5,
            color='#0369a1', style='italic', ha='center')
    # MQTT → data.log
    ax.annotate('', xy=(7.2, 3.4), xytext=(10.5, 4.8),
                arrowprops=dict(arrowstyle='->', color='#64748b', lw=1.2,
                                linestyle='dashed', connectionstyle='arc3,rad=0.3'))
    ax.text(9.3, 4.1, "Publish OK:\nBlackbox copy", fontsize=7,
            color='#64748b', style='italic', ha='center')

    ax.set_title("Hình 3.x: Kiến trúc Bộ đệm Phân cấp Hai tầng (Two-tier Buffering Architecture)",
                 fontsize=11, fontweight='bold', pad=12, color='#1e293b')
    plt.tight_layout()
    plt.savefig('so_do_two_tier_buffer.png', dpi=180, bbox_inches='tight',
                facecolor='white', edgecolor='none')
    plt.close()
    print("[OK] Saved: so_do_two_tier_buffer.png")


# ============================================================
# HÌNH 3: LƯU ĐỒ WATCHDOG TASK
# ============================================================
def ve_luu_do_watchdog():
    fig, ax = plt.subplots(figsize=(13, 14))
    ax.set_xlim(0, 13)
    ax.set_ylim(0, 14)
    ax.axis('off')

    C_PROC  = '#dbeafe'; E_PROC  = '#1d4ed8'
    C_DEC   = '#fef9c3'; E_DEC   = '#b45309'
    C_ACT   = '#dcfce7'; E_ACT   = '#15803d'
    C_ERR   = '#fee2e2'; E_ERR   = '#dc2626'
    C_START = '#1e293b'

    def proc(x, y, w, h, text, fc=C_PROC, ec=E_PROC, fs=8.5):
        rect = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.12",
                              facecolor=fc, edgecolor=ec, linewidth=1.8)
        ax.add_patch(rect)
        ax.text(x+w/2, y+h/2, text, ha='center', va='center',
                fontsize=fs, color='#1e293b', multialignment='center',
                fontfamily='DejaVu Sans')

    def diamond(cx, cy, dx, dy, text, fc=C_DEC, ec=E_DEC, fs=8):
        xs = [cx, cx+dx, cx, cx-dx, cx]
        ys = [cy+dy, cy, cy-dy, cy, cy+dy]
        ax.fill(xs, ys, facecolor=fc, edgecolor=ec, linewidth=1.8)
        ax.text(cx, cy, text, ha='center', va='center', fontsize=fs,
                color='#1e293b', multialignment='center', fontfamily='DejaVu Sans')

    def arr(x1, y1, x2, y2, label='', lpos='mid'):
        ax.annotate('', xy=(x2,y2), xytext=(x1,y1),
                    arrowprops=dict(arrowstyle='->', color='#475569', lw=1.6))
        if label:
            mx = (x1+x2)/2 + 0.15
            my = (y1+y2)/2
            ax.text(mx, my, label, fontsize=7.5, color='#475569', style='italic')

    # Start
    circ = plt.Circle((6.5, 13.5), 0.35, color=C_START)
    ax.add_patch(circ)
    ax.text(6.5, 13.5, "Bắt đầu\n(30s)", ha='center', va='center',
            fontsize=7.5, color='white', fontweight='bold')

    arr(6.5, 13.15, 6.5, 12.6)

    # Check connecting state
    diamond(6.5, 12.1, 2.0, 0.5,
            "Đang ở trạng thái\nConnecting?")
    arr(6.5, 11.6, 6.5, 11.0)
    ax.text(6.65, 11.3, "Không", fontsize=8, color='#475569')
    # Yes branch → skip
    ax.annotate('', xy=(10.5, 12.1), xytext=(8.5, 12.1),
                arrowprops=dict(arrowstyle='->', color='#475569', lw=1.5))
    ax.text(9.0, 12.2, "Có → Bỏ qua", fontsize=8, color='#b45309', style='italic')
    proc(10.5, 11.7, 2.2, 0.8, "Skip vòng lặp\n→ vTaskDelay(30s)",
         fc='#f1f5f9', ec='#94a3b8', fs=8)

    # AT+CSQ
    proc(5.0, 10.3, 3.0, 0.7, "AT+CSQ\nKiểm tra RSSI", fc=C_PROC, ec=E_PROC)
    arr(6.5, 11.0, 6.5, 11.0)
    arr(6.5, 11.0, 6.5, 10.3)

    diamond(6.5, 9.5, 1.5, 0.4, "Phản hồi\nOK?")
    # FAIL branch
    ax.annotate('', xy=(9.5, 9.5), xytext=(8.0, 9.5),
                arrowprops=dict(arrowstyle='->', color='#dc2626', lw=1.5))
    ax.text(8.1, 9.65, "FAIL", fontsize=8, color='#dc2626')
    proc(9.5, 9.1, 3.0, 0.8, "wdg_fail_count++",
         fc=C_ERR, ec=E_ERR, fs=8)
    diamond(11.0, 8.2, 1.3, 0.35, "≥ 3 lần?")
    ax.annotate('', xy=(11.0, 7.6), xytext=(11.0, 7.85),
                arrowprops=dict(arrowstyle='->', color='#dc2626', lw=1.5))
    proc(9.7, 7.1, 2.6, 0.8,
         "s_modem_reset_detected=true\ns_state=PIPE_RECONNECTING",
         fc=C_ERR, ec=E_ERR, fs=7.5)
    ax.text(11.2, 8.0, "Có", fontsize=8, color='#dc2626')
    ax.text(9.5, 8.25, "Không → cũng cập nhật state", fontsize=7.5,
            color='#b45309', style='italic')

    # OK branch
    arr(6.5, 9.1, 6.5, 8.5)
    ax.text(6.65, 8.8, "OK", fontsize=8, color='#15803d')
    diamond(6.5, 8.0, 2.0, 0.45, "Response chứa\nURC boot?\n'RDY'/'*ATREADY'?")
    # Yes
    ax.annotate('', xy=(3.0, 8.0), xytext=(4.5, 8.0),
                arrowprops=dict(arrowstyle='->', color='#dc2626', lw=1.5))
    ax.text(3.5, 8.15, "Có", fontsize=8, color='#dc2626')
    proc(1.2, 7.6, 2.2, 0.8,
         "Modem vừa Reset!\ns_state=PIPE_RECONNECTING",
         fc=C_ERR, ec=E_ERR, fs=7.5)
    # No
    arr(6.5, 7.55, 6.5, 7.0)
    ax.text(6.65, 7.3, "Không", fontsize=8, color='#475569')

    # Time sync
    proc(5.0, 6.3, 3.0, 0.7, "Đồng bộ giờ định kỳ\nlte_time_sync()", fc=C_ACT, ec=E_ACT)
    arr(6.5, 7.0, 6.5, 6.3)

    # SD check
    proc(4.5, 5.4, 4.0, 0.8,
         "Kiểm tra SD:\nfile_size(data.log / offline.buf / processing)",
         fc=C_PROC, ec=E_PROC)
    arr(6.5, 6.3, 6.5, 5.4)

    diamond(6.5, 4.6, 2.0, 0.45, "Tất cả = -1?\n(SD không phản hồi)")
    arr(6.5, 5.4, 6.5, 5.05)

    # SD fail
    ax.annotate('', xy=(10.0, 4.6), xytext=(8.5, 4.6),
                arrowprops=dict(arrowstyle='->', color='#dc2626', lw=1.5))
    ax.text(8.6, 4.75, "Có", fontsize=8, color='#dc2626')
    proc(10.0, 4.2, 2.8, 0.8, "sd_fail_streak++\n≥3 → sd_log_reinit()\nUnmount→Remount",
         fc=C_ERR, ec=E_ERR, fs=7.5)

    # SD ok
    arr(6.5, 4.15, 6.5, 3.55)
    ax.text(6.65, 3.85, "Không", fontsize=8, color='#475569')

    # Heap + heartbeat
    proc(4.5, 2.8, 4.0, 0.75,
         "Log Heap / Queue / Packet Counters\n(produced, delivered, offline, dropped)",
         fc=C_PROC, ec=E_PROC)

    diamond(6.5, 2.1, 2.0, 0.45, "PIPE_ONLINE\nvà đã 30s?")
    arr(6.5, 2.8, 6.5, 2.55)

    # Publish heartbeat
    ax.annotate('', xy=(10.0, 2.1), xytext=(8.5, 2.1),
                arrowprops=dict(arrowstyle='->', color='#15803d', lw=1.5))
    ax.text(8.6, 2.25, "Có", fontsize=8, color='#15803d')
    proc(10.0, 1.7, 2.8, 0.8,
         "Publish Heartbeat JSON\n/status topic",
         fc=C_ACT, ec=E_ACT, fs=8)

    # Loop back
    arr(6.5, 1.65, 6.5, 0.8)
    ax.text(6.65, 1.25, "Không", fontsize=8, color='#475569')
    proc(5.0, 0.3, 3.0, 0.55, "vTaskDelay(30s) → Lặp lại",
         fc='#f1f5f9', ec='#94a3b8', fs=8)

    ax.set_title("Hình 3.x: Lưu đồ hoạt động của Watchdog Task (chu kỳ 30 giây)",
                 fontsize=11, fontweight='bold', pad=10, color='#1e293b')
    plt.tight_layout()
    plt.savefig('luu_do_watchdog_task.png', dpi=180, bbox_inches='tight',
                facecolor='white', edgecolor='none')
    plt.close()
    print("[OK] Saved: luu_do_watchdog_task.png")


# ============================================================
# MAIN
# ============================================================
if __name__ == '__main__':
    import os
    out_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(out_dir)
    print("=== Vẽ sơ đồ bổ sung Chương 3 ===")
    ve_bang_gpio()
    ve_two_tier_buffer()
    ve_luu_do_watchdog()
    print("\nHoàn thành! 3 file PNG đã được lưu trong:", out_dir)
    print("Chèn vào Word theo vị trí:")
    print("  bang_gpio_mapping.png     -> Mục 3.1.2, sau đoạn mô tả 5 ngoại vi")
    print("  so_do_two_tier_buffer.png -> Mục 3.3.2, sau Hình 3.7")
    print("  luu_do_watchdog_task.png  -> Mục 3.3.1, sau 4 bullet Watchdog")
