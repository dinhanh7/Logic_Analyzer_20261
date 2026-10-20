#!/usr/bin/env python3
"""Run the built C driver against an independent Python PTY firmware model."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from emulator import Emulator
from lar2 import pattern,RATES
CLI=str(ROOT/'.local/bin/sigrok-cli')

class DriverTests(unittest.TestCase):
    def capture(self,device,path,count=100003,rate=25000000,channels=None):
        args=[CLI,'-l','4','-d','rp2350-la:conn='+device.path,'--config','samplerate='+str(rate),'--samples',str(count),'-o',str(path)]
        if channels: args+=['--channels',channels]
        return subprocess.run(args,text=True,capture_output=True,timeout=15)
    def read_samples(self,path):
        with zipfile.ZipFile(path) as z:
            names=sorted((n for n in z.namelist() if n.startswith('logic-1-')),key=lambda n:int(n.rsplit('-',1)[1]))
            return b''.join(z.read(n) for n in names),z.read('metadata').decode()
    def test_fragmented_maximum_capture(self):
        with tempfile.TemporaryDirectory() as tmp, Emulator(fragment=7) as d:
            path=Path(tmp)/'data.sr'; p=self.capture(d,path,262144)
            self.assertEqual(p.returncode,0,p.stderr)
            self.assertIn('Capture verified:',p.stderr)
            data,meta=self.read_samples(path)
            self.assertEqual(data,pattern(262144,25000000))
            self.assertIn('samplerate=25 MHz',meta)
            self.assertIn('probe8=CH7',meta)
    def test_all_rates_and_odd_lengths(self):
        with tempfile.TemporaryDirectory() as tmp, Emulator() as d:
            for rate in RATES:
                with self.subTest(rate=rate):
                    path=Path(tmp)/f'{rate}.sr'; p=self.capture(d,path,1031,rate)
                    self.assertEqual(p.returncode,0,p.stderr)
                    self.assertIn('Capture verified:',p.stderr)
                    self.assertEqual(self.read_samples(path)[0],pattern(1031,rate))
    def test_disabled_channels_preserve_bit_positions(self):
        with tempfile.TemporaryDirectory() as tmp, Emulator() as d:
            path=Path(tmp)/'masked.sr'; p=self.capture(d,path,1025,1000000,'CH0,CH7')
            self.assertEqual(p.returncode,0,p.stderr)
            self.assertIn('Capture verified:',p.stderr)
            self.assertEqual(self.read_samples(path)[0],pattern(1025,1000000))
    def test_reject_corruption_and_incomplete_capture(self):
        for fault in ('crc','sequence','metadata','capture-crc','truncated'):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as tmp, Emulator(fault=fault) as d:
                p=self.capture(d,Path(tmp)/'bad.sr',2049)
                # libsigrok callback errors are logged, not propagated through
                # sigrok-cli's process status. Never treat exit=0 as proof.
                self.assertNotIn('Capture verified:',p.stderr)
                self.assertTrue(any(s in p.stderr for s in ('invalid','sequence gap','timeout')),p.stderr)
    def test_invalid_config(self):
        with tempfile.TemporaryDirectory() as tmp, Emulator() as d:
            for count,rate in ((0,1000000),(262145,1000000),(100,123456)):
                p=self.capture(d,Path(tmp)/f'{count}-{rate}.sr',count,rate)
                self.assertNotIn('Capture verified:',p.stderr)
                self.assertNotEqual(p.returncode,0,p.stderr)
    def test_capture_wrapper_rejects_invalid_file(self):
        with tempfile.TemporaryDirectory() as tmp, Emulator(fault='crc') as d:
            path=Path(tmp)/'invalid.sr'
            p=subprocess.run([str(ROOT/'scripts/capture.sh'),d.path,str(path),'1m','2049'],capture_output=True,text=True,timeout=15)
            self.assertNotEqual(p.returncode,0)
            self.assertFalse(path.exists())
    def test_uart_decoder(self):
        with tempfile.TemporaryDirectory() as tmp, Emulator(mode='uart') as d:
            path=Path(tmp)/'uart.sr'; p=self.capture(d,path,10000,1000000)
            self.assertIn('Capture verified:',p.stderr)
            p=subprocess.run([CLI,'-i',str(path),'-P','uart:rx=CH0:baudrate=115200','-A','uart=rx-data'],capture_output=True,text=True,timeout=15)
            self.assertEqual(p.returncode,0,p.stderr)
            self.assertIn('52',p.stdout)  # ASCII R
            self.assertIn('4F',p.stdout.upper()) # ASCII O

if __name__=='__main__': unittest.main(verbosity=2)
