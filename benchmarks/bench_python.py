"""Python equivalents of the Vigil benchmark cases, run as subprocesses to
match the measurement methodology used by bench_vigil.py (process spawn +
import + execution are all included in the timing)."""
import os
import subprocess
import sys
import tempfile
import time

ITERATIONS = 5

CASES = {
    "vm_arith": """\
total = 0
for i in range(5000):
    row = 0
    for j in range(200):
        row += (i * 7 + j * 13) % 97
    total += row
assert total > 0
""",
    "math_ops": """\
import math
acc = 0.0
for i in range(60000):
    x = i * 0.001 + 1.0
    acc += math.sin(x) + math.cos(x) + math.sqrt(x) + math.log(x) + x ** 0.25
assert acc != 0.0
""",
    "parse_ops": """\
total = 0
for _ in range(6000):
    whole = int("12345")
    ratio = float("98.125")
    flag = "true" == "true"
    assert flag
    total += whole + int(ratio)
assert total > 0
""",
    "regex_scan": """\
import re
text = "a1 bb22 ccc333 dddd4444 eeeee55555 ffffff666666 ggggggg7777777"
pat_find = re.compile(r"[a-z]+[0-9]+")
pat_replace = re.compile(r"[0-9]+")
total = 0
for _ in range(4000):
    matches = pat_find.findall(text)
    assert len(matches) == 7
    total += len(matches[0])
    total += len(pat_replace.sub("XX", text))
assert total > 0
""",
    "csv_roundtrip": (
        'import csv, io\n'
        'line = \'"alpha,one",42,"say ""hi""",delta,999\'\n'
        'total = 0\n'
        'for _ in range(20000):\n'
        '    row = next(csv.reader([line]))\n'
        '    assert len(row) == 5\n'
        '    buf = io.StringIO()\n'
        '    csv.writer(buf).writerow(row)\n'
        '    out = buf.getvalue().rstrip("\\r\\n")\n'
        '    total += len(out) + len(row[0]) + len(row[2])\n'
        'assert total > 0\n'
    ),
}

for name, code in CASES.items():
    fd, script = tempfile.mkstemp(suffix=".py")
    try:
        os.write(fd, code.encode())
        os.close(fd)
        # warmup
        subprocess.run([sys.executable, script], capture_output=True)
        times = []
        for _ in range(ITERATIONS):
            t0 = time.perf_counter()
            r = subprocess.run([sys.executable, script], capture_output=True)
            elapsed = (time.perf_counter() - t0) * 1000
            if r.returncode != 0:
                print(f"{name}: FAILED (exit {r.returncode})", file=sys.stderr)
                print(r.stderr.decode(), file=sys.stderr)
                break
            times.append(elapsed)
        else:
            avg = sum(times) / len(times)
            label = name.ljust(13)
            print(f"{label}: avg={avg:.2f}ms  min={min(times):.2f}ms  max={max(times):.2f}ms")
    finally:
        os.unlink(script)
