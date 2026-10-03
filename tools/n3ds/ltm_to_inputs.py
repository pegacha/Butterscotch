#!/usr/bin/env python3
"""Convert a libTAS movie (.ltm, keyboard only) into Butterscotch's input recording JSON (--playback-inputs).

usage: ltm_to_inputs.py movie.ltm out.json [frame_offset]
libTAS stores the held X11 keysyms for every frame; the JSON wants the GameMaker keys pressed/released per frame.
"""
import gzip
import io
import json
import sys
import tarfile

X11_TO_VK = {
    0xff51: 37, 0xff52: 38, 0xff53: 39, 0xff54: 40,  # arrows
    0xff0d: 13, 0xff1b: 27, 0xff08: 8, 0xff09: 9,    # return, escape, backspace, tab
    0xffe1: 16, 0xffe2: 16, 0xffe3: 17, 0xffe4: 17,  # shift, control
    0xffe9: 18, 0xffea: 18, 0x20: 32,                # alt, space
    0xff50: 36, 0xff57: 35, 0xff55: 33, 0xff56: 34,  # home, end, page up/down
    0xff63: 45, 0xffff: 46, 0xff13: 19,              # insert, delete, pause
    # keypad with NumLock off (navigation keys), as GameMaker sees them on Windows
    0xff95: 36, 0xff96: 37, 0xff97: 38, 0xff98: 39, 0xff99: 40, 0xff9a: 33, 0xff9b: 34,
    0xff9c: 35, 0xff9d: 12, 0xff9e: 45, 0xff9f: 46, 0xff8d: 13,
    # keypad with NumLock on
    0xffaa: 106, 0xffab: 107, 0xffad: 109, 0xffae: 110, 0xffaf: 111,
}
X11_TO_VK.update({0xffb0 + i: 96 + i for i in range(10)})   # keypad 0-9
X11_TO_VK.update({0xffbe + i: 112 + i for i in range(12)})  # F1-F12


def vk(sym):
    if sym in X11_TO_VK:
        return X11_TO_VK[sym]
    if 0x61 <= sym <= 0x7a:  # a-z -> 'A'-'Z'
        return sym - 0x20
    if 0x30 <= sym <= 0x39:  # 0-9
        return sym
    raise ValueError("unmapped keysym 0x%x" % sym)


def read_inputs(path):
    data = open(path, "rb").read()
    while data[:2] == b"\x1f\x8b":  # tasvideos serves it gzipped once more
        try:
            tarfile.open(fileobj=io.BytesIO(data), mode="r:gz").getmember("inputs")
            break
        except tarfile.TarError:
            data = gzip.decompress(data)
    tar = tarfile.open(fileobj=io.BytesIO(data), mode="r:gz")
    return tar.extractfile("inputs").read().decode().splitlines()


def main():
    lines = read_inputs(sys.argv[1])
    offset = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    frames = {}
    held = set()
    for i, line in enumerate(lines):
        keys = set()
        part = line.strip("|")
        if part.startswith("K") and len(part) > 1:
            keys = {vk(int(k, 16)) for k in part[1:].split(":") if k}
        pressed, released = sorted(keys - held), sorted(held - keys)
        if pressed or released:
            frames[str(i + offset)] = {"keysPressed": pressed, "keysReleased": released}
        held = keys
    if held:
        frames[str(len(lines) + offset)] = {"keysPressed": [], "keysReleased": sorted(held)}
    json.dump(frames, open(sys.argv[2], "w"))
    print("%d frames, %d input changes" % (len(lines), len(frames)))


if __name__ == "__main__":
    main()
