# Nguồn symbol / footprint / 3D

Symbol và footprint được trích từ thư viện KiCad đi kèm bản cài **10.0.8** trên máy, giữ hình học/pad của footprint chuẩn rồi đổi sang tên trong thư viện project `LogicAnalyzer`.

Nguồn gốc: [KiCad symbols](https://gitlab.com/kicad/libraries/kicad-symbols), [KiCad footprints](https://gitlab.com/kicad/libraries/kicad-footprints), [KiCad 3D models](https://gitlab.com/kicad/libraries/kicad-packages3D). Xem [giấy phép thư viện KiCad](https://www.kicad.org/libraries/license/) và giấy phép của từng repository. Các thư viện KiCad công bố theo CC BY-SA 4.0 với ngoại lệ cho sản xuất thiết kế điện tử; việc tạo project này không thay quyền sở hữu đối với thư viện gốc.

Đối chiếu tên nguồn được lưu trong `scripts/generate_schematic.py`, biến `sources` và `fp_sources`. Những thay đổi riêng:

- `Pico2_SOCKET`: symbol 40 chân theo thứ tự socket vật lý, pinout theo [Pico 2 Datasheet](https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf). Footprint từ `Module:RaspberryPi_Pico_Common_THT`; bỏ vùng cấm anten dành cho Pico W, giữ pad, cơ khí và khoảng USB. Model Pico là hình minh họa cơ khí từ thư viện gốc.
- `BAV99`: sửa tên pin 1/2/3 thành A1/K2/K1_A2 theo [Nexperia](https://assets.nexperia.com/documents/data-sheet/BAV99.pdf); không đổi số pin hoặc hình diode.
- `PR02_1k_2W_P17.78`: footprint riêng theo kích thước [Vishay PR02](https://www.vishay.com/docs/28729/pr010203.pdf), thân 10×3,9 mm, pitch 17,78 mm. VRML `PR02_Illustrative.wrl` do project tạo để minh họa, không phải model STEP được nhà sản xuất chứng nhận.

Datasheet để chọn package/pinout: [SN74LVC1G17](https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf), [BZX85](https://www.vishay.com/docs/85607/bzx85.pdf), [BC817](https://assets.nexperia.com/documents/data-sheet/BC817_SER.pdf), [BC807](https://assets.nexperia.com/documents/data-sheet/BC807_SER.pdf). PCB dùng pin transistor **1 B / 2 E / 3 C** và buffer DBV **1 NC / 2 A / 3 GND / 4 Y / 5 VCC**.
