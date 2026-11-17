#!/usr/bin/env python3
"""Real ESP32 -> Pico 2 -> TFT + PulseView demo. No synthetic fallback."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import struct
import subprocess
import sys
import time

import host_la
import lar2

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / 'scripts/run-sigrok-cli.sh'
GUI = ROOT / 'build/pulseview/pulseview'
PROTOCOLS = {
    'UART': ('uart:rx=CH0:baudrate=115200', 'uart=rx-data', '4C 41 31 32 33 34'),
    'I2C': ('i2c:scl=CH1:sda=CH2', 'i2c=address-write', 'Address write: 68'),
    'SPI': ('spi:clk=CH4:mosi=CH5:cs=CH3', 'spi=mosi-data', 'DE AD BE EF 00 01 02 03'),
    'CAN': ('can:can_rx=CH7:nominal_bitrate=250000', 'can=id', 'Identifier: 291'),
}
FIRMWARE_WANT = {'UART': 'LA1234', 'I2C': '0x68 W ACK 0x01 ACK 0x5A ACK',
                 'SPI': 'DE AD BE EF 00 01 02 03', 'CAN': 'CAN ID=0x123 DLC=4'}
GUI_CLASS = {'UART': 'rx-data', 'I2C': 'address-write', 'SPI': 'mosi-data', 'CAN': 'id'}


def run(args, log, timeout=60, env=None):
    proc = subprocess.run([str(a) for a in args], cwd=ROOT, capture_output=True,
                          text=True, timeout=timeout, env=env)
    Path(log).write_text(proc.stdout + '\n' + proc.stderr)
    if proc.returncode:
        raise RuntimeError(f'{args[0]} failed; see {log}')
    return proc.stdout, proc.stderr


def require(text, want, label):
    if want not in text:
        raise RuntimeError(f'{label}: expected {want!r}; received {text[:240]!r}')


def open_device(port):
    dev = host_la.Device(port)
    try:
        ident = dev.hello()
        return dev, ident
    except BaseException:
        dev.close()
        raise


def setup_generator(port, output):
    # Setting DTR/RTS before open avoids resetting the generator on each command.
    import serial
    with serial.Serial(port=None, baudrate=115200, timeout=0.1) as console:
        console.dtr = False
        console.rts = False
        console.port = port
        console.open()
        time.sleep(0.4)
        transcript = []
        for command in ('uart on', 'set uart 115200', 'set i2c 100',
                        'set spi 1000', 'set can 250', 'burst on', 'status'):
            console.write((command + '\n').encode())
            console.flush()
            deadline = time.monotonic() + 0.5
            reply = bytearray()
            while time.monotonic() < deadline:
                reply += console.read(4096)
            transcript.append(command + '\n' + reply.decode('utf-8', 'replace'))
        (output / 'esp32-console.log').write_text('\n'.join(transcript))


def qt_bytes(value):
    return '@ByteArray(' + ''.join('\\x%02x' % b for b in value) + ')'


def write_setup(path, trigger='CH0', live=False):
    # Format follows PulseView Session::restore_setup and DecodeSignal::restore_settings.
    # Channel indexes are decoder order, not physical GPIO order.
    stacks = [('UART', 'uart', [0, None], [('baudrate', 'x', struct.pack('<q', 115200)),
                                          ('format', 's', b'hex\0')]),
              ('I2C', 'i2c', [1, 2], []),
              ('SPI', 'spi', [4, 6, 5, 3], []),
              ('CAN', 'can', [7], [('nominal_bitrate', 'x', struct.pack('<q', 250000))])]
    lines = ['[General]', 'decode_signals=4', 'generated_signals=0', 'views=1', 'meta_objs=0']
    colors = ['4281891832', '4294677052', '4283082880', '4290806780']
    for i in range(8):
        lines += [f'\n[CH{i}]', f'name=CH{i}', 'enabled=true', f'color={colors[i % 4]}']
    for i, (name, decoder, channels, options) in enumerate(stacks):
        lines += [f'\n[decode_signal{i}]', f'name={name}', 'enabled=true',
                  f'color={colors[i]}', 'decoders=1', f'channels={len(channels)}',
                  f'decoder0\\id={decoder}', 'decoder0\\visible=true',
                  f'decoder0\\options={len(options)}']
        row_count, visible_rows = {'UART': (10, {1}), 'I2C': (3, {1}),
                                   'SPI': (7, {4}), 'CAN': (3, {1})}[name]
        for row in range(row_count):
            lines.append(f'decoder0\\row{row}\\visible={str(row in visible_rows).lower()}')
        for j, (key, kind, data) in enumerate(options):
            lines += [f'decoder0\\option{j}\\name={key}', f'decoder0\\option{j}\\type={kind}',
                      f'decoder0\\option{j}\\value={qt_bytes(data)}']
        for j, ch in enumerate(channels):
            lines += [f'channel{j}\\initial_pin_state=2']  # SRD_INITIAL_PIN_SAME_AS_SAMPLE0
            if ch is not None:
                lines += [f'channel{j}\\assigned_signal_name=CH{ch}']
    if live:
        lines += ['\n[rp2350]', 'samplerate=25000000', 'samples=65536',
                  f'trigger_source={trigger}', 'autorun=true']
    lines += ['\n[view0]']
    lines += [f'CH{i}\\trace_height=24' for i in range(8)]
    Path(path).write_text('\n'.join(lines) + '\n')


def check_cli(path, output, attempt):
    for name, (decoder, annotation, want) in PROTOCOLS.items():
        stdout, _ = run([CLI, '-i', path, '-P', decoder, '-A', annotation],
                        output / f'capture-{attempt}-{name.lower()}.log')
        text = ' '.join(re.sub(r'^\S+-1:\s*', '', line).strip()
                        for line in stdout.splitlines())
        require(text, want, 'PC ' + name)
        if name == 'I2C':
            stdout, _ = run([CLI, '-i', path, '-P', decoder, '-A', 'i2c=data-write'],
                            output / f'capture-{attempt}-i2c-data.log')
            require(stdout, 'Data write: 01', 'PC I2C register')
            require(stdout, 'Data write: 5A', 'PC I2C payload')


def capture_driver(port, output):
    # Acquisition goes through the same real C driver that PulseView uses.
    for attempt, ch in enumerate((0, 0, 7, 1, 3, 0), 1):
        path = output / 'capture.sr'
        try:
            path.unlink(missing_ok=True)
            _, stderr = run([CLI, '-l', '4', '-d', f'rp2350-la:conn={port}',
                '--config', 'samplerate=25000000', '--config', f'triggersource=CH{ch}',
                '--config', 'triggerslope=falling', '--config', 'horiz_triggerpos=0.5',
                '--samples', '65536', '-o', path], output / f'capture-{attempt}.log', timeout=30)
            require(stderr, 'Capture verified:', 'driver capture')
            check_cli(path, output, attempt)
            print(f'PASS driver capture + PC UART/I2C/SPI/CAN (attempt {attempt})', flush=True)
            return path, ch
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            print(f'Retrying real capture {attempt}/6: {exc}', flush=True)
    raise RuntimeError('No capture decoded all four protocols; check ESP32, eight wires and common GND')


def check_tft_table(name, table):
    expected = {'UART': [0x4C, 0x41, 0x31, 0x32, 0x33],
                'I2C': [0xD0, None, 0x01, None, 0x5A],
                'SPI': [0xDE, 0xAD, 0xBE, 0xEF, 0x00],
                'CAN': [0x01, 0x23, 0x04]}[name]
    rows = [r.split('|') for r in table.strip().splitlines()]
    if len(rows) < len(expected):
        raise RuntimeError(f'TFT {name}: incomplete byte table')
    for row, value in zip(rows, expected):
        if len(row) != 4 or (value is not None and row[1] != f'0x{value:02X}'):
            raise RuntimeError(f'TFT {name}: unexpected byte row {row}')
        if value is None and row[2] != 'ACK':
            raise RuntimeError(f'TFT {name}: expected ACK row')


def tft(dev, output, dwell):
    result = {}
    # Same physical RAM window just acquired by the driver; HELLO does not replace it.
    for screen in (2, 3):
        dev.analyze(lar2.ANALYZE_UI_SHOW_CAPTURE, screen, 0)
        state = dev.analyze(lar2.ANALYZE_RUNTIME, strip_status=False)
        if len(state) != 32 or state[30] != screen:
            raise RuntimeError(f'TFT did not enter waveform screen {screen}')
        time.sleep(dwell)
    for proto, name in enumerate(FIRMWARE_WANT, 1):
        dev.analyze(lar2.ANALYZE_UI_SHOW_CAPTURE, 4, proto)
        text = dev.analyze(lar2.ANALYZE_UI_TEXT, strip_status=False).decode()
        table = dev.analyze(lar2.ANALYZE_UI_TABLE, strip_status=False).decode()
        badge = dev.analyze(lar2.ANALYZE_UI_STATE, strip_status=False).decode()
        state = dev.analyze(lar2.ANALYZE_RUNTIME, strip_status=False)
        require(text, FIRMWARE_WANT[name], 'TFT ' + name)
        check_tft_table(name, table)
        if not table.strip() or not badge.startswith(f'{proto} ') or state[30] != 4:
            raise RuntimeError(f'TFT {name}: wrong screen, selector or empty byte table')
        result[name] = dict(text=text, table=table, badge=badge)
        (output / 'tft.json').write_text(json.dumps(result, ensure_ascii=False, indent=2))
        print(f'PASS TFT {name}: {text.strip()}', flush=True)
        time.sleep(dwell)
    dev.analyze(lar2.ANALYZE_UI_SHOW_CAPTURE, 4, 1)  # leave readable LA1234 on TFT
    return result


def check_gui_report(report):
    channels = report.get('channels', [])
    if any(f'CH{i}' not in channels for i in range(8)):
        raise RuntimeError('PulseView report does not contain eight channels')
    decoders = {d['name']: d for d in report.get('decoders', [])}
    for name, (_, _, want) in PROTOCOLS.items():
        items = decoders.get(name, {}).get('annotations', [])
        # Only the relevant visible annotation class counts; generic bits or
        # another decoder containing the same number must never produce PASS.
        items = sorted((a for a in items if a.get('class') == GUI_CLASS[name]
                        and a.get('visible')), key=lambda a: a.get('sample', 0))
        text = ' '.join(a['text'] for a in items)
        require(text, want, 'PulseView ' + name)
        if name == 'I2C':
            data = decoders[name]['annotations']
            text = ' '.join(a['text'] for a in data
                            if a.get('class') == 'data-write' and a.get('visible'))
            require(text, 'Data write: 01', 'PulseView I2C register')
            require(text, 'Data write: 5A', 'PulseView I2C payload')


def pulseview(output, port, path, ch, live):
    setup = output / ('live.pvs' if live else 'capture.pvs')
    write_setup(setup, f'CH{ch}', live)
    report = output / 'pulseview.json'
    env = dict(os.environ, RP2350_DEMO_REPORT=str(report),
               XDG_CONFIG_HOME=str(output / 'pulseview-config'))
    args = [str(GUI), '-D', '-s', str(setup)]
    args += ['-d', f'rp2350-la:conn={port}'] if live else ['-i', str(path)]
    with (output / 'pulseview.log').open('w') as log:
        proc = subprocess.Popen(args, cwd=ROOT, env=env, stdout=log, stderr=log,
                                start_new_session=True)
    last_error = 'no PulseView annotation report yet'
    retry_at = time.monotonic() + 10
    retries = 0
    deadline = time.monotonic() + 75
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError('PulseView exited; see pulseview.log')
        try:
            data = json.loads(report.read_text())
            check_gui_report(data)
            if data.get('toolbar_samples') != 65536 or data.get('toolbar_samplerate') != 25000000:
                raise RuntimeError('PulseView toolbar does not show 65536 samples / 25 MHz')
            if live:
                require(data.get('device', ''), 'RP2350', 'PulseView device')
                expected = dict(samplerate=25000000, samples=65536,
                                trigger_source=f'CH{ch}', trigger_slope='falling', pretrigger=0.5)
                if data.get('device_config') != expected:
                    raise RuntimeError(f'PulseView config mismatch: {data.get("device_config")}')
            if not Path(str(report) + '.png').exists():
                raise RuntimeError('PulseView screenshot not saved yet')
            if not data.get('screenshot_saved'):
                raise RuntimeError('PulseView screenshot is not from this acquisition yet')
            print('PASS annotations generated inside PulseView: UART/I2C/SPI/CAN', flush=True)
            return proc.pid
        except (OSError, ValueError, RuntimeError) as exc:
            last_error = str(exc)
        # Reports are written only while capture has stopped. USR1 is the
        # built-in PulseView Run shortcut, so do not toggle an active capture.
        if live and report.exists() and time.monotonic() >= retry_at and retries < 4:
            report.unlink(missing_ok=True)
            proc.send_signal(signal.SIGUSR1)
            retries += 1
            retry_at = time.monotonic() + 12
        time.sleep(0.4)
    raise RuntimeError(f'GUI verification timed out: {last_error}. PulseView stays open for inspection.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default=os.environ.get('LA_PORT', '/dev/ttyACM0'))
    parser.add_argument('--esp32-port', default=os.environ.get('ESP32_PORT', '/dev/ttyUSB0'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--no-gui', action='store_true', help='hardware checks only; GUI is not marked PASS')
    parser.add_argument('--recorded', action='store_true', help='show verified real snapshot instead of a new live GUI capture')
    parser.add_argument('--no-flash', action='store_true', help='refuse old firmware instead of upgrading it')
    parser.add_argument('--dwell', type=float, default=2.0, help='seconds per TFT screen')
    args = parser.parse_args()
    output = (args.output or ROOT / 'build/demo' / time.strftime('%Y%m%d-%H%M%S')).resolve()
    output.mkdir(parents=True, exist_ok=False)
    summary = {'status': 'RUNNING', 'output': str(output), 'gui': 'NOT_RUN',
               'physical_tft_visual': 'requires observation; digital state is checked'}
    try:
        for port in (args.port, args.esp32_port):
            if not Path(port).exists():
                raise RuntimeError(f'{port} is not visible. Run scripts/demo.sh on the PC connected to the boards.')
        if not args.no_gui:
            if not os.environ.get('DISPLAY') and not os.environ.get('WAYLAND_DISPLAY'):
                raise RuntimeError('No desktop display. Run from a terminal on your desktop.')
            if not GUI.exists() or b'RP2350_DEMO_REPORT' not in GUI.read_bytes():
                print('Building PulseView demo support...', flush=True)
                run(['bash', 'scripts/build-demo.sh'], output / 'demo-build.log', 300)
        dev, ident = open_device(args.port)
        try:
            caps = dev.caps()
        finally:
            dev.close()
        if caps['channels'] != 8 or 25000000 not in caps['rates']:
            raise RuntimeError('Board does not advertise eight channels and 25 MHz')
        summary.update(board=ident, capabilities=caps)
        print('PASS HELLO/GET_CAPS:', ident, caps, flush=True)
        if '+ui18+sync' not in ident:
            if args.no_flash:
                raise RuntimeError('Firmware needs UI_SHOW_CAPTURE. Run again without --no-flash.')
            run(['cmake', '--build', 'build/firmware-pico2', '-j4'], output / 'firmware-build.log', 180)
            run([sys.executable, 'tools/flash_pico.py'], output / 'flash.log', 90,
                env=dict(os.environ, LA_PORT=args.port))
            dev, ident = open_device(args.port)
            dev.close()
            require(ident, '+ui18+sync', 'flashed firmware')
            summary['board'] = ident
        setup_generator(args.esp32_port, output)
        stdout, stderr = run([CLI, '-d', f'rp2350-la:conn={args.port}', '--show'],
                             output / 'device-show.log')
        show = stdout + stderr
        require(show, 'RP2350', 'driver identity')
        for i in range(8):
            require(show, f'CH{i}', 'driver channels')
        path, ch = capture_driver(args.port, output)
        dev, _ = open_device(args.port)
        try:
            summary['tft'] = tft(dev, output, max(0, args.dwell))
        finally:
            dev.close()
        # The port is now free for PulseView's own acquisition. Its JSON and
        # screenshot prove its decoder stack actually produced annotations.
        if not args.no_gui:
            summary['pulseview_pid'] = pulseview(output, args.port, path, ch, not args.recorded)
            summary['gui'] = 'PASS_RECORDED' if args.recorded else 'PASS_LIVE'
        summary['status'] = 'PASS_HARDWARE_ONLY' if args.no_gui else 'PASS'
        print(f'\n{summary["status"]}: results in {output}', flush=True)
        print('TFT ends on UART; short OK edits PROTO, DOWN changes protocol. PulseView stays open.')
        return 0
    except (Exception, KeyboardInterrupt) as exc:
        summary.update(status='FAIL', error=str(exc) or 'interrupted')
        print(f'\nFAIL: {summary["error"]}\nLogs: {output}', file=sys.stderr, flush=True)
        return 1
    finally:
        (output / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    sys.exit(main())
