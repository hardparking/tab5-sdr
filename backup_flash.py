#!/usr/bin/env python3
"""Back up the Tab5's 16MB flash in resumable chunks.

The P4's USB-Serial-JTAG link drops on long reads ([Errno 6] Device not
configured around the 5MB mark), so read 1MB at a time, retry each chunk, and
skip any chunk already on disk. Safe to re-run until it reports all chunks OK.

usage: backup_flash.py <port> <outdir>
"""
import os, subprocess, sys, time

port, outdir = sys.argv[1], sys.argv[2]
CHUNK = 0x100000          # 1MB
TOTAL = 0x1000000         # 16MB
RETRIES = 4

os.makedirs(outdir, exist_ok=True)
missing = []

for i in range(TOTAL // CHUNK):
    off = i * CHUNK
    part = os.path.join(outdir, f"chunk_{i:02d}.bin")
    if os.path.exists(part) and os.path.getsize(part) == CHUNK:
        print(f"chunk {i:02d} @ 0x{off:06x}  already have it")
        continue

    for attempt in range(1, RETRIES + 1):
        r = subprocess.run(
            ["esptool.py", "--port", port, "--after", "no_reset",
             "read_flash", hex(off), hex(CHUNK), part],
            capture_output=True, text=True,
        )
        if r.returncode == 0 and os.path.exists(part) and os.path.getsize(part) == CHUNK:
            print(f"chunk {i:02d} @ 0x{off:06x}  OK (attempt {attempt})")
            break
        if os.path.exists(part):
            os.remove(part)          # partial -- don't let it look complete
        print(f"chunk {i:02d} @ 0x{off:06x}  FAILED attempt {attempt}")
        time.sleep(2)                # let the USB device re-enumerate
    else:
        missing.append(i)

print()
if missing:
    print(f"INCOMPLETE -- missing chunks: {missing}")
    sys.exit(1)

full = os.path.join(outdir, "tab5-factory-16MB.bin")
with open(full, "wb") as out:
    for i in range(TOTAL // CHUNK):
        with open(os.path.join(outdir, f"chunk_{i:02d}.bin"), "rb") as f:
            out.write(f.read())
print(f"COMPLETE -- {os.path.getsize(full)} bytes -> {full}")
