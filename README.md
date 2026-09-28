# LGames for RP2350B

This repository ports LTris and LBreakout2 to an RP2350B system with HSTX
video, PSRAM, SD storage, USB gamepad input, PCM5102A audio, and an in-game
settings menu.

## Upstream projects

- [LTris](https://lgames.sourceforge.io/LTris/) 1.3.2 by Michael Speck,
  imported in `upstream/ltris` and licensed under GPL-2.0-or-later.
- [LBreakout2](https://lgames.sourceforge.io/LBreakout2/) 2.6.5 by Michael
  Speck, imported in `upstream/lbreakout2` and licensed under GPL-2.0-or-later.

The exact imported releases are identified by LTris `configure.ac` and
LBreakout2 `configure`. Their original `COPYING` files are preserved. The
hardware adaptation is distributed under the same GPL terms; see `LICENSE`.

Vendored support code is listed in `.release-vendored`. FatFs integration and
lwmem retain their own licence files. The repository also contains pico_hdmi,
pico_psram, lodepng, and NES-pad support derived from
[fhoedemakers/pico_shared](https://github.com/fhoedemakers/pico_shared); source
headers retain their applicable notices. The RP2350B board header and adapted
second-stage flash loader come from Raspberry Pi's Pico SDK 2.3.0 under its
BSD-3-Clause licence; their licence and copyright headers are preserved.

## Port layout

- `apps/`: selector and game entry points.
- `platform/`: video, audio, input, storage, launcher, SDL compatibility, and
  settings-menu integration.
- `upstream/`: imported game sources with `LGAMES_PORT` platform hooks.
- `tools/prepare_sd.py`: converts upstream assets to RGB565 SD files.
- `tools/package_lgames.py`: combines the selector and isolated game slots.
- `tests/host/`: host rendering, memory, input, and bundle tests.

Compare the upstream directories with the releases above, then inspect the
`LGAMES_PORT` conditional sections and the platform files to review the port.

## Build

Prerequisites are Arm GNU Toolchain 13.3.Rel1, Raspberry Pi Pico SDK 2.3.0,
Pico-PIO-USB, CMake, and Python 3.

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
export PICO_PIO_USB_PATH=/path/to/Pico-PIO-USB
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DRP_LGAMES_CONTAINER=ON
cmake --build build --target lgames_bundle -j1
```

The combined output is `build/lgames-bundle.uf2`. Intermediate game UF2 files
are relocated bundle components and are not standalone applications.

## SD files

```sh
python3 tools/prepare_sd.py --output sdcard
```

Copy `sdcard/games/ltris` and `sdcard/games/lbreakout2` under `/games` on the
SD card. Runtime-generated high-score and screenshot files are ignored by Git.

## Controls

- D-pad navigates menus and controls the games.
- A confirms and launches the selected game.
- Select+Start opens Settings in the selector and during gameplay.
- LTris uses A for hold, B for hard drop, and Y for rotate or pause according
  to the active context.
- LBreakout2 accepts A/B/X/Y to release the ball; Start pauses.

## Validation status

USB timing, SD access, HDMI stability, analogue audio, repeated launches, and
gameplay were exercised on the RP2350B target during hardware acceptance. Host
rendering, memory-pressure, selector, settings-menu, and bundle tests also
passed. No ROMs or proprietary game data are included; shipped assets are from
the GPL upstream games.
