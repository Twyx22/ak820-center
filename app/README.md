# AK820 Center — native Linux app for the AJAZZ AK820 Pro keyboard

Open-source reimplementation, compiled natively for Linux, of the official
AJAZZ AK820 Pro Windows app (V1.0.0.5, decompiled — see
[`../PROTOCOL.md`](../PROTOCOL.md)).

## Features

| Tab | Content |
|---|---|
| Lighting | 20 onboard effects + color + rainbow, brightness/speed (0-5), direction (← ↓ ↑ →) |
| Keys | Remap **base** layer and **Fn** layer: default, disabled, keyboard+modifiers, media, mouse, macro |
| Custom RGB | Per-key painting (144 LEDs), read/write of the stored bank |
| Macros | Up to 100 macros (400-byte index + blocks, 3072-byte firmware budget) |
| TFT screen | GIF/image import → RGB565 128×128, 4096-byte chunks, slots 1-3 |
| Settings | Sleep (never/1/5/30 min), clock sync, battery |

## Build

```bash
sudo pacman -S --needed qt6-base hidapi cmake gcc
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release
cmake --build app/build -j$(nproc)
```

## Manual install

```bash
sudo install -Dm755 app/build/ak820-center /usr/local/bin/ak820-center
sudo install -Dm644 extracted/app/layouts/rgb-keyboard.xml /usr/share/ak820-center/rgb-keyboard.xml
sudo install -Dm644 extracted/app/skins/theme1/logo/logo.png /usr/share/ak820-center/icon.png
sudo install -Dm644 app/packaging/ak820-center.desktop /usr/share/applications/
# HID permissions (required):
sudo cp app/packaging/99-ajazz-ak820pro.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# then: re-login OR usermod -aG input $USER
```

## Arch package (PKGBUILD)

```bash
cd app/packaging && makepkg -si
```

## CLI test tool

`tools/ak820ctl` (see sources): `probe`, `battery [dongle]`, `clock`,
`sleep 0-3`, `light <mode> <rrggbb> [bright] [speed] [dir] [rainbow]`,
`readrgb 3|9`.

## Protocol

**Wired (USB-C) mode only**: vendor interface usage page `0xFF13`,
65-byte feature reports, transactions `START 04 18 → cmd → COMMIT 04 02 →
FINISH 04 F0` with 35 ms between writes. Full details in
[`../PROTOCOL.md`](../PROTOCOL.md). The keyboard does not expose keymap/macro
reads: state is persisted client-side
(`~/.config/ak820-center/state.json`), exactly like the official app keeps
its `config.xml`.

## Status / known limitations

- The 2.4G dongle mode exposes a different vendor interface (`0xFF60`):
  lighting/remap over the dongle is **not yet hardware-verified**.
- Some sub-values (mouse p1/p2, extended pages 0x07) still need hardware
  confirmation — tables isolated in `keymapwidget.cpp`.
- TFT screen: the number of legacy frames verified is ≤ 30 (the official app
  advertises 140; the 256-byte config.xml header is implemented).
