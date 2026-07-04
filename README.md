# SILTA
**Infra tool-set mod.**

```
                                                                                             ███
                                                                               ███         ██████
                                                                              █████        ███████
                                                                              ██████        ██████
                                                                                            ██████                          █████████
                                                                                  █████     ██████                          ██████████████
                                                                         ██████   ██████     ██████                         █████████████████
                                                                     ███████████   █████     ██████                               ██████████████
                                                                 ███████████████   ██████     █████                                     ████████████
                                                               ███████████          █████     █████                                     █████████████
                                                             ████████               █████     █████      ██████████████████             █████   ██████
                                                           ███████                  █████      ████████████████████████████████         █████    ██████
                                                          ██████                    █████      ████████████████        ███████████    ███████      ███████
                                                          ████████████              █████       ████████                   ███████   ███████        ██████
                                                          █████████████████         █████                                     ███    ██████           ██
                                                            █████████████████      ██████                                             ███
                                                                      ███████        ██                                                        █████████████
                                                                        █████                                                                  ████████████████████
                                                                      ██████                                                                   ██████████████████████
                                                                     ██████                                                                                     ██████
                                                                   ███████                                                                             ████       █████
                                                                 ███████                                                                              ███████     █████
                                                              █████████                                                                                ███████    █████
                                                           ██████████                                                                                    ███████  █████
                                                         ██████████                                                                                       ███████ █████
                                                     ███████████                                                                                            ███   █████
                                                  ███████████                                                                                                     █████
                                              ███████████                                                                                                         █████
                                         █████████████                                                                                                            █████
                                      ████████████                                                                                                                █████
                                 █████████████                                                                                                                     ████
                             █████████████
                         ██████████████
                     █████████████
                ██████████████
           █████████████
       █████████████
 ██████████████
██████████
 █████

                                                   ___  __          ___  __   __           __   ___ ___           __   __
                                           | |\ | |__  |__)  /\      |  /  \ /  \ |    __ /__` |__   |      |\/| /  \ |  \
                                           | | \| |    |  \ /~~\     |  \__/ \__/ |___    .__/ |___  |      |  | \__/ |__/ .
```

A utility mod for [INFRA](https://store.steampowered.com/app/251110/INFRA/), implemented as a
Source engine plugin. This is a **fork** of Floorb's Infra Mod that merges three
community projects into a single DLL and adds extra configuration:

| Source | What it contributed |
| --- | --- |
| [Neetpone's InfraMod](https://github.com/Neetpone/InfraMod) | Base code: success counters, **functional camera (photo)**, **inventory (battery) overlay** |
| [INFRA Success Counters](https://www.moddb.com/mods/infra-success-counters) by *fehc* | The original success-counters concept this is based on |
| [INFRA Success Counters FIX](https://www.speedrun.com/infra/resources/mjtgi) by *MrMagnetix* | Corrected per-map maximum counts (baked in + drop-in support) |

## Why a merge was needed
The original **Success Counters** mod (fehc, ModDB) is built on *x360ce* and ships as a proxy
`xinput1_3.dll`. Neetpone's mod is **also** a proxy `xinput1_3.dll`. A game folder can only
contain **one** `xinput1_3.dll`, so the two mods **cannot be installed at the same time** — they
directly conflict.

This merged build resolves that by being the single DLL that does everything: it already contains
the success counters, so you do **not** install fehc's original DLL alongside it. Drop this in and
remove any other `xinput1_3.dll` from the INFRA folder.

## Corrected map data (the FIX)
MrMagnetix's corrected maximum counts are **baked into the default data** (see the diff in
`MERGE_NOTES.md`), so the counters are right out of the box — including the `c6_m6_central`
defect/corruption swap and several wrong photo totals (bunker, stormdrain, cistern, npp, …).

In addition, on startup the mod looks for a **`mapdata.txt`** next to `infra.exe`. If present and
valid, it is used instead of the embedded data. This is the exact filename/location MrMagnetix's FIX
uses, so any future updated `mapdata.txt` is a **drop-in** — and you can tweak counts without
rebuilding the DLL. A ready-to-use copy is included in this repo.

## Installation
1. Locate your INFRA installation folder (the folder with `infra.exe`) and go one level deeper into
   the `infra/` folder under it — the one that contains `gameinfo.txt`.
2. Place the plugin files. With the included `.vdf` (whose `"file"` value is the bare
   `silta`), Source looks for the DLL at the `infra/` root, so you have:
   - `infra/silta.dll`
   - `infra/addons/silta.vdf`  *(included in this repo under `addons/`)*

   The `.vdf` (which lives in `addons/`) is what tells Source to load the plugin; its `"file"` path is
   resolved relative to the `infra/` mod folder. If you'd rather keep the DLL inside `addons/`, set the
   `"file"` value back to `addons/silta`.
3. **Remove any other `xinput1_3.dll`** (e.g. fehc's original Success Counters DLL) from the INFRA
   folder so they don't conflict.
4. *(Optional)* copy the included `mapdata.txt` next to `infra.exe`. Not required — the corrected
   values are already built in — but handy if you want to edit counts or apply a newer FIX.
5. Launch the game. Press **Insert** to toggle the overlays.

## Features

Everything below is configurable (or fully disableable) in `silta.ini`.

**Survey & progress**
- **Success counters** — per-map `current / maximum` for photos, corruption, repairs,
  geocaches and flow meters, with MrMagnetix's corrected map data baked in (+ drop-in
  `mapdata.txt` support), per-row colours, canon labels, completion styles and a total line.
- **Study outlook** (F1) — live photo/corruption tallies against the game's real ending
  thresholds (>=50% study, 90% Raven Research), with an "on track for" verdict.
- **Progress popup** — per-category % and next achievement whenever the cursor is out.
- **End-of-game survey report** — on the ending map, writes `NCG_Survey_Report.txt` next to
  `infra.exe` and shows a themed N.C.G. report card with the verdict.

**Camera (IMAGE-IN Crystal-shot)**
- Photos save hitch-free to `DCIM\<Site>\DSC#####.jpg` (per-site subfolders, digital-camera
  numbering; encoding runs on a background thread).
- **Lore-accurate 480p** by default: the Crystal-shot's box art says "480p ready", so photos
  save at 660x480 with a real-camera center-crop. Fully configurable (`photo_width/height`,
  `aspect = crop|stretch`; 0x0 = native full resolution).
- **Burn-in caption** — N.C.G. survey strip (location / date / surveyor), colours configurable.
- **EXIF metadata** — camera identity (IMAGE-IN / Crystal-shot by default, configurable via
  `exif_make/model/software`), per-chapter in-world timestamp (+20 min per mission),
  site description, N.C.G. asset tag, and **player coordinates** (comment + GPS tags anchored to
  Stolland's canonical Baltic position) out of the box (origin member at 0x2C baked in; `calibrate` remains as a recovery tool).
- **Survey log** — every shot appended to `DCIM\survey_log.txt` (file, site, time, coords).
- **Contact sheet** (F5) — thumbnail grid of every photo on the card (previous sessions
  included), lazy-loaded with zero stutter; click to isolate a shot with zoom/pan; Rescan.

**Field kit**
- **N.C.G. Field Calculator** (F2) — Basic / Scientific / Programmer (bases + bitwise) /
  Text (hex<->ASCII, binary, Caesar cipher) modes plus structural-analyst helpers (unit
  conversions, flow Q=V·A, stress σ=F/A, slope). Skins: N.C.G., "Osmo Olut", or **custom** —
  a live in-game Style tab with color pickers and a save-to-ini button.
- **NCG Sketchbook** (F3) — paintable survey sheet (letterhead, grid, title block), square or
  rectangular canvas, trace mode over the game scene, PNG export to `sketches\`.
- **Notes scratchpad** (F4) — persistent notes in several paper styles.

**HUD**
- **Inventory overlay** — flashlight/camera batteries with trailing icons, OS coins (coin icon
  appears once collected), hidden automatically on maps that don't use it (`hidden_maps`).
- **Flashlight gauge** — real-time battery charge (segments + %) while the flashlight drains,
  with the spare count; reads the live charge member found via SILTA's built-in autoscanner.
  Three skins (default / subtle / custom colors), smooth fade-out, numbers toggle.
- **Hotkey tip bar** — sorted F1..F12; **draggable overlays** (F11 unlock) with a curated
  default layout and positions saved across sessions; version watermark in the main menu.

## Hotkeys (defaults, all rebindable)

| Key | Action |
| --- | --- |
| Insert | Show / hide all overlays |
| F1 | Study outlook | 
| F2 | Field calculator |
| F3 | Sketchbook |
| F4 | Notes scratchpad |
| F5 | Contact sheet |
| F6 | Reload `silta.ini` live |
| F7 / F8 | Toggle counters / inventory |
| F9 / F10 | Cycle counters / inventory corner |
| F11 | Unlock (drag overlays) / lock |
| F12 | Reset positions |

## Configuration

On first run the mod writes a fully-commented **`silta.ini`** next to `infra.exe` — that file
(and the `silta.ini.example` in this repo) is the authoritative reference for every option.
Delete `silta.ini` to regenerate documented defaults; press **F6** in-game to reload edits live.
Window positions persist in `silta_layout.ini` (delete it once to re-seed the curated default
layout). Before publishing, verify `[report] ending_maps` matches your build's real epilogue map (check the map name in console). Set `[log] verbose = true` when hunting a bug — timestamps + immediate flush + crash
marker in `silta.log`.

## Version history

- **v0.9 (pre-release)** — everything above: counters + FIX data, hitch-free Crystal-shot
  camera (480p lore default, EXIF with in-world time / coordinates / Stolland GPS, survey
  log, per-site folders), contact sheet, study outlook, end-of-game report with the SILTA
  banner, field calculator (4 modes + skins + live Style tab), sketchbook, notes,
  real-time flashlight gauge (draggable), per-window layouts, custom skins, verbose
  logging with crash marker, menu watermark. First public release will be v1.0.
