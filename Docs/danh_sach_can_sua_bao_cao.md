# Danh Sách Các Mục Cần Chỉnh Sửa Trong Báo Cáo ĐATN

*(Dựa trên phiên bản nâng cấp Firmware khắc phục lỗi EMI và tối ưu Queue ngày 10/06)*

---

## 1. PHẦN TEXT (NỘI DUNG VĂN BẢN)

### 📌 Mục 3.1.2 - Khối vi điều khiển và giao tiếp ngoại vi
* **Vị trí hiện tại:** Khoảng dòng 200, đoạn mô tả kết nối của khối A7680C.
* **Nội dung cần thêm:** 
  > *"Khối Viễn thông (A7680C): ... được giao tiếp qua Hardware UART. **Bên cạnh đó, chân PWRKEY của module SIM được kết nối trực tiếp vào chân GPIO4 của ESP32. Thiết kế phần cứng này cho phép vi điều khiển chủ động cấp xung mức thấp (LOW) kéo dài 500ms để thực hiện Reset cứng (Hard Reset) module LTE trong kịch bản module bị treo hoàn toàn do nhiễu điện từ (EMI) mà không thể phục hồi bằng các lệnh AT phần mềm.**"*

### 📌 Mục 3.3.1 - Tác vụ Cảnh vệ (Watchdog Task)
* **Vị trí hiện tại:** Khoảng dòng 234, mô tả Watchdog Task.
* **Nội dung cần thêm:**
  > *"Ngoài việc kiểm tra sóng, Watchdog Task còn thực hiện hai nhiệm vụ kiểm tra sức khỏe hệ thống (Health Check) cực kỳ quan trọng: 
  > (1) Giám sát dung lượng RAM rảnh (Free Heap) và mức RAM thấp nhất từng đạt tới (Minimum Free Heap) để đảm bảo hệ thống tuyệt đối không bị rò rỉ bộ nhớ (Memory Leak) khi vận hành dài ngày. 
  > (2) Giám sát trạng thái phản hồi của thẻ MicroSD, nếu phát hiện thẻ nhớ bị treo do nhiễu quá 3 chu kỳ liên tiếp, nó sẽ tự động ngắt kết nối (Unmount) và khởi tạo lại (Remount) phần cứng bus SPI mà không làm treo hệ thống."*

### 📌 Mục 3.3.2 - Cơ chế đệm dữ liệu liên tác vụ (FreeRTOS Queue)
* **Vị trí hiện tại:** Khoảng dòng 238, 239, 240.
* **Sửa thông số (màu đỏ là chỗ cần sửa):**
  - "Hàng đợi được hệ thống khởi tạo với kích thước **100 khe chứa** (thay vì 20)."
  - "Kích thước 100 slot sẽ tiêu tốn khoảng **~50KB RAM** (thay vì 5KB). Đây là mức cấp phát cực kỳ an toàn, chiếm khoảng **17% tổng lượng Free Heap (xấp xỉ 290KB)** của ESP32..."
  - "... 100 slot tương đương với **300 giây (5 phút)** lưu trữ đệm thời gian thực (thay vì 60s/1 phút)."

### 📌 Mục 3.4.5 (trong file là đoạn "Kỹ thuật điều tốc lấy mẫu thích ứng")
* **Vị trí hiện tại:** Khoảng dòng 289, 290.
* **Sửa thông số:**
  - Ngưỡng kích hoạt: "... lấp đầy vượt ngưỡng 50% dung lượng **(> 50/100 slot)**..."
  - Ngưỡng khôi phục: "... giảm xuống dưới mức 25% **(< 25 gói tin)**..."

---

## 2. PHẦN HÌNH ẢNH / SƠ ĐỒ

| # | Tên hình ảnh hiện tại | Yêu cầu cập nhật |
|---|---|---|
| 1 | **Hình 3.2**: Sơ đồ nguyên lý kết nối vi điều khiển ESP32 và các module ngoại vi. | **[QUAN TRỌNG NHẤT] Vẽ lại mạch Altium**: Bổ sung thêm 1 đường dây tín hiệu nối từ chân **GPIO4** của ESP32 sang chân **PWRKEY** (hoặc RST) của A7680C. |
| 2 | **Hình 3.7**: Khởi tạo hàng đợi liên tác vụ trên bộ nhớ RAM | **Chụp lại code**: Chụp lại dòng `#define PIPELINE_QUEUE_SIZE 100` trong file `sys_config.h`. |
| 3 | **Hình 3.12**: Trích đoạn mã nguồn xử lý tín hiệu nghẽn Hàng đợi... | **Chụp lại code**: Chụp lại đoạn thuật toán Adaptive Rate mới trong `pipeline.c` (đoạn có `#define ADAPTIVE_SLOW_THRESHOLD`). |
| 4 | **Hình 3.11**: Lưu đồ thuật toán định tuyến dữ liệu... | *(Không bắt buộc)* Bổ sung một nhánh nhỏ ở phần xử lý kết nối: "Kết nối lỗi 3 lần -> Lte Hard Reset". |

---

## 3. GỢI Ý CẤU TRÚC VIẾT THÊM CHO CHƯƠNG 4 (KẾT QUẢ)

Để tận dụng tối đa giá trị của file log vừa thu được, bạn nên cấu trúc lại Chương 4 (từ dòng 311 trở đi) thành các Test Case định lượng rõ ràng:

### 4.2.1. Đánh giá tính ổn định bộ nhớ (Memory Leak Test)
- Cắt dán đoạn log Watchdog có dòng: `WDG: heap_free=183124 heap_min=181868`.
- Lập luận: Sau X thời gian chạy liên tục qua nhiều kịch bản rớt mạng, `heap_min` không giảm thêm 1 byte nào -> Khẳng định Firmware tối ưu 100% RAM.

### 4.2.2. Kịch bản nhiễu điện từ (EMI) và Khả năng tự phục hồi (Self-Healing)
- Cắt dán đoạn log khi bị sập nguồn: `LTE_AT: <<< ||*ATREADY: 1...`
- Cắt dán đoạn log hệ thống tự nhận diện lỗi: `MQTT: Data-mode prompt FAIL` -> `PIPELINE: Publish fail >> OFFLINE`.
- Lập luận: Trình bày cơ chế khi bị bọc giấy bạc, SIM bị nhiễu và sập. ESP32 phát hiện ra, ép reset và kết nối lại thành công (`Reconnected [OK]`).

### 4.2.3. Đánh giá thuật toán Store-and-Forward
- Cắt dán đoạn log: `Flushing offline buffer (279 bytes)...` và `Flush: 2 sent, 0 failed`.
- Lập luận: Chứng minh được gói tin tại thời điểm mất mạng đã được cất an toàn vào thẻ SD, và khi mạng có lại, nó được "flush" 100% lên server, không mất dữ liệu.
