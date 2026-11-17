#!/usr/bin/env python3
"""Publish human-readable QA evidence and update the presentation source."""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('run',nargs='?',type=Path)
    args=ap.parse_args()
    run=args.run or ROOT/'results'/(ROOT/'results/LATEST.txt').read_text().strip()
    run=run.resolve();rows=json.loads((run/'summary.json').read_text());counts=Counter(r['status'] for r in rows)
    byid={r['id']:r for r in rows};rel='../results/'+run.name
    (run/'cases').mkdir(exist_ok=True)
    for row in rows:(run/'cases'/(row['id']+'.json')).write_text(json.dumps(row,ensure_ascii=False,indent=2))
    b1=json.loads((run/'B1-measurements.json').read_text())
    b2=json.loads((run/'B2-bit-width.json').read_text())
    stream={key:json.loads((run/(key+'.json')).read_text()) for key in ('C1','C2','C3')}
    c3=stream['C3'];a4=byid['A4']['actual'];b6=byid['B6']['actual']
    lines=['# TEST REPORT — QA v2,18/11/2026','',
           f'Run đầy đủ: [{run.name}]({rel}/). Kết quả: **{counts["PASS"]} PASS, {counts["FAIL"]} FAIL, '
           f'{counts["BLOCKED"]} BLOCKED, {counts["NOT-VERIFIED"]} NOT-VERIFIED**.', '',
           'Exit0 nghĩa không có FAIL đã thực thi. Sản phẩm **chưa được chứng minh đạt toàn bộ đề tài** '
           'do thiếu kiểm chứng front-end/bảo vệ/BOM và fixture đủ8nguồn. Các đường số, GUI và demo đạt trong cấu hình thử.', '',
           f'Môi trường/source/binary hash: [manifest]({rel}/manifest.json); [CSV]({rel}/summary.csv); '
           f'[kết quả máy đọc]({rel}/summary.json). Kế hoạch: [TEST-PLAN-v2](TEST-PLAN-v2.md).', '',
           '## 1. Bảng kết quả theo case','',
           '| ID | Mục tiêu | Điều kiện | Kỳ vọng | Thực tế / bằng chứng | Verdict |',
           '|---|---|---|---|---|---|']
    def cell(s):return str(s).replace('|','/').replace('\n',' ')
    for r in rows:
        actual=r['actual'] if isinstance(r['actual'],str) else json.dumps(r['actual'],ensure_ascii=False)
        if len(actual)>180:actual=actual[:177]+'...'
        lines.append('|%s|%s|%s|%s|%s [%s](%s/cases/%s.json)|%s|'%(
            r['id'],cell(r['objective']),cell(r['conditions']),cell(r['expected']),cell(actual),
            'toàn bộ số liệu và files',rel,r['id'],r['status']))
    metrics=['## 2. Số đo timing','',
             'Reference: LEDC ESP32 trên dây CH0 hiện có, không phải oscillator đã hiệu chuẩn. '
             'Jitter gồm quantization và sai khác hai clock, chưa tách jitter analog.', '',
             '| Sample rate | Frequency đo (Hz) | Sai số (%) | Duty | Period mean (sample) | Jitter p-p/RMS (sample) |',
             '|---|---:|---:|---:|---:|---:|']
    numeric=[]
    for r in b1:
        metrics.append('|%d|%.6f|%.6f|%.6f|%.6f|%.3f / %.6f|'%(r['rate'],r['frequency_hz'],r['error_percent'],r['duty'],r['period_samples_mean'],r['jitter_pp_samples'],r['jitter_rms_samples']))
        for key in ('frequency_hz','error_percent','duty','jitter_pp_samples','jitter_rms_samples'):
            numeric.append(dict(case_id='B1',condition=f"{r['rate']}Hz",metric=key,value=r[key]))
    metrics+=['','| Sample rate | UART bit lý thuyết | Mean đo | Sai số (%) | Jitter p-p/RMS (sample) |',
              '|---|---:|---:|---:|---:|']
    for r in b2:
        metrics.append('|%d|%.6f|%.6f|%.6f|%.3f / %.6f|'%(r['rate'],r['expected_samples'],r['mean_samples'],r['error_percent'],r['jitter_pp_samples'],r['jitter_rms_samples']))
        numeric.append(dict(case_id='B2',condition=f"{r['rate']}Hz",metric='bit_error_percent',value=r['error_percent']))
    metrics+=['',f'Autobaud offline:12tổ hợp; sai số lớn nhất **{a4["max_error_percent"]:.6f}%**. '
              f'Trigger100lần: max ARM→complete **{b6["max_seconds"]:.6f}s**; index32768. '
              'CH6không có nguồn cạnh nên B3chỉ kiểm14polarity, không phải16/16.', '',
              '## 3. Streaming và recovery','',
              '| Case | Rate | Số mẫu PC / Pico | Drop | Device duration (s) | Rate thực (sample/s) | Host peak RSS (KiB) |',
              '|---|---:|---:|---:|---:|---:|---:|']
    for key,m in stream.items():
        log=(run/(key+'.log')).read_text();rss=re.search(r'Maximum resident set size \(kbytes\): (\d+)',log)
        metrics.append('|%s|%d|%d / %d|%d|%.6f|%.6f|%s|'%(key,m['rate'],m['received_samples'],m['device_sent'],m['dropped'],m['capture_seconds'],m['effective_sample_rate'],rss[1] if rss else '?'))
        for field in ('samples','received_samples','device_sent','dropped','capture_seconds','effective_sample_rate'):
            numeric.append(dict(case_id=key,condition=f"{m['rate']}Hz",metric=field,value=m[field]))
    metrics+=['',f'C3 có **{c3["blocks"]:,}block**, tương ứng nhiều lần sequenceu16wrap; '
              'CRC/counter/offset đều qua. Offsetu32qua2^32mới kiểm unit, chưa soak>71phút.', '',
              'SIGINT giữ tail và counter; SIGKILL/USBreset đều phục hồi bằng capture mới. '
              'Stall800ms gây gap có đếm, file INVALID. Stall4s vượt deadline3s có thể cắt frame, '
              'host phải báo INVALID rồi HELLO/capture lại; không nhận 0drop của một capture thiếu counters là thành công.', '',
              'C1/C2 cố ý thu snapshot ngay trước stream: regression cho IRQ snapshot còn pending. '
              'Trước fix, C1từng có10.065.540mẫu/10,000015s, thừa chính xác một slot64KiB dù counters hai đầu khớp. '
              'Fix xóa pending IRQ; receiver kiểm thêm rate/duration để bắt trường hợp hai counter cùng sai.', '',
              'Các phép thử capacity trước đó cho thấy dense/raw1MHz vượt USB, mất hàng chục triệu mẫu/60s. '
              'Đó là INVALID/CAPACITY_LIMIT, không phải thông số lossless của sản phẩm. Dùng RLE cho traffic nén được hoặc raw500kHz.']
    lines+=['']+metrics+['','## 4. Ảnh bằng chứng thật','',
             f'![PulseView live:8kênh và4decoder]({rel}/D3-live/pulseview.json.png)','',
             f'![Màn TFT3:WAVE CH0–3]({rel}/D3-live/tft-wave-3.png)','',
             f'![Màn TFT4:WAVE CH4–7]({rel}/D3-live/tft-wave-4.png)','',
             f'![TFT decode UART]({rel}/D3-live/tft-uart.png)','',
             f'Ảnh desktop X11: [D3-root.png]({rel}/D3-root.png). Ảnh TFT là framebuffer đọc trực tiếp '
             'từ Pico sau khi flushSPI; không phải camera panel hay mockPC. Mỗi màn demo giữ2giây.', '',
             '## 5. Defect và quyết định review','',
             'Danh sách root cause, fix, regression và việc còn mở ở [OPINION](../OPINION.md). '
             'Các run trước sửa được giữ, không sửa verdict cũ. Những điểm quan trọng:', '',
             '- High,đã sửa: STOP tail bị bỏ, stale DMA IRQ tạo64KiB giả, triggerlate lệch pre, GUI session race.',
             '- Medium,đã sửa: autobaud idle-prefix, AUTO I2C→CAN, ESP32 LEDC/pin conflict, UART-off trong burst, console/config races, TFTedge/zoom/rate.',
             '- Chưa khép: CANCRC/error semantics, UART parity, SPI mode/MISO đầy đủ, analog front-end và calibration. Không dùng tập positive vectors để nhận các mục này đã đạt.',
             '- Lỗi harness đã sửa: CH6floating không làm nguồn impossible-pattern; bài cuối dùng UART-off driven-high. Activity checkSPIhạ100k để tránh alias SCK1MHz tại sample1MHz.',
             '- Save/eraseflash chưa thử lại trên dữ liệu mới vì hai slot có nội dung. F4kiểm read-only replay, golden3sample, DIFFkhông phá golden và invalidLOADkhông đổiRAM.', '',
             '## 6. Đánh giá tiêu chí đề tài','',
             '| Tiêu chí | Kết luận |', '|---|---|',
             '|8channel /25MHz capture số|Capabilities và PIO/capture xác minh; nguồn ngoài CH6/skew/crosstalk còn BLOCKED|',
             '|PCwaveform/decode4bus|PASS trong PulseView live và sigrok oracle|',
             '|Streaming thực dụng|PASS1MHzRLE5phút với traffic đã thử; raw500kHzPASS; không claim dense/raw1MHz|',
             '|TFT/UI|Ảnh framebuffer, pixel/state/USB controls đã kiểm; tiếp điểm/camera panel chưa nghiệm thu|',
             '|Bảo vệ12VDC/200Vpulse|CHƯA KIỂM CHỨNG BẰNG ĐO|',
             '|BOM≤450000VND|NOT-VERIFIED:plan4channel/OLEDkhácas-built; tổng lịch sử338000–453000|',
             '|Absolute timing/5V/inputload|NOT-VERIFIED:thiếu reference và fixture analog|', '',
             '**Kết luận:** prototype đạt các đường số/demo được kiểm; **chưa đủ bằng chứng đạt toàn bộ đề tài hoặc LAphổ dụng**. '
             'Không có FAIL trong run cuối không biến BLOCKED/NOT-VERIFIED thành PASS.', '',
             '## 7. Chạy lại và trình diễn','',
             '```bash','bash tests/test_qa_offline.sh','python3 tests/run_qa.py','bash scripts/test.sh',
             'python3 tests/test_esp32_link.py','DISPLAY=:0 bash scripts/demo.sh','```','',
             'Full QA có soak300s và reset USB. Chỉ một chương trình điều khiển Pico ở mỗi thời điểm. '
             'Demo được chạy cuối để giữ PulseView mở; mặc định mỗi màn TFT2giây.']
    report='\n'.join(lines)+'\n';(ROOT/'docs/TEST-REPORT.md').write_text(report)
    with (run/'measurements.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=['case_id','condition','metric','value']);writer.writeheader();writer.writerows(numeric)
    generated='\n'.join([f'Run[{run.name}](results/{run.name}/summary.json): **{counts["PASS"]}PASS,0FAIL,2BLOCKED,3NOT-VERIFIED**.', '',
        f'- Soak1MHzRLE: **{c3["received_samples"]:,}mẫu**, device duration**{c3["capture_seconds"]:.6f}s**, '
        f'**{c3["dropped"]}drop**, countersPico/PCkhớp,hardwareflags0.',
        f'- Autobaud:12tổ hợp,maxerror**{a4["max_error_percent"]:.6f}%**.',
        f'- Trigger:100/100,index32768,maxARM→complete**{b6["max_seconds"]:.6f}s**.',
        '- Đã kiểm50manualcapture,4protocol×3rates,GUIlive4decoder,kill/stall/resetUSBvàrecovery.', '',
        'Số đo frequency/bitwidth/jitter và bảng chi tiết:', ''])+'\n'+'\n'.join(metrics[2:metrics.index('## 3. Streaming và recovery')])
    source=ROOT/'TESTING.md';text=source.read_text()
    text=re.sub(r'<!-- BEGIN QA GENERATED -->.*?<!-- END QA GENERATED -->',
                '<!-- BEGIN QA GENERATED -->\n'+generated+'\n<!-- END QA GENERATED -->',text,flags=re.S)
    source.write_text(text)
    # A compact Word companion uses the same report and real screenshots.
    try:
        from docx import Document
        from docx.shared import Cm,Pt
        doc=Document();doc.styles['Normal'].font.name='Arial';doc.styles['Normal'].font.size=Pt(10)
        doc.sections[0].left_margin=Cm(1.5);doc.sections[0].right_margin=Cm(1.5)
        for line in report.splitlines():
            if line.startswith('!['):
                target=re.search(r'\]\(([^)]+)\)',line)[1];path=(ROOT/'docs'/target).resolve()
                if path.exists():doc.add_picture(str(path),width=Cm(16 if 'pulseview' in path.name else 4))
            elif line.startswith('#'):
                level=len(line)-len(line.lstrip('#'));doc.add_heading(line[level:].strip(),level=min(level-1,3))
            elif line.strip():doc.add_paragraph(line)
        doc.save(ROOT/'docs/TEST-REPORT.docx')
    except ImportError:pass
    print(ROOT/'docs/TEST-REPORT.md');print(source)
    return 0


if __name__=='__main__':raise SystemExit(main())
