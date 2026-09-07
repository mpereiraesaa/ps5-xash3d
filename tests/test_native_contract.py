#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    source = (ROOT / "native/main.c").read_text(encoding="utf-8")
    builder = (ROOT / "tools/build_native.sh").read_text(encoding="utf-8")
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    assets = (ROOT / "native/shader_assets.S").read_text(encoding="utf-8")
    bsp_metadata = (ROOT / "tools/generate_bsp_build_metadata.py").read_text(
        encoding="utf-8"
    )
    studio_metadata = (
        ROOT / "tools/generate_studio_build_metadata.py"
    ).read_text(encoding="utf-8")
    pad_backend = (
        ROOT / "xash/platform_ps5/in_ps5.c"
    ).read_text(encoding="utf-8")
    engine_builder = (ROOT / "xash/build_engine.sh").read_text(encoding="utf-8")
    required = (
        '"LOG_SCHEMA=3"',
        '"LOG_TRANSPORT=ps5log/1 tcp structured"',
        '"LOG_FS_SINKS=disabled"',
        'ps5log_hex64(PS5LOG_INFO, "LOG_BOOT_MONOTONIC_NS", boot_token)',
        '"GEARS_LOOP_BEGIN mode=continuous buffers=2 "',
        'gears_frame_loop_init(&loop, &input)',
        'gears_frame_loop_step(&loop)',
        '"GEARS_HEARTBEAT frames=%llu max_in_flight=%u "',
        '"retired_fences=zero tokens=exact guards=intact errors=%llu"',
        '"present_interval_avg_ns=%llu present_interval_max_ns=%llu "',
        'input.present_interval_budget_ns = UINT64_C(17000000)',
    )
    for item in required:
        if item not in source:
            raise SystemExit(f"native telemetry/teardown contract missing: {item}")
    if 'LOG_BOOT_MONOTONIC_NS=' in source:
        raise SystemExit("ps5log_hex64 label must not contain its own equals sign")
    for item in (
        "sceUserServiceGetForegroundUser(",
        "scePadRead( pad.stats.pad_handle, samples, PS5_PAD_MAX_SAMPLES )",
        "PS5_PAD_MAX_SAMPLES 64",
        "process_neutral( sample->timestamp )",
        '"XASH_PAD_ACTION schema=1 name=%s state=%s timestamp_us=%llu"',
        '"XASH_PAD_SUMMARY schema=1 polls=%llu samples=%llu empty_reads=%llu "',
        '"XASH_PAD_TEARDOWN schema=1 handle=%d close_rc=%d owned_user_service=%d "',
        '"XASH_PAD_COMPLETE schema=1 movement=%d look=%d jump=%d crouch=%d "',
        "scePadClose( pad.stats.pad_handle )",
        "sceUserServiceTerminate( )",
        "Joy_AxisMotionEvent((engineAxis_t)axis, value )",
        "Key_Event( xash_button_map[button], down )",
    ):
        if item not in pad_backend:
            raise SystemExit(f"ScePad backend contract missing: {item}")
    for item in (
        "XASH_PAD_GATE",
        "#define PS5_XASH_PAD_GATE $pad_gate",
        "xash/platform_ps5/in_ps5.c",
    ):
        if item not in engine_builder:
            raise SystemExit(f"ScePad engine build contract missing: {item}")
    if "engine-pad-native-release" not in makefile or \
            "XASH_PAD_GATE=1" not in makefile:
        raise SystemExit("ScePad release target missing")
    if 'make -C "$foundation" app' in builder:
        raise SystemExit("standalone builder must not build the foundation sample title")
    for unit in ("native_app_builder.cpp", "self_container.cpp",
                 "elf_object.cpp", "sce_module_writer.cpp"):
        if unit not in builder:
            raise SystemExit(f"foundation host tool source missing: {unit}")
    for obsolete in ("PS5_GEARS_FRAME_COUNT", "PS5_GEARS_RELEASE_CHUNK_FRAMES",
                     "PS5_GEARS_VISIBLE_HOLD_SECONDS", "gears_run_frames(",
                     "deadline_misses"):
        if obsolete in source or obsolete in builder:
            raise SystemExit(f"production runtime still contains test policy: {obsolete}")
    for item in ('bsp_flat_shader_metadata.h',
                 'bsp_textured_shader_metadata.h'):
        if item not in makefile:
            raise SystemExit(f"BSP shader metadata build product missing: {item}")
    for item in ('bsp_flat.gs.bin', 'bsp_flat.ps.bin',
                 'bsp_textured.gs.bin', 'bsp_textured.ps.bin'):
        if item not in assets:
            raise SystemExit(f"BSP shader native asset missing: {item}")
    for item in ('bsp-inspect', 'generate_bsp_build_metadata.py',
                 '-DPS5_BSP_VIEWER=1', 'map.ps5bsp'):
        if item not in builder:
            raise SystemExit(f"BSP private release contract missing: {item}")
    if 'PS5_BSP_BUNDLE_SHA256' not in bsp_metadata:
        raise SystemExit("BSP bundle SHA-256 metadata contract missing")
    for item in (
        '"/app0/map.ps5bsp"', 'BSP_BUNDLE_READY vertices=%u',
        'bsp_flat_compose(', 'BSP_VIDEOOUT_TOKEN frame=%llu buffer=%u',
        'BSP_READBACK_FNV64 buffer0=%016llx',
        'BSP_GATE1_COMPLETE fixed_camera=true',
        'BSP_GATE_FRAME_COUNT = 600u', 'bright_pixel_count(',
        'geometry_visible=true tokens=exact guards=intact',
    ):
        if item not in source:
            raise SystemExit(f"BSP native gate contract missing: {item}")
    if "BSP viewer requires PS5LOG_DEV_CONF" not in builder:
        raise SystemExit("BSP release must fail closed without TCP config")
    for item in (
        "BSP_NOCLIP requires BSP_BUNDLE",
        "-DPS5_BSP_NOCLIP=1",
        "src/bsp_noclip.c",
        "src/bsp_texture_descriptor.c",
    ):
        if item not in builder:
            raise SystemExit(f"BSP noclip native build contract missing: {item}")
    for item in (
        "BSP_GATE_FRAME_COUNT = 10000u",
        "scePadReadState(",
        "bsp_noclip_step(",
        "bsp_flat_update_camera(",
        "BSP_NOCLIP_PAD_READY sticks=dual triggers=vertical",
        "BSP_LOOP_BEGIN mode=noclip-soak",
        "BSP_NOCLIP_SOAK_COMPLETE frames=%llu",
        "BSP_NOCLIP_MIN_MOVING_FRAMES = 600u",
        "BSP_NOCLIP_MIN_LOOKING_FRAMES = 120u",
        'ps5log_close("bsp-noclip-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"BSP noclip runtime contract missing: {item}")
    if "bsp-noclip-native-release" not in makefile or "BSP_NOCLIP=1" not in makefile:
        raise SystemExit("BSP noclip release target missing")
    for item in (
        "BSP_TEXTURED requires BSP_NOCLIP=1",
        "-DPS5_BSP_TEXTURED=1",
        "src/bsp_textured_draw.c",
    ):
        if item not in builder:
            raise SystemExit(f"BSP textured native build contract missing: {item}")
    for item in (
        "BSP_GATE_FRAME_COUNT = 60000u",
        "bsp_runtime_plan_textured(",
        "bsp_texture_build_tables(",
        "bsp_textured_compose(",
        "BSP_TEXTURED_BOOT schema=1 target=gfx1013",
        "composition=base_x_lightmap",
        "BSP_TEXTURE_TABLES_READY textures=%u descriptor_dwords=%u",
        "BSP_LOOP_BEGIN mode=textured-noclip-soak",
        "BSP_TEXTURED_READBACK buffer0=%016llx buffer1=%016llx",
        "BSP_TEXTURED_SOAK_COMPLETE frames=%llu connected_frames=%llu",
        "textured-render-or-input-continuity-gate-failure",
        'ps5log_close("bsp-textured-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"BSP textured runtime contract missing: {item}")
    if ("bsp-textured-native-release" not in makefile or
            "BSP_TEXTURED=1" not in makefile):
        raise SystemExit("BSP textured release target missing")
    for item in (
        "BSP_RESOURCE_FOUNDATION requires BSP_TEXTURED=1",
        "-DPS5_RESOURCE_FOUNDATION=1",
        "src/bsp_resource_frame.c", "src/bsp_resource_draw.c",
        "src/ps5_resource_pool.c", "src/ps5_transient_ring.c",
        "src/ps5_cache_contract.c",
    ):
        if item not in builder:
            raise SystemExit(f"resource-foundation native build contract missing: {item}")
    for item in (
        "BSP_TEXTURE_PATH requires BSP_RESOURCE_FOUNDATION=1",
        "-DPS5_TEXTURE_PATH=1", "src/bsp_dynamic_lightmap.c",
    ):
        if item not in builder:
            raise SystemExit(f"texture-path native build contract missing: {item}")
    for item in (
        "BSP_RESOURCE_BOOT schema=1 target=gfx1013",
        "RESOURCE_HEAP_READY bytes=%llu allocations=4",
        "overlay_depth=disabled",
        "RESOURCE_FRAME_READY frame=%llu slot=%u",
        "RESOURCE_FRAME_SEALED frame=%llu slot=%u token=%llu",
        "RESOURCE_FRAME_SUBMITTED frame=%llu slot=%u token=%llu",
        "RESOURCE_FRAME_RETIRED frame=%llu slot=%u token=%llu",
        "RESOURCE_RING_RETIRED slots=2 reusable=%s tokens=exact",
        "BSP_RESOURCE_SOAK_COMPLETE frames=%llu connected_frames=%llu",
        "RESOURCE_POOL_RETIRED token=%llu reclaimed=%u",
        'ps5log_close("bsp-resource-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"resource-foundation runtime contract missing: {item}")
    for item in (
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=dynamic-lightmap",
        "BSP_GATE_FRAME_COUNT = 10000u",
        "DYNAMIC_LIGHTMAP_READY image=%ux%u image_bytes=%llu",
        "DYNAMIC_LIGHTMAP_FRAME frame=%llu slot=%u pattern=%u",
        "BSP_LOOP_BEGIN mode=texture-path-lightmap-soak",
        "DYNAMIC_LIGHTMAP_READBACK pattern0=%016llx pattern1=%016llx",
        "BSP_TEXTURE_PATH_LIGHTMAP_COMPLETE frames=%llu",
        'ps5log_close("bsp-texture-path-lightmap-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"texture-path runtime contract missing: {item}")
    if "bsp-texture-path-native-release" not in makefile or \
            "BSP_TEXTURE_PATH=1" not in makefile:
        raise SystemExit("texture-path release target missing")
    for item in (
        "BSP_TEXTURE_MIP_GATE requires BSP_TEXTURE_PATH=1",
        "-DPS5_TEXTURE_MIP_GATE=1",
    ):
        if item not in builder:
            raise SystemExit(f"mip gate native build contract missing: {item}")
    for item in (
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=mip-sampler",
        "MIP_CHAINS_READY textures=%u layout=addr-sw-linear",
        "MIP_SAMPLER_FRAME frame=%llu slot=%u filter=%s",
        "BSP_LOOP_BEGIN mode=texture-path-mip-soak",
        "MIP_SAMPLER_READBACK trilinear_buffer=%016llx",
        "BSP_TEXTURE_PATH_MIP_COMPLETE frames=%llu textures=%u",
        'ps5log_close("bsp-texture-path-mip-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"mip gate runtime contract missing: {item}")
    if "bsp-texture-mip-native-release" not in makefile or \
            "BSP_TEXTURE_MIP_GATE=1" not in makefile:
        raise SystemExit("mip gate release target missing")
    for item in (
        "BSP_TEXTURE_ALPHA_GATE requires BSP_TEXTURE_PATH=1",
        "-DPS5_TEXTURE_ALPHA_GATE=1",
        "src/bsp_alpha_test.c",
    ):
        if item not in builder:
            raise SystemExit(f"alpha-test gate native build contract missing: {item}")
    for item in (
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=alpha-test",
        "ALPHA_TEST_READY textures=%u draws=%u opaque_draws=%u",
        "ALPHA_TEST_FRAME frame=%llu slot=%u mode=%s",
        "BSP_LOOP_BEGIN mode=texture-path-alpha-soak",
        "ALPHA_TEST_READBACK opaque_control_buffer=%016llx",
        "BSP_TEXTURE_PATH_ALPHA_COMPLETE frames=%llu textures=%u draws=%u",
        'ps5log_close("bsp-texture-path-alpha-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"alpha-test gate runtime contract missing: {item}")
    if "bsp-texture-alpha-native-release" not in makefile or \
            "BSP_TEXTURE_ALPHA_GATE=1" not in makefile:
        raise SystemExit("alpha-test gate release target missing")
    for item in (
        "BSP_TEXTURE_SKY_GATE requires BSP_TEXTURE_PATH=1",
        "-DPS5_TEXTURE_SKY_GATE=1",
        "src/bsp_sky.c",
    ):
        if item not in builder:
            raise SystemExit(f"sky gate native build contract missing: {item}")
    for item in (
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=sky",
        "SKY_PASS_READY textures=%u draws=%u opaque_draws=%u",
        "SKY_PASS_FRAME frame=%llu slot=%u mode=%s",
        "const uint32_t expected_sky_draws =",
        "const uint32_t expected_map_draws =",
        "resource_composed.sky_draws !=",
        "BSP_LOOP_BEGIN mode=texture-path-sky-soak",
        "SKY_PASS_READBACK skip_control_buffer=%016llx",
        "BSP_TEXTURE_PATH_SKY_COMPLETE frames=%llu textures=%u draws=%u",
        'ps5log_close("bsp-texture-path-sky-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"sky gate runtime contract missing: {item}")
    if "bsp-texture-sky-native-release" not in makefile or \
            "BSP_TEXTURE_SKY_GATE=1" not in makefile:
        raise SystemExit("sky gate release target missing")
    for item in (
        "BSP_TEXTURE_ACCOUNTING_GATE requires BSP_TEXTURE_PATH=1",
        "-DPS5_TEXTURE_ACCOUNTING_GATE=1",
        "src/bsp_texture_accounting.c",
    ):
        if item not in builder:
            raise SystemExit(f"accounting gate native build contract missing: {item}")
    for item in (
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=accounting",
        "TEXTURE_RESIDENCY_READY schema=1 pool_capacity_bytes=%llu",
        "TEXTURE_UPLOAD_FRAME schema=1 frame=%llu slot=%u",
        "TEXTURE_UPLOAD_SUMMARY schema=1 frames=%llu",
        "TEXTURE_FEATURES_READY mip_chains=%u opaque_draws=%u",
        "BSP_LOOP_BEGIN mode=texture-path-accounting-soak",
        "input_gate=not-repeated",
        "input_dependency=none",
        "BSP_TEXTURE_PATH_ACCOUNTING_COMPLETE frames=%llu",
        'ps5log_close("bsp-texture-path-accounting-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"accounting gate runtime contract missing: {item}")
    if "bsp-texture-accounting-native-release" not in makefile or \
            "BSP_TEXTURE_ACCOUNTING_GATE=1" not in makefile:
        raise SystemExit("accounting gate release target missing")
    for item in (
        "BSP_TEXTURE_FINAL_GATE requires BSP_TEXTURE_PATH=1",
        "-DPS5_TEXTURE_FINAL_GATE=1",
    ):
        if item not in builder:
            raise SystemExit(f"final texture gate build contract missing: {item}")
    for item in (
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=final",
        "BSP_LOOP_BEGIN mode=texture-path-final-soak",
        "BSP_TEXTURE_PATH_FINAL_COMPLETE schema=1 frames=%llu",
        'ps5log_close("bsp-texture-path-final-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"final texture gate runtime contract missing: {item}")
    if "bsp-texture-final-native-release" not in makefile or \
            "BSP_TEXTURE_FINAL_GATE=1" not in makefile:
        raise SystemExit("final texture gate release target missing")
    for item in (
        "GOLDSRC_LIGHTING_GATE requires GOLDSRC_PHASE4=1",
        "-DPS5_GOLDSRC_LIGHTING_GATE=1",
        "src/goldsrc_lightmap_lighting.c",
    ):
        if item not in builder:
            raise SystemExit(f"Phase 4 lighting build contract missing: {item}")
    for item in (
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-lighting",
        "GOLDSRC_LIGHTING_READY schema=1 face=%u draw=%u",
        "GOLDSRC_LIGHTING_FRAME schema=1 frame=%llu slot=%u",
        "GOLDSRC_LIGHTING_READBACK schema=1 frame=%llu",
        "BSP_LOOP_BEGIN mode=goldsrc-lighting-soak buffers=2",
        "GOLDSRC_LIGHTING_COMPLETE schema=1 frames=%llu modes=%u",
        'ps5log_close("goldsrc-phase4-lighting-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"Phase 4 lighting runtime contract missing: {item}")
    if "bsp-phase4-lighting-native-release" not in makefile or \
            "GOLDSRC_LIGHTING_GATE=1" not in makefile:
        raise SystemExit("Phase 4 lighting release target missing")
    for item in (
        "GOLDSRC_SPRITE_PARTICLE_GATE requires GOLDSRC_PHASE4=1",
        "-DPS5_GOLDSRC_SPRITE_PARTICLE_GATE=1",
        "src/goldsrc_sprite_particles.c",
    ):
        if item not in builder:
            raise SystemExit(
                f"Phase 4 sprite/particle build contract missing: {item}")
    for item in (
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-sprite-particles",
        "GOLDSRC_SPRITE_PARTICLE_READY schema=1 atlas=%ux%u",
        "GOLDSRC_SPRITE_PARTICLE_FRAME schema=1 frame=%llu",
        "GOLDSRC_SPRITE_PARTICLE_DRAW schema=1 frame=%llu",
        "GOLDSRC_SPRITE_PARTICLE_READBACK schema=1",
        "BSP_LOOP_BEGIN mode=goldsrc-sprite-particle-soak buffers=2",
        "GOLDSRC_SPRITE_PARTICLE_COMPLETE schema=1 frames=%llu",
        'ps5log_close("goldsrc-phase4-sprite-particle-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(
                f"Phase 4 sprite/particle runtime contract missing: {item}")
    if "bsp-phase4-sprite-particles-native-release" not in makefile or \
            "GOLDSRC_SPRITE_PARTICLE_GATE=1" not in makefile:
        raise SystemExit("Phase 4 sprite/particle release target missing")
    for item in (
        "GOLDSRC_STUDIO_GATE requires GOLDSRC_PHASE4=1",
        "GOLDSRC_STUDIO_GATE requires STUDIO_BUNDLE",
        "-DPS5_GOLDSRC_STUDIO_GATE=1",
        "src/goldsrc_studio_bundle.c",
        "src/goldsrc_studio_model.c",
        'cp "$studio_bundle" "$dist/model.ps5mdl"',
    ):
        if item not in builder:
            raise SystemExit(f"Phase 4 studio build contract missing: {item}")
    for item in (
        '"/app0/model.ps5mdl"',
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-studio",
        "GOLDSRC_STUDIO_READY schema=1 source_fnv64=%016llx",
        "GOLDSRC_STUDIO_FRAME schema=1 frame=%llu slot=%u",
        "GOLDSRC_STUDIO_DRAW schema=1 frame=%llu slot=%u",
        "GOLDSRC_STUDIO_READBACK schema=1 frame=%llu",
        "BSP_LOOP_BEGIN mode=goldsrc-studio-soak buffers=2",
        "GOLDSRC_STUDIO_COMPLETE schema=1 frames=%llu modes=%u",
        'ps5log_close("goldsrc-phase4-studio-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"Phase 4 studio runtime contract missing: {item}")
    if "bsp-phase4-studio-native-release" not in makefile or \
            "GOLDSRC_STUDIO_GATE=1" not in makefile:
        raise SystemExit("Phase 4 studio release target missing")
    for item in (
        "GOLDSRC_BRUSH_GATE requires GOLDSRC_PHASE4=1",
        "-DPS5_GOLDSRC_BRUSH_GATE=1",
        "src/goldsrc_brush_entities.c",
    ):
        if item not in builder:
            raise SystemExit(f"Phase 4 brush build contract missing: {item}")
    for item in (
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-brush",
        "GOLDSRC_BRUSH_READY schema=1 models=%u entities=%u",
        "GOLDSRC_BRUSH_FRAME schema=1 frame=%llu slot=%u",
        "GOLDSRC_BRUSH_DRAW schema=1 frame=%llu slot=%u",
        "GOLDSRC_BRUSH_READBACK schema=1 frame=%llu",
        "BSP_LOOP_BEGIN mode=goldsrc-brush-soak buffers=2",
        "GOLDSRC_BRUSH_COMPLETE schema=1 frames=%llu modes=%u",
        'ps5log_close("goldsrc-phase4-brush-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"Phase 4 brush runtime contract missing: {item}")
    if "bsp-phase4-brush-native-release" not in makefile or \
            "GOLDSRC_BRUSH_GATE=1" not in makefile:
        raise SystemExit("Phase 4 brush release target missing")
    for item in (
        "GOLDSRC_VISIBILITY_GATE requires GOLDSRC_PHASE4=1",
        "-DPS5_GOLDSRC_VISIBILITY_GATE=1",
        "src/goldsrc_visibility.c",
    ):
        if item not in builder:
            raise SystemExit(
                f"Phase 4 visibility build contract missing: {item}")
    for item in (
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-visibility",
        "GOLDSRC_VISIBILITY_READY schema=1 planes=%u nodes=%u leaves=%u",
        "GOLDSRC_VISIBILITY_FRAME schema=1 frame=%llu slot=%u",
        "GOLDSRC_VISIBILITY_DRAW schema=1 frame=%llu slot=%u",
        "GOLDSRC_VISIBILITY_READBACK schema=1 frame=%llu",
        "BSP_LOOP_BEGIN mode=goldsrc-visibility-soak buffers=2",
        "GOLDSRC_VISIBILITY_COMPLETE schema=1 frames=%llu modes=%u",
        'ps5log_close("goldsrc-phase4-visibility-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(
                f"Phase 4 visibility runtime contract missing: {item}")
    if "bsp-phase4-visibility-native-release" not in makefile or \
            "GOLDSRC_VISIBILITY_GATE=1" not in makefile:
        raise SystemExit("Phase 4 visibility release target missing")
    for item in (
        "GOLDSRC_PHASE4_FINAL_GATE requires GOLDSRC_PHASE4=1",
        "GOLDSRC_PHASE4_FINAL_GATE requires STUDIO_BUNDLE",
        "-DPS5_GOLDSRC_PHASE4_FINAL_GATE=1",
    ):
        if item not in builder:
            raise SystemExit(f"Phase 4 final build contract missing: {item}")
    for item in (
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-phase4-final",
        "BSP_LOOP_BEGIN mode=goldsrc-phase4-final-soak buffers=2",
        "GOLDSRC_PHASE4_FINAL_READY schema=1 frames=%u",
        "GOLDSRC_PHASE4_SCENE_READY schema=1 water_entity=%u",
        "GOLDSRC_PHASE4_FINAL_FRAME schema=1 frame=%llu slot=%u",
        "GOLDSRC_PHASE4_FINAL_READBACK schema=1 frame=%llu",
        "GOLDSRC_PHASE4_FINAL_COMPLETE schema=1 frames=%llu",
        "brush_entities=true water=true ",
        'ps5log_close("goldsrc-phase4-final-soak-complete")',
    ):
        if item not in source:
            raise SystemExit(f"Phase 4 final runtime contract missing: {item}")
    if "bsp-phase4-final-native-release" not in makefile or \
            "GOLDSRC_PHASE4_FINAL_GATE=1" not in makefile:
        raise SystemExit("Phase 4 final release target missing")
    for item in ("PS5_STUDIO_BUNDLE_SHA256", "PS5_STUDIO_BUNDLE_BYTES"):
        if item not in studio_metadata:
            raise SystemExit(f"studio metadata contract missing: {item}")
    for item in ('bsp_resource.gs.bin', 'bsp_resource.ps.bin',
                 'bsp_alpha_test.gs.bin', 'bsp_alpha_test.ps.bin',
                 'bsp_sky.gs.bin', 'bsp_sky.ps.bin',
                 'bsp_overlay.gs.bin', 'bsp_overlay.ps.bin'):
        if item not in assets:
            raise SystemExit(f"resource-foundation shader asset missing: {item}")
    if ("bsp-resource-native-release" not in makefile or
            "BSP_RESOURCE_FOUNDATION=1" not in makefile):
        raise SystemExit("resource-foundation release target missing")
    print("native telemetry and teardown source contract passed")


if __name__ == "__main__":
    main()
