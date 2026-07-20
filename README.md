# Phần mềm nhúng (Firmware) Hệ thống Giám sát Hành trình Tàu hỏa IoT

## Project Overview
Dự án này cung cấp mã nguồn phần mềm nhúng (firmware) cho Hệ thống Giám sát Hành trình Tàu hỏa IoT. Hệ thống được phát triển nhằm mục đích thu thập và truyền tải dữ liệu cảm biến theo thời gian thực về vị trí, gia tốc, và trạng thái môi trường trên tàu hỏa. Với môi trường đặc thù của đường sắt thường xuyên bị mất sóng LTE/3G và nhiễu điện từ (EMI), hệ thống được thiết kế hướng tới độ tin cậy cao, tự động phục hồi lỗi và đảm bảo không mất mát dữ liệu.

## System Architecture
Kiến trúc hệ thống bao gồm hai luồng hoạt động song song trên vi điều khiển ESP32: Luồng thu thập dữ liệu (Sensor Core) và Luồng xử lý truyền thông (Network Core). Toàn bộ dữ liệu được đồng bộ hóa và quản lý thông qua cơ chế Queue, Mutex của FreeRTOS, đảm bảo tính an toàn dữ liệu giữa các tiến trình.

## Hardware
- **Vi điều khiển chính:** ESP32 (Dual-core, XTensa LX6)
- **Module kết nối mạng:** Module LTE/4G (Giao tiếp qua tập lệnh AT)
- **Module định vị:** GPS/GNSS
- **Cảm biến:** Gia tốc kế, Cảm biến nhiệt độ/độ ẩm
- **Lưu trữ ngoại tuyến:** Thẻ nhớ SD (Giao tiếp SPI)
- **Nguồn:** Hệ thống quản lý pin và dự phòng năng lượng.

## Software Stack
- **Hệ điều hành:** FreeRTOS (ESP-IDF v5.x)
- **Ngôn ngữ lập trình:** C (C99/C11)
- **Giao thức truyền tải:** MQTT (QoS 1)
- **File System:** FAT32 (Cho thẻ SD)

## Features
- **Store-and-Forward:** Tự động lưu trữ dữ liệu vào thẻ nhớ SD khi mất mạng và gửi tiếp khi kết nối khôi phục (Zero Data Loss).
- **Interleaved Flush & Adaptive Rate:** Xả dữ liệu đệm linh hoạt kết hợp với việc điều chỉnh tốc độ lấy mẫu dựa trên băng thông mạng để tránh quá tải MQTT broker.
- **Hardware-Level Fault Tolerance:** Tích hợp Watchdog tự chuẩn đoán lỗi và điều khiển khôi phục phần cứng (thông qua GPIO PWRKEY) khi module viễn thông bị treo do nhiễu.
- **Core-Pinned Multitasking:** Cô lập các tác vụ để tránh gián đoạn tiến trình quan trọng.

## Directory Structure
- `main/`: Source code cốt lõi của hệ thống (ESP-IDF).
- `Hardware_Architecture/`: Các bản vẽ sơ đồ nguyên lý (Schematic) và layout mạch.
- `Software_Architecture/`: Các tài liệu và sơ đồ khối về luồng phần mềm.
- `Demo/`: Video và hình ảnh hoạt động thực tế của hệ thống.

## Build
Dự án sử dụng toolchain tiêu chuẩn của ESP-IDF. Để biên dịch:

```bash
# 1. Thiết lập môi trường ESP-IDF
get_idf

# 2. Cấu hình dự án (nếu cần thay đổi thông số)
idf.py menuconfig

# 3. Biên dịch mã nguồn thành file binary (.bin)
idf.py build
```

## Flash
Để nạp chương trình xuống thiết bị và theo dõi log hệ thống:

```bash
# 4. Nạp chương trình xuống ESP32 và mở màn hình theo dõi log
idf.py -p COM_PORT flash monitor
```

## Demo Video
*(Link video demo thực tế của hệ thống sẽ được cập nhật tại thư mục Demo)*

## Future Work
- Cải thiện thuật toán **Adaptive Sampling** (Lấy mẫu động) thông minh hơn dựa trên bối cảnh môi trường để tối ưu hóa năng lượng và băng thông.
- Nâng cấp File System từ FAT32 sang LittleFS để tối ưu hóa ghi xóa (Wear Leveling) trên thẻ nhớ, chống phân mảnh bộ nhớ.
- Tích hợp thêm các cơ chế bảo mật cho kết nối MQTT qua TLS (mbedTLS).
- Triển khai cập nhật phần mềm từ xa qua mạng (OTA Update).

## License
[MIT License](LICENSE)
