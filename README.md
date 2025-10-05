Cách dùng nhanh

Build

cl /EHsc proxy_downloader.cpp winhttp.lib


Cài Scheduled Task (Admin)
Mở Command Prompt as Administrator, chạy:

proxy_downloader.exe install-task 02:00


Nếu không truyền 02:00 sẽ dùng mặc định 02:00.

Task đặt lệnh chạy hàng ngày và redirect stdout/stderr vào run.log nằm cạnh EXE.

Gỡ Task

proxy_downloader.exe uninstall-task


Chạy thủ công / test

proxy_downloader.exe


hoặc

proxy_downloader.exe loop