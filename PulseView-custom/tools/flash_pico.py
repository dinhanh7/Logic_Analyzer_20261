#!/usr/bin/env python3
"""Flash the RP2350 firmware without touching the board.

    python3 tools/flash_pico.py                    # build/firmware-pico2/rp2350_la.uf2
    python3 tools/flash_pico.py build/x.uf2

If the board is already in the UF2 bootloader (mounted as .../RP2350) the UF2
is copied straight in. Otherwise the running firmware is asked to reboot into
the bootloader through LAR2_ANALYZE_BOOTLOADER, so no BOOTSEL press is needed.
"""
import glob
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import host_la  # noqa: E402
from lar2 import ANALYZE_BOOTLOADER  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_UF2 = os.path.join(REPO, 'build', 'firmware-pico2', 'rp2350_la.uf2')
PORT = host_la.default_port()


def bootsel_dir():
    for path in glob.glob('/media/*/RP2350'):
        if os.path.isfile(os.path.join(path, 'INFO_UF2.TXT')):
            return path
    return None


def wait_for(predicate, timeout, what):
    end = time.time() + timeout
    while time.time() < end:
        value = predicate()
        if value:
            return value
        time.sleep(0.25)
    raise SystemExit('timed out waiting for %s' % what)


def enter_bootloader():
    deadline = time.time() + 10
    while time.time() < deadline:
        try:
            dev = host_la.Device(PORT)
            dev.hello()
            dev.analyze(ANALYZE_BOOTLOADER, timeout=3)
            dev.close()
            return
        except Exception:
            time.sleep(0.5)
    raise SystemExit('could not reach the firmware on %s' % PORT)


def main():
    uf2 = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_UF2
    if not os.path.isfile(uf2):
        raise SystemExit('no such UF2: %s' % uf2)

    target = bootsel_dir()
    if target is None:
        if not os.path.exists(PORT):
            #1200baud reset can remove CDC before the desktop mounts UF2.
            target = wait_for(bootsel_dir, 12, 'BOOTSEL mount after USB reset')
    if target is None:
        print('asking the firmware to reboot into the UF2 bootloader...')
        enter_bootloader()
        target = wait_for(bootsel_dir, 20, 'BOOTSEL mount')
    print('bootloader at', target)

    shutil.copy(uf2, os.path.join(target, 'firmware.uf2'))
    os.sync()
    print('copied %s (%d bytes)' % (os.path.basename(uf2), os.path.getsize(uf2)))

    def alive():
        if not os.path.exists(PORT):
            return None
        try:
            dev = host_la.Device(PORT)
            ident = dev.hello()
            dev.close()
            return ident
        except Exception:
            return None

    print('board back up:', wait_for(alive, 25, 'firmware on ' + PORT))


if __name__ == '__main__':
    main()
