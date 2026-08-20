#!/usr/bin/env python3
"""Reset the Tab5 and capture its boot log to stdout + a file.

usage: capture.py <port> <seconds> <outfile>

Uses the USB-Serial-JTAG reset sequence (RTS drives EN, DTR drives GPIO0);
on the P4 both are exposed through the native USB peripheral.
"""
import sys, time, serial

port, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]

s = serial.Serial(port, 115200, timeout=0.1)

# USB-Serial-JTAG reset: pull EN low with GPIO0 high (normal boot), release.
s.setDTR(False)          # GPIO0 high -> boot from flash, not download mode
s.setRTS(True)           # EN low  -> hold in reset
time.sleep(0.1)
s.setRTS(False)          # EN high -> run
s.reset_input_buffer()

buf = bytearray()
end = time.time() + secs
try:
    while time.time() < end:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
            sys.stdout.write(chunk.decode("utf-8", "replace"))
            sys.stdout.flush()
except serial.SerialException as e:
    # The Tab5 flips its USB port into host mode during init, which yanks the
    # CDC device out from under us. Expected -- keep whatever we captured.
    sys.stderr.write(f"\n--- port dropped ({e}); keeping partial capture ---\n")
try:
    s.close()
except Exception:
    pass

with open(out, "wb") as f:
    f.write(buf)
sys.stderr.write(f"\n--- captured {len(buf)} bytes -> {out} ---\n")
