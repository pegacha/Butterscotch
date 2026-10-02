# Nintendo 3DS port (PLATFORM=n3ds)

Branch `port/n3ds`, from upstream `main` at `d8575ec`. 3DS backend from
[Project-Sunshine-Native/cinnamon](https://github.com/Project-Sunshine-Native/cinnamon) `UNDERTALE-3DS` (`f139a60`).
First target: AM2R 1.1 (WAD 14).

## Build and run

```bash
tools/n3ds/build.sh ../build-n3ds                    # -> butterscotch.3dsx (devkitARM, citro2d/citro3d)
cmake -S tools/n3ds-preprocess -B ../build-preprocess && cmake --build ../build-preprocess
../build-preprocess/n3ds-preprocess data.win <sd>/3ds/butterscotch   # -> gfx/ (tex3ds atlases)
```

SD card: `sdmc:/3ds/butterscotch/` = `butterscotch.3dsx`, `data.win`, `gfx/`, `lang/` (AM2R's), saves.
`audio/` from the preprocessor is not needed (audio is stubbed).

Emulator loop (Windows host, WSL build): `tools/n3ds/run_in_azahar.ps1` (`-Harness`, `-Save`, `-ClearSaves`,
`-Inputs`, `-WaitSeconds` hard limit). `harness.txt` = scripted presses / self-screenshots / exit, with
`after_room` anchors. `inputs.json` = keyboard playback in the desktop `--playback-inputs` format, replayed
with a fixed seed; `tools/n3ds/ltm_to_inputs.py` converts libTAS movies, `harness_to_desktop.py` turns a
harness into a desktop reference run.

## Interface adaptations (Cinnamon fork-era -> upstream)

- Vtable: `endFrame` -> `endFrameInit`/`endFrameEnd`; `drawTiled` -> `drawSpriteTiled`; `prewarmRoom` slot gone
  (main calls `N3DSRenderer_prewarmRoom` on room change); `drawTriangle` takes per-vertex colours; `drawText*`
  take `lineSeparation`; `beginGUI` takes a target surface; `gpuSetBlendModeExt` has alpha factors; surface API
  rewritten (`setRenderTarget`, `ensureApplicationSurface`, `drawSurface` with a source rect, ...).
- View mapping: computed from upstream's world-view-projection matrix and the viewport (x and y map
  independently; rotated views are logged as UNIMPL). `MATRIX_*` start as identity.
- Surfaces: citro3d render targets backed by `C3D_Tex` (RGBA8, VRAM else linear). The application surface is a
  real surface, presented 1:1 and pillarboxed on the top screen. Targets/textures are deleted between frames,
  two frames after `surface_free` (citro3d panics on in-frame deletes). Surfaces over 1024 keep their logical
  size but only the top-left 1024x1024 is stored (AM2R's 2048x1024 pause map shows only its corner).
- Main loop: own loop in `src/n3ds/main.c` following `loop.c`'s sequence (`loop.c` is GL/windowing specific).
  The GPU frame is opened before `Runner_step`, since GML draws to surfaces from any event.
- File system: upstream `OverlayFileSystem` (Cinnamon's lacked `file_bin_*`/directories) wrapped by
  `N3DSCachedFileSystem` (existence checks cached; SD stats cost milliseconds and AM2R checks a file every frame).
- Input: 3DS buttons -> gamepad slot 0 (Switch-port layout) plus D-pad/Start/Select -> arrows/Enter/Escape.
- Dropped Undertale-only code: borders, battle top-screen layout, sprite-name hacks, GMS2 tile-layer cache,
  `sdmc:/3ds/UNDERTALE` paths.
- `n3ds-preprocess`: builds against upstream (`src/image/image_decoder.c`, bzip2, log shim, const-safe `repeat`).

## Cinnamon engine changes re-applied

None so far: AM2R runs on upstream's engine unmodified (no AUDO header parsing, `normalizeFixedArrayScope` or
`strengthenReturnValue` needed yet; `parseAudo=false` + per-chunk loading keep memory down).

## Open UNIMPL items

None hit in title, menus, file select, intro or the first area (`rm_a0h01`..`rm_a0h06`).
Stubbed and logged if hit: shaders (AM2R has 2, not used so far), primitives/vertex buffers, gradient sprite
parts/surfaces, `surface_get_pixels`, `sprite_create_from_surface`, fog, colour write masks, rotated views.

## Known issues

- Atlas pages are RGBA5551 (1-bit alpha): soft edges/glows lose their partial alpha.
- File-select hint line shows raw placeholder strings.
- Audio stubbed. Performance numbers so far are from Azahar (~55 fps in the first area at 300% CPU clock);
  real New 3DS numbers pending.
