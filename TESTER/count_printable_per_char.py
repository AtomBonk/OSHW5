#!/usr/bin/env python3
import sys
from collections import Counter

counts = Counter()
for fname in sys.argv[1:]:
    with open(fname, "rb") as f:
        data = f.read()
        for b in data:
            if 32 <= b <= 126:
                counts[b] += 1
for i in range(32, 127):
    if counts[i] > 0:
        print(f"char '{chr(i)}' : {counts[i]} times")
