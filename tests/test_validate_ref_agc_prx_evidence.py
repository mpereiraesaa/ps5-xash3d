#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "tools" / "validate_ref_agc_prx_evidence.py"
ENGINE = "9aa39ad"
HLSDK = "e277ffa"
BUNDLE = "1" * 64
STUDIO = "2" * 64


def write_run(directory: Path, name: str, app: str, messages: list[str], *,
              raw: list[str] | None = None, started: str) -> Path:
    raw = raw or []
    boot = "0x1234" if app == "xash3d-engine" else "0x5678"
    reason = "xash-engine-boot-complete" if app == "xash3d-engine" \
        else "ref-agc-live-complete" if any(
            message.startswith("REF_AGC_LIVE_COMPLETE ")
            for message in messages) else "ref-agc-runtime-complete"
    lines = [f"HELLO ps5log/1 title=PPSA99996 app={app} boot={boot} tag=test"]
    for seq, message in enumerate(messages, 1):
        lines.append(f"{seq}\t{seq * 10}\tMARK\t{message}")
        if seq == 2:
            lines.extend(raw)
    lines.append(f"BYE seq={len(messages)} reason={reason}")
    data = ("\n".join(lines) + "\n").encode()
    log_name = name + ".log"
    (directory / log_name).write_bytes(data)
    manifest = {
        "identity": {"title": "PPSA99996", "app": app, "boot": boot},
        "protocol": "ps5log/1", "transport": "tcp", "hello": True,
        "bye": True, "clean": True, "gaps": [], "oversized_lines": 0,
        "raw_lines": len(raw), "records": len(messages),
        "last_seq": len(messages), "log_path": log_name,
        "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
        "run_id": name, "started_utc": started,
        "bye_fields": {"seq": str(len(messages)), "reason": reason},
    }
    path = directory / (name + ".json")
    path.write_text(json.dumps(manifest), encoding="utf-8")
    return path


def engine_messages(frame_hash: str = "d8c9aadab3c82cdb", *,
                    live: bool = True, consumer: bool = False,
                    resources: bool = False,
                    boot_map: str = "c1a0",
                    live_menu: bool = False) -> list[str]:
    complete = [
        "XASH_SERVER_PRX_COMPLETE module=server.prx stop_result=0 active_modules=4 ownership=exact",
        "XASH_MENU_PRX_COMPLETE module=menu.prx stop_result=0 active_modules=3 ownership=exact pass=1",
        "XASH_CLIENT_PRX_COMPLETE module=client.prx stop_result=0 active_modules=2 ownership=exact pass=1",
    ]
    if resources and not consumer:
        raise ValueError("resource evidence requires the live consumer")
    exports = 40 if resources else 31 if consumer else 26 if live else 16
    backend = "phase7-live" if consumer else "phase4-native"
    ownership = "fence+videoout+ack" if consumer else "fence+videoout"
    ready_live = (" live_frames=0 live_view_frames=0 "
                  "live_view_hash=0000000000000000 live_view_changes=0 "
                  "live_map_serial=0 world_surfaces=0 entity_peak=0 "
                  "draw2d_peak=0 dropped_entities=0 dropped_2d=0") if live else ""
    state_live = (" live_frames=100 live_view_frames=99 "
                  "live_view_hash=1234567890abcdef live_view_changes=0 "
                  "live_map_serial=1 world_surfaces=3695 entity_peak=22 "
                  "draw2d_peak=3 dropped_entities=0 dropped_2d=0") if live else ""
    ready_consumer = (" consumed_frames=0 consumed_serial=0 "
                      "consumed_view_frames=0 consumed_camera_hash=0000000000000000 "
                      "consumed_camera_changes=0") if consumer else ""
    state_consumer = (" consumed_frames=100 consumed_serial=100 "
                      "consumed_view_frames=99 consumed_camera_hash=abcdef1234567890 "
                      "consumed_camera_changes=0") if consumer else ""
    ready_resources = (" texture_revision=0 texture_creates=0 texture_updates=0 "
                       "texture_frees=0 texture_handles=0 texture_peak_active=0 "
                       "texture_peak_bytes=0 world_texture_refs=0 "
                       "world_textures_resolved=0") if resources else ""
    state_resources = (" texture_revision=251 texture_creates=250 texture_updates=1 "
                       "texture_frees=0 texture_handles=250 texture_peak_active=250 "
                       "texture_peak_bytes=3145728 world_texture_refs=121 "
                       "world_textures_resolved=121") if resources else ""
    menu_begin = ([
        f"XASH_PHASE7_MENU_GATE_BEGIN schema=1 menu_seconds=5 map={boot_map} "
        "boot=mainui transition=engine-command-buffer",
    ] if live_menu else [])
    menu_complete = ([
        f"XASH_PHASE7_MENU_GATE_COMPLETE schema=1 map={boot_map} "
        "map_queued=1 host_result=0 ownership=engine-command-buffer pass=1",
    ] if live_menu else [])
    return [
        "LOG_BOOT_MONOTONIC_NS=0x1234",
        f"XASH_BOOT schema=1 slice=engine-boot mode=client ref=agc fw=12.02 engine={ENGINE} "
        f"hlsdk={HLSDK} rodir=/app0/xash3d basedir=/download0/xash3d gamedir=valve "
        f"map={boot_map} gate_seconds=20 pad_gate=0 audio_gate=0 memory_gate=0 "
        "thread_time_gate=0 libc_shim_gate=0 prx_gate=0 filesystem_prx=1 "
        "server_prx=1 menu_prx=1 client_prx=1 ref_agc_prx=1 rodir_present=1",
        "XASH_PRX_LOAD path=/app0/sce_module/ref_agc.prx module=ref_agc.prx handle=0xd3 "
        f"segments=4 exports={exports} init_result=0 result=0",
        "XASH_REF_AGC_PRX_READY module=ref_agc.prx api=18 state=0 runtime_result=0 "
        "teardown_result=0 engine_mask=0 expected_mask=63 frames=0 "
        "frame_hash=0000000000000000 bright_pixels=0 begin_calls=0 scene_calls=0 "
        f"end_calls=0 newmap_calls=0{ready_live}{ready_consumer}{ready_resources} backend={backend} ownership={ownership} pass=1",
        *menu_begin,
        *complete,
        "XASH_REF_AGC_PRX_STATE module=ref_agc.prx api=18 state=5 runtime_result=0 "
        f"teardown_result=0 engine_mask=63 expected_mask=63 frames={100 if consumer else 600} frame_hash={frame_hash} "
        f"bright_pixels=820521 begin_calls=100 scene_calls=99 end_calls=100 newmap_calls=1{state_live} "
        f"{state_consumer}{state_resources} backend={backend} ownership={ownership} pass=1",
        "XASH_PRX_UNLOAD module=ref_agc.prx result=0 stop_result=0 ownership=released",
        "XASH_REF_AGC_PRX_COMPLETE module=ref_agc.prx stop_result=0 active_modules=1 "
        "ownership=exact pass=1",
        "XASH_FS_PRX_COMPLETE module=filesystem_stdio.prx stop_result=0 active_modules=0 "
        "ownership=exact",
        *menu_complete,
        "XASH_EXIT result=0 ref_agc_prx=1",
    ]


def renderer_messages(errors: str = "0") -> list[str]:
    return [
        f"BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-phase4-final target=gfx1013 fw=12.02 "
        f"ownership=fence+videoout bundle_sha256={BUNDLE} bundle_bytes=100 studio_sha256={STUDIO} "
        "studio_bytes=200 soak_frames=600 input_gate=not-required",
        "BSP_LOOP_BEGIN mode=ref-agc-integration frames=600 combined_window=0..599 "
        "geometry=phase4-baked-c1a0e retirement=fence+videoout input_dependency=none",
        "REF_AGC_RUNTIME_READY backend=phase4-native api=18 videoout=owned "
        "direct_memory=owned agc=initialized scene=planned",
        f"BSP_RESOURCE_READBACK buffer0=a9e62c5188ca6bf5 buffer1=0044418de19349d8 "
        f"bright_pixels0=1 bright_pixels1=2 guards=intact frames=600 errors={errors}",
        "GOLDSRC_PHASE4_FINAL_COMPLETE schema=1 frames=600 combined_frames=600 readbacks=2 "
        "both_slots=true tokens=exact guards=intact errors=0 ownership=fence+videoout "
        "sprites=true particles=true cpu_skinning=true brush_entities=true water=true "
        "glass=true pvs=true frustum=true animation_changes=true",
        "RESOURCE_POOL_RETIRED token=1 reclaimed=6 completion=fence+videoout",
        "REF_AGC_TEARDOWN videoout=closed direct_memory=released agc=unloaded "
        "result=0 ownership=exact",
    ]


def phase7_renderer_messages(serial: int = 100, *,
                             resources: bool = False,
                             world: bool = False,
                             special: bool = False,
                             live_2d: bool = False,
                             live_menu: bool = False) -> list[str]:
    if world and not resources:
        raise ValueError("live world evidence requires GPU textures")
    if special and not world:
        raise ValueError("special-surface evidence requires the live world")
    texture = ([
        "REF_AGC_GPU_TEXTURE_COMPLETE revision=251 creates=200 updates=1 "
        "deletes=0 active=200 peak=200 resident_bytes=4194304 "
        "peak_bytes=4194304 source_bytes=3145728 flushes=201 "
        "descriptor_hash=0123456789abcdef arena_bytes=67108864 "
        "descriptors=rgba8+bilinear memory=direct "
        "ownership=fence+videoout-before-reuse errors=0",
    ] if resources else [])
    special_counts = (" sky_draws=158 sky_indices=948 "
                      "turbulent_draws=35 turbulent_indices=312") \
        if special else ""
    gpu_world = ([
        "REF_AGC_GPU_WORLD_COMPLETE revision=1 publishes=1 clears=0 "
        "vertices=17245 indices=29565 draws=3695 texture_tables=3695 "
        f"lightmapped_draws=3695{special_counts} "
        "lightmap=1024x256 row_pitch=4096 "
        "lightmap_bytes=1048576 lightmap_rgb_sum=66594990 "
        "lightmap_nonzero_texels=186051 lightmap_rgb_range=0..255 "
        "resident_bytes=1047584 peak_bytes=1047584 "
        "source_hash=ba427a54bcdc4cb9 upload_hash=934960d09d207e22 "
        "flushes=2 arena_bytes=33554432 geometry=live-refapi "
        "textures=live-refapi memory=direct source_indices=u32 "
        "gpu_indices=per-draw-u16 "
        "ownership=fence+videoout-before-reuse errors=0",
    ] if world else [])
    special_markers = ([
        "REF_AGC_LIVE_SPECIAL_SURFACES schema=1 frame=120 "
        "source_sky_draws=158 source_sky_indices=948 "
        "skybox_draws=6 skybox_indices=36 sky_active=1 sky_revision=7 "
        "sky_geometry_hash=1122334455667788 "
        "sky_texture_hash=8877665544332211 "
        "turbulent_draws=35 turbulent_indices=312 "
        "animation_time_milli=50 paused=0 "
        "sky=engine-six-sided-camera-centred "
        "turbulent=engine-time-classic-warp ownership=transient-slot",
        "REF_AGC_LIVE_SPECIAL_SURFACES schema=1 frame=240 "
        "source_sky_draws=158 source_sky_indices=948 "
        "skybox_draws=6 skybox_indices=36 sky_active=1 sky_revision=7 "
        "sky_geometry_hash=1122334455667788 "
        "sky_texture_hash=8877665544332211 "
        "turbulent_draws=35 turbulent_indices=312 "
        "animation_time_milli=2050 paused=0 "
        "sky=engine-six-sided-camera-centred "
        "turbulent=engine-time-classic-warp ownership=transient-slot",
    ] if special else [])
    studio = "" if world else \
        f" studio_sha256={STUDIO} studio_bytes=200"
    geometry = "live-refapi" if world else "baked-c1a0"
    lists = "world+2d" if world and live_2d else \
        "world" if world else "world+entities+2d"
    textures = " textures=live-refapi" if world else ""
    lightmaps = " lightmaps=live-atlas" if world else ""
    return [
        f"BSP_TEXTURE_PATH_BOOT schema=1 slice=phase7-live-consumer target=gfx1013 "
        f"fw=12.02 ownership=fence+videoout+ack bundle_sha256={BUNDLE} "
        f"bundle_bytes=100{studio} "
        "lifetime=engine-owned input_owner=engine",
        "BSP_LOOP_BEGIN mode=phase7-live-consumer buffers=2 color_dma=false "
        "depth_dma=true indexed=true frames=engine-owned camera=live-refapi "
        f"geometry={geometry}{textures} lists={lists}{lightmaps} "
        "retirement=fence+videoout+ack input_dependency=engine",
        "REF_AGC_RUNTIME_READY backend=phase4-native api=18 videoout=owned "
        "direct_memory=owned agc=initialized scene=planned",
        "REF_AGC_LIVE_FRAME_INPUT serial=1 map_serial=0 view_valid=0 "
        "viewport=0,0,0,0 entities=0 draw2d=1 drops=zero",
        "REF_AGC_LIVE_CONSUMED serial=1 consumed=1 view_frames=0 "
        "camera_hash=0000000000000000 camera_changes=0 map_serial=0 "
        "entities=0 draw2d=1 ack=exact drops=zero",
        *texture,
        *gpu_world,
        *special_markers,
        *([
            "REF_AGC_LIVE_2D_FRAME schema=1 frame=0 serial=1 "
            "input_commands=4 mode_commands=1 stretch_quads=2 fill_quads=1 "
            "batches=3 alpha_batches=1 additive_batches=1 opaque_batches=1 "
            "draws=3 indices=18 texture_binds=3 unresolved=0 "
            "command_hash=1234567890abcdef layout_hash=fedcba0987654321 "
            "transient_bytes=1024 order=source-exact geometry=transient-slot "
            "ownership=fence+videoout",
            "REF_AGC_LIVE_2D_FRAME schema=1 frame=1 serial=2 "
            "input_commands=2 mode_commands=2 stretch_quads=0 fill_quads=0 "
            "batches=0 alpha_batches=0 additive_batches=0 opaque_batches=0 "
            "draws=0 indices=0 texture_binds=0 unresolved=0 "
            "command_hash=2234567890abcdef layout_hash=eedcba0987654321 "
            "transient_bytes=0 order=source-exact geometry=transient-slot "
            "ownership=fence+videoout",
            "REF_AGC_LIVE_2D_COMPLETE schema=1 frames=100 "
            "frames_with_draws=1 input_commands=203 mode_commands=200 "
            "quads=3 draws=3 indices=18 peak_batches=3 unresolved=0 "
            "command_hash=3234567890abcdef order=source-exact "
            "geometry=transient-slot ownership=fence+videoout errors=0",
        ] if live_2d else []),
        *([
            "REF_AGC_LIVE_MENU_FIRST schema=1 serial=1 quads=1 "
            "draws=1 map_serial=0 source=mainui-2d "
            "ownership=fence+videoout",
            "REF_AGC_LIVE_MENU_TRANSITION schema=1 serial=2 map_serial=1 "
            "premap_frames=1 premap_quads=3 premap_draws=3 "
            "order=menu-then-map",
            "REF_AGC_LIVE_MENU_COMPLETE schema=1 frames=1 quads=3 "
            "draws=3 first_serial=1 map_first_serial=2 "
            "order=menu-then-map presentation=native-agc ownership=exact "
            "errors=0 pass=1",
        ] if live_menu else []),
        f"REF_AGC_LIVE_COMPLETE frames=100 serial={serial} view_frames=99 "
        "camera_hash=abcdef1234567890 camera_changes=0 "
        "buffer0=a9e62c5188ca6bf5 buffer1=0044418de19349d8 "
        "frame_hash=d8c9aadab3c82cdb "
        f"bright_pixels=820521 resource_reclaimed={8 if world else 7 if resources else 6} "
        "ownership=fence+videoout+ack guards=intact errors=0",
        "REF_AGC_TEARDOWN videoout=closed direct_memory=released agc=unloaded "
        "result=0 ownership=exact",
    ]


def run(engine: Path, renderer: Path, *,
        require_live_lightmaps: bool = False,
        require_live_special_surfaces: bool = False,
        require_live_2d: bool = False,
        require_live_menu: bool = False,
        boot_map: str = "c1a0") -> subprocess.CompletedProcess[str]:
    command = [
        "python3", "-B", str(VALIDATOR), str(engine), str(renderer),
        "--engine-commit", ENGINE, "--hlsdk-commit", HLSDK,
        "--bundle-sha256", BUNDLE, "--bundle-bytes", "100",
        "--studio-sha256", STUDIO, "--studio-bytes", "200",
        "--map", boot_map,
    ]
    if require_live_lightmaps:
        command.append("--require-live-lightmaps")
    if require_live_special_surfaces:
        command.append("--require-live-special-surfaces")
    if require_live_2d:
        command.append("--require-live-2d")
    if require_live_menu:
        command.append("--require-live-menu")
    return subprocess.run(command, text=True, capture_output=True, check=False)


def main() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        raw = ["filesystem_stdio successfully loaded", "Spawn Server: c1a0",
               'Dll loaded for game "Half-Life"', "Game started",
               "Loading renderer: agc -> ref_agc",
               "PS5_XASH_GATE_TIMEOUT seconds=20 action=quit"]
        engine = write_run(directory, "engine", "xash3d-engine", engine_messages(),
                           raw=raw, started="2026-09-08T19:13:27.933+00:00")
        renderer = write_run(directory, "renderer", "ps5-xash3d", renderer_messages(),
                             started="2026-09-08T19:13:27.984+00:00")
        valid = run(engine, renderer)
        assert valid.returncode == 0, valid.stderr
        summary = json.loads(valid.stdout)
        assert summary["pass"] and summary["frames"] == 600
        assert summary["start_skew_ms"] == 51

        legacy_engine = write_run(
            directory, "legacy-engine", "xash3d-engine",
            engine_messages(live=False), raw=raw,
            started="2026-09-08T19:13:27.933+00:00")
        legacy = run(legacy_engine, renderer)
        assert legacy.returncode == 0, legacy.stderr

        consumer_engine = write_run(
            directory, "consumer-engine", "xash3d-engine",
            engine_messages(consumer=True), raw=raw,
            started="2026-09-08T19:13:27.933+00:00")
        consumer_mismatch = run(consumer_engine, renderer)
        assert consumer_mismatch.returncode != 0 \
            and "phase mismatch" in consumer_mismatch.stderr

        phase7_renderer = write_run(
            directory, "phase7-renderer", "ps5-xash3d",
            phase7_renderer_messages(),
            started="2026-09-08T19:13:27.984+00:00")
        phase7_valid = run(consumer_engine, phase7_renderer)
        assert phase7_valid.returncode == 0, phase7_valid.stderr
        phase7_summary = json.loads(phase7_valid.stdout)
        assert phase7_summary["phase"] == 7 \
            and phase7_summary["frames"] == 100

        resource_engine = write_run(
            directory, "resource-engine", "xash3d-engine",
            engine_messages(consumer=True, resources=True), raw=raw,
            started="2026-09-08T19:13:27.933+00:00")
        resource_renderer = write_run(
            directory, "resource-renderer", "ps5-xash3d",
            phase7_renderer_messages(resources=True),
            started="2026-09-08T19:13:27.984+00:00")
        resource_valid = run(resource_engine, resource_renderer)
        assert resource_valid.returncode == 0, resource_valid.stderr
        resource_summary = json.loads(resource_valid.stdout)
        assert resource_summary["ref_agc_texture_handles"] == 250
        assert resource_summary["ref_agc_world_texture_refs"] == 121
        assert resource_summary["ref_agc_world_textures_resolved"] == 121
        assert resource_summary["gpu_texture"]["revision"] == "251"

        world_renderer = write_run(
            directory, "world-renderer", "ps5-xash3d",
            phase7_renderer_messages(resources=True, world=True),
            started="2026-09-08T19:13:27.984+00:00")
        world_valid = run(
            resource_engine, world_renderer, require_live_lightmaps=True)
        assert world_valid.returncode == 0, world_valid.stderr
        world_summary = json.loads(world_valid.stdout)
        assert world_summary["gpu_world"]["draws"] == "3695"
        assert world_summary["gpu_world"]["vertices"] == "17245"
        studio_marker = (
            "REF_AGC_GPU_STUDIO_CACHE_COMPLETE schema=1 revision=74 creates=74 "
            "updates=0 deletes=0 active=74 peak=74 resident_bytes=3077376 "
            "peak_bytes=3077376 source_bytes=3069050 flushes=74 arena_bytes=33554432 "
            "source=engine-decoded-studio-v10 memory=direct "
            "ownership=fence+videoout-before-reuse errors=0")
        nine_reclaims = [m.replace("resource_reclaimed=8", "resource_reclaimed=9")
                         for m in phase7_renderer_messages(resources=True, world=True)]
        for name, markers, accepted in (
            ("studio-valid", [studio_marker, *nine_reclaims], True),
            ("studio-missing", nine_reclaims, False),
            ("studio-unretired", [studio_marker, *phase7_renderer_messages(
                resources=True, world=True)], False),
            ("studio-overflow", [studio_marker.replace("peak_bytes=3077376",
                "peak_bytes=33554433"), *nine_reclaims], False),
        ):
            candidate = write_run(directory, name, "ps5-xash3d", markers,
                                  started="2026-09-08T19:13:27.984+00:00")
            checked = run(resource_engine, candidate, require_live_lightmaps=True)
            assert (checked.returncode == 0) == accepted, checked.stderr

        special_engine = write_run(
            directory, "special-engine", "xash3d-engine",
            engine_messages(consumer=True, resources=True,
                            boot_map="c1a0e"),
            raw=[line.replace("c1a0", "c1a0e") for line in raw],
            started="2026-09-08T19:13:27.933+00:00")
        special_renderer = write_run(
            directory, "special-renderer", "ps5-xash3d",
            phase7_renderer_messages(resources=True, world=True,
                                     special=True),
            started="2026-09-08T19:13:27.984+00:00")
        special_valid = run(
            special_engine, special_renderer, require_live_lightmaps=True,
            require_live_special_surfaces=True, boot_map="c1a0e")
        assert special_valid.returncode == 0, special_valid.stderr
        special_summary = json.loads(special_valid.stdout)[
            "special_surfaces"]
        assert special_summary["sky_draws"] == 158
        assert special_summary["turbulent_draws"] == 35
        assert special_summary["animation_time_milli"] == [50, 2050]

        live_2d_renderer = write_run(
            directory, "live-2d-renderer", "ps5-xash3d",
            phase7_renderer_messages(
                resources=True, world=True, live_2d=True),
            started="2026-09-08T19:13:27.984+00:00")
        live_2d_valid = run(
            resource_engine, live_2d_renderer,
            require_live_lightmaps=True, require_live_2d=True)
        assert live_2d_valid.returncode == 0, live_2d_valid.stderr
        live_2d_summary = json.loads(live_2d_valid.stdout)["live_2d"]
        assert live_2d_summary["frames"] == 100
        assert live_2d_summary["quads"] == 3
        assert live_2d_summary["draws"] == 3
        assert live_2d_summary["indices"] == 18

        menu_raw = [
            "filesystem_stdio successfully loaded",
            "[00:00:05] XASH_PHASE7_MENU_GATE_TRANSITION "
            "seconds=5 action=map map=c1a0",
            "[00:00:05] Spawn Server: c1a0",
            'Dll loaded for game "Half-Life"', "Game started",
            "Loading renderer: agc -> ref_agc",
            "PS5_XASH_GATE_TIMEOUT seconds=20 action=quit",
        ]
        menu_engine = write_run(
            directory, "menu-engine", "xash3d-engine",
            engine_messages(
                consumer=True, resources=True, live_menu=True),
            raw=menu_raw, started="2026-09-08T19:13:27.933+00:00")
        menu_renderer = write_run(
            directory, "menu-renderer", "ps5-xash3d",
            phase7_renderer_messages(
                resources=True, world=True, live_2d=True, live_menu=True),
            started="2026-09-08T19:13:27.984+00:00")
        menu_valid = run(
            menu_engine, menu_renderer, require_live_lightmaps=True,
            require_live_2d=True, require_live_menu=True)
        assert menu_valid.returncode == 0, menu_valid.stderr
        menu_summary = json.loads(menu_valid.stdout)
        assert menu_summary["engine_menu"]["menu_seconds"] == 5
        assert menu_summary["live_menu"] == {
            "draws": 3, "first_serial": 1, "frames": 1,
            "map_first_serial": 2, "map_serial": 1,
            "presentation": "native-agc", "quads": 3,
        }

        menu_without_2d = run(
            menu_engine, menu_renderer, require_live_menu=True)
        assert menu_without_2d.returncode != 0 \
            and "requires live 2D" in menu_without_2d.stderr

        bad_menu_order_engine = write_run(
            directory, "bad-menu-order-engine", "xash3d-engine",
            engine_messages(
                consumer=True, resources=True, live_menu=True),
            raw=[menu_raw[0], menu_raw[2], menu_raw[1], *menu_raw[3:]],
            started="2026-09-08T19:13:27.933+00:00")
        rejected = run(
            bad_menu_order_engine, menu_renderer,
            require_live_2d=True, require_live_menu=True)
        assert rejected.returncode != 0 \
            and "did not precede map spawn" in rejected.stderr

        bad_menu_totals = write_run(
            directory, "bad-menu-totals-renderer", "ps5-xash3d",
            [message.replace("frames=1 quads=3", "frames=2 quads=3")
             for message in phase7_renderer_messages(
                 resources=True, world=True, live_2d=True, live_menu=True)],
            started="2026-09-08T19:13:27.984+00:00")
        rejected = run(
            menu_engine, bad_menu_totals,
            require_live_2d=True, require_live_menu=True)
        assert rejected.returncode != 0 \
            and "menu completion contract mismatch" in rejected.stderr

        missing_live_2d_messages = [
            message for message in phase7_renderer_messages(
                resources=True, world=True, live_2d=True)
            if not message.startswith("REF_AGC_LIVE_2D_COMPLETE ")
        ]
        missing_live_2d_renderer = write_run(
            directory, "missing-live-2d-renderer", "ps5-xash3d",
            missing_live_2d_messages,
            started="2026-09-08T19:13:27.984+00:00")
        rejected = run(
            resource_engine, missing_live_2d_renderer, require_live_2d=True)
        assert rejected.returncode != 0 \
            and "frame/completion evidence is missing" in rejected.stderr

        bad_live_2d_messages = [message.replace(
            "quads=3 draws=3 indices=18", "quads=3 draws=3 indices=24")
            for message in phase7_renderer_messages(
                resources=True, world=True, live_2d=True)]
        bad_live_2d_renderer = write_run(
            directory, "bad-live-2d-renderer", "ps5-xash3d",
            bad_live_2d_messages,
            started="2026-09-08T19:13:27.984+00:00")
        rejected = run(
            resource_engine, bad_live_2d_renderer, require_live_2d=True)
        assert rejected.returncode != 0 \
            and "completion contract mismatch" in rejected.stderr

        stalled_special_messages = [message.replace(
            "animation_time_milli=2050", "animation_time_milli=50")
            for message in phase7_renderer_messages(
                resources=True, world=True, special=True)]
        stalled_special_renderer = write_run(
            directory, "stalled-special-renderer", "ps5-xash3d",
            stalled_special_messages,
            started="2026-09-08T19:13:27.984+00:00")
        rejected = run(
            special_engine, stalled_special_renderer,
            require_live_special_surfaces=True, boot_map="c1a0e")
        assert rejected.returncode != 0 \
            and "engine time did not advance" in rejected.stderr

        bad_lightmap_messages = [message.replace(
            "lightmap_nonzero_texels=186051", "lightmap_nonzero_texels=0")
            for message in phase7_renderer_messages(
                resources=True, world=True)]
        bad_lightmap_renderer = write_run(
            directory, "bad-lightmap-renderer", "ps5-xash3d",
            bad_lightmap_messages,
            started="2026-09-08T19:13:27.984+00:00")
        rejected = run(resource_engine, bad_lightmap_renderer)
        assert rejected.returncode != 0 \
            and "live lightmap atlas contract" in rejected.stderr

        no_lightmap_marker_messages = [message.replace(
            " lightmaps=live-atlas", "")
            for message in phase7_renderer_messages(
                resources=True, world=True)]
        no_lightmap_marker_renderer = write_run(
            directory, "no-lightmap-marker-renderer", "ps5-xash3d",
            no_lightmap_marker_messages,
            started="2026-09-08T19:13:27.984+00:00")
        rejected = run(
            resource_engine, no_lightmap_marker_renderer,
            require_live_lightmaps=True)
        assert rejected.returncode != 0 \
            and "lightmap atlas marker is missing" in rejected.stderr

        bad_world_messages = [message.replace(
            "draws=3695 texture_tables=3695 lightmapped_draws=3695",
            "draws=3694 texture_tables=3694 lightmapped_draws=3694")
            for message in phase7_renderer_messages(
                resources=True, world=True)]
        bad_world_renderer = write_run(
            directory, "bad-world-renderer", "ps5-xash3d",
            bad_world_messages,
            started="2026-09-08T19:13:27.984+00:00")
        rejected = run(resource_engine, bad_world_renderer)
        assert rejected.returncode != 0 \
            and "engine/GPU world accounting" in rejected.stderr

        missing_world_messages = [message for message in
                                  phase7_renderer_messages(
                                      resources=True, world=True)
                                  if not message.startswith(
                                      "REF_AGC_GPU_WORLD_COMPLETE ")]
        missing_world_renderer = write_run(
            directory, "missing-world-renderer", "ps5-xash3d",
            missing_world_messages,
            started="2026-09-08T19:13:27.984+00:00")
        rejected = run(resource_engine, missing_world_renderer)
        assert rejected.returncode != 0 \
            and "live world loop/evidence" in rejected.stderr

        missing_gpu = run(resource_engine, phase7_renderer)
        assert missing_gpu.returncode != 0 \
            and "engine/GPU texture accounting" in missing_gpu.stderr

        future_gpu_messages = [message.replace(
            "revision=251 creates=200", "revision=252 creates=200")
            for message in phase7_renderer_messages(resources=True)]
        future_gpu_renderer = write_run(
            directory, "future-gpu-renderer", "ps5-xash3d",
            future_gpu_messages,
            started="2026-09-08T19:13:27.984+00:00")
        rejected = run(resource_engine, future_gpu_renderer)
        assert rejected.returncode != 0 \
            and "engine/GPU texture accounting" in rejected.stderr

        unresolved_messages = [message.replace(
            "world_textures_resolved=121", "world_textures_resolved=120")
            for message in engine_messages(consumer=True, resources=True)]
        unresolved_engine = write_run(
            directory, "unresolved-engine", "xash3d-engine",
            unresolved_messages, raw=raw,
            started="2026-09-08T19:13:27.933+00:00")
        rejected = run(unresolved_engine, phase7_renderer)
        assert rejected.returncode != 0 \
            and "world textures unresolved" in rejected.stderr

        capture_phase7_mismatch = run(engine, phase7_renderer)
        assert capture_phase7_mismatch.returncode != 0 \
            and "phase mismatch" in capture_phase7_mismatch.stderr

        short_legacy_messages = [message.replace(
            "frames=600", "frames=599")
            for message in engine_messages()]
        short_legacy_engine = write_run(
            directory, "short-legacy-engine", "xash3d-engine",
            short_legacy_messages, raw=raw,
            started="2026-09-08T19:13:27.933+00:00")
        rejected = run(short_legacy_engine, renderer)
        assert rejected.returncode != 0 and "runtime state" in rejected.stderr

        bad_phase7_renderer = write_run(
            directory, "bad-phase7-renderer", "ps5-xash3d",
            phase7_renderer_messages(serial=99),
            started="2026-09-08T19:13:27.984+00:00")
        rejected = run(consumer_engine, bad_phase7_renderer)
        assert rejected.returncode != 0 \
            and "completion mismatch" in rejected.stderr

        bad_ack_messages = [message.replace(
            "consumed_serial=100", "consumed_serial=99")
            for message in engine_messages(consumer=True)]
        bad_ack_engine = write_run(
            directory, "bad-ack-engine", "xash3d-engine",
            bad_ack_messages, raw=raw,
            started="2026-09-08T19:13:27.933+00:00")
        rejected = run(bad_ack_engine, renderer)
        assert rejected.returncode != 0 and "consumer/ACK" in rejected.stderr

        bad_engine = write_run(directory, "bad-engine", "xash3d-engine",
                               engine_messages("0000000000000000"), raw=raw,
                               started="2026-09-08T19:13:27.933+00:00")
        rejected = run(bad_engine, renderer)
        assert rejected.returncode != 0 and "runtime state" in rejected.stderr

        dropped_messages = [message.replace(
            "dropped_entities=0", "dropped_entities=1")
            for message in engine_messages()]
        dropped_engine = write_run(
            directory, "dropped-engine", "xash3d-engine", dropped_messages,
            raw=raw, started="2026-09-08T19:13:27.933+00:00")
        rejected = run(dropped_engine, renderer)
        assert rejected.returncode != 0 and "live frame capture" in rejected.stderr

        bad_renderer = write_run(directory, "bad-renderer", "ps5-xash3d",
                                 renderer_messages("1"),
                                 started="2026-09-08T19:13:27.984+00:00")
        rejected = run(engine, bad_renderer)
        assert rejected.returncode != 0 and "GPU readback" in rejected.stderr
    print("ref_agc paired evidence validator tests passed")


if __name__ == "__main__":
    main()
