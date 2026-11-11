# Gerber và drill — Rev A1 để duyệt

Xuất từ `LogicAnalyzer_Pico2.kicad_pcb` sau ERC/DRC và kiểm tra parity. PCB 160×110 mm, 2 lớp đồng, FR4 dày 1,6 mm; chọn đồng 35 µm, solder mask hai mặt, silkscreen mặt trên khi hỏi giá. Chưa duyệt nhà sản xuất, dung sai cơ khí, connector TFT, linh kiện chịu xung hoặc BOM.

- GTL/GBL: đồng trên/dưới.
- GTS/GBS: solder mask trên/dưới.
- GTO/GBO: silkscreen; mặt dưới không có chữ lắp.
- GM1: viền Edge.Cuts kín.
- PTH.drl: lỗ mạ; NPTH.drl: lỗ không mạ; kèm PDF bản đồ khoan.

Các file này là dữ liệu chế tạo **để review**, không phải xác nhận đã đủ điều kiện đặt sản xuất. Xem checklist cụ thể trong README ở thư mục project. Không lật/mirror file Gerber hoặc drill trước khi gửi xưởng. Nếu sửa PCB, phải xuất lại toàn bộ bộ file cùng lúc.
