#!/usr/bin/env python3
"""Capture the framebuffer actually sent to the TFT on the connected Pico.

This is an on-device screenshot, not a host-rendered mock or a camera photo.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

import host_la
import lar2


def view_state(dev):
    payload = dev.analyze(lar2.ANALYZE_UI_VIEW)
    if len(payload) != 28:
        raise ValueError('invalid UI_VIEW reply')
    return dict(zip(('start', 'visible', 'count', 'trigger', 'rate', 'screen', 'zoom', 'protocol', 'events'),
                    struct.unpack('<4IQ4B', payload)))


def save_frame(dev, path):
    from PIL import Image
    for attempt in range(3):
        raw, version, total = bytearray(), None, 40960
        while len(raw) < total:
            reply = dev.analyze(lar2.ANALYZE_UI_FRAME, len(raw), 1000, strip_status=False)
            if len(reply) <= 11 or reply[0] != 0:
                raise ValueError('invalid framebuffer reply')
            width, height = reply[1:3]
            got_version, offset, got_total = struct.unpack_from('<IHH', reply, 3)
            if width != 128 or height != 160 or offset != len(raw) or got_total != 40960:
                raise ValueError('invalid framebuffer dimensions/offset')
            if version is not None and got_version != version:
                break
            version = got_version
            raw.extend(reply[11:])
        if len(raw) != total:
            continue  # A blink/key changed the panel while reading it.
        rgb = bytearray()
        for word, in struct.iter_unpack('>H', raw):
            rgb.extend((((word >> 11) & 31) * 255 // 31,
                        ((word >> 5) & 63) * 255 // 63, (word & 31) * 255 // 31))
        Image.frombytes('RGB', (width, height), bytes(rgb)).save(path)
        metadata = dict(source='RP2350 TFT framebuffer', width=width, height=height,
                        frame_version=version, rgb565_sha256=hashlib.sha256(raw).hexdigest(),
                        view=view_state(dev))
        Path(path).with_suffix('.json').write_text(json.dumps(metadata, indent=2))
        return metadata
    raise RuntimeError('TFT changed while taking screenshot; retry without pressing keys')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('output', type=Path)
    ap.add_argument('--port', default=host_la.default_port())
    args = ap.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    dev = host_la.Device(args.port)
    try:
        dev.hello()
        print(json.dumps(save_frame(dev, args.output), indent=2))
    finally:
        dev.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
