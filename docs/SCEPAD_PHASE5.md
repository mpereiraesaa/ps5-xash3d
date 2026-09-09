# ScePad input — Phase 5

## Opt-in Studio coverage QA (not release mapping)

`XASH_STUDIO_COVERAGE_QA=1` enables a temporary touchpad-click cycle, starting
at mode 0 each launch. It requires a complete MainUI/ref_agc stack and at least
300 map-relative seconds; it excludes the wall/lighting/HUD/recovery and weapon
grant probes. All other DualSense v5 controls stay unchanged. The cvar is not
archived and normal builds do not intercept the touchpad for this test.

| Click mode | What to observe |
| --- | --- |
| 0 — Normal | Natural pose transitions and accepted lighting/chrome. |
| 1 — Linear controller | Deliberate slow sweep of model controller ranges; mouth remains natural. |
| 2 — Circular controller | Forced wrap from byte 248 to 8 for rotation controls; it should take the short arc, not a full turn. |
| 3 — 2 blends | First actual two-blend sequence in each visible model, with swept blending. Unsupported models remain unchanged and report `supported=0`. |
| 4 — 4 blends | Same for a four-blend sequence; unchanged output is NOT proof if no such sequence exists. |
| 5 — Glowshell | Normal NPC body plus an expanded blue/cyan shell; moving highlight, no persistent state leak after returning to 0. |

Click again after 5 to return to 0. Stay in `c1a0`; approach NPCs and observe
several angles. Deliberately forced head/pose movement in modes 1–4 is not
normal gameplay animation. The 2026-09-09 hardware run passed visual QA and
paired resource validation. Four-blend hardware coverage remains pending:
none of the visible models supported it. See `PHASE7_STUDIO_LIGHTING.md` for
run identities and the precise accepted subset.

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

### Release controller profile v5 (current accepted aim baseline)

The maintained profile is [`xash/config/dualsense.cfg`](../xash/config/dualsense.cfg).
Install it as `valve/userconfig.d/10-dualsense.cfg` in the writable game data.
The pinned engine executes `userconfig.d/*.cfg` after `config.cfg`, so this
profile survives `unbindall` in the saved config. It only assigns the gamepad
buttons listed below and enables the two sticks; keyboard/mouse bindings and
left-stick behavior are preserved. Right-stick sensitivity is now explicitly
configured as described below. Touchpad is not assigned or cleared by this
profile, keeping the optional QA hook available. No Remote Play is required.

Profile v4 includes the v3 mapping of R2 to primary attack for all weapons (including crowbar),
and R1 to secondary attack. Profile v4 is deployed and startup-confirmed;
the operator accepted v5 aim feel for now and confirmed R2 crowbar attack.
Other untested button actions remain pending.
Reload with Square was subsequently operator-confirmed on 2026-09-09:
animation plays and the magazine refills; crowbar attack with R2 was reconfirmed.
The viewmodel-event run is recorded in `PHASE7_STUDIO_LIGHTING.md`.
The table below records
upstream defaults, not these two overrides.
The last column is the **historical pre-install audit**, not the post-install
state. L2/L3 use the engine's
`+speed` modifier, not a newly implemented sprint. Weapon selection uses the
inventory directly: D-pad left equips the previous weapon, right the next,
without pressing R1. Profile v2 sets `hud_fastswitch 1`. The pinned HLSDK
originally applies this cvar only to slot selection, so the build generates
`ps5_ammo.cpp` with the same opt-in behavior for `invprev` and `invnext`.
It sends the weapon command directly, never synthesizes attack, and retains
the upstream death/HUD/ammo guards. `hud_fastswitch 0` restores confirmation.
This cvar also affects keyboard/mouse weapon cycling, but their bindings are
unchanged. The operator confirmed immediate cycling with either direction
and repeated presses returning to the pistol, without R1. With two weapons,
both directions and `lastinv` look equivalent; `lastinv` still means the
previously equipped weapon, not inventory cycling. D-pad up remains spray;
no cheats have been added to the release profile.

Profile v2 deployment: FTP readback matches local SHA-256
`f704d6d8ec6cf166ecd8c978842108f21a54e9f137e75123cf582ad454438eba`.
Client PRX SHA-256:
`44da7c9ef86a650b84129538ec20de988fefdf5da535edb1736d541b07b240fd`.
Run `20260909T162323591Z_PPSA99996_xash3d-engine_0x155a5f82b3e1f`
confirms `PS5_DUALSENSE_PROFILE version=2 loaded=1`. Native build and
`tests/test_prepare_client_ammo.py` passed; the latter checks both generated
selection sites and rejects upstream source drift, not gameplay behavior.

For release packaging, include this cfg at the path above and verify the
startup marker `PS5_DUALSENSE_PROFILE version=5 loaded=1`; do not assume
engine defaults survive existing user configs. To customize the layout, edit
this profile or add a later-sorted cfg in `userconfig.d`. To stop enforcing it,
remove that profile; already saved bindings remain until changed explicitly.
Hardware action validation (jump/use/fire/reload/weapon selection) is separate
from confirming the file was installed or executed.

Effects QA (2026-09-09): operator accepted pistol muzzleflash, wall marks,
blood and the more visible blood/sprite-lighting configuration. This does not
change any DualSense binding. `ps5_blood_amount` defaults to 1.5 (more droplets
and moderately larger sprites); set it to 1 for original presentation. It does
not change damage or give weapons. The normal no-grant build was restored
without relaunch; graphics QA audio remains disabled.

Historical profile v1 was installed on PPSA99996 on 2026-09-09 with owner approval. Local and FTP
readback SHA-256 both:
`d3aac3278dfab617475bf146ec5385ad07311b05def365d10bff5a53d25c45ca`.
Run `20260909T161409756Z_PPSA99996_xash3d-engine_0x15525055f36e6`
logs `execing userconfig.d/10-dualsense.cfg` followed by the version=1
loaded=1 marker. All 16 button bindings match the pinned engine defaults in
a host comparison. No existing cfg was overwritten. On 2026-09-09 the operator
confirmed pistol fire with R1, jump with Cross, crouch with both L1 and R3,
and pistol-to-crowbar selection with D-pad left followed by R1. These are
manual gameplay observations, not independent action telemetry. Reload, use,
secondary attack and the remaining bindings still need explicit gameplay QA;
this installation is not a resource teardown gate.

### Right-stick aim baseline — profile v5

Operator feedback on v4: R2 crowbar attack works and fine adjustment overshoots
less, but aiming still feels too fast. V5 changes only the two look rates from
180/135 to 140/105 degrees/second (yaw/pitch), preserving the radial deadzone,
curve, movement and bindings. On 2026-09-09 the operator accepted v5 for now:
"listo dejemoslo asi por ahora esta mejor que antes". Keep these settings as
the current baseline; no further sensitivity changes requested. This accepts
the subjective aim feel, not every QA item below or the entire viewmodel gate.
V5 local/FTP SHA-256:
`1fb70cc9172a82136e69e0a780869d1e437880d468524fdfa03a004adc13b9db`.
Run `20260909T164158161Z_PPSA99996_xash3d-engine_0x156a978a27888`
confirms profile version=5 loaded=1; binaries unchanged from the v4 aim run.

Only client-mode right-stick yaw/pitch pass through `pad_aim.h`; movement,
trigger values, button edges, and the dedicated platform gate are unchanged.
Normalize the vector by 32767, compute radius r, return zero inside the radial
deadzone d, otherwise preserve direction and use magnitude
`((min(r, 1) - d) / (1 - d)) ^ exponent`. The transform is memoryless: no
history, smoothing delay, time-based acceleration, or aim assist. Diagonal
input is normalized rather than receiving a square-corner speed boost.

Candidate parameters, subject to operator QA rather than universal defaults:

| Setting | Value | Meaning |
|---|---|---|
| `ps5_aim_enable` | 1 | Enable PS5 right-stick shaping |
| `ps5_aim_deadzone` | 0.10 | Radial inner deadzone, clamped 0..0.4 |
| `ps5_aim_exponent` | 1.6 | Fine center response, clamped 1..3 |
| `joy_yaw` | 140 | Maximum horizontal speed, degrees/second |
| `joy_pitch` | 105 | Vertical axis rate, 25% below horizontal |
| `joy_yaw_deadzone`, `joy_pitch_deadzone` | 0 | Avoid a second axial cutoff |

`set` creates the PS5 profile variables before the first native pad poll.
Nonfinite deadzone/exponent fall back to 0.10/1.6. Values can be edited in
the profile and reloaded. To restore the previous linear feel, set
`ps5_aim_enable 0`, `joy_pitch 100`, `joy_yaw 100` and both look deadzones
to 4096. Merely disabling shaping does not restore those other settings.

Host tests cover neutral input, inner deadzone, signed endpoints, diagonal
normalization, full positive-axis monotonicity, symmetry and invalid settings.
Hardware QA must check idle drift, small adjustments on a fixed edge, tracking,
full-stick turns, release-to-stop, diagonal motion, unchanged movement, and
R2 attacks with pistol/crowbar. Loading the cfg alone does not accept aim feel.

Deployment evidence (2026-09-09): local/FTP profile SHA-256
`40936db7c11d2ac14494bd327ef74affb7bc4ae25cdf59884526d439a0baa707`;
engine ELF `7cb95f38a88638e01dce8f981e210e4d044fad0bd851d4a69d1c27c49429b644`;
SELF `d6ff88953965ac2308d5c22303659d5f764eefc26c89e0c4335498e493cecc20`.
Run `20260909T163602896Z_PPSA99996_xash3d-engine_0x15656c176bdac`
logs profile version=4 loaded=1. Native build, host suite (`make all`) and
ASan/UBSan radial tests passed. Existing unsupported legacy config commands
still warn at startup; this is not a zero-warning claim or a completed QA gate.

### Historical pre-profile config audit

During viewmodel QA, read-only FTP inspection of the running title's
`/mnt/sandbox/PPSA99996_000/download0/xash3d/valve/config.cfg` found `unbindall`
followed by only `bind "START" "cancelselect"` for gamepad buttons. Joystick
axes are enabled. Upstream `Key_Unbindall_f` clears all bindings and restores
Escape/Start only; defaults listed below do not survive that config. This is
saved configuration evidence, not a live in-memory bindlist dump. No bindings
were changed during this audit. Other runtime cfg overrides have not been
exhaustively excluded.

| DualSense | Engine default action | Saved console config |
|---|---|---|
| Left stick | Move | Enabled |
| Right stick | Look | Enabled |
| Cross | Jump | Not rebound after unbindall |
| Circle | Use/interact | Not rebound |
| Square | Reload | Not rebound |
| Triangle | Flashlight | Not rebound |
| R1 | Primary attack | Not rebound |
| R2 | Secondary attack | Not rebound |
| L1 / R3 | Crouch | Neither rebound |
| L2 / L3 | Speed modifier (`+speed`) | Neither rebound |
| D-pad left/right | Previous/next inventory weapon | Not rebound |
| D-pad down | Last weapon | Not rebound |
| D-pad up | Spray | Not rebound |
| Options | Cancel selection/menu (`cancelselect`) | Explicitly bound |
| Create | Pause | Not rebound |
| Touchpad | Unassigned | Unassigned in normal/viewmodel builds |

Button names are translated by `xash/platform_ps5/in_ps5.c`; default commands
are in `third_party/xash3d-fwgs/engine/client/input/in_keys.c`. The table below
describes the original platform gate, not proof of the currently loaded game
bindings. Restore a scoped gamepad profile before requiring attack/reload QA;
do not reset unrelated keyboard or user settings without authorization.

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
