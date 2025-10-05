# Proxy Downloader — README

**Mục đích:** Tự động tải danh sách proxy từ Webshare, lưu master và chia thành 2 file.

## Build
Mở Visual Studio Developer Command Prompt:
```cmd
cl /EHsc proxy_downloader.cpp winhttp.lib
```

## Cài Scheduled Task (Admin)
Mở CMD **Run as Administrator**, vào thư mục chứa exe, chạy:
```cmd
proxy_downloader.exe install-task 02:00
```
(default 02:00 nếu không truyền tham số). Task redirect stdout/stderr vào `run.log` cạnh exe.

## Gỡ Task
```cmd
proxy_downloader.exe uninstall-task
```

## Chạy thủ công / test
```cmd
proxy_downloader.exe
# hoặc (không khuyến nghị, chỉ test)
proxy_downloader.exe loop
```

## Nơi lưu file
- Master: `C:\Users\Administrator\Desktop\G2G\proxy\Webshare 100 proxies.txt`  
- Split 1: `C:\Users\Administrator\Desktop\G2G\01\proxy\proxy.txt`  
- Split 2: `C:\Users\Administrator\Desktop\G2G\02\proxy\proxy.txt`  
- Task log: `run.log` (cùng thư mục exe)

## Ghi chú ngắn
- Để tạo task bằng `install-task` **phải chạy dưới quyền Admin**.  
- Đổi giờ: chạy lại `install-task <HH:MM>` (sẽ overwrite).  
- Nếu task bị bỏ lỡ do máy ngủ/tắt: bật `Wake the computer to run this task` và `Run task as soon as possible after a missed start` trong Task Scheduler (GUI).

---
