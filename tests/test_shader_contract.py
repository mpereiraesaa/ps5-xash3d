#!/usr/bin/env python3
"""Check the independently authored LLPC pipe contract without compiling it."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    native = (ROOT / "native/main.c").read_text(encoding="utf-8")
    clear = native.split('state->live_compose_stage = "live-world-clear";', 1)[1]
    clear = clear.split("RefAgcGpuWorldStats live_world_stats", 1)[0]
    assert clear.index("state->overlay_depth_disabled") < clear.index("bsp_resource_compose_clear(")
    assert clear.index("bsp_resource_compose_clear(") < clear.index("state->depth_registers")
    assert "clear_test=0 clear_write=0 world_depth=restored" in clear
    text = (ROOT / "shaders/gears_lit.pipe").read_text(encoding="utf-8")
    required = (
        "version = 65",
        "layout(location = 0) in vec3 in_position;",
        "layout(location = 1) in vec3 in_normal;",
        "mat4 mvp;",
        "vec4 rotation;",
        "vec4 material;",
        "userDataNode[0].sizeInDwords = 24",
        "userDataNode[1].offsetInDwords = 24",
        "userDataNode[1].type = IndirectUserDataVaPtr",
        "topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST",
        "colorBuffer[0].format = VK_FORMAT_B8G8R8A8_UNORM",
        "binding[0].stride = 24",
        "attribute[1].offset = 12",
    )
    for value in required:
        if text.count(value) != 1:
            raise SystemExit(f"shader contract is missing or duplicates: {value}")
    forbidden = ("gfx" + "1030", ".text.bin", ".pal.elf", "/home/", "/data/")
    for value in forbidden:
        if value in text:
            raise SystemExit(f"shader source contains forbidden value: {value}")
    flat = (ROOT / "shaders/bsp_flat.pipe").read_text(encoding="utf-8")
    flat_required = (
        "layout(location = 0) in vec3 in_position;",
        "mat4 mvp;",
        "vec4 face_color;",
        "userDataNode[0].sizeInDwords = 20",
        "userDataNode[1].offsetInDwords = 20",
        "binding[0].stride = 32",
        "topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST",
    )
    for value in flat_required:
        if flat.count(value) != 1:
            raise SystemExit(f"BSP flat shader contract is missing: {value}")
    for value in forbidden:
        if value in flat:
            raise SystemExit(f"BSP flat shader contains forbidden value: {value}")
    textured = (ROOT / "shaders/bsp_textured.pipe").read_text(encoding="utf-8")
    textured_required = (
        "layout(location = 1) in vec2 in_base_uv;",
        "layout(location = 2) in vec2 in_light_uv;",
        "uniform sampler2D base_texture;",
        "uniform sampler2D lightmap_texture;",
        "base.rgb * texture(lightmap_texture, light_uv).rgb",
        "userDataNode[2].type = DescriptorTableVaPtr",
        "userDataNode[2].offsetInDwords = 0",
        "userDataNode[2].next[0].type = DescriptorCombinedTexture",
        "userDataNode[2].next[0].offsetInDwords = 0",
        "userDataNode[2].next[1].offsetInDwords = 12",
        "attribute[1].offset = 12",
        "attribute[2].offset = 20",
    )
    for value in textured_required:
        if textured.count(value) != 1:
            raise SystemExit(f"BSP textured shader contract is missing: {value}")
    for value in forbidden:
        if value in textured:
            raise SystemExit(f"BSP textured shader contains forbidden value: {value}")
    resource = (ROOT / "shaders/bsp_resource.pipe").read_text(encoding="utf-8")
    resource_required = (
        "layout(set = 0, binding = 0, std140) uniform FrameConstants",
        "mat4 mvp;",
        "vec4 control;",
        "vec4 debug_values[3];",
        "userDataNode[0].sizeInDwords = 1",
        "userDataNode[0].next[0].type = DescriptorConstBuffer",
        "userDataNode[1].type = IndirectUserDataVaPtr",
        "userDataNode[2].type = DescriptorTableVaPtr",
        "binding[0].stride = 32",
    )
    for value in resource_required:
        if value not in resource:
            raise SystemExit(f"BSP resource shader contract is missing: {value}")
    if "discard;" in resource:
        raise SystemExit("opaque BSP resource permutation must not discard")
    alpha_test = (ROOT / "shaders/bsp_alpha_test.pipe").read_text(
        encoding="utf-8"
    )
    for value in resource_required + (
        "if (base.a < 0.5)",
        "discard;",
    ):
        if value not in alpha_test:
            raise SystemExit(f"BSP alpha-test shader contract is missing: {value}")
    sky = (ROOT / "shaders/bsp_sky.pipe").read_text(encoding="utf-8")
    for value in resource_required + (
        "vec4 sky = texture(base_texture, base_uv);",
        "color = vec4(sky.rgb, 1.0);",
    ):
        if value not in sky:
            raise SystemExit(f"BSP sky shader contract is missing: {value}")
    if "discard;" in sky or "base.rgb * texture(lightmap_texture" in sky:
        raise SystemExit("BSP sky permutation must be unlit and non-discarding")
    turbulent = (ROOT / "shaders/bsp_turbulent.pipe").read_text(
        encoding="utf-8"
    )
    for value in resource_required + (
        "animation_time = frame.debug_values[1].x;",
        "sin((raw_uv.y * 0.125 + animation_time) * tau) * 8.0",
        "sin((raw_uv.x * 0.125 + animation_time) * tau) * 8.0",
        "texture(base_texture, warped_uv)",
    ):
        if value not in turbulent:
            raise SystemExit(
                f"BSP turbulent shader contract is missing: {value}"
            )
    if "discard;" in turbulent or "texture(lightmap_texture, light_uv)" in turbulent:
        raise SystemExit(
            "BSP turbulent permutation must be unlit and non-discarding"
        )
    overlay = (ROOT / "shaders/bsp_overlay.pipe").read_text(encoding="utf-8")
    overlay_required = (
        "uniform OverlayConstants",
        "vec4 debug_values[7];",
        "gl_VertexIndex",
        "userDataNode[0].sizeInDwords = 1",
        "userDataNode[0].type = DescriptorTableVaPtr",
        "userDataNode[0].next[0].type = DescriptorConstBuffer",
    )
    for value in overlay_required:
        if value not in overlay:
            raise SystemExit(f"BSP overlay shader contract is missing: {value}")
    goldsrc_template = (ROOT / "shaders/goldsrc_surface.template.pipe").read_text(
        encoding="utf-8"
    )
    for value in (
        "uniform GoldSrcDrawConstants", "vec4 render_color;",
        "vec4 fog_color_density;",
        "color = vec4(surface, base.a * render_color.a);",
        "binding[0].stride = 32",
    ):
        if value not in goldsrc_template:
            raise SystemExit(f"GoldSrc shader template contract is missing: {value}")
    for value in ("@MASK_BLOCK@", "@LIGHTMAP_EXPR@", "@FOG_BLOCK@"):
        if goldsrc_template.count(value) != 1:
            raise SystemExit(f"GoldSrc shader template token is invalid: {value}")
    screen = (ROOT / "shaders/goldsrc_screen_2d.pipe").read_text(
        encoding="utf-8"
    )
    for value in (
        "uniform ScreenConstants", "mat4 projection;", "vec4 color_scale;",
        "layout(location = 0) in vec2 in_position;",
        "layout(location = 2) in vec4 in_color;",
        "out_color = texture(image, uv) * color;",
        "binding[0].stride = 32", "attribute[2].offset = 16",
    ):
        if screen.count(value) != 1:
            raise SystemExit(f"GoldSrc 2D shader contract is missing: {value}")
    for name, pipe in (("resource", resource), ("alpha-test", alpha_test),
                       ("sky", sky), ("turbulent", turbulent),
                       ("overlay", overlay),
                       ("GoldSrc template", goldsrc_template),
                       ("GoldSrc screen", screen)):
        for value in forbidden:
            if value in pipe:
                raise SystemExit(f"BSP {name} shader contains forbidden value: {value}")
    print("shader source contract passed: gears, BSP and GoldSrc Phase 4 ABIs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
