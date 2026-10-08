#!/usr/bin/env python3

import sys
if "--help" in sys.argv or "-h" in sys.argv:
    print('Generate impulse wav.\npython tools/codegen/generate_impulse_wav.py [output.wav]\nWrites 16-bit mono PCM at 22050 Hz. Default: samples/data/clap_test.wav (overwritten).')
    raise SystemExit(0)
# generate_impulse_wav.py
#
# NOT PART OF THE ORIGINAL.  Project tooling.
#
# Write samples/data/clap_test.wav as 22050 Hz, 16-bit mono PCM.
# Separate transients with silence so reverb decay remains audible.

import struct
import math
import sys

RATE = 22050
DURATION_S = 6.0
CLICK_TIMES = [0.25, 1.5, 2.75, 4.25]
CLICK_LEN_S = 0.015
AMPLITUDE = 30000

n_samples = int(RATE * DURATION_S)
samples = [0] * n_samples

for t0 in CLICK_TIMES:
	start = int(t0 * RATE)
	click_len = int(CLICK_LEN_S * RATE)
	for i in range(click_len):
		idx = start + i
		if idx >= n_samples:
			break
		# A decaying tone burst excites multiple comb-filter frequencies.
		env = (1.0 - i / click_len)
		val = AMPLITUDE * env * math.sin(2 * math.pi * 900 * i / RATE)
		val += AMPLITUDE * 0.5 * env * math.sin(2 * math.pi * 2400 * i / RATE)
		samples[idx] = max(-32768, min(32767, int(val)))

out_path = sys.argv[1] if len(sys.argv) > 1 else "samples/data/clap_test.wav"

with open(out_path, "wb") as f:
	data = struct.pack("<%dh" % n_samples, *samples)
	block_align = 2
	byte_rate = RATE * block_align
	f.write(b"RIFF")
	f.write(struct.pack("<I", 36 + len(data)))
	f.write(b"WAVEfmt ")
	f.write(struct.pack("<IHHIIHH", 16, 1, 1, RATE, byte_rate, block_align, 16))
	f.write(b"data")
	f.write(struct.pack("<I", len(data)))
	f.write(data)

print("wrote %s: %d samples, %.1fs, %d clicks" % (out_path, n_samples, DURATION_S, len(CLICK_TIMES)))
