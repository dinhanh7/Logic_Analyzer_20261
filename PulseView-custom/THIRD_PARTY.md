# Upstream được pin

Mã upstream được tải khi build, không vendor vào Git của bài tập. `sources.lock.json` là nguồn khóa phiên bản; script kiểm tra HEAD trước khi build.

| Thành phần | Commit | Thay đổi tại dự án |
|---|---|---|
| [libsigrok](https://github.com/sigrokproject/libsigrok) | `0bc2487778e660f4d3116729b6f4aee2b1996bb0` | Thêm driver rp2350-la và đăng ký configure.ac / Makefile.am |
| [libsigrokdecode](https://github.com/sigrokproject/libsigrokdecode) | `71f451443029322d57376214c330b518efd84f88` | Giữ nguyên, dùng decoder UART/SPI/I²C upstream |
| [sigrok-cli](https://github.com/sigrokproject/sigrok-cli) | `f44dd91347e7ac797cefc23162b9fcf0b7329f1f` | Giữ nguyên, công cụ kiểm tra driver |
| [pulseview](https://github.com/sigrokproject/pulseview) | `af02198741b4e57c9f9b796bd5e6c0f2ae9f2f2b` | Patch tiêu đề cửa sổ, chọn thiết bị qua launcher |
| [pico-sdk](https://github.com/raspberrypi/pico-sdk) | `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` | 2.2.0, giữ nguyên; chỉ init submodule TinyUSB |

Pico SDK pin TinyUSB ở `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`; công cụ pioasm/picotool theo cấu hình SDK 2.2.0. Không bật wireless; Pico 2 W chỉ dùng như MCU/USB CDC.

Giấy phép upstream: libsigrok và libsigrokdecode GPL-3.0-or-later; sigrok-cli và PulseView GPL-2.0-or-later; Pico SDK và các thư viện phụ thuộc theo LICENSE của từng thành phần (SDK BSD-3-Clause, TinyUSB MIT). Khi phân phối binary phải kèm thông tin giấy phép và cung cấp mã nguồn tương ứng theo điều kiện áp dụng.
