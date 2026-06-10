# Phân tích Thiết kế Hệ thống Bộ đệm và Thuật toán Gửi bù Xen kẽ

## 1. Thiết kế dung lượng Hàng đợi FreeRTOS (Queue Sizing)

### 1.1. Các thông số hệ thống

| Thông số | Giá trị | Ghi chú |
|---|---|---|
| Kích thước mỗi gói dữ liệu (`data_pkt_t`) | 516 bytes | `PAYLOAD_MAX_LEN` (512) + `uint32_t ts` (4) |
| Vùng nhớ Heap khả dụng khi khởi động | 292.644 bytes (~286 KB) | Đo thực tế qua `esp_get_free_heap_size()` |
| Chu kỳ đọc cảm biến (Producer) | 3.000 ms | `SENSOR_INTERVAL_MS` |
| Thời gian publish 1 gói MQTT (Consumer) | ~1.200 ms (bình thường) — ~9.000 ms (sóng yếu) | Đo từ log thực tế (TOPIC + PAYLOAD + PUB) |

### 1.2. Bài toán đặt ra

Khi mất kết nối LTE (ví dụ: tàu đi vào hầm hoặc vùng lõm sóng), Consumer (Transmit Task) ngừng tiêu thụ dữ liệu từ Queue. Toàn bộ gánh nặng lưu trữ đặt lên Queue RAM và thẻ nhớ SD (Tầng 2). Tuy nhiên, trong kịch bản xấu nhất (worst case) khi thẻ SD cũng không khả dụng (lỏng kết nối cơ học do rung lắc), Queue RAM trở thành **tuyến phòng thủ cuối cùng** trước khi dữ liệu bị mất.

**Yêu cầu thiết kế:** Hệ thống phải chịu được tối thiểu **5 phút** mất kết nối mà không mất dữ liệu, ngay cả khi không có thẻ SD.

### 1.3. Tính toán kích thước Queue

Số gói dữ liệu sinh ra trong khoảng thời gian T:

$$N = \frac{T_{offline}}{T_{sensor}} = \frac{T \times 60}{3}$$

| Kích thước Queue | Thời gian chịu offline (worst case, không SD) | RAM tiêu thụ | % Heap |
|---|---|---|---|
| 20 (ban đầu) | 20 × 3s = **60 giây** | 10.320 bytes (~10 KB) | 3,5% |
| 60 | 60 × 3s = **3 phút** | 30.960 bytes (~30 KB) | 10,6% |
| **100** | 100 × 3s = **5 phút** | 51.600 bytes (~50 KB) | **17,6%** |
| 200 | 200 × 3s = **10 phút** | 103.200 bytes (~101 KB) | 35,3% |

### 1.4. Kiểm chứng tính khả thi về bộ nhớ

Tổng bộ nhớ RAM khả dụng sau khi khởi động: **292 KB**.

Phân bổ bộ nhớ khi Queue = 100:

| Thành phần | Dung lượng | Ghi chú |
|---|---|---|
| Queue (100 slots) | ~50 KB | `data_pkt_t` × 100 |
| Stack 3 FreeRTOS Tasks | ~16 KB | Sensor (4KB) + Transmit (8KB) + Watchdog (4KB) |
| UART RX/TX Buffer | ~5 KB | `LTE_RX_BUF_SIZE` (4KB) + `LTE_TX_BUF_SIZE` (1KB) |
| MQTT URC Queue + Buffers | ~10 KB | `s_buf[2048]` + `s_urc_queue` (8 × 1KB) |
| Overhead hệ thống (FreeRTOS, VFS, FAT) | ~30 KB | Ước tính |
| **Tổng sử dụng** | **~111 KB** | |
| **Còn trống** | **~181 KB (62%)** | Đủ an toàn |

### 1.5. Kết luận thiết kế

Giá trị `PIPELINE_QUEUE_SIZE = 100` được chọn dựa trên:

1. **Yêu cầu thời gian chịu lỗi 5 phút** — tương đương thời gian tàu đi qua hầm hoặc vùng lõm sóng trên tuyến đường sắt Bắc-Nam.
2. **Tiêu thụ 17,6% Heap** — nằm trong ngưỡng an toàn, vẫn dư 62% cho các tác vụ hệ thống.
3. **Không yêu cầu phần cứng bổ sung** (PSRAM) — module ESP32-WROOM-32 (D0WD-V3) không có PSRAM ngoại vi, nên giải pháp phải nằm trong phạm vi Internal SRAM.

---

## 2. Thuật toán Gửi bù Xen kẽ (Interleaved Flushing — Weighted Round-Robin)

### 2.1. Bài toán

Khi kết nối LTE được phục hồi, hệ thống phải thực hiện đồng thời hai nhiệm vụ trên cùng một kênh truyền MQTT duy nhất:

- **Gửi bù dữ liệu cũ** (offline buffer trên thẻ SD — có thể lên đến hàng ngàn bản ghi).
- **Gửi dữ liệu thời gian thực** (sensor vẫn đang liên tục đẩy dữ liệu mới vào Queue mỗi 3 giây).

Nếu chỉ ưu tiên gửi dữ liệu cũ: dữ liệu live bị tắc nghẽn, Queue tràn, operator không biết vị trí hiện tại của tàu.
Nếu chỉ ưu tiên gửi dữ liệu live: dữ liệu cũ không bao giờ được gửi hết, offline buffer phình to vô hạn.

### 2.2. Giải pháp: Weighted Round-Robin (WRR)

Thuật toán chia kênh truyền thành các vòng luân phiên (round), mỗi vòng gồm hai pha:

```
Pha 1 (Flush cũ):  Gửi tối đa FLUSH_BATCH_SIZE = 4 gói từ offline buffer
Pha 2 (Drain live): Gửi tối đa 10 gói từ Queue (dữ liệu thời gian thực)
→ Lặp lại cho đến khi offline buffer hết.
```

**Tỉ lệ băng thông:** Live : Cũ = 10 : 4 = **2,5 : 1**

Dữ liệu thời gian thực được ưu tiên gấp 2,5 lần vì trong ứng dụng giám sát tàu, **vị trí hiện tại** quan trọng hơn vị trí lịch sử. Dữ liệu cũ mang tính bổ sung, được gửi dần trong nền (background).

### 2.3. Phân tích chi tiết tham số FLUSH_BATCH_SIZE = 4

Mỗi lần publish một gói qua MQTT tốn **~1,2 giây** (đo từ log thực tế). Vòng lặp flush có thêm `vTaskDelay(200ms)` giữa các gói. Tổng thời gian mỗi gói cũ: **~1,4 giây**.

Trong thời gian flush 4 gói cũ, sensor (Producer) vẫn đang chạy:

$$N_{live} = \frac{FLUSH\_BATCH\_SIZE \times T_{publish\_old}}{T_{sensor}} = \frac{4 \times 1{,}4}{3} \approx 1{,}87 \approx 2 \text{ gói}$$

Bảng so sánh các giá trị FLUSH_BATCH_SIZE:

| FLUSH_BATCH_SIZE | Thời gian chiếm kênh | Gói live tích lũy trong Queue | Đánh giá |
|---|---|---|---|
| 1 | 1,4 giây | ~0,5 gói | Quá bảo thủ — flush 2.000 dòng mất ~46 phút |
| **4** | 5,6 giây | **~2 gói** | **Cân bằng — flush nhanh, live không bị nghẽn** |
| 10 | 14 giây | ~5 gói | Queue bắt đầu tích tụ đáng kể |
| 20 | 28 giây | ~9 gói | Nguy hiểm — gần ngưỡng kích hoạt Adaptive Rate (≥10) |

**Kết luận:** Với `FLUSH_BATCH_SIZE = 4`, trong thời gian flush dữ liệu cũ, Queue live chỉ tích tụ khoảng **2 gói** — rất xa ngưỡng kích hoạt Adaptive Rate Limiting (≥10 gói), đảm bảo sensor không bị giảm tần suất một cách vô lý chỉ vì hệ thống đang bận gửi bù.

### 2.4. Phân tích tham số Drain Live = 10

Con số 10 là **giới hạn trên** (ceiling), không phải số lượng cố định. Hàm `prv_drain_queue()` sử dụng `pdMS_TO_TICKS(50)` làm timeout — nếu Queue chỉ có 2 gói, nó lấy 2 gói rồi thoát ngay, không chờ đủ 10.

| Tình huống | Gói live thực tế trong Queue | Hành vi |
|---|---|---|
| Bình thường (sóng tốt) | 1–2 gói | Drain 1–2 gói trong ~2,4s, quay lại flush cũ |
| Sóng chập chờn (publish chậm) | 5–8 gói | Drain hết 5–8 gói, xả áp lực Queue |
| Spike bất thường | 10+ gói | Drain tối đa 10 gói (~12s), ngăn Queue tiệm cận ngưỡng tràn |

**Tại sao ceiling = 10 mà không phải 5 hay 20?**

- Nếu 5: Khi có spike, chỉ xả được 5 gói, Queue tiếp tục tích tụ → có thể chạm ngưỡng Adaptive Rate.
- Nếu 10: Xả đủ nhanh (12 giây) để kéo Queue về mức an toàn, đồng thời không bỏ quên offline buffer quá lâu.
- Nếu 20: Dành 24 giây chỉ cho live data → offline buffer bị "đóng băng" quá lâu, gây ấn tượng sai rằng hệ thống đã hết dữ liệu cũ.

### 2.5. Mô hình thời gian một vòng WRR

```
┌─── Pha 1: Flush cũ ───┐    ┌──── Pha 2: Drain live ────┐
│ Gói cũ 1  (1.4s)       │    │ Gói live 1  (1.2s)        │
│ Gói cũ 2  (1.4s)       │    │ Gói live 2  (1.2s)        │
│ Gói cũ 3  (1.4s)       │    │ (Queue hết → thoát sớm)   │
│ Gói cũ 4  (1.4s)       │    │                            │
├─────────────────────────┤    ├────────────────────────────┤
│ Tổng: ~5.6s             │    │ Tổng: ~2.4s (thực tế)     │
│ Kênh: dữ liệu lịch sử  │    │ Kênh: dữ liệu thời gian  │
│                         │    │       thực                 │
└─────────────────────────┘    └────────────────────────────┘

Tổng 1 vòng WRR ≈ 8 giây → xử lý 4 gói cũ + 2 gói live = 6 gói
Throughput flush: 4 gói cũ / 8s = 0.5 gói/s
→ Flush 2.000 dòng offline ≈ 67 phút (chấp nhận được, vì live không bị gián đoạn)
```
