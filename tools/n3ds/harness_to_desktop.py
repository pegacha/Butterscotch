#!/usr/bin/env python3
"""Turn a 3DS harness.txt into a desktop reference run (same frames, keyboard instead of buttons).

usage: harness_to_desktop.py harness.txt out.json  -> prints the extra butterscotch CLI arguments
"""
import json
import sys

# 3DS button -> AM2R default keyboard binding (A = jump/menu OK = Z, B = fire = X, Start = Enter).
KEYS = {"START": 13, "A": 90, "B": 88, "SELECT": 27, "UP": 38, "DOWN": 40, "LEFT": 37, "RIGHT": 39,
        "X": ord("X"), "Y": ord("C"), "L": ord("A"), "R": ord("S")}

frames = {}
args = []
for line in open(sys.argv[1]):
    parts = line.split()
    if not parts or parts[0].startswith("#"):
        continue
    if parts[0] == "press":
        frame, key = int(parts[1]), KEYS.get(parts[2].upper())
        length = int(parts[3]) if len(parts) > 3 else 1
        if key is None:
            continue
        frames.setdefault(str(frame), {"keysPressed": [], "keysReleased": []})["keysPressed"].append(key)
        frames.setdefault(str(frame + length), {"keysPressed": [], "keysReleased": []})["keysReleased"].append(key)
    elif parts[0] == "screenshot":
        args += ["--screenshot-at-frame", parts[1]]
    elif parts[0] == "exit":
        args += ["--exit-at-frame", parts[1]]
json.dump(frames, open(sys.argv[2], "w"))
print(" ".join(args))
