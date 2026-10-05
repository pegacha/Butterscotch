#!/usr/bin/env python3
"""Screenshot every room on the desktop runner and in Azahar (3DS build), and compare them.

One harness for both: start a game from the title screen, then for each room `goto` it, wait, screenshot. The desktop
gets the same run through harness_to_desktop.py (--goto-room, --screenshot-at-frame); the 3DS build runs it from
harness.txt on Azahar's SD card in screen mode 1x, so its top screen holds the game's 320x240 unscaled at x 40.
Each screenshot is paired with the room it was taken in (from the "Room changed" / "Screenshot saved" log lines: Samus
keeps her position across a goto and can walk into a door), and the pairs are ranked by how much they differ.

Rooms are ranked by content present on one side and not the other (sprites, objects, effects), not by colour: edge
maps are compared with a few pixels' tolerance, and the largest connected block of 8x8 cells with unmatched edges is
the score. Output (--out): report.html (worst first: desktop | 3DS | 3DS with the differences boxed), summary.csv
(also the plain colour differences), desktop/, 3ds/, logs.

usage (Linux, from this repository; needs Python 3 with Pillow, Xvfb for the desktop window):
  tools/n3ds/compare_rooms.py --game ~/am2r --desktop build-desktop/butterscotch \\
      --azahar azahar/squashfs-root/AppRun --sd ~/.local/share/azahar-emu/sdmc/3ds/am2r \\
      --cia build-n3ds/am2r.cia --out /tmp/rooms
  --match REGEX (default ^rm_a: the game's rooms) or --rooms a,b,c picks the rooms; --skip-desktop / --skip-3ds reuse
  an earlier run's screenshots in --out; --per-room / --settle set the frames per room and the wait before the shot.

The title -> game presses are the ones of harness/ingame.txt: with a save in slot A they load it, else start a new game;
both sides need the same saves (copy the desktop folder's sav* to the SD card, or remove both).
The desktop runner must be built with screenshots (a GL renderer) and the 3DS build with the harness (any build).
"""
import argparse
import csv
import glob
import os
import re
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageChops, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# Title screen -> in game (harness/ingame.txt), then rooms from START_FRAME.
PREAMBLE = ["press 1010 START 4", "press 1110 START 4", "press 1210 A 4", "press 1310 A 4",
            "press 1700 START 4", "press 1850 START 4", "press 2010 A 4"]
START_FRAME = 2300
THREEDS_CROP = (40, 0, 360, 240)  # the game's 320x240 on the 400x240 top screen in 1x mode


def log(msg):
    print(msg, flush=True)


def list_rooms(args):
    out = subprocess.run([args.desktop, "data.win", "--print-rooms"], cwd=args.game, capture_output=True, text=True).stdout
    names = re.findall(r"^\[\d+\] (\S+)", out, re.M)
    if args.rooms:
        wanted = args.rooms.split(",")
        return [r for r in wanted if r in names] or wanted
    return [r for r in names if re.search(args.match, r)]


def write_harness(rooms, args, path):
    lines = ["# compare_rooms.py: title -> game, then each room"] + PREAMBLE
    frame = START_FRAME
    shots = {}
    for room in rooms:
        lines.append(f"goto {frame} {room}")
        lines.append(f"screenshot {frame + args.settle}")
        shots[frame + args.settle] = room
        frame += args.per_room
    lines.append(f"exit {frame}")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return shots, frame


def ensure_display():
    if os.environ.get("DISPLAY"):
        return
    subprocess.Popen(["Xvfb", ":99", "-screen", "0", "1280x960x24"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.environ["DISPLAY"] = ":99"
    time.sleep(1)


def run_desktop(args, harness):
    ensure_display()
    outdir = os.path.join(args.out, "desktop")
    os.makedirs(outdir, exist_ok=True)
    inputs = os.path.join(args.out, "desktop_inputs.json")
    extra = subprocess.run([sys.executable, os.path.join(HERE, "harness_to_desktop.py"), harness, inputs],
                           capture_output=True, text=True, check=True).stdout.split()
    cmd = [os.path.abspath(args.desktop), "data.win", "--disable-log-colours", "--seed", "1", "--playback-inputs", inputs,
           "--screenshot", os.path.join(outdir, "frame_%d.png")] + extra
    log(f"desktop: {len(extra) // 2} commands")
    with open(os.path.join(args.out, "desktop.log"), "w") as f:
        subprocess.run(cmd, cwd=args.game, stdout=f, stderr=subprocess.STDOUT, timeout=args.timeout)


def run_3ds(args, harness):
    ensure_display()
    sd = args.sd
    sdmc = os.path.dirname(os.path.dirname(os.path.abspath(sd)))
    if args.cia:
        log("3ds: installing " + args.cia)
        subprocess.run([args.azahar, "-i", args.cia], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
    apps = glob.glob(os.path.join(sdmc, "Nintendo 3DS", "*", "*", "title", "00040000", "0a2e2100", "content", "*.app"))
    if not apps:
        sys.exit("3ds: AM2R isn't installed in Azahar (pass --cia)")
    apps.sort(key=os.path.getmtime, reverse=True)  # the latest install
    # Our files on the card for this run; whatever was there is put back afterwards.
    backup = os.path.join(args.out, "sd_backup")
    os.makedirs(backup, exist_ok=True)
    touched = ["harness.txt", "inputs.json", "cheats.ini", "done.txt"]
    had = {}
    for name in touched:
        p = os.path.join(sd, name)
        had[name] = os.path.exists(p)
        if had[name]:
            shutil.copy2(p, os.path.join(backup, name))
    try:
        shutil.copy2(harness, os.path.join(sd, "harness.txt"))
        with open(os.path.join(sd, "inputs.json"), "w") as f:
            f.write("{}")  # a fixed random seed, as the desktop's --seed 1; no keyboard input
        with open(os.path.join(sd, "cheats.ini"), "w") as f:
            f.write("master=0\nhealth=0\nammo=0\nbeam=0\nbombs=0\nmissiles=0\nscreen=1\nframeskip=0\n")  # 1x
        shutil.rmtree(os.path.join(sd, "shots"), ignore_errors=True)
        done = os.path.join(sd, "done.txt")
        if os.path.exists(done):
            os.remove(done)
        log("3ds: running " + apps[0])
        with open(os.path.join(args.out, "azahar.log"), "w") as f:
            proc = subprocess.Popen([args.azahar, apps[0]], stdout=f, stderr=subprocess.STDOUT)
            deadline = time.time() + args.timeout
            while time.time() < deadline and not os.path.exists(done) and proc.poll() is None:
                time.sleep(5)
            time.sleep(2)
            proc.terminate()
            try:
                proc.wait(10)
            except subprocess.TimeoutExpired:
                proc.kill()
        if not os.path.exists(done):
            log("3ds: no done.txt (timed out or crashed); comparing what there is")
        outdir = os.path.join(args.out, "3ds")
        shutil.rmtree(outdir, ignore_errors=True)
        shutil.copytree(os.path.join(sd, "shots"), outdir, dirs_exist_ok=True) if os.path.isdir(os.path.join(sd, "shots")) else os.makedirs(outdir)
        if os.path.exists(os.path.join(sd, "log.txt")):
            shutil.copy2(os.path.join(sd, "log.txt"), os.path.join(args.out, "3ds.log"))
    finally:
        for name in touched:
            p = os.path.join(sd, name)
            if had[name]:
                shutil.copy2(os.path.join(backup, name), p)
            elif os.path.exists(p):
                os.remove(p)


def rooms_at_shots(logpath):
    """frame -> the room the screenshot was taken in, from a log's Room changed / Screenshot saved lines."""
    result = {}
    room = None
    if not os.path.exists(logpath):
        return result
    for line in open(logpath, errors="replace"):
        m = re.search(r"Room changed: \S+ \(room \d+\) -> (\S+) \(room", line)
        if m:
            room = m.group(1)
        m = re.search(r"Screenshot saved: \S*frame_0*(\d+)\.png", line)
        if m:
            result[int(m.group(1))] = room
    return result


CELL = 8          # missing-content grid (8x8 pixel cells)
EDGE = 40         # an edge: grey-level gradient above this
NEAR = 7          # an edge counts as present if the other image has one within 3 px (animation, sub-pixel offsets)


def edges(img):
    g = img.convert("L").filter(ImageFilter.FIND_EDGES)
    return g.point(lambda v: 255 if v > EDGE else 0)


def missing_cells(a_edges, b_edges_near):
    """8x8 cells where a has edges that b has nothing near: the content b lacks (colour doesn't matter)."""
    w, h = a_edges.size
    ap = a_edges.load()
    bp = b_edges_near.load()
    cells = set()
    for cy in range(0, h, CELL):
        for cx in range(0, w, CELL):
            total = lost = 0
            for y in range(cy, min(cy + CELL, h)):
                for x in range(cx, min(cx + CELL, w)):
                    if ap[x, y]:
                        total += 1
                        if not bp[x, y]:
                            lost += 1
            if total >= 6 and lost * 10 >= total * 6:
                cells.add((cx // CELL, cy // CELL))
    return cells


def largest_cluster(cells):
    best, seen = 0, set()
    for c in cells:
        if c in seen:
            continue
        stack, size = [c], 0
        seen.add(c)
        while stack:
            x, y = stack.pop()
            size += 1
            for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if n in cells and n not in seen:
                    seen.add(n)
                    stack.append(n)
        best = max(best, size)
    return best


def compare(args, shots):
    desk_rooms = rooms_at_shots(os.path.join(args.out, "desktop.log"))
    ds_rooms = rooms_at_shots(os.path.join(args.out, "3ds.log"))
    sbs_dir = os.path.join(args.out, "sbs")
    os.makedirs(sbs_dir, exist_ok=True)
    rows = []
    for frame, room in sorted(shots.items()):
        dp = os.path.join(args.out, "desktop", f"frame_{frame}.png")
        tp = os.path.join(args.out, "3ds", f"frame_{frame:05d}.png")
        row = {"room": room, "frame": frame, "desktop_room": desk_rooms.get(frame), "3ds_room": ds_rooms.get(frame),
               "missing_on_3ds": None, "extra_on_3ds": None, "missing_cells": None, "mean_diff": None, "pct_differing": None,
               "image": ""}
        if os.path.exists(dp) and os.path.exists(tp):
            d = Image.open(dp).convert("RGB")
            if d.size != (320, 240):
                d = d.resize((320, 240), Image.NEAREST)
            t = Image.open(tp).convert("RGB").crop(THREEDS_CROP)
            diff = ImageChops.difference(d, t)
            px = list(diff.get_flattened_data() if hasattr(diff, "get_flattened_data") else diff.getdata())
            row["mean_diff"] = round(sum(sum(p) for p in px) / (len(px) * 3), 2)
            row["pct_differing"] = round(100.0 * sum(1 for p in px if max(p) > 48) / len(px), 2)
            # Content (sprites, objects, effects) on one side and not the other, ignoring colour: edges with nothing
            # near them in the other image, as 8x8 cells; a room ranks by its largest connected block of them.
            de, te = edges(d), edges(t)
            lost = missing_cells(de, te.filter(ImageFilter.MaxFilter(NEAR)))
            extra = missing_cells(te, de.filter(ImageFilter.MaxFilter(NEAR)))
            row["missing_on_3ds"] = largest_cluster(lost)
            row["extra_on_3ds"] = largest_cluster(extra)
            row["missing_cells"] = len(lost)
            mark = t.copy()
            mp = mark.load()
            for cells, colour in ((lost, (255, 0, 255)), (extra, (0, 255, 255))):
                for cx, cy in cells:
                    for x in range(cx * CELL, min(cx * CELL + CELL, 320)):
                        for y in (cy * CELL, min(cy * CELL + CELL, 240) - 1):
                            mp[x, y] = colour
                    for y in range(cy * CELL, min(cy * CELL + CELL, 240)):
                        for x in (cx * CELL, min(cx * CELL + CELL, 320) - 1):
                            mp[x, y] = colour
            sbs = Image.new("RGB", (960, 240))
            sbs.paste(d, (0, 0))
            sbs.paste(t, (320, 0))
            sbs.paste(mark, (640, 0))
            row["image"] = f"sbs/{frame}_{room}.png"
            sbs.save(os.path.join(args.out, row["image"]))
        rows.append(row)
    rows.sort(key=lambda r: (-1, 0) if r["missing_on_3ds"] is None else (max(r["missing_on_3ds"], r["extra_on_3ds"]), r["missing_cells"]),
              reverse=True)

    with open(os.path.join(args.out, "summary.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else ["room"])
        w.writeheader()
        w.writerows(rows)

    def cell(r):
        mismatch = r["desktop_room"] != r["3ds_room"] or r["desktop_room"] != r["room"]
        where = "" if not mismatch else f" <b>(desktop in {r['desktop_room']}, 3DS in {r['3ds_room']})</b>"
        if r["pct_differing"] is None:
            return f"<tr><td>{r['room']}{where}</td><td colspan=2>missing screenshot</td><td></td></tr>"
        return (f"<tr><td>{r['room']}{where}<br>frame {r['frame']}</td><td>{r['missing_on_3ds']}</td><td>{r['extra_on_3ds']}</td>"
                f"<td><img src='{r['image']}' width=960 height=240 loading=lazy></td></tr>")
    html = ["<!doctype html><meta charset=utf-8><title>Room comparison</title>",
            "<style>body{font:14px sans-serif;background:#111;color:#ddd}td{padding:4px;vertical-align:top}"
            "img{image-rendering:pixelated}</style>",
            "<h1>Desktop vs 3DS (Azahar): missing content, worst first</h1>",
            "<p>Each image: desktop | 3DS (1x, cropped) | 3DS with the differences boxed: <b style='color:#f0f'>magenta</b> ="
            " on the desktop, missing on the 3DS; <b style='color:#0ff'>cyan</b> = only on the 3DS. Colour is ignored (edges are"
            " compared, within 3 px). missing / extra = the largest connected block of such 8x8 cells. Small scattered boxes are"
            " usually animation; rooms in bold weren't the same room on both sides (Samus took a door).</p>",
            "<table><tr><th>room</th><th>missing on 3DS</th><th>extra on 3DS</th><th></th></tr>"]
    html += [cell(r) for r in rows]
    html.append("</table>")
    with open(os.path.join(args.out, "report.html"), "w") as f:
        f.write("\n".join(html))
    log(f"{len(rows)} rooms; report: {os.path.join(args.out, 'report.html')}")
    for r in rows[:20]:
        log(f"  {r['room']:<16} missing {r['missing_on_3ds']}, extra {r['extra_on_3ds']} (cells {r['missing_cells']})"
            f"{'' if r['desktop_room'] == r['3ds_room'] == r['room'] else '  (rooms: desktop %s, 3ds %s)' % (r['desktop_room'], r['3ds_room'])}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--game", required=True, help="desktop game folder (data.win and its files)")
    p.add_argument("--desktop", required=True, help="desktop butterscotch binary")
    p.add_argument("--azahar", help="Azahar executable (AppRun of the extracted AppImage)")
    p.add_argument("--sd", help="AM2R's folder on Azahar's SD card (.../sdmc/3ds/am2r)")
    p.add_argument("--cia", help="install this am2r.cia into Azahar first")
    p.add_argument("--out", required=True)
    p.add_argument("--match", default=r"^rm_a", help="regex on room names (default: the game's rooms)")
    p.add_argument("--rooms", help="comma-separated room names instead of --match")
    p.add_argument("--per-room", type=int, default=100, help="frames per room")
    p.add_argument("--settle", type=int, default=60, help="frames after the goto before the screenshot")
    p.add_argument("--timeout", type=int, default=3600, help="seconds per run")
    p.add_argument("--skip-desktop", action="store_true")
    p.add_argument("--skip-3ds", action="store_true")
    args = p.parse_args()
    os.makedirs(args.out, exist_ok=True)

    rooms = list_rooms(args)
    harness = os.path.join(args.out, "harness.txt")
    shots, last = write_harness(rooms, args, harness)
    log(f"{len(rooms)} rooms, {last} frames")
    if not args.skip_desktop:
        run_desktop(args, harness)
    if not args.skip_3ds:
        if not args.azahar or not args.sd:
            sys.exit("--azahar and --sd are needed for the 3DS run (or --skip-3ds)")
        run_3ds(args, harness)
    compare(args, shots)


if __name__ == "__main__":
    main()
