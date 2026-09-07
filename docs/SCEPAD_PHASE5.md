# ScePad input — Phase 5

This gate adds the native DualSense input backend to the stable dedicated
Xash3D host. It proves the PS5 platform layer independently of the Phase 6
client/menu/renderer integration: the gate records canonical Xash events now,
and the same backend binds those events to `Joy_AxisMotionEvent` and
`Key_Event` when client mode is enabled.

## Provenance and ABI

The 120-byte ScePad record, button masks, connection-generation behavior and
`scePadRead` batching policy are derived from the independently authored,
hardware-tested
[ps5-native-gamepad-input-research](https://github.com/blackbearreloaded/ps5-native-gamepad-input-research)
at commit `16e9b953b26a7102bc801a380f08fbf00060d84b` (GPL-3.0). The repository
keeps an independently reviewable C adapter and project-owned compatibility
declarations; it does not import vendor headers.

Compile-time assertions pin the record size and the offsets used by the
backend: buttons `0x00`, sticks `0x04`, triggers `0x08`, timestamp `0x50`,
connection generation `0x68`, unique-data length `0x6b` and unique data
`0x6c`. `tests/test_ps5_platform_abi.c` prevents silent layout drift.

The normal research lifecycle uses the initial user. A ShadowMount/LNC launch
on the tested console can leave that user detached from the DualSense, so this
port deliberately opens the pad for `sceUserServiceGetForegroundUser`. That is
the same user-selection rule already proven by the BSP viewer. UserService is
terminated only when this backend acquired it; the pad handle is closed once.

## Translation contract

`scePadRead(handle, records, 64)` returns a chronological batch, oldest record
first. The backend consumes every returned record rather than only the newest,
preserving short button edges between engine polls. Stick bytes are centered
at 128 and mapped to signed Xash axes; trigger bytes map from 0–255 to
0–32767.

| DualSense control | Xash event / gate action |
| --- | --- |
| Left stick X / Y | side / forward movement |
| Right stick X / Y | yaw / pitch look |
| Cross | A button / jump |
| Circle | B button / use |
| L1 or R3 | crouch (logical OR, without an early release) |
| R1 | fire |
| Square, Triangle | X, Y buttons |
| Create, Options | back, start |
| L3, R3, L1, R1, L2, R2 | standard Xash pad buttons |
| D-pad, touchpad | standard Xash pad buttons |
| L2 / R2 analog values | left / right trigger axes |

A disconnected record, the interception flag, a controller-generation change
or a read failure emits a neutral state immediately. This prevents stuck
movement or buttons after reassignment. Host tests also cover combined
L1/R3 crouch edges, a full 64-record batch, non-owned UserService, failed
open/read and rollback.

## Build and acceptance

Build the bounded hardware gate with private game data and telemetry config:

```sh
git submodule update --init third_party/xash3d-fwgs third_party/hlsdk-portable
XASH_GAME_DATA=/private/path/half-life \
PS5LOG_DEV_CONF=/private/path/dev.conf \
XASH_GATE_SECONDS=120 make engine-pad-native-release
```

The validator requires `pad_gate=1`; connected input; movement and look;
press/release pairs for jump, crouch, use and fire; no read error; one exact
pad close; exact UserService ownership; `XASH_PAD_COMPLETE ... pass=1`; a
clean engine exit and gap-free `BYE`:

```sh
python3 tools/validate_engine_boot_evidence.py /path/to/run.json \
  --engine-commit 9aa39ad --hlsdk-commit e277ffa --map c1a0 --pad-gate
```

## Accepted FW 12.02 evidence

- Run: `20260907T181827569Z_PPSA99996_xash3d-engine_0xbec4d1cc932e`
- fSELF SHA-256: `6681a8a822edf1114a5e9f32d01286b90442430a35a180295909d3ab8ca15d82`
- Linked ELF SHA-256: `46da56f13a7d17f0b7d2323e2cc5d0fa16cb0d40997c25f9e5c73e527a15500a`
- Transcript SHA-256: `6dd2db62d23b2aa33f0387bacf2c4b534e4e64317562c4489b5bb3a2b21d20da`
- Engine/hlsdk pins: `9aa39ad` / `e277ffa`
- Polls / samples / empty reads: 4,361 / 24,535 / 3,608
- Maximum batch: 62 records
- Connected / disconnected / intercepted: 24,535 / 0 / 0
- Axis / button events: 3,654 / 12
- Movement / look samples: 1,286 / 1,258
- Jump / crouch / use / fire edges: 1/1, 2/2, 2/2, 1/1
- Read errors: 0
- Teardown: `scePadClose=0`, owned `sceUserServiceTerminate=0`
- Transport: clean, gap-free `ps5log/1` BYE after 36.552 seconds on device

The 62-record maximum demonstrates that the batched path ran on hardware. The
structured action transcript proves both edges of every required gameplay
action; visual observation alone is not the acceptance criterion.
