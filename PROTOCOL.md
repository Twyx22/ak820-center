# AJAZZ AK820 Pro — "legacy" HID protocol (feature-report firmware)

Specification recovered by decompiling the official Windows app
`DeviceDriver.exe` V1.0.0.5 (Ghidra addresses noted as `FUN_…`), cross-checked
against the vespovios/wsclx `ak820pro-modder` crates (`legacy*.rs`, verified on
hardware) and gohv/MemerGamer fixtures.

Applies to boards with the vendor **usage page 0xFF13** collection (control,
65-byte *feature* reports) + **0xFF68** (TFT, 4096-byte outputs).
USB IDs: VID `0C45` (Sonix), PIDs `8009`/`800A` (wired), `FDFD` (2.4G dongle —
config via interface MI_03), `7140` (ISP — never touch).

> **Wired USB only.** Everything below targets the wired vendor interface.
> The 2.4G dongle exposes different interfaces (`0xFF60`, output/input
> reports, no feature reports) and is not covered by this spec yet.

## Transports

### T1 — 65-byte feature reports (main protocol)
- 1 report-ID byte (0) + 64-byte payload. `HidD_SetFeature` with length
  `0x41` (FUN_00451440, called from FUN_0044eed0).
- Every write is preceded/followed by `Sleep(cmd_delaytime)` = **35 ms**
  (from the official config.xml). Reads wait 50 ms.
- Multi-packet writes: len ≥ 0x42 → sends `len>>6` packets of 64 bytes,
  10 retries per packet on failure (FUN_0044eed0).

### T2 — 33-byte feature reports with checksum (single-key ops)
- Same report-ID 0, total size `0x21`. The byte sum of the prepared 65-byte
  buffer is written to **byte 8** (checksum) — FUN_0044f5f0.
- Response validated by bytes 1..3 matching the request; 20 retries,
  10 ms sleep between.

## Generic transaction (T1)

```
START (04 18)        → retry while reply is empty or [04,18,00,FF]
<cmd> (04 XX …)      → data packets (64 B each, echo without status)
COMMIT (04 02)       → ack [04,02,*,01]
FINISH (04 F0, flag) → ack; sent even after a failure
```
Generic ack: `reply[0]==4 && reply[1]==cmd && reply[3]==1`.
Control packet: `[04, cmd, subcmd, 0,0,0,0,0, value_lo, value_hi, 0…]`
(FUN_0044b900: `04 18` then `04 13` with byte8=1).

## T1 commands

| Cmd | Name | Dir | byte8 (value) | Data |
|----:|------|-----|---------------|------|
| `0x02` | COMMIT | W | 0 | — |
| `0x11` | WRITE keymap **base layer** | W | 9 (block count) | 576 B: 144 slots × 4 + `AA 55` |
| `0x13` | Write LED effect | W | 1 | mode packet (below) |
| `0x17` | Write sleep timer | W | 1 | data[8] = 0/1/2/3 (never, 1, 5, 30 min) |
| `0x18` | START | W | flag (1 settings, 0 image/RGB) | — |
| `0x19` | Open MACRO session | W | — | — |
| `0x20` | Battery read | R | — | req **without the 04 prefix**: `[20 01]`, reply `[20 01 00 pct]` (FF = wired full) |
| `0x23` | Write custom RGB bank | W | 9 (block count) | 576 B: 144 × `[id, r, g, b]` + `AA 55` |
| `0x27` | WRITE keymap **Fn layer** | W | 9 | same as 0x11 |
| `0x28` | Clock | W | 1 | `[00 01 5A YY MM DD hh mm ss 00 04 … AA 55]` |
| `0x72` | TFT image config | W | `slot` in byte2, 4K block count in byte8 | then 4096-B chunks as **output reports** on the 0xFF68 interface, 64-B ack per chunk |
| `0xF0` | FINISH | W | flag | — |
| `0xF5` | **Read** custom RGB bank | R | block count (3 or 9) | reply: n × 64 B, slot i = `[id, r, g, b]` @ `i*4`; then `04 02` |

No keymap/macro reads exist: the official app keeps state client-side
(`%APPDATA%\AJAZZ AK820Pro Driver\config.xml`).

### LED mode packet (cmd 0x13)
16 useful bytes, trailer `AA 55` at 14..15 (0x55aa LE — exact vendor value):
```
[0]=mode [1..3]=R,G,B  [8]=colorful  [9]=brightness 0-5
[10]=speed 0-5  [11]=direction 0=L,1=D,2=U,3=R  [14..15]=AA 55
```

**Modes** — full firmware list (language keys 521-540 of the official
driver; also used by the real-time list FUN_0042b3b0, ids 1..19):

| Id | Key | Name |
|---:|---|---|
| `0x00` | 540 | LED Off |
| `0x01` | 521 | Static (true static — do NOT remap) |
| `0x02` | 522 | Single On |
| `0x03` | 523 | Single Off |
| `0x04` | 524 | Glittering |
| `0x05` | 525 | Falling |
| `0x06` | 526 | Colourful |
| `0x07` | 527 | Breath |
| `0x08` | 528 | Spectrum |
| `0x09` | 529 | Outward |
| `0x0A` | 530 | Scrolling |
| `0x0B` | 531 | Rolling |
| `0x0C` | 532 | Rotating |
| `0x0D` | 533 | Explode |
| `0x0E` | 534 | Launch |
| `0x0F` | 535 | Ripples |
| `0x10` | 536 | Flowing |
| `0x11` | 537 | Pulsating |
| `0x12` | 538 | Tilt |
| `0x13` | 539 | Shuttle |
| `0x80` | — | Custom (per-key RGB) |

Notes:
- The main lighting-tab combo (lan 370-378) only exposes a 9-mode subset.
- Modes are sent as-is; the vendor driver applies **no remapping**
  (older community code mapped Static→Breath@speed0 — unnecessary here).
- The mono path (FUN_00434090) sends only `[mode, 0×13, AA 55]` (no color);
  the colorful path (FUN_0042b100) adds color + flags + parameters.
- The `colorful` column of t_light_data: 0 = fixed, 1 = rainbow,
  2 = reversed rainbow.

### Keymap bank (cmd 0x11 / 0x27)
144 4-byte slots indexed by the **light_index** of the layout XML
(FUN_0044ba10 / FUN_00418420), ending with `AA 55`:

| Page | Meaning | p1 | p2 | p3 |
|-----:|---------|----|----|----|
| `0x00` | Default key | | | |
| `0x01` | Mouse | sub-action | button | |
| `0x02` | Keyboard (custom key / Windows shortcut) | modifier mask (0x01=Ctrl, 0x02=Shift, 0x04=Alt, 0x08=Win, 0x10-0x80 = right-hand variants) | HID usage | |
| `0x03` | Multimedia (consumer) | code | (multi-key: second code) | |
| `0x05` | 03 = disabled; 02,code = extended type | | | |
| `0x06` | Macro | macro index | trigger (0 once, 1 ×N, 2 loop) | repeat count |
| `0x07` | Extended (3 params) | p1 | p2 | p3 |

Special case type 2: code 0xE0-0xE7 (bare modifier) → `[02, conv(code), 00]`
via FUN_00451ca0 (0xE0→0x01 … 0xE7→0x80).

### Macros (FUN_0042d6f0)
1. `04 19` (T1, ack)
2. `04 15` value=blockCount+1, then **multi-packet**: 400-byte index
   (100 slots × 4 B, u16 LE data offset, 0 = empty) + blocks starting at
   offset 400, trailer `AA 55`; each action = 4 B
   `[delay_lo, delay_hi, key, flags]`;
   flags: keyboard `B0`(press)/`30`(release), mouse `90`/`10`
   (buttons 1/2/4), pure delay flag `50` (min 10 ms).
3. `04 02` COMMIT, Sleep 30 ms.

### TFT screen (FUN_00422980; matches legacy_tft.rs)
1. START flag 0
2. `04 72 <slot>` value = 4096-B block count (slot 1..3, default 1)
3. 4096-B chunks as **output reports** on the TFT interface + 64-B ack per
   chunk (1 s timeout)
4. `04 02` COMMIT; FINISH flag 0 on failure.
Payload format: 256-B header (gif_headlength): `[N, delay1, …, delayN-1,
00, FF…]` delays in **2 ms units**, then N frames RGB565 LE 128×128
(32 768 B), `FF` padding to a 4096 multiple. Verified legacy max: 30 frames
(official config.xml announces gif_maxframes 140).

**TFT interface per board variant** (hardware-verified 2026-10-31):
- "family B" board (0C45:8009, product "AK820", control 0xFF13):
  TFT data on **0xFF68** — descriptor
  `06 68ff 09 61 [09 62 … 81 02 : in 64 B][09 63 … 96 0010 91 02 : out 4096 B] c0`
- "family A" boards (web-output): 0xFF67 (4104-B outputs).
Try FF68 first, then FF67. Per-chunk ack = 64 B on the same interface.
Full test upload verified: 17 chunks acked + COMMIT ok.

## T2 commands (33 B, checksum at byte 8)

| Cmd | Meaning | Payload |
|----:|---------|---------|
| `0x05` | Custom-mode select + brightness | `[05 10 00 … bright<<8 …]` |
| `0x0F` | Apply settings (wireless mode) | — |
| `0x14` | Write parameter i/N | `[14 <size> <idx> 0 <val…>]` — 7 params (layout A) / 21 params of 28 B (last 16 B) for 144 LEDs |
| `0x1F` | **Apply** (settings OK button) | — |
| single key (T2) | `[00 03|0D 04 <slot> <page p1 p2 p3> <csum>]` — 03 = base, 0D = Fn |

## Official app behaviour
- Persistence: client-side `config.xml`; XML exports (macros, keymap, light).
- Timing: 35 ms (cmd_delaytime) before each write, 50 ms before reads.
- Two hardware layouts: 48-LED mono (0xC0/3 blocks) and 144-LED RGB
  (0x240/9 blocks).
- The driver always sends COMMIT then FINISH; FINISH without COMMIT →
  status FF and a mute keyboard for ~2 s.
- Wireless mode (`*(this+0x784) == 2`) routes settings through `04 0F` (T2);
  lighting still uses T1 packets.

## 2.4G dongle (0C45:FDFD) — preliminary notes
- Interfaces `MI_03`/`MI_04` are vendor `0xFF60`:
  - MI_03: 32-byte input/output reports (no feature reports!)
  - MI_04: 64-byte input/output reports
- The T2 transport (checksum, byte 8) uses **WriteFile** output reports
  (FUN_00451220) — it is the dongle transport, not a feature transport.
- With the keyboard in wired mode the dongle answers every write with the
  status packet `0f 01 01 11`; command acks should only appear once the
  keyboard is actually connected over 2.4G.
