CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
BUILD := build/host
STUDIO_SEQUENCE ?= fire

.PHONY: all test shaders bsp-bundle bsp-inspect studio-bundle studio-inspect \
	engine-boot-native-release engine-pad-native-release \
	engine-audio-native-release engine-audio-client-link \
	engine-memory-native-release engine-thread-time-native-release \
	engine-prx-loader-native-release \
	engine-filesystem-prx-native-release \
	engine-server-prx-native-release \
	engine-ref-agc-prx-native-release \
	native native-release \
	bsp-native-release bsp-noclip-native-release \
	bsp-textured-native-release bsp-resource-native-release \
	bsp-texture-path-native-release bsp-texture-mip-native-release \
	bsp-texture-alpha-native-release bsp-texture-sky-native-release \
	bsp-texture-accounting-native-release bsp-texture-final-native-release \
	bsp-phase4-pipeline-native-release bsp-phase4-viewport-native-release \
	bsp-phase4-state-matrix-native-release bsp-phase4-2d-native-release \
	bsp-phase4-lighting-native-release \
	bsp-phase4-sprite-particles-native-release \
	bsp-phase4-studio-native-release bsp-phase4-brush-native-release \
	bsp-phase4-visibility-native-release bsp-phase4-final-native-release \
	bsp-phase5-gpu-flip-timing-native-release \
	audit clean
all: test audit

$(BUILD):
	mkdir -p $@

define test_rule
$(BUILD)/$(1): $(2) | $(BUILD)
	$(CC) $(CFLAGS) $$^ $(3) -o $$@
endef

$(eval $(call test_rule,test_gears_mesh,tests/test_gears_mesh.c src/gears_mesh.c,-lm))
$(eval $(call test_rule,test_gears_scene,tests/test_gears_scene.c src/gears_scene.c,-lm))
$(eval $(call test_rule,test_gears_frame_tracker,tests/test_gears_frame_tracker.c src/gears_frame_tracker.c,))
$(eval $(call test_rule,test_gears_draw_compose,tests/test_gears_draw_compose.c src/gears_draw_compose.c,))
$(eval $(call test_rule,test_gears_animation,tests/test_gears_animation.c src/gears_animation.c src/gears_frame_tracker.c src/gears_scene.c,-lm))
$(eval $(call test_rule,test_gears_telemetry,tests/test_gears_telemetry.c src/gears_telemetry.c,))
$(eval $(call test_rule,test_gears_frame_runner,tests/test_gears_frame_runner.c src/gears_frame_runner.c src/gears_animation.c src/gears_frame_tracker.c src/gears_scene.c src/gears_telemetry.c,-lm))
$(eval $(call test_rule,test_gears_rt_clear,tests/test_gears_rt_clear.c src/gears_rt_clear.c,))
$(eval $(call test_rule,test_gears_renderer,tests/test_gears_renderer.c src/gears_renderer.c src/gears_rt_clear.c src/gears_draw_compose.c,))
$(eval $(call test_rule,test_ps5_surface,tests/test_ps5_surface.c src/ps5_surface.c,))
$(eval $(call test_rule,test_ps5_present,tests/test_ps5_present.c src/ps5_present.c,))
$(eval $(call test_rule,test_ps5_frame_completion,tests/test_ps5_frame_completion.c src/ps5_frame_completion.c,))
$(eval $(call test_rule,test_ps5_agc_abi,tests/test_ps5_agc_abi.c native/stubs/libSceAgc.c native/stubs/libSceAgcDriver.c,))
$(eval $(call test_rule,test_ps5_color_target,tests/test_ps5_color_target.c src/ps5_color_target.c,))
$(eval $(call test_rule,test_ps5_depth_target,tests/test_ps5_depth_target.c src/ps5_depth_target.c,))
$(eval $(call test_rule,test_ps5_pipeline,tests/test_ps5_pipeline.c src/ps5_pipeline.c,))
$(eval $(call test_rule,test_ps5_event_adapter,tests/test_ps5_event_adapter.c src/ps5_event_adapter.c src/ps5_frame_completion.c,))
$(eval $(call test_rule,test_ps5_gpu_span,tests/test_ps5_gpu_span.c src/ps5_gpu_span.c,))
$(eval $(call test_rule,test_ps5_gpu_flip_timing,tests/test_ps5_gpu_flip_timing.c src/ps5_gpu_flip_timing.c,))
$(eval $(call test_rule,test_ps5_submission,tests/test_ps5_submission.c src/ps5_submission.c src/ps5_present.c,))
$(eval $(call test_rule,test_ps5_direct_memory,tests/test_ps5_direct_memory.c src/ps5_direct_memory.c,))
$(eval $(call test_rule,test_ps5_platform_abi,tests/test_ps5_platform_abi.c,))
$(eval $(call test_rule,test_in_ps5,tests/test_in_ps5.c xash/platform_ps5/in_ps5.c,-Iinclude -Ixash/platform_ps5 -Inative/ps5log))
$(eval $(call test_rule,test_ps5_audio,tests/test_ps5_audio.c xash/platform_ps5/audio_ps5.c,-Iinclude -Ixash/platform_ps5 -Inative/ps5log -lpthread))
$(eval $(call test_rule,test_ps5_audio_pattern,tests/test_ps5_audio_pattern.c xash/platform_ps5/audio_pattern_ps5.c,-Iinclude -Ixash/platform_ps5))
$(eval $(call test_rule,test_ps5log_host,tests/test_ps5log_host.c native/ps5log/ps5log.c,-Inative/ps5log))
$(eval $(call test_rule,test_ps5_shader_header,tests/test_ps5_shader_header.c src/ps5_shader_header.c,))
$(eval $(call test_rule,test_ps5_agc_writer,tests/test_ps5_agc_writer.c src/ps5_agc_writer.c src/ps5_gpu_span.c,))
$(eval $(call test_rule,test_ps5_agc_submit,tests/test_ps5_agc_submit.c src/ps5_agc_submit.c src/ps5_gpu_span.c,))
$(eval $(call test_rule,test_ps5_videoout,tests/test_ps5_videoout.c src/ps5_videoout.c src/ps5_surface.c,))
$(eval $(call test_rule,test_bsp_bundle,tests/test_bsp_bundle.c src/bsp_bundle.c,))
$(eval $(call test_rule,test_bsp_command_plan,tests/test_bsp_command_plan.c src/bsp_command_plan.c src/bsp_flat_draw.c,))
$(eval $(call test_rule,test_bsp_flat_draw,tests/test_bsp_flat_draw.c src/bsp_flat_draw.c,))
$(eval $(call test_rule,test_bsp_flat_scene,tests/test_bsp_flat_scene.c src/bsp_flat_scene.c,-lm))
$(eval $(call test_rule,test_bsp_noclip,tests/test_bsp_noclip.c src/bsp_noclip.c,-lm))
$(eval $(call test_rule,test_bsp_runtime_plan,tests/test_bsp_runtime_plan.c src/bsp_runtime_plan.c,))
$(eval $(call test_rule,test_bsp_texture_descriptor,tests/test_bsp_texture_descriptor.c src/bsp_texture_descriptor.c src/bsp_bundle.c src/ps5_gfx1013_descriptor.c,))
$(eval $(call test_rule,test_bsp_textured_draw,tests/test_bsp_textured_draw.c src/bsp_textured_draw.c src/ps5_gpu_span.c,))
$(eval $(call test_rule,test_ps5_bump_allocator,tests/test_ps5_bump_allocator.c src/ps5_bump_allocator.c,))
$(eval $(call test_rule,test_ps5_resource_pool,tests/test_ps5_resource_pool.c src/ps5_resource_pool.c,))
$(eval $(call test_rule,test_ps5_memory_arena,tests/test_ps5_memory_arena.c xash/platform_ps5/memory_arena_ps5.c,-Ixash/platform_ps5 -lpthread))
$(eval $(call test_rule,test_mem_ps5,tests/test_mem_ps5.c xash/platform_ps5/mem_ps5.c xash/platform_ps5/memory_arena_ps5.c,-Iinclude -Ixash/platform_ps5 -DPS5_ENGINE_HEAP_BYTES=1048576))
$(eval $(call test_rule,test_thread_time_ps5,tests/test_thread_time_ps5.c xash/platform_ps5/thread_time_ps5.c,-Ixash/platform_ps5 -lpthread))
$(eval $(call test_rule,test_libc_shims_ps5,tests/test_libc_shims_ps5.c xash/platform_ps5/libc_shims_ps5.c,-D_GNU_SOURCE -Ixash/platform_ps5 -Inative/ps5log))
$(eval $(call test_rule,test_prx_loader_ps5,tests/test_prx_loader_ps5.c xash/platform_ps5/prx_loader_ps5.c,-Ixash/platform_ps5))
$(eval $(call test_rule,test_ps5_transient_ring,tests/test_ps5_transient_ring.c src/ps5_transient_ring.c,))
$(eval $(call test_rule,test_ps5_gfx1013_descriptor,tests/test_ps5_gfx1013_descriptor.c src/ps5_gfx1013_descriptor.c,))
$(eval $(call test_rule,test_ps5_cache_contract,tests/test_ps5_cache_contract.c src/ps5_cache_contract.c src/ps5_gpu_span.c,))
$(eval $(call test_rule,test_ps5_transient_table,tests/test_ps5_transient_table.c src/ps5_transient_table.c src/ps5_transient_ring.c src/ps5_gpu_span.c,))
$(eval $(call test_rule,test_bsp_resource_frame,tests/test_bsp_resource_frame.c src/bsp_resource_frame.c src/bsp_flat_scene.c src/bsp_texture_descriptor.c src/bsp_bundle.c src/ps5_gfx1013_descriptor.c src/ps5_transient_table.c src/ps5_transient_ring.c src/ps5_gpu_span.c,-lm))
$(eval $(call test_rule,test_bsp_dynamic_lightmap,tests/test_bsp_dynamic_lightmap.c src/bsp_dynamic_lightmap.c src/ps5_transient_ring.c,-lm))
$(eval $(call test_rule,test_bsp_alpha_test,tests/test_bsp_alpha_test.c src/bsp_alpha_test.c,-lm))
$(eval $(call test_rule,test_bsp_sky,tests/test_bsp_sky.c src/bsp_sky.c,-lm))
$(eval $(call test_rule,test_bsp_texture_accounting,tests/test_bsp_texture_accounting.c src/bsp_texture_accounting.c,))
$(eval $(call test_rule,test_goldsrc_render_state,tests/test_goldsrc_render_state.c src/goldsrc_render_state.c,))
$(eval $(call test_rule,test_goldsrc_state_matrix,tests/test_goldsrc_state_matrix.c src/goldsrc_state_matrix.c src/goldsrc_render_state.c,))
$(eval $(call test_rule,test_goldsrc_2d,tests/test_goldsrc_2d.c src/goldsrc_2d.c src/bsp_texture_descriptor.c src/bsp_bundle.c src/ps5_gfx1013_descriptor.c src/ps5_transient_table.c src/ps5_transient_ring.c src/ps5_gpu_span.c,))
$(eval $(call test_rule,test_goldsrc_lightmap_lighting,tests/test_goldsrc_lightmap_lighting.c src/goldsrc_lightmap_lighting.c src/bsp_dynamic_lightmap.c src/ps5_transient_ring.c,-lm))
$(eval $(call test_rule,test_goldsrc_sprite_particles,tests/test_goldsrc_sprite_particles.c src/goldsrc_sprite_particles.c src/bsp_flat_scene.c src/bsp_texture_descriptor.c src/bsp_bundle.c src/ps5_gfx1013_descriptor.c src/ps5_transient_table.c src/ps5_transient_ring.c src/ps5_gpu_span.c,-lm))
$(eval $(call test_rule,test_goldsrc_studio_bundle,tests/test_goldsrc_studio_bundle.c src/goldsrc_studio_bundle.c,))
$(eval $(call test_rule,test_goldsrc_studio_model,tests/test_goldsrc_studio_model.c src/goldsrc_studio_model.c src/bsp_flat_scene.c src/bsp_texture_descriptor.c src/bsp_bundle.c src/ps5_gfx1013_descriptor.c src/ps5_transient_table.c src/ps5_transient_ring.c src/ps5_gpu_span.c,-lm))
$(eval $(call test_rule,test_goldsrc_brush_entities,tests/test_goldsrc_brush_entities.c src/goldsrc_brush_entities.c src/bsp_flat_scene.c src/bsp_texture_descriptor.c src/bsp_bundle.c src/ps5_gfx1013_descriptor.c src/ps5_transient_table.c src/ps5_transient_ring.c src/ps5_gpu_span.c,-lm))
$(eval $(call test_rule,test_goldsrc_visibility,tests/test_goldsrc_visibility.c src/goldsrc_visibility.c src/bsp_flat_scene.c src/ps5_transient_ring.c,-lm))
$(eval $(call test_rule,test_ps5_goldsrc_render_state,tests/test_ps5_goldsrc_render_state.c src/ps5_goldsrc_render_state.c src/goldsrc_render_state.c,))
$(eval $(call test_rule,test_goldsrc_pipeline_cache,tests/test_goldsrc_pipeline_cache.c src/goldsrc_pipeline_cache.c src/ps5_goldsrc_render_state.c src/goldsrc_render_state.c,))
$(eval $(call test_rule,test_ps5_viewport_scissor,tests/test_ps5_viewport_scissor.c src/ps5_viewport_scissor.c,))
$(eval $(call test_rule,test_ps5_shader_pipeline_slot,tests/test_ps5_shader_pipeline_slot.c src/ps5_shader_pipeline_slot.c src/ps5_shader_header.c src/ps5_pipeline.c,))
$(eval $(call test_rule,test_ps5_goldsrc_pipeline_runtime,tests/test_ps5_goldsrc_pipeline_runtime.c src/ps5_goldsrc_pipeline_runtime.c src/goldsrc_pipeline_cache.c src/ps5_goldsrc_render_state.c src/goldsrc_render_state.c src/ps5_gpu_span.c,))
$(eval $(call test_rule,test_ref_agc_live_frame,tests/test_ref_agc_live_frame.c src/ref_agc_live_frame.c,-Isrc -lpthread))
$(eval $(call test_rule,test_bsp_resource_draw,tests/test_bsp_resource_draw.c src/bsp_resource_draw.c src/ps5_gpu_span.c,))
$(eval $(call test_rule,inspect_bsp_bundle,tools/inspect_bsp_bundle.c src/bsp_bundle.c src/bsp_dynamic_lightmap.c src/goldsrc_lightmap_lighting.c src/goldsrc_brush_entities.c src/goldsrc_visibility.c src/bsp_flat_scene.c src/bsp_alpha_test.c src/bsp_sky.c src/bsp_texture_descriptor.c src/ps5_gfx1013_descriptor.c src/ps5_transient_table.c src/ps5_gpu_span.c src/ps5_transient_ring.c,-Isrc -lm))

TESTS := test_gears_mesh test_gears_scene test_gears_frame_tracker \
	test_gears_draw_compose test_gears_animation test_gears_telemetry \
	test_gears_frame_runner test_gears_rt_clear test_gears_renderer \
	test_ps5_surface test_ps5_present test_ps5_frame_completion \
	test_ps5_agc_abi test_ps5_color_target test_ps5_depth_target \
	test_ps5_pipeline test_ps5_event_adapter test_ps5_gpu_span \
	test_ps5_gpu_flip_timing test_ps5_submission test_ps5_direct_memory \
	test_ps5_platform_abi \
	test_in_ps5 test_ps5_audio test_ps5_audio_pattern \
	test_ps5log_host test_ps5_shader_header test_ps5_agc_writer \
	test_ps5_agc_submit test_ps5_videoout test_bsp_bundle test_bsp_command_plan \
	test_bsp_flat_draw test_bsp_flat_scene test_bsp_noclip test_bsp_runtime_plan \
	test_bsp_texture_descriptor test_bsp_textured_draw test_ps5_bump_allocator \
	test_ps5_resource_pool test_ps5_memory_arena test_mem_ps5 test_thread_time_ps5 test_libc_shims_ps5 test_prx_loader_ps5 test_ps5_transient_ring \
	test_ps5_gfx1013_descriptor test_ps5_cache_contract \
	test_ps5_transient_table test_bsp_resource_frame test_bsp_resource_draw \
	test_bsp_dynamic_lightmap test_bsp_alpha_test test_bsp_sky \
	test_bsp_texture_accounting test_goldsrc_render_state \
	test_goldsrc_state_matrix test_goldsrc_2d \
	test_goldsrc_lightmap_lighting test_goldsrc_sprite_particles \
	test_goldsrc_studio_bundle test_goldsrc_studio_model \
	test_goldsrc_brush_entities test_goldsrc_visibility \
	test_ps5_goldsrc_render_state test_goldsrc_pipeline_cache \
	test_ps5_viewport_scissor test_ps5_shader_pipeline_slot \
	test_ps5_goldsrc_pipeline_runtime test_ref_agc_live_frame

test: $(addprefix $(BUILD)/,$(TESTS))
	@set -e; for test in $^; do $$test; done
	python3 tests/test_shader_contract.py
	python3 tests/test_build_shader.py
	python3 tests/test_generate_agc_metadata.py
	python3 tests/test_generate_goldsrc_shader_assets.py
	python3 tests/test_generate_goldsrc_shader_catalog.py
	python3 tests/test_generate_goldsrc_shader_variants.py
	python3 tests/test_generate_pipeline_table.py
	python3 tests/test_generate_bsp_build_metadata.py
	python3 tests/test_generate_studio_build_metadata.py
	python3 tests/test_native_contract.py
	python3 tests/test_title_identity.py
	python3 tests/test_bake_bsp.py
	python3 tests/test_bake_studio.py
	python3 tests/test_validate_bsp_noclip_evidence.py
	python3 tests/test_validate_bsp_textured_evidence.py
	python3 tests/test_validate_bsp_resource_evidence.py
	python3 tests/test_validate_texture_path_lightmap_evidence.py
	python3 tests/test_validate_texture_path_mip_evidence.py
	python3 tests/test_validate_texture_path_alpha_evidence.py
	python3 tests/test_validate_texture_path_sky_evidence.py
	python3 tests/test_validate_texture_path_accounting_evidence.py
	python3 tests/test_validate_texture_path_final_evidence.py
	python3 tests/test_validate_phase4_render_state_evidence.py
	python3 tests/test_validate_phase4_final_evidence.py
	python3 tests/test_validate_gpu_flip_timing_evidence.py
	python3 tests/test_generate_static_library_tables.py
	python3 tests/test_generate_prx_descriptor.py
	python3 tests/test_deploy_engine_bundle.py
	python3 tests/test_instrument_fs_trace.py
	python3 tests/test_deploy_game_data.py
	python3 tests/test_audit_dyn_imports.py
	python3 tests/test_ps5_libc_contract.py
	python3 tests/test_validate_engine_boot_evidence.py
	python3 tests/test_validate_ref_agc_prx_evidence.py
	rm -rf build tools/__pycache__ xash/tools/__pycache__ tests/__pycache__

bsp-bundle: $(BUILD)/inspect_bsp_bundle
	@test -n "$(BSP_INPUT)" || { echo 'BSP_INPUT is required' >&2; exit 2; }
	mkdir -p build/bsp
	python3 tools/bake_bsp.py "$(BSP_INPUT)" build/bsp/map.ps5bsp
	$(BUILD)/inspect_bsp_bundle build/bsp/map.ps5bsp

bsp-inspect: $(BUILD)/inspect_bsp_bundle
	@test -n "$(BSP_BUNDLE)" || { echo 'BSP_BUNDLE is required' >&2; exit 2; }
	$(BUILD)/inspect_bsp_bundle "$(BSP_BUNDLE)"

studio-bundle:
	@test -n "$(STUDIO_INPUT)" || { echo 'STUDIO_INPUT is required' >&2; exit 2; }
	mkdir -p build/studio
	python3 tools/bake_studio.py "$(STUDIO_INPUT)" \
		build/studio/model.ps5mdl --sequence "$(STUDIO_SEQUENCE)"
	python3 tools/inspect_studio_bundle.py build/studio/model.ps5mdl

studio-inspect:
	@test -n "$(STUDIO_BUNDLE)" || { echo 'STUDIO_BUNDLE is required' >&2; exit 2; }
	python3 tools/inspect_studio_bundle.py "$(STUDIO_BUNDLE)"

shaders:
	@test -n "$(AMDLLPC)" || { echo 'AMDLLPC is required' >&2; exit 2; }
	@test -n "$(LLVM_READELF)" || { echo 'LLVM_READELF is required' >&2; exit 2; }
	python3 tools/build_shader.py --amdllpc "$(AMDLLPC)" \
		--readelf "$(LLVM_READELF)" --output-dir build/shaders
	python3 tools/build_shader.py --pipe shaders/bsp_flat.pipe --name bsp_flat \
		--amdllpc "$(AMDLLPC)" --readelf "$(LLVM_READELF)" \
		--output-dir build/shaders
	python3 tools/build_shader.py --pipe shaders/bsp_textured.pipe \
		--name bsp_textured --amdllpc "$(AMDLLPC)" \
		--readelf "$(LLVM_READELF)" --output-dir build/shaders
	python3 tools/build_shader.py --pipe shaders/bsp_resource.pipe \
		--name bsp_resource --amdllpc "$(AMDLLPC)" \
		--readelf "$(LLVM_READELF)" --output-dir build/shaders
	python3 tools/build_shader.py --pipe shaders/bsp_alpha_test.pipe \
		--name bsp_alpha_test --amdllpc "$(AMDLLPC)" \
		--readelf "$(LLVM_READELF)" --output-dir build/shaders
	python3 tools/build_shader.py --pipe shaders/bsp_sky.pipe \
		--name bsp_sky --amdllpc "$(AMDLLPC)" \
		--readelf "$(LLVM_READELF)" --output-dir build/shaders
	python3 tools/build_shader.py --pipe shaders/bsp_overlay.pipe \
		--name bsp_overlay --amdllpc "$(AMDLLPC)" \
		--readelf "$(LLVM_READELF)" --output-dir build/shaders
	python3 tools/generate_goldsrc_shader_variants.py
	@set -e; for name in goldsrc_surface goldsrc_surface_lightmap \
		goldsrc_surface_fog goldsrc_surface_lightmap_fog goldsrc_masked \
		goldsrc_masked_lightmap goldsrc_masked_fog \
		goldsrc_masked_lightmap_fog; do \
		python3 tools/build_shader.py \
			--pipe "build/generated-shaders/$$name.pipe" --name "$$name" \
			--amdllpc "$(AMDLLPC)" --readelf "$(LLVM_READELF)" \
			--output-dir build/shaders; \
	done
	python3 tools/build_shader.py --pipe shaders/goldsrc_screen_2d.pipe \
		--name goldsrc_screen_2d --amdllpc "$(AMDLLPC)" \
		--readelf "$(LLVM_READELF)" --output-dir build/shaders
	python3 tools/generate_agc_metadata.py \
		--manifest build/shaders/gears_lit.manifest.json \
		--output build/generated/gears_shader_metadata.h
	python3 tools/generate_agc_metadata.py \
		--manifest build/shaders/bsp_flat.manifest.json \
		--output build/generated/bsp_flat_shader_metadata.h \
		--prefix BSP_FLAT --symbol-prefix ps5_bsp_flat
	python3 tools/generate_agc_metadata.py \
		--manifest build/shaders/bsp_textured.manifest.json \
		--output build/generated/bsp_textured_shader_metadata.h \
		--prefix BSP_TEXTURED --symbol-prefix ps5_bsp_textured
	python3 tools/generate_agc_metadata.py \
		--manifest build/shaders/bsp_resource.manifest.json \
		--output build/generated/bsp_resource_shader_metadata.h \
		--prefix BSP_RESOURCE --symbol-prefix ps5_bsp_resource
	python3 tools/generate_agc_metadata.py \
		--manifest build/shaders/bsp_alpha_test.manifest.json \
		--output build/generated/bsp_alpha_test_shader_metadata.h \
		--prefix BSP_ALPHA_TEST --symbol-prefix ps5_bsp_alpha_test
	python3 tools/generate_agc_metadata.py \
		--manifest build/shaders/bsp_sky.manifest.json \
		--output build/generated/bsp_sky_shader_metadata.h \
		--prefix BSP_SKY --symbol-prefix ps5_bsp_sky
	python3 tools/generate_agc_metadata.py \
		--manifest build/shaders/bsp_overlay.manifest.json \
		--output build/generated/bsp_overlay_shader_metadata.h \
		--prefix BSP_OVERLAY --symbol-prefix ps5_bsp_overlay
	python3 tools/generate_agc_metadata.py --manifest build/shaders/goldsrc_surface.manifest.json --output build/generated/goldsrc_surface_shader_metadata.h --prefix GOLDSRC_SURFACE --symbol-prefix ps5_goldsrc_surface
	python3 tools/generate_agc_metadata.py --manifest build/shaders/goldsrc_surface_lightmap.manifest.json --output build/generated/goldsrc_surface_lightmap_shader_metadata.h --prefix GOLDSRC_SURFACE_LIGHTMAP --symbol-prefix ps5_goldsrc_surface_lightmap
	python3 tools/generate_agc_metadata.py --manifest build/shaders/goldsrc_surface_fog.manifest.json --output build/generated/goldsrc_surface_fog_shader_metadata.h --prefix GOLDSRC_SURFACE_FOG --symbol-prefix ps5_goldsrc_surface_fog
	python3 tools/generate_agc_metadata.py --manifest build/shaders/goldsrc_surface_lightmap_fog.manifest.json --output build/generated/goldsrc_surface_lightmap_fog_shader_metadata.h --prefix GOLDSRC_SURFACE_LIGHTMAP_FOG --symbol-prefix ps5_goldsrc_surface_lightmap_fog
	python3 tools/generate_agc_metadata.py --manifest build/shaders/goldsrc_masked.manifest.json --output build/generated/goldsrc_masked_shader_metadata.h --prefix GOLDSRC_MASKED --symbol-prefix ps5_goldsrc_masked
	python3 tools/generate_agc_metadata.py --manifest build/shaders/goldsrc_masked_lightmap.manifest.json --output build/generated/goldsrc_masked_lightmap_shader_metadata.h --prefix GOLDSRC_MASKED_LIGHTMAP --symbol-prefix ps5_goldsrc_masked_lightmap
	python3 tools/generate_agc_metadata.py --manifest build/shaders/goldsrc_masked_fog.manifest.json --output build/generated/goldsrc_masked_fog_shader_metadata.h --prefix GOLDSRC_MASKED_FOG --symbol-prefix ps5_goldsrc_masked_fog
	python3 tools/generate_agc_metadata.py --manifest build/shaders/goldsrc_masked_lightmap_fog.manifest.json --output build/generated/goldsrc_masked_lightmap_fog_shader_metadata.h --prefix GOLDSRC_MASKED_LIGHTMAP_FOG --symbol-prefix ps5_goldsrc_masked_lightmap_fog
	python3 tools/generate_agc_metadata.py --manifest build/shaders/goldsrc_screen_2d.manifest.json --output build/generated/goldsrc_screen_2d_shader_metadata.h --prefix GOLDSRC_SCREEN_2D --symbol-prefix ps5_goldsrc_screen_2d
	python3 tools/generate_pipeline_table.py
	python3 tools/validate_goldsrc_shader_manifests.py
	python3 tools/generate_goldsrc_shader_assets.py
	python3 tools/generate_goldsrc_shader_catalog.py

native:
	bash tools/build_native.sh

native-release:
	bash tools/build_native.sh

# Phase 5 gate 1: Xash3D dedicated engine boot title (no shaders required).
engine-boot-native-release:
	bash xash/build_engine.sh

# Phase 5 ScePad gate: the dedicated engine remains the stable host while the
# platform backend records every canonical Xash action from a physical pad.
engine-pad-native-release:
	XASH_PAD_GATE=1 bash xash/build_engine.sh

# Phase 5 SceAudioOut gate: the dedicated engine stays the stable host while a
# deterministic 44.1 kHz pattern runs through the ring, the 147/160 resampler,
# the worker and libSceAudioOut. Needs an operator in front of the console.
engine-audio-native-release:
	XASH_AUDIO_GATE=1 bash xash/build_engine.sh

# Compile/link proof only: substitutes s_ps5.c for s_stub.c in a client build.
# This is not Phase 6 integration and produces no hardware claim.
engine-audio-client-link:
	XASH_MODE=client XASH_AUDIO=1 bash xash/build_engine.sh

# Phase 5 allocator/direct-memory gate: the dedicated host exercises the same
# arena used by engine allocations plus representative generation-tagged GPU
# resource handles, then loads c1a0 and proves a balanced root teardown.
engine-memory-native-release:
	XASH_MEMORY_GATE=1 bash xash/build_engine.sh

# Dedicated Phase 5 thread/time gate: exercises the pthread surface actually
# used by the engine, validates CLOCK_MONOTONIC and measures sleep granularity.
engine-thread-time-native-release:
	XASH_THREAD_TIME_GATE=1 bash xash/build_engine.sh

# Dedicated Phase 5 shim gate: verifies the project-owned assert reporter,
# fixed engine identity and deterministic dladdr fallback before engine boot.
engine-libc-shims-native-release:
	XASH_LIBC_SHIM_GATE=1 bash xash/build_engine.sh

# Phase 6 gate 1: load, resolve, call and unload an application-owned PRX
# through the engine's COM_* library API. Existing engine modules stay static.
engine-prx-loader-native-release:
	XASH_PRX_GATE=1 bash xash/build_engine.sh

# Phase 6 gate 2: load filesystem_stdio from an application-owned PRX while
# preserving the static server rollback point.
engine-filesystem-prx-native-release:
	XASH_FILESYSTEM_PRX=1 bash xash/build_engine.sh

# Phase 6 gate 3: load both the proven filesystem and the HLSDK server from
# application-owned PRXs, spawn the map and release server before filesystem.
engine-server-prx-native-release:
	XASH_FILESYSTEM_PRX=1 XASH_SERVER_PRX=1 bash xash/build_engine.sh

# Phase 6 gate 4: client-mode boot with mainui in an application-owned PRX.
# The proven filesystem/server PRXs remain packaged; client/ref_soft stay
# static so this gate changes only the menu module boundary.
engine-menu-prx-native-release:
	XASH_MODE=client XASH_FILESYSTEM_PRX=1 XASH_SERVER_PRX=1 \
		XASH_MENU_PRX=1 bash xash/build_engine.sh

# Phase 6 gate 5: move the GoldSrc/HLSDK client behind client.prx while the
# accepted filesystem, server and menu PRXs remain the rollback point.
engine-client-prx-native-release:
	XASH_MODE=client XASH_FILESYSTEM_PRX=1 XASH_SERVER_PRX=1 \
		XASH_MENU_PRX=1 XASH_CLIENT_PRX=1 bash xash/build_engine.sh

# Phase 6 gate 6: keep all accepted application PRXs and replace the static
# diagnostic renderer with ref_agc.prx, which owns the Phase 4 AGC backend.
engine-ref-agc-prx-native-release: bsp-bundle studio-bundle shaders
	XASH_MODE=client XASH_REF=agc XASH_FILESYSTEM_PRX=1 XASH_SERVER_PRX=1 \
		XASH_MENU_PRX=1 XASH_CLIENT_PRX=1 XASH_REF_AGC_PRX=1 \
		bash xash/build_engine.sh

bsp-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" bash tools/build_native.sh

bsp-noclip-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		bash tools/build_native.sh

bsp-textured-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 bash tools/build_native.sh

bsp-resource-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 bash tools/build_native.sh

bsp-texture-path-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		bash tools/build_native.sh

bsp-texture-mip-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		BSP_TEXTURE_MIP_GATE=1 bash tools/build_native.sh

bsp-texture-alpha-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		BSP_TEXTURE_ALPHA_GATE=1 bash tools/build_native.sh

bsp-texture-sky-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		BSP_TEXTURE_SKY_GATE=1 bash tools/build_native.sh

bsp-texture-accounting-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		BSP_TEXTURE_ACCOUNTING_GATE=1 bash tools/build_native.sh

bsp-texture-final-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		BSP_TEXTURE_FINAL_GATE=1 bash tools/build_native.sh

bsp-phase4-pipeline-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		GOLDSRC_PHASE4=1 bash tools/build_native.sh

bsp-phase4-viewport-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		GOLDSRC_PHASE4=1 GOLDSRC_VIEWPORT_GATE=1 bash tools/build_native.sh

bsp-phase4-state-matrix-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		GOLDSRC_PHASE4=1 GOLDSRC_STATE_MATRIX_GATE=1 bash tools/build_native.sh

bsp-phase4-2d-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		GOLDSRC_PHASE4=1 GOLDSRC_2D_GATE=1 bash tools/build_native.sh

bsp-phase4-lighting-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		GOLDSRC_PHASE4=1 GOLDSRC_LIGHTING_GATE=1 bash tools/build_native.sh

bsp-phase4-sprite-particles-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		GOLDSRC_PHASE4=1 GOLDSRC_SPRITE_PARTICLE_GATE=1 \
		bash tools/build_native.sh

bsp-phase4-studio-native-release: bsp-bundle studio-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" \
		STUDIO_BUNDLE="$(CURDIR)/build/studio/model.ps5mdl" \
		BSP_NOCLIP=1 BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 \
		BSP_TEXTURE_PATH=1 GOLDSRC_PHASE4=1 GOLDSRC_STUDIO_GATE=1 \
		bash tools/build_native.sh

bsp-phase4-brush-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		GOLDSRC_PHASE4=1 GOLDSRC_BRUSH_GATE=1 bash tools/build_native.sh

bsp-phase4-visibility-native-release: bsp-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" BSP_NOCLIP=1 \
		BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 BSP_TEXTURE_PATH=1 \
		GOLDSRC_PHASE4=1 GOLDSRC_VISIBILITY_GATE=1 bash tools/build_native.sh

bsp-phase4-final-native-release: bsp-bundle studio-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" \
		STUDIO_BUNDLE="$(CURDIR)/build/studio/model.ps5mdl" \
		BSP_NOCLIP=1 BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 \
		BSP_TEXTURE_PATH=1 GOLDSRC_PHASE4=1 \
		GOLDSRC_PHASE4_FINAL_GATE=1 bash tools/build_native.sh

bsp-phase5-gpu-flip-timing-native-release: bsp-bundle studio-bundle
	BSP_BUNDLE="$(CURDIR)/build/bsp/map.ps5bsp" \
		STUDIO_BUNDLE="$(CURDIR)/build/studio/model.ps5mdl" \
		BSP_NOCLIP=1 BSP_TEXTURED=1 BSP_RESOURCE_FOUNDATION=1 \
		BSP_TEXTURE_PATH=1 GOLDSRC_PHASE4=1 \
		GOLDSRC_PHASE4_FINAL_GATE=1 GPU_FLIP_TIMING_GATE=1 \
		bash tools/build_native.sh

audit:
	python3 tools/audit_publication.py

clean:
	rm -rf build dist tools/__pycache__ tests/__pycache__
