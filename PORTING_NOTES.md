# Nintendo 3DS port (PLATFORM=n3ds)

Branch `port/n3ds`, from upstream `main` at `d8575ec`. 3DS backend from
[Project-Sunshine-Native/cinnamon](https://github.com/Project-Sunshine-Native/cinnamon) `UNDERTALE-3DS` (`f139a60`).
First target: AM2R 1.1 (WAD 14), New 3DS (804 MHz, L2 cache, 124 MB mode, core 2).

## Build and run

```bash
tools/n3ds/build.sh ../build-n3ds           # -> am2r.cia + am2r.3dsx (BS_N3DS_GAME=generic: plain runner)
tools/n3ds/make_sd.sh <game dir> <out>      # -> <out>/3ds/am2r/: data.win, lang/, gfx/, audio/, config.ini seed
```

The program ships in the CIA (title `AM2R`, unique ID `0xA2E21`); the SD folder `sdmc:/3ds/am2r/` holds the game's
static files (`data.win`, `lang/`, preprocessed `gfx/` and `audio/`) plus saves and `log.txt`. The CIA checks the
folder's `sd_data_rev.txt` against `tools/n3ds/sd_data_rev.txt` and stops with a message if the card is stale, so a
preprocessor change means a new revision and a new SD folder.

Emulator loop (Windows host, WSL build): `tools/n3ds/run_in_azahar.ps1` installs the CIA into Azahar (New 3DS mode,
Vulkan) and collects `log.txt`, screenshots and Azahar's log (`-Harness`, `-Save`, `-ClearSaves`, `-Inputs`,
`-WaitSeconds` hard limit, `-CpuClock`: **100 approximates hardware**; 300 hides CPU-bound problems).
`harness.txt` = scripted presses / self-screenshots / exit, with `after_room` anchors (`hwcheck.txt`: title, load,
pause, walk; `pause.txt`; `explore.txt`). `inputs.json` = keyboard playback in the desktop `--playback-inputs`
format with a fixed seed; `tools/n3ds/run_tas.ps1` (and `run_tas.bat`, drag a `.ltm` onto it) replays a libTAS
movie via `ltm_to_inputs.py`. `-DENABLE_VM_GML_PROFILER=ON` logs a GML profile every 120 frames.

## Interface adaptations (Cinnamon fork-era -> upstream)

- Vtable: `endFrame` -> `endFrameInit`/`endFrameEnd`; `drawTiled` -> `drawSpriteTiled`; `prewarmRoom` slot gone
  (main calls `N3DSRenderer_prewarmRoom` on room change); `drawTriangle` takes per-vertex colours; `drawText*`
  take `lineSeparation`; `beginGUI` takes a target surface; `gpuSetBlendModeExt` has alpha factors; surface API
  rewritten (`setRenderTarget`, `ensureApplicationSurface`, `drawSurface` with a source rect, ...).
- View mapping: computed from upstream's world-view-projection matrix and the viewport (x and y map
  independently; rotated views are logged as UNIMPL). `MATRIX_*` start as identity.
- Surfaces: citro3d render targets in VRAM (citro3d can't render into linear memory). Freed ones go back to a
  small pool two frames after `surface_free` (citro3d panics on in-frame deletes) and are reused by the next
  `surface_create` of that size: AM2R recreates some every frame, which fragmented VRAM. VRAM is two 3 MB banks
  and a target must fit in one; a surface that doesn't fit at RGBA8 drops to RGBA5551, then to a smaller stored
  area (top-left part). Over 1024 only the top-left 1024 is stored (PICA limit). AM2R's 2048x1024 pause map ends
  up as 1024x512 RGBA5551, which holds the whole map (it uses about 600x464).
- Screen: AM2R lays out its own 320x240 (application surface and HUD at its own offsets), so the default mode
  (`N3DS_SCREEN_MODE`) stretches it to 400x240 (also: widescreen hack, 1:1 pillarbox). The touch screen belongs to
  the pause screen.
- Pause screen (`n3ds_pause.c`): while the game is in `rm_subscreen` (or the transition into it), the game's
  screen (`RENDER_TARGET_HOST_FRAMEBUFFER`) is the bottom screen, 1:1, and the top screen shows the game as it was
  when paused. That frame is captured when Start goes into the input delay line (AM2R blanks the screen on the
  frame it leaves for the pause room). Touch becomes the presses the pause screen already understands (AM2R's own
  Menu1/Menu2 bindings and the D-pad), plus a few variable writes:
  - the hint strip at the bottom (it shows the Menu2 hint) is Menu2: change page / back;
  - page cross: tap an icon;
  - map: drag to pan (`oMapCamera.targetx/targety`), tap a cell to put the marker there, tap the marker to delete it;
  - inventory: tap an item to select it (shows its tip), tap again to toggle it;
  - logs: tap an entry to select it, tap again to expand; drag to scroll the list or the text;
  - options and submenus: tap a row to select, tap again to activate; touch and hold the left/right half of a
    selected value box to turn it down/up (the volume sliders only move while the direction is held).
  Touch must never become a mouse: AM2R moves Samus to the mouse while button 1 is held (a debug leftover).
- Start+Select toggles the debug monitor (on the top screen while paused). Start and Select reach the game 3 frames
  late, press for press, so the chord is caught before the game sees either.
- "Saving..." (small, top screen, bottom-right) while the game's files have changes not yet on the SD card.
- Builtins vs game scripts: the VM prefers a builtin over a game script of the same name (for GMS 2.3+ games'
  compatibility scripts). For games older than 2.3 the 3DS front end points those calls back at the game's script
  (AM2R's `string_split(str, sep, index)`; the GMS2 builtin returns an array and every button hint read "<array>").
- Main loop: own loop in `src/n3ds/main.c` following `loop.c`'s sequence (`loop.c` is GL/windowing specific).
  The GPU frame is opened before `Runner_step`, since GML draws to surfaces from any event. Pacing is
  `C3D_FrameRate` at the room speed only.
- File system: upstream `OverlayFileSystem` wrapped by `N3DSCachedFileSystem`, which keeps the SD card out of the
  frame loop: existence checks cached; binary files are memory buffers between open and close; files the game
  reads or writes are held in memory and written back a second after the game stops changing them, only if they
  differ from the card (flushed at exit too), by a writer thread from a snapshot. AM2R's `crypt` script XORs its 236 KB save in place a byte at a time
  with a seek per byte, and decrypts/re-encrypts on every save read: seconds per read through stdio, nothing now.
- Input: 3DS buttons -> gamepad slot 0 by position (B = face1, A = face2, Y = face3, X = face4, L/R, ZL/ZR,
  Select, Start, D-pad, circle pad/C-stick axes) plus keyboard arrows/Enter/Escape. AM2R binds its actions in its
  own `config.ini`; `res/n3ds/am2r/sd/config.ini` seeds a 3DS layout (B jump, Y fire, A morph, L/ZL aim, R
  missiles, ZR aim lock, Select weapon, Start pause; menus A = OK, B = back) when the card has none.
- Audio: NDSP (Cinnamon's system on upstream's interface). Sound effects: a DSP-ADPCM bank read once into linear
  memory and played in place. Music (GameMaker streamed sounds, `AUDIO_ENTRY_FLAG_IS_EMBEDDED` clear): BCWAV
  DSP-ADPCM files streamed from the SD by a worker thread on core 2.
- Textures: the preprocessor packs atlas pages (ETC1A4/RGBA5551/LA4/L4, RGBA8 where an image has partial alpha)
  into `gfx/atlas.bin`; sprite sheets into `gfx/direct_assets.bin`; `room_manifest.bin` lists each room's pages.
  On a room change the room's pages are queued for a background loader thread (own handle on `atlas.bin`, core 2,
  priority above the main thread: below it, it starved); a page needed before its turn is read or waited for.
- `drawTile` culls against the visible room rectangle before any lookup (the runner hands over every tile in
  the room; ~2x on AM2R's draw time).
- Dropped Undertale-only code: borders, battle top-screen layout, sprite-name hacks, GMS2 tile-layer cache,
  `sdmc:/3ds/UNDERTALE` paths.
- `n3ds-preprocess`: builds against upstream (`src/image/image_decoder.c`, bzip2, log shim, const-safe `repeat`).

## Diagnostics

Bottom screen (top screen while paused; toggle Start+Select): FPS, average frame ms and the worst in the last quarter second; `S` step (GML),
`W` waiting for the GPU/VBlank, `D` building the GPU frame, `P` room prewarm; `IO` SD reads by the renderer,
`TX` texture uploads/surface allocations, `FS` the game's file I/O, `AU` audio calls (all ms per frame, main
thread only); VRAM, heap used/total, linear free, UNIMPL count, room. `log.txt`: a `Perf:` line every 5 s, every
room's first frame and any frame over 100 ms with the same breakdown.

## Cinnamon engine changes re-applied

None so far: AM2R runs on upstream's engine unmodified (no AUDO header parsing, `normalizeFixedArrayScope` or
`strengthenReturnValue` needed yet; `parseAudo=false` + per-chunk loading keep memory down).

## Open UNIMPL items

None hit in title, menus, file select, intro, the pause screen or the first area (`rm_a0h01`..`rm_a0h06`).
Stubbed and logged if hit: shaders (AM2R has 2, not used so far), primitives/vertex buffers, gradient sprite
parts/surfaces, `surface_get_pixels`, `sprite_create_from_surface`, fog, colour write masks, rotated views.

## Performance (Azahar, New 3DS, CPU clock 100%)

| | before | now |
|---|---|---|
| save select opens | 4.9 s | 0.6 s |
| loading a save | 5.1 s | 0.7 s |
| pause screen | 5 fps | 56 fps |
| first area, gameplay draw | 20 ms | 8-10 ms |
| first frame of `rm_a0h01` / `rm_a0h02` | 1056 / 230 ms | 666 / 78 ms |

Hardware (before these changes): gameplay 37 fps with 13.7 ms draw; freezes of seconds to minutes at the save
screens and room changes, pause screen ~7.5 fps.

## Known issues

- A room change on hardware once left the game in the transition room (Samus gone, old view shown, game still
  running); not reproduced in Azahar.
- Start/Select reach the game 3 frames (50 ms) late (Start+Select chord detection).
- `file_text_open_append` is not an upstream builtin (AM2R's `writelog` silently writes nothing).
