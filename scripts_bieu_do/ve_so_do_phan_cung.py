import matplotlib.pyplot as plt
import matplotlib.patches as patches
import os

RED = '#DC2626'
ORG = '#EA580C'
BLK = '#1E293B'
BLU = '#2563EB'
COL_I2C = '#8B5CF6'
COL_UART = '#10B981'
COL_SPI = '#3B82F6'
COL_IO = '#F59E0B'

def generate_hardware_diagram():
    fig, ax = plt.subplots(figsize=(18, 11))
    ax.set_xlim(-3.5, 15)
    ax.set_ylim(-1.5, 10.5)
    ax.axis('off')

    pins = {}

    def draw_box(prefix, x, y, w, h, title, pins_left, pins_right, bg_color):
        rect = patches.Rectangle((x, y), w, h, linewidth=3, edgecolor='#334155', facecolor=bg_color)
        ax.add_patch(rect)
        ax.text(x + w/2, y + h - 0.35, title, ha='center', va='center', 
                fontweight='bold', fontsize=13, color='#0F172A', family='sans-serif')
        ax.plot([x, x+w], [y+h-0.7, y+h-0.7], color='#334155', lw=2)

        if pins_left:
            step = (h - 0.7) / (len(pins_left) + 1)
            for i, pin in enumerate(pins_left):
                py = y + h - 0.7 - step * (i + 1)
                ax.text(x + 0.15, py, pin, ha='left', va='center', fontsize=11, family='monospace', fontweight='bold')
                ax.plot([x-0.2, x], [py, py], color='#475569', lw=3)
                if pin: pins[f"{prefix}_{pin}"] = (x - 0.2, py)
                
        if pins_right:
            step = (h - 0.7) / (len(pins_right) + 1)
            for i, pin in enumerate(pins_right):
                py = y + h - 0.7 - step * (i + 1)
                ax.text(x + w - 0.15, py, pin, ha='right', va='center', fontsize=11, family='monospace', fontweight='bold')
                ax.plot([x+w, x+w+0.2], [py, py], color='#475569', lw=3)
                if pin: pins[f"{prefix}_{pin}"] = (x + w + 0.2, py)

    # Khối Adapter 12V
    draw_box("PWR", -3.0, 7.5, 2.5, 2, "Power Adapter\n(12V - 3A)", 
             [], ["12V", "GND"], "#F8FAFC")

    # Khối LM2596
    draw_box("LM", 0.5, 7.5, 3.5, 2, "LM2596 Power Supply\n(Step-down 5V)", 
             ["IN+ (12V)", "IN- (GND)"], ["OUT+ (5V)", "OUT- (GND)"], "#FEE2E2")

    # ESP32
    draw_box("ESP", 5.8, 2, 4, 6.5, "ESP32 DevKit V1\n(Central Controller)", 
             ["VIN (5V)", "3V3 (Output)", "GND", "", "GPIO32 (SDA)", "GPIO33 (SCL)", "", "GPIO16 (RX2)", "GPIO17 (TX2)"],
             ["GPIO26 (TX1)", "GPIO27 (RX1)", "GPIO4 (RST)", "", "GPIO5 (CS)", "GPIO18 (SCK)", "GPIO23 (MOSI)", "GPIO19 (MISO)", "", "GPIO25 (Fault_IN)", "GPIO2 (LED)"], 
             "#F1F5F9")

    # Dời SIM, SD, JMP sang phải để lấy không gian vẽ Tụ (x = 12.0)
    draw_box("SIM", 12.0, 6.5, 2.8, 3, "A7680C\n(LTE Module)", 
             ["5V (VIN)", "RX", "TX", "RST", "GND"], [], "#FEF3C7")

    draw_box("SD", 12.0, 2, 2.8, 3.5, "MicroSD Module", 
             ["VCC (5V)", "CS", "SCK", "MOSI", "MISO", "GND"], [], "#E0F2FE")

    draw_box("MPU", 0.5, 4.3, 2.5, 2.5, "MPU6050 (IMU)\n(SIL Simulated)", 
             [], ["VCC (5V)", "SDA", "SCL", "GND"], "#DCFCE7")

    draw_box("GPS", 0.5, 1.3, 2.5, 2.5, "GPS NEO-7M\n(SIL Simulated)", 
             [], ["VCC (5V)", "TX", "RX", "GND"], "#DCFCE7")

    draw_box("JMP", 12.0, 0.2, 2.8, 1.5, "LTE Fault Trigger\n(Fault Injection)", 
             ["Fault_IN", "GND"], [], "#F3F4F6")

    # Hàm đi dây vuông góc
    def route(p1, p2, cx, color):
        ax.plot([p1[0], cx], [p1[1], p1[1]], color=color, lw=3.5)
        ax.plot([cx, cx], [p1[1], p2[1]], color=color, lw=3.5)
        ax.plot([cx, p2[0]], [p2[1], p2[1]], color=color, lw=3.5)

    # Ký hiệu Mass (GND)
    def draw_gnd(p, direction='left'):
        x, y = p
        px = x - 0.3 if direction == 'left' else x + 0.3
        ax.plot([x, px], [y, y], color=BLK, lw=3)
        ax.plot([px, px], [y, y-0.15], color=BLK, lw=3)
        ax.plot([px-0.2, px+0.2], [y-0.15, y-0.15], color=BLK, lw=3)
        ax.plot([px-0.12, px+0.12], [y-0.22, y-0.22], color=BLK, lw=3)
        ax.plot([px-0.05, px+0.05], [y-0.29, y-0.29], color=BLK, lw=3)

    # Thực thi vẽ GND
    draw_gnd(pins['LM_OUT- (GND)'], 'right')
    draw_gnd(pins['ESP_GND'], 'left')
    draw_gnd(pins['SIM_GND'], 'left')
    draw_gnd(pins['SD_GND'], 'left')
    draw_gnd(pins['MPU_GND'], 'right')
    draw_gnd(pins['GPS_GND'], 'right')
    draw_gnd(pins['JMP_GND'], 'left')

    # Nguồn 12V nối trực tiếp (12V và GND)
    ax.plot([pins['PWR_12V'][0], pins['LM_IN+ (12V)'][0]], [pins['PWR_12V'][1], pins['LM_IN+ (12V)'][1]], color=RED, lw=4)
    ax.plot([pins['PWR_GND'][0], pins['LM_IN- (GND)'][0]], [pins['PWR_GND'][1], pins['LM_IN- (GND)'][1]], color=BLK, lw=4)

    # Dây đỏ Bus 5V
    plm = pins['LM_OUT+ (5V)']
    pesp = pins['ESP_VIN (5V)']
    psim = pins['SIM_5V (VIN)']
    psd = pins['SD_VCC (5V)']
    pmpu_vcc = pins['MPU_VCC (5V)']
    pgps_vcc = pins['GPS_VCC (5V)']
    
    ax.plot([plm[0], 4.9], [plm[1], plm[1]], color=RED, lw=4)
    ax.plot([4.9, 4.9], [plm[1], 9.2], color=RED, lw=4)  # Đi lên ray nguồn chính
    ax.plot([4.9, 10.9], [9.2, 9.2], color=RED, lw=4)    # Ray ngang nguồn 5V
    
    # Thả xuống ESP32
    ax.plot([4.9, 4.9], [9.2, pesp[1]], color=RED, lw=4)
    ax.plot([4.9, pesp[0]], [pesp[1], pesp[1]], color=RED, lw=4)

    # Thả xuống SIM
    ax.plot([10.9, 10.9], [9.2, psim[1]], color=RED, lw=4)
    ax.plot([10.9, psim[0]], [psim[1], psim[1]], color=RED, lw=4)
    
    # Thả xuống SD
    ax.plot([10.9, 10.9], [psim[1], psd[1]], color=RED, lw=4)
    ax.plot([10.9, psd[0]], [psd[1], psd[1]], color=RED, lw=4)

    # Thả xuống MPU và GPS (nhánh 5V bên trái)
    ax.plot([4.9, 4.9], [plm[1], pgps_vcc[1]], color=RED, lw=4) # Dọc xuống
    ax.plot([4.9, pmpu_vcc[0]], [pmpu_vcc[1], pmpu_vcc[1]], color=RED, lw=4) # Ngang vào MPU
    ax.plot([4.9, pgps_vcc[0]], [pgps_vcc[1], pgps_vcc[1]], color=RED, lw=4) # Ngang vào GPS

    # Tín hiệu (Phải)
    route(pins['ESP_GPIO26 (TX1)'], pins['SIM_RX'], 10.2, COL_UART)
    route(pins['ESP_GPIO27 (RX1)'], pins['SIM_TX'], 10.35, COL_UART)
    route(pins['ESP_GPIO4 (RST)'], pins['SIM_RST'], 10.5, COL_IO)
    
    route(pins['ESP_GPIO5 (CS)'], pins['SD_CS'], 10.65, COL_SPI)
    route(pins['ESP_GPIO18 (SCK)'], pins['SD_SCK'], 10.50, COL_SPI)
    route(pins['ESP_GPIO23 (MOSI)'], pins['SD_MOSI'], 10.35, COL_SPI)
    route(pins['ESP_GPIO19 (MISO)'], pins['SD_MISO'], 10.20, COL_SPI)
    
    route(pins['JMP_Fault_IN'], pins['ESP_GPIO25 (Fault_IN)'], 10.1, COL_IO)

    # Tín hiệu (Trái)
    route(pins['ESP_GPIO32 (SDA)'], pins['MPU_SDA'], 5.3, COL_I2C)
    route(pins['ESP_GPIO33 (SCL)'], pins['MPU_SCL'], 5.1, COL_I2C)
    
    route(pins['ESP_GPIO16 (RX2)'], pins['GPS_TX'], 5.1, COL_UART)
    route(pins['ESP_GPIO17 (TX2)'], pins['GPS_RX'], 5.3, COL_UART)

    # Tiêu đề & Chú thích
    plt.text(6, 10.0, "EXPERIMENTAL HARDWARE WIRING DIAGRAM", ha='center', fontweight='bold', fontsize=18, color='#0F172A')
    
    leg_x, leg_y = 1.75, -1.2
    rect = patches.Rectangle((leg_x, leg_y), 8.5, 1.6, linewidth=2, edgecolor='#94A3B8', facecolor='#F8FAFC')
    ax.add_patch(rect)
    plt.text(leg_x+4.25, leg_y+1.35, "WIRING LEGEND", ha='center', va='top', fontweight='bold', fontsize=12)
    
    # Cột 1
    ax.plot([leg_x+0.2, leg_x+1.0], [leg_y+0.95, leg_y+0.95], color=RED, lw=4); plt.text(leg_x+1.2, leg_y+0.95, "Power (5V/12V)", va='center', fontsize=11, fontweight='bold')
    ax.plot([leg_x+0.2, leg_x+1.0], [leg_y+0.55, leg_y+0.55], color=BLK, lw=4); plt.text(leg_x+1.2, leg_y+0.55, "Ground (GND)", va='center', fontsize=11, fontweight='bold')
    ax.plot([leg_x+0.2, leg_x+1.0], [leg_y+0.15, leg_y+0.15], color=COL_IO, lw=4); plt.text(leg_x+1.2, leg_y+0.15, "Digital I/O (RST, Fault)", va='center', fontsize=11, fontweight='bold')

    # Cột 2
    ax.plot([leg_x+4.5, leg_x+5.3], [leg_y+0.95, leg_y+0.95], color=COL_UART, lw=4); plt.text(leg_x+5.5, leg_y+0.95, "UART", va='center', fontsize=11, fontweight='bold')
    ax.plot([leg_x+4.5, leg_x+5.3], [leg_y+0.55, leg_y+0.55], color=COL_SPI, lw=4); plt.text(leg_x+5.5, leg_y+0.55, "SPI", va='center', fontsize=11, fontweight='bold')
    ax.plot([leg_x+4.5, leg_x+5.3], [leg_y+0.15, leg_y+0.15], color=COL_I2C, lw=4); plt.text(leg_x+5.5, leg_y+0.15, "I2C", va='center', fontsize=11, fontweight='bold')

    out_path = os.path.join(os.path.dirname(__file__), 'so_do_hoan_thien_v23.png')
    plt.savefig(out_path, dpi=300, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f"[OK] Đã tạo ảnh hoàn thiện V23 tại: {out_path}")

if __name__ == "__main__":
    generate_hardware_diagram()
