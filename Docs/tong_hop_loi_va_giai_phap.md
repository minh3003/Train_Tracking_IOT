# Tổng hợp các vấn đề và giải pháp — Hệ thống Train Tracking IoT

> Tài liệu này tổng hợp toàn bộ các lỗ hổng được phát hiện qua quá trình review code,
> phân tích kiến trúc, và kiểm thử thực tế trên phần cứng DevKit.
> Mỗi vấn đề đều có: mô tả, phân tích nguyên nhân gốc, và giải pháp cụ thể.

---

## Mục lục

1. [Lỗi nghiêm trọng — Hệ thống không tự phục hồi khi mất sóng](#1-lỗi-nghiêm-trọng--hệ-thống-không-tự-phục-hồi-khi-mất-sóng)
2. [Bộ đệm Queue quá nhỏ (20 slots = 60 giây)](#2-bộ-đệm-queue-quá-nhỏ)
3. [AT Command Parser có nguy cơ False Positive](#3-at-command-parser-có-nguy-cơ-false-positive)
4. [Thiếu giám sát Memory Leak (Heap Monitoring)](#4-thiếu-giám-sát-memory-leak)
5. [Thiếu phương pháp kiểm thử định lượng](#5-thiếu-phương-pháp-kiểm-thử-định-lượng)
6. [Quản lý dự án — Tài liệu nằm ngoài project](#6-quản-lý-dự-án--tài-liệu-nằm-ngoài-project)
7. [SPI vs SDMMC — Giải trình thiết kế](#7-spi-vs-sdmmc--giải-trình-thiết-kế)
8. [PSRAM — Giải trình thiết kế](#8-psram--giải-trình-thiết-kế)

---

## 1. Lỗi nghiêm trọng — Hệ thống không tự phục hồi khi mất sóng

**Mức độ: CRITICAL**
**Loại: Phần cứng + Phần mềm**

### Hiện tượng

Khi bọc giấy bạc kín anten SIM A7680C (mô phỏng mất sóng hoàn toàn), sau vài chục giây:
- Module SD Card ngừng hoạt động (mọi thao tác ghi/đọc đều fail)
- HOẶC module SIM A7680C bị treo cứng (không phản hồi lệnh AT)
- Khi gỡ giấy bạc ra, hệ thống **không tự phục hồi** — phải reboot thủ công

### Phân tích nguyên nhân gốc

**Nguyên nhân phần cứng: Nhiễu điện từ (EMI)**

Khi mất sóng hoàn toàn, module SIM A7680C dò mạng ở công suất phát cực đại (2W, Class 4).
Giấy bạc tạo thành buồng cộng hưởng (resonant cavity) — năng lượng RF không thoát ra ngoài được
mà phản xạ ngược lại trong khoảng cách vài cm. Điều này tạo ra cường độ trường điện từ cục bộ
rất cao, nhiễu trực tiếp vào:

- **Bus SPI → SD Card**: Dây dupont 10-20cm trên breadboard hoạt động như anten thu.
  Tín hiệu clock (SCK) và data (MOSI/MISO) bị flip bit → CRC fail → SD card controller
  rơi vào trạng thái lỗi.
- **UART → SIM Module**: Byte phản hồi AT bị sai lệch → parser không nhận ra "OK\r\n"
  → timeout liên tục.

> **Lưu ý:** Nguồn cấp 12V/3A qua LM2596 dư sức cho SIM (peak 2A) + SD (100mA).
> Sụt áp (brownout) **không phải** nguyên nhân chính trong cấu hình này.

**Nguyên nhân phần mềm: Không có cơ chế phục hồi**

*Bug 1 — SD Card không có hàm re-init:*

File `sd_log.c`: Khi SD bị lỗi do EMI, biến `s_mounted` vẫn giữ giá trị `true` nhưng mọi
thao tác `fopen()`/`fprintf()` đều fail. Không có hàm nào trong code thực hiện unmount rồi
mount lại thẻ SD. Một khi SD lỗi, nó **lỗi vĩnh viễn** cho đến khi reboot.

*Bug 2 — SIM Module không có hardware reset:*

File `pin_config.h`: Chỉ khai báo chân UART (TX=26, RX=27), **không có chân PWRKEY** để
reset cứng module SIM. Khi module SIM bị treo do EMI, Watchdog phát hiện (AT fail 3 lần)
và gọi `lte_full_init()` — nhưng hàm này chỉ gửi lệnh AT qua UART. Module đã treo thì
không phản hồi AT → retry vô hạn → không bao giờ phục hồi.

### Giải pháp

#### Phần cứng (thay đổi trên breadboard, không cần đổi board)

| # | Hành động | Chi tiết |
|---|---|---|
| 1 | Nối chân PWRKEY | Trên DevKit A7680C, tìm chân **PWRKEY** → nối vào **GPIO 4** của ESP32. Cho phép ESP32 reset cứng module SIM bằng cách kéo PWRKEY LOW 1,5 giây. |
| 2 | Tách xa dây SPI | Đặt module SD Card và module SIM ở **hai đầu đối diện** của breadboard. Dây SPI (CS, SCK, MOSI, MISO) giữ ngắn nhất có thể (< 10cm). |

#### Phần mềm (sửa code)

| # | File | Thay đổi |
|---|---|---|
| 1 | `pin_config.h` | Thêm `#define LTE_PWRKEY_PIN 4` |
| 2 | `lte_at.c` | Thêm hàm `lte_hard_reset()`: toggle PWRKEY LOW 1,5s → HIGH → chờ boot 5s |
| 3 | `sd_log.c` | Thêm hàm `sd_log_reinit()`: unmount (`esp_vfs_fat_sdcard_unmount`) → delay 1s → mount lại (`esp_vfs_fat_sdspi_mount`) |
| 4 | `pipeline.c` | Nâng cấp Watchdog: đếm số lần SD fail liên tiếp → nếu ≥ 5 lần → gọi `sd_log_reinit()`. Đếm AT fail → nếu software recovery fail 3 lần → gọi `lte_hard_reset()` thay vì chỉ `lte_full_init()`. |

---

## 2. Bộ đệm Queue quá nhỏ

**Mức độ: HIGH**
**Loại: Phần mềm**

### Hiện tượng

Queue hiện tại chỉ có 20 slots. Khi mất kết nối LTE, Consumer (Transmit Task) ngừng tiêu thụ.
Queue tràn trong **60 giây**. Nếu SD Card cũng không khả dụng → dữ liệu bị mất.

Log thực tế đã ghi nhận:
```
W (89015) PIPELINE: Queue full - SD FAIL #21 (overflow: 1)
```

### Phân tích

| Thông số | Giá trị |
|---|---|
| Kích thước mỗi gói (`data_pkt_t`) | 516 bytes |
| Chu kỳ sensor | 3.000 ms |
| Queue 20 slots: thời gian chịu offline | 20 × 3s = **60 giây** |
| Queue 100 slots: thời gian chịu offline | 100 × 3s = **5 phút** |
| RAM tiêu thụ Queue 100 | 100 × 516 = 51.600 bytes ≈ 50 KB |
| % Heap (292 KB khả dụng) | 17,6% — an toàn |

### Giải pháp

Tăng `PIPELINE_QUEUE_SIZE` trong `sys_config.h` từ **20 → 100**.

Mở rộng bộ đệm RAM từ 60 giây lên 5 phút, đủ cho hầu hết các vùng lõm sóng trên tuyến
đường sắt. Tiêu thụ 17,6% Heap, vẫn dư 62% (~181 KB) cho các tác vụ hệ thống.

> Chi tiết đầy đủ: xem file `Docs/phan_tich_thiet_ke_buffer.md`

---

## 3. AT Command Parser có nguy cơ False Positive

**Mức độ: MEDIUM**
**Loại: Phần mềm**

### Hiện tượng

Hàm `prv_resp_done()` trong `lte_at.c` dùng `strstr()` để kiếm chuỗi `"OK\r\n"` ở
**bất kỳ đâu** trong buffer phản hồi:

```c
// lte_at.c, line 152-158
static bool prv_resp_done(const char *buf)
{
    if (strstr(buf, "\r\nOK\r\n"))    return true;   // tìm ở BẤT KỲ ĐÂU
    if (strstr(buf, "OK\r\n"))         return true;
    if (strstr(buf, "\r\nERROR\r\n")) return true;
    ...
}
```

### Nguy cơ

Nếu một tin nhắn MQTT gửi xuống (payload) tình cờ chứa chuỗi `"OK\r\n"`, hàm sẽ **nhầm lẫn**
đó là phản hồi thành công của module SIM, trong khi module chưa xử lý xong lệnh AT.

Đây là lỗi kinh điển khi parse giao thức AT command. Xác suất xảy ra thấp trong hệ thống
hiện tại (vì payload đều là JSON), nhưng không thể loại trừ nếu mở rộng tính năng sau này.

### Giải pháp

Thay `strstr()` bằng kiểm tra **suffix** (đuôi chuỗi): chỉ khớp khi `"OK\r\n"` hoặc
`"ERROR\r\n"` nằm ở **cuối cùng** của buffer nhận được, không phải ở giữa payload.

```c
// Kiểm tra chuỗi kết thúc nằm ở cuối buffer
static bool prv_ends_with(const char *buf, int len, const char *suffix)
{
    int slen = strlen(suffix);
    if (len < slen) return false;
    return memcmp(buf + len - slen, suffix, slen) == 0;
}
```

---

## 4. Thiếu giám sát Memory Leak

**Mức độ: MEDIUM**
**Loại: Phần mềm**

### Hiện tượng

Code không sử dụng `malloc()` động (tốt), nhưng hội đồng bảo vệ có thể hỏi:
*"Làm sao chứng minh hệ thống không bị rò rỉ bộ nhớ sau 3 tháng chạy liên tục?"*

Hiện tại không có dòng log nào ghi lại trạng thái Heap theo thời gian.

### Giải pháp

Thêm 2 dòng log vào `pipeline_watchdog_task()` trong `pipeline.c`:

```c
LOG_I(TAG, "WDG: Heap free=%lu min=%lu",
      (unsigned long)esp_get_free_heap_size(),
      (unsigned long)esp_get_minimum_free_heap_size());
```

- `esp_get_free_heap_size()`: Heap hiện tại
- `esp_get_minimum_free_heap_size()`: Heap thấp nhất từ lúc boot (watermark)

Nếu giá trị watermark giữ **ổn định** qua nhiều giờ chạy → chứng minh không có Memory Leak.
Đây là bằng chứng định lượng có giá trị học thuật, thay vì chỉ tuyên bố "code không dùng malloc".

---

## 5. Thiếu phương pháp kiểm thử định lượng

**Mức độ: HIGH**
**Loại: Phương pháp luận**

### Hiện tượng

Trong bản nháp Chương 4, phương pháp đánh giá chủ yếu là:
- Quan sát MQTT Explorer bằng mắt thường
- Ngắt mạng LTE vài phút rồi xem log

Đây không đạt yêu cầu học thuật. Thiếu:
- Dữ liệu định lượng (bao nhiêu gói gửi thành công, bao nhiêu mất, latency trung bình)
- Biểu đồ minh chứng (Heap theo thời gian, Queue depth, timeline ONLINE/OFFLINE)
- Quy trình kiểm thử có thể lặp lại (reproducible test procedure)

### Giải pháp

Tạo 2 công cụ Python chạy trên PC:

**Tool 1: `test/serial_stress_test.py`**
- Kết nối Serial Monitor (COM3) qua `pyserial`
- Ghi toàn bộ log vào file CSV có timestamp
- Tự động đếm: PUB OK, PUB FAIL, Queue full, DATA LOST, OFFLINE, Reconnected
- Trích xuất giá trị Heap từ dòng log WDG
- Sau khi dừng (Ctrl+C): in bảng tổng kết Pass/Fail

**Tool 2: `test/generate_test_report.py`**
- Đọc file CSV từ Tool 1
- Vẽ 3 biểu đồ bằng matplotlib:
  - Biểu đồ 1: Heap usage theo thời gian (đường phẳng = không leak)
  - Biểu đồ 2: Queue depth theo thời gian (thấy adaptive rate kích hoạt)
  - Biểu đồ 3: Timeline sự kiện ONLINE/OFFLINE/RECONNECTING
- Export file PNG để dán vào báo cáo Chương 4

**Quy trình test 30 phút:**

| Phút | Hành động | Mục đích |
|---|---|---|
| 0–10 | Để hệ thống chạy bình thường | Baseline: Heap ổn định, Queue < 5, PUB OK liên tục |
| 10–15 | Rút anten SIM (hoặc bọc giấy bạc nhẹ, không kín) | Kích hoạt OFFLINE, ghi SD, Adaptive Rate |
| 15–20 | Cắm anten lại | Reconnect, flush offline buffer, WRR interleave |
| 20–25 | Gửi lệnh `set_interval` = 1000ms qua MQTT | Stress test Queue, kích hoạt Adaptive Rate |
| 25–30 | Gửi lệnh `set_interval` = 3000ms (khôi phục) | Xác nhận hệ thống ổn định trở lại |

---

## 6. Quản lý dự án — Tài liệu nằm ngoài project

**Mức độ: LOW**
**Loại: Quản lý**

### Hiện tượng

File `Báo cáo DATN.docx` nằm ở gốc project, nhưng các bản nháp chương (Chương 1–4)
được soạn trong các phiên làm việc riêng lẻ, không được gộp vào thư mục project.

### Giải pháp

Đã tạo thư mục `Docs/` trong project. Các file phân tích kỹ thuật đã được đặt tại đây:
- `Docs/phan_tich_thiet_ke_buffer.md` — Queue Sizing + WRR Flushing
- `Docs/tong_hop_loi_va_giai_phap.md` — File này

Cần di chuyển `Báo cáo DATN.docx` vào `Docs/` và đưa toàn bộ project lên Git.

---

## 7. SPI vs SDMMC — Giải trình thiết kế

**Mức độ: INFO (Không cần sửa)**
**Loại: Giải trình cho hội đồng**

### Vấn đề từ review

Review đề xuất chuyển giao tiếp thẻ nhớ từ SPI (4MHz) sang SDMMC Host Controller
để tăng tốc độ và giảm tải CPU.

### Phân tích thực tế

| Tiêu chí | SPI (hiện tại) | SDMMC |
|---|---|---|
| Tốc độ | 4MHz → ~400 KB/s | 40MHz → ~4 MB/s |
| Throughput cần thiết | ~140 bytes / 3s = **47 B/s** | 47 B/s |
| Dư thừa so với nhu cầu | **8.500 lần** | 85.000 lần |
| Chân GPIO | 4 chân tùy chọn | 3 chân cố định (IO MUX) |
| Ổn định trên breadboard | ✅ Tốt (tốc độ thấp, ít nhiễu) | ❌ Kém (tốc độ cao, dây dài gây ringing) |
| Cần đấu lại dây | Không | Có (chân 2 đang dùng cho LED) |

### Kết luận

SPI được giữ nguyên vì:
1. Throughput dư **8.500 lần** — bottleneck nằm ở MQTT/LTE (1,2s/publish), không phải SD Card
2. SDMMC trên breadboard prototype **tệ hơn** do signal integrity ở tốc độ cao
3. Không cần thay đổi phần cứng

> Ghi vào báo cáo: *"Giao tiếp SPI được lựa chọn có chủ đích (design trade-off) do tính
> linh hoạt chân GPIO và độ tin cậy cao trên mạch prototype. Với throughput yêu cầu 47 B/s,
> SPI ở 4MHz (400 KB/s) dư thừa 8.500 lần, nên SDMMC không mang lại lợi ích thực tế."*

---

## 8. PSRAM — Giải trình thiết kế

**Mức độ: INFO (Không cần sửa)**
**Loại: Giải trình cho hội đồng**

### Vấn đề từ review

Review đề xuất sử dụng PSRAM (4–8MB) làm Ring Buffer khổng lồ thay vì Queue 20 slots trên RAM.

### Phân tích thực tế

Chip ESP32 trong hệ thống là **ESP32-D0WD-V3** (xác nhận qua log flash: `Chip is ESP32-D0WD-V3`).
Đây là phiên bản **WROOM** — **không có PSRAM** tích hợp.

Giải pháp thay thế: Tăng Queue Size lên 100 slots (50 KB) từ Internal SRAM (292 KB khả dụng).
Đạt được 5 phút buffer mà chỉ dùng 17,6% Heap — không cần phần cứng bổ sung.

> Ghi vào báo cáo: *"Module ESP32-WROOM-32 (D0WD-V3) không tích hợp PSRAM ngoại vi.
> Thay vào đó, hệ thống tối ưu Internal SRAM bằng cách mở rộng Queue lên 100 slots (~50 KB),
> đạt thời gian chịu lỗi 5 phút với mức tiêu thụ 17,6% Heap khả dụng."*

---

## Tổng kết — Trạng thái các vấn đề

| # | Vấn đề | Mức độ | Loại sửa | Trạng thái |
|---|---|---|---|---|
| 1 | Không tự phục hồi khi mất sóng (SD + SIM chết) | CRITICAL | HW + SW | ⏳ Cần sửa |
| 2 | Queue 20 slots quá nhỏ (60 giây) | HIGH | SW | ⏳ Cần sửa |
| 3 | AT Parser false positive | MEDIUM | SW | ⏳ Cần sửa |
| 4 | Thiếu Heap Monitoring | MEDIUM | SW | ⏳ Cần sửa |
| 5 | Thiếu kiểm thử định lượng | HIGH | Tooling | ⏳ Cần tạo tools |
| 6 | Tài liệu nằm ngoài project | LOW | Quản lý | ✅ Đã tạo `Docs/` |
| 7 | SPI vs SDMMC | INFO | Giải trình | ✅ Giữ SPI — có lý do |
| 8 | PSRAM | INFO | Giải trình | ✅ Không có HW — dùng Internal RAM |
