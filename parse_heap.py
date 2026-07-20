import re
import csv
import os

# Đọc file log vừa copy
log_path = 'log_heap.txt'
if not os.path.exists(log_path):
    print("LỖI: Không tìm thấy file log_heap.txt. Anh đã copy Terminal ra Notepad và lưu đúng tên chưa?")
    exit(1)

with open(log_path, 'r', encoding='utf-8', errors='ignore') as f:
    lines = f.readlines()

# Tạo file CSV để vẽ Excel (dùng utf-8-sig để Excel hiển thị tiếng Việt chuẩn)
with open('heap_data.csv', 'w', newline='', encoding='utf-8-sig') as f_out:
    writer = csv.writer(f_out)
    writer.writerow(['Thời gian (s)', 'Heap_Free (Bytes)', 'Heap_Min_Free (Bytes)'])
    
    time_counter = 0
    found_data = False
    for line in lines:
        if "WDG: heap_free=" in line:
            # Tìm và bóc tách các con số
            match = re.search(r'heap_free=(\d+)\s+heap_min=(\d+)', line)
            if match:
                heap_free = match.group(1)
                heap_min = match.group(2)
                writer.writerow([time_counter, heap_free, heap_min])
                time_counter += 30  # Watchdog của anh nhảy 30s/lần
                found_data = True
                
if found_data:
    print("Tuyệt vời! Đã lọc xong số liệu. Hãy mở file heap_data.csv bằng Excel!")
else:
    print("Ủa alo? File log_heap.txt của anh không có dòng 'WDG: heap_free=' nào cả. Khả năng là chưa chạy đủ lâu hoặc copy nhầm rồi!")
