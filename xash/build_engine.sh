#!/usr/bin/env bash
# Build the Xash3D engine title for PS5 (Phase 5 host and Phase 6 gates).
#
# Produces dist/engine-boot/<title>/ with a signed eboot.bin that boots the
# Xash3D FWGS engine with a hybrid library backend. The proven filesystem and
# server remain static until their individual Phase 6 conversions; application
# PRXs load through the same COM_* API. No shaders, no AGC renderer yet.
#
# Environment:
#   PS5_NATIVE_FOUNDATION  boilerplate checkout (default .deps/, pinned)
#   PS5LOG_DEV_CONF        private dev.conf copied into the title (optional)
#   XASH_GAME_DATA         private directory holding valve/ (optional; staged
#                          under dist/.../xash3d, never committed)
#   XASH_BOOT_MAP          map executed after boot (default c1a0)
#   XASH_GATE_SECONDS      queue "quit" after N seconds (default 90; 0 = never)
#   XASH_GATE_FROM_MAP     1 rebases the timeout once at active client signon
#   XASH_JOBS              parallel compile jobs (default nproc)
#   XASH_MODE              dedicated (Phase 5 evidence) or client (early Phase 6
#                          diagnostic: engine, mainui, hlsdk client, ref_null
#                          and ref_soft, headless video)
#   XASH_REF               renderer requested in client mode (default soft)
#   XASH_FS_TRACE          generate guarded upstream FS trace copies (default 0)
#   XASH_FS_TRACE_PATH     exact relative path selected by the trace build
#   XASH_LIBC_SMOKE        call four optional libc helpers at boot (default 0;
#                          evidence-only build, never the production default)
#   XASH_PAD_GATE          exercise the native ScePad backend and quit only
#                          after all canonical actions pass (default 0)
#   XASH_AUDIO_GATE        push the deterministic 44.1 kHz pattern through the
#                          SceAudioOut ring/resampler/worker before the engine
#                          starts, then quit on the first failure (default 0)
#   XASH_AUDIO_USER        system (0xff, default) or foreground; the accepted
#                          variant is recorded, never chosen silently
#   XASH_AUDIO_GATE_FRAMES cap the gate pattern to N source frames for the
#                          minimal ABI smoke run (default 0 = full sequence)
#   XASH_AUDIO             link the SNDDMA binding instead of s_stub.c in client
#                          mode (compile/link proof only, default 0)
#   XASH_MEMORY_GATE       exercise the direct-memory engine arena and the
#                          generation-tagged GPU resource contract (default 0)
#   XASH_THREAD_TIME_GATE  exercise the engine's pthread surface, monotonic
#                          clock and measured nanosleep/usleep timing (default 0)
#   XASH_LIBC_SHIM_GATE    exercise project-owned assert formatting, fixed
#                          identity and the dladdr fallback (default 0)
#   XASH_PRX_GATE          load/call/unload a minimal application-owned PRX
#                          through the engine COM_* API (default 0)
#   XASH_FILESYSTEM_PRX   package filesystem_stdio as the first real dynamic
#                         engine module; server remains static (default 0)
#   XASH_SERVER_PRX       package the HLSDK server as a dynamic module on top
#                         of the proven filesystem PRX checkpoint (default 0)
#   XASH_MENU_PRX         package mainui as a dynamic module in client mode on
#                         top of the filesystem/server rollback point; boot to
#                         the menu instead of queuing a map (default 0)
#   XASH_CLIENT_PRX       package the GoldSrc/HLSDK client as a dynamic module
#                         on top of the filesystem/server/menu rollback point
#                         (default 0)
#   XASH_REF_AGC_PRX      package ref_agc as the final Phase 6 module and bind
#                         it to the Phase 4 native AGC/VideoOut owner (default 0)
#   XASH_PHASE7_MENU_GATE boot the complete Phase 7 stack into MainUI, retain
#                         it for XASH_PHASE7_MENU_SECONDS, then queue the map
#                         through the engine command buffer (default 0)
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
pin=1e9b564a4dd1d567e63ee0d292ed9a026ce06008
url=https://github.com/mpereiraesaa/ps5-native-app-boilerplate.git
foundation=${PS5_NATIVE_FOUNDATION:-$root/.deps/ps5-native-app-boilerplate}
xash=$root/third_party/xash3d-fwgs
hlsdk=$root/third_party/hlsdk-portable
boot_map=${XASH_BOOT_MAP:-c1a0}
gate_seconds=${XASH_GATE_SECONDS:-90}
gate_from_map=${XASH_GATE_FROM_MAP:-0}
sampling_probe=${XASH_SAMPLING_PROBE:-0}
studio_ab=${XASH_STUDIO_AB:-0}
viewmodel_qa=${XASH_VIEWMODEL_QA:-0}
[[ $viewmodel_qa =~ ^[01]$ ]] || { echo "XASH_VIEWMODEL_QA must be 0 or 1" >&2; exit 2; }
[[ $studio_ab =~ ^[01]$ ]] || { echo "XASH_STUDIO_AB must be 0 or 1" >&2; exit 2; }
[[ $studio_ab != 1 || $sampling_probe != 1 ]] || { echo "Studio A/B and wall QA are mutually exclusive" >&2; exit 2; }
texture_memory_probe=${XASH_TEXTURE_MEMORY_PROBE:-0}
hud_trace=${XASH_HUD_TRACE:-0}
hud_probe=${XASH_HUD_PROBE:-0}
texture_mib=${XASH_TEXTURE_MIB:-0}
texture_reserve_mib=${XASH_TEXTURE_RESERVE_MIB:-512}
texture_auto_percent=${XASH_TEXTURE_AUTO_PERCENT:-10}
for texture_setting in "$texture_mib" "$texture_reserve_mib"; do
    [[ $texture_setting =~ ^(0|[1-9][0-9]{0,4})$ ]] || {
        echo "Texture MiB settings must be decimal integers 0..99999" >&2; exit 2;
    }
done
[[ $texture_auto_percent =~ ^([1-9]|[1-9][0-9]|100)$ ]] || {
    echo "XASH_TEXTURE_AUTO_PERCENT must be 1..100" >&2; exit 2;
}
[[ $texture_memory_probe =~ ^[01]$ ]] || { echo "XASH_TEXTURE_MEMORY_PROBE must be 0 or 1" >&2; exit 2; }
[[ $hud_trace =~ ^[01]$ ]] || { echo "XASH_HUD_TRACE must be 0 or 1" >&2; exit 2; }
[[ $hud_probe =~ ^[01]$ ]] || { echo "XASH_HUD_PROBE must be 0 or 1" >&2; exit 2; }
[[ $sampling_probe =~ ^[01]$ ]] || { echo "XASH_SAMPLING_PROBE must be 0 or 1" >&2; exit 2; }
[[ $gate_from_map =~ ^[01]$ ]] || { echo "XASH_GATE_FROM_MAP must be 0 or 1" >&2; exit 2; }
mode=${XASH_MODE:-dedicated}
fs_trace=${XASH_FS_TRACE:-0}
fs_trace_path=${XASH_FS_TRACE_PATH:-gfx/palette.lmp}
libc_smoke=${XASH_LIBC_SMOKE:-0}
pad_gate=${XASH_PAD_GATE:-0}
audio_gate=${XASH_AUDIO_GATE:-0}
audio_user=${XASH_AUDIO_USER:-system}
audio_gate_frames=${XASH_AUDIO_GATE_FRAMES:-0}
audio=${XASH_AUDIO:-0}
memory_gate=${XASH_MEMORY_GATE:-0}
thread_time_gate=${XASH_THREAD_TIME_GATE:-0}
libc_shim_gate=${XASH_LIBC_SHIM_GATE:-0}
prx_gate=${XASH_PRX_GATE:-0}
filesystem_prx=${XASH_FILESYSTEM_PRX:-0}
server_prx=${XASH_SERVER_PRX:-0}
menu_prx=${XASH_MENU_PRX:-0}
client_prx=${XASH_CLIENT_PRX:-0}
ref_agc_prx=${XASH_REF_AGC_PRX:-0}
phase7_menu_gate=${XASH_PHASE7_MENU_GATE:-0}
phase7_menu_seconds=${XASH_PHASE7_MENU_SECONDS:-5}
ref_name=${XASH_REF:-soft}
[[ $mode == dedicated || $mode == client ]] || { echo "XASH_MODE must be dedicated or client" >&2; exit 2; }
[[ $ref_name =~ ^[a-z0-9_]+$ ]] || { echo "XASH_REF must be a renderer short name" >&2; exit 2; }
[[ $fs_trace_path =~ ^[A-Za-z0-9_./-]+$ ]] || { echo "XASH_FS_TRACE_PATH contains unsafe characters" >&2; exit 2; }
[[ $libc_smoke == 0 || $libc_smoke == 1 ]] || { echo "XASH_LIBC_SMOKE must be 0 or 1" >&2; exit 2; }
[[ $pad_gate == 0 || $pad_gate == 1 ]] || { echo "XASH_PAD_GATE must be 0 or 1" >&2; exit 2; }
[[ $audio_gate == 0 || $audio_gate == 1 ]] || { echo "XASH_AUDIO_GATE must be 0 or 1" >&2; exit 2; }
[[ $audio == 0 || $audio == 1 ]] || { echo "XASH_AUDIO must be 0 or 1" >&2; exit 2; }
[[ $memory_gate == 0 || $memory_gate == 1 ]] || { echo "XASH_MEMORY_GATE must be 0 or 1" >&2; exit 2; }
[[ $thread_time_gate == 0 || $thread_time_gate == 1 ]] || { echo "XASH_THREAD_TIME_GATE must be 0 or 1" >&2; exit 2; }
[[ $libc_shim_gate == 0 || $libc_shim_gate == 1 ]] || { echo "XASH_LIBC_SHIM_GATE must be 0 or 1" >&2; exit 2; }
[[ $prx_gate == 0 || $prx_gate == 1 ]] || { echo "XASH_PRX_GATE must be 0 or 1" >&2; exit 2; }
[[ $filesystem_prx == 0 || $filesystem_prx == 1 ]] || { echo "XASH_FILESYSTEM_PRX must be 0 or 1" >&2; exit 2; }
[[ $server_prx == 0 || $server_prx == 1 ]] || { echo "XASH_SERVER_PRX must be 0 or 1" >&2; exit 2; }
[[ $menu_prx == 0 || $menu_prx == 1 ]] || { echo "XASH_MENU_PRX must be 0 or 1" >&2; exit 2; }
[[ $client_prx == 0 || $client_prx == 1 ]] || { echo "XASH_CLIENT_PRX must be 0 or 1" >&2; exit 2; }
[[ $ref_agc_prx == 0 || $ref_agc_prx == 1 ]] || { echo "XASH_REF_AGC_PRX must be 0 or 1" >&2; exit 2; }
[[ $phase7_menu_gate == 0 || $phase7_menu_gate == 1 ]] || { echo "XASH_PHASE7_MENU_GATE must be 0 or 1" >&2; exit 2; }
[[ $phase7_menu_seconds =~ ^[1-9][0-9]*$ ]] || { echo "XASH_PHASE7_MENU_SECONDS must be a positive integer" >&2; exit 2; }
if [[ $server_prx == 1 && $filesystem_prx != 1 ]]; then
    echo "XASH_SERVER_PRX=1 requires the proven XASH_FILESYSTEM_PRX=1 checkpoint" >&2
    exit 2
fi
if [[ $menu_prx == 1 && ( $mode != client || $filesystem_prx != 1 || $server_prx != 1 ) ]]; then
    echo "XASH_MENU_PRX=1 requires XASH_MODE=client plus the proven filesystem/server PRX checkpoint" >&2
    exit 2
fi
if [[ $client_prx == 1 && ( $mode != client || $filesystem_prx != 1 || $server_prx != 1 || $menu_prx != 1 ) ]]; then
    echo "XASH_CLIENT_PRX=1 requires XASH_MODE=client plus the proven filesystem/server/menu PRX checkpoint" >&2
    exit 2
fi
if [[ $ref_agc_prx == 1 && ( $mode != client || $filesystem_prx != 1 || $server_prx != 1 || $menu_prx != 1 || $client_prx != 1 || $ref_name != agc ) ]]; then
    echo "XASH_REF_AGC_PRX=1 requires XASH_MODE=client, XASH_REF=agc and the proven filesystem/server/menu/client PRX checkpoint" >&2
    exit 2
fi
if [[ $phase7_menu_gate == 1 && $ref_agc_prx != 1 ]]; then
    echo "XASH_PHASE7_MENU_GATE=1 requires the complete client/ref_agc PRX stack" >&2
    exit 2
fi
if [[ $hud_probe == 1 && ( $ref_agc_prx != 1 || $gate_from_map != 1 || ! $gate_seconds =~ ^[0-9]+$ ) ]]; then
    echo "XASH_HUD_PROBE=1 requires client/ref_agc PRXs and a map-relative bounded gate" >&2
    exit 2
fi
if [[ $hud_probe == 1 ]] && (( 10#$gate_seconds < 110 )); then
    echo "XASH_HUD_PROBE=1 requires at least 110 map seconds" >&2
    exit 2
fi
[[ $audio_user == system || $audio_user == foreground ]] || {
    echo "XASH_AUDIO_USER must be system or foreground" >&2; exit 2; }
[[ $audio_gate_frames =~ ^[0-9]+$ ]] || {
    echo "XASH_AUDIO_GATE_FRAMES must be an integer" >&2; exit 2; }
jobs=${XASH_JOBS:-$(nproc)}
dev_conf=${PS5LOG_DEV_CONF:-$root/dev.conf}
game_data=${XASH_GAME_DATA:-}
objcopy=${LLVM_OBJCOPY:-$(command -v llvm-objcopy-18 || command -v llvm-objcopy || true)}
readelf=${LLVM_READELF:-$(command -v llvm-readelf-18 || command -v llvm-readelf || true)}
# Relocatable module links use a host linker: the SDK lld emits one .rela.text
# per COMDAT group when merging, which the final link then rejects.
ld_reloc=${LD_RELOCATABLE:-$(command -v ld.lld-18 || command -v ld.bfd || command -v ld || true)}

[[ $boot_map =~ ^[A-Za-z0-9_]+$ ]] || { echo "XASH_BOOT_MAP must be a map name" >&2; exit 2; }
[[ $gate_seconds =~ ^[0-9]+$ ]] || { echo "XASH_GATE_SECONDS must be an integer" >&2; exit 2; }
if [[ $phase7_menu_gate == 1 && ( $gate_seconds == 0 || $gate_seconds -le $phase7_menu_seconds ) ]]; then
    echo "XASH_PHASE7_MENU_GATE requires XASH_GATE_SECONDS greater than XASH_PHASE7_MENU_SECONDS" >&2
    exit 2
fi
[[ -n $objcopy && -x $objcopy ]] || { echo "llvm-objcopy is required" >&2; exit 2; }
[[ -n $readelf && -x $readelf ]] || { echo "llvm-readelf is required" >&2; exit 2; }
[[ -n $ld_reloc && -x $ld_reloc ]] || { echo "a host ld for relocatable links is required" >&2; exit 2; }
[[ -f $xash/engine/common/host.c ]] || {
    echo "third_party/xash3d-fwgs is not checked out; run git submodule update --init" >&2
    exit 2
}
[[ -f $hlsdk/dlls/h_export.cpp ]] || {
    echo "third_party/hlsdk-portable is not checked out; run git submodule update --init" >&2
    exit 2
}
if [[ ! -f $xash/3rdparty/library_suffix/include/build.h ||
      ! -f $xash/3rdparty/bzip2/bzip2/bzlib.c ]]; then
    git -C "$xash" submodule update --init 3rdparty/library_suffix 3rdparty/bzip2/bzip2
fi
if [[ $mode == client && ! -f $xash/3rdparty/mainui/BaseMenu.cpp ]]; then
    git -C "$xash" submodule update --init --recursive 3rdparty/mainui
fi
# Client mode compiles the engine's codec and emulator trees, so their sources
# have to be there; only mainui was being initialized before.
if [[ $mode == client && ! -f $xash/3rdparty/opus/opus/include/opus_custom.h ]]; then
    git -C "$xash" submodule update --init --recursive \
        3rdparty/opus 3rdparty/opusfile 3rdparty/libogg 3rdparty/vorbis
fi
if [[ $mode == client && ! -f $xash/3rdparty/MultiEmulator/include/multi_emulator.h ]]; then
    git -C "$xash" submodule update --init --recursive 3rdparty/MultiEmulator
fi

if [[ ! -d $foundation/.git ]]; then
    mkdir -p -- "$(dirname -- "$foundation")"
    git clone --filter=blob:none "$url" "$foundation"
fi
actual=$(git -C "$foundation" rev-parse HEAD)
if [[ $actual != "$pin" ]]; then
    git -C "$foundation" fetch origin "$pin"
    git -C "$foundation" checkout --detach "$pin"
fi
[[ $(git -C "$foundation" rev-parse HEAD) == "$pin" ]] || {
    echo "native foundation pin verification failed" >&2; exit 2;
}
make -C "$foundation" deps libc >/dev/null

sdk="$foundation/.deps/native/ps5-payload-sdk"
native="$foundation/tooling/native"
tool="$foundation/build/host/ps5-native-tool"
tool_stamp="$foundation/build/host/ps5-native-tool.commit"
if [[ ! -x $tool || ! -f $tool_stamp || $(<"$tool_stamp") != "$pin" ]]; then
    zlib_root="$foundation/.deps/native/zlib/root"
    zlib_archive=$(find "$zlib_root" -type f -name libz.a -print -quit)
    cxx=$(command -v clang++-18 || command -v clang++ || true)
    [[ -n $cxx && -n $zlib_archive ]] || {
        echo "native foundation host-tool dependencies are unavailable" >&2
        exit 2
    }
    mkdir -p "$foundation/build/host"
    "$cxx" -std=c++20 -O2 -Wall -Wextra -Werror \
        -I "$zlib_root/usr/include" \
        "$native/native_app_builder.cpp" "$native/self_container.cpp" \
        "$native/elf_object.cpp" "$native/sce_module_writer.cpp" \
        "$zlib_archive" -o "$tool"
    printf '%s\n' "$pin" > "$tool_stamp"
fi
[[ -x $tool && -d $sdk && -f $foundation/runtime/libc.prx ]] || {
    echo "native foundation did not produce its SDK, tool and runtime" >&2
    exit 2
}

title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' \
    "$root/sce_sys/param.json")
build="$root/build/engine-boot"
dist="$root/dist/engine-boot/$title_id"
gen="$build/generated"
rm -rf -- "$build" "$dist"
mkdir -p "$build/obj" "$gen/bzip2" "$dist/sce_sys" "$dist/sce_module"

engine_commit=$(git -C "$xash" rev-parse --short=7 HEAD)
engine_date=$(git -C "$xash" log -1 --format=%cs HEAD)
hlsdk_commit=$(git -C "$hlsdk" rev-parse --short=7 HEAD)
cat > "$gen/vcs_info_server.c" <<VCS
/* generated by xash/build_engine.sh; hlsdk's waf normally emits this */
const char *g_VCSInfo_Commit = "$hlsdk_commit";
const char *g_VCSInfo_Branch = "ps5";
VCS
cat > "$gen/ps5_xash_build.h" <<HEADER
/* generated by xash/build_engine.sh; do not edit */
#define PS5_XASH_ENGINE_COMMIT "$engine_commit"
#define PS5_XASH_HLSDK_COMMIT "$hlsdk_commit"
#define PS5_XASH_BOOT_MAP "$boot_map"
#define PS5_XASH_GATE_SECONDS $gate_seconds
#define PS5_XASH_GATE_FROM_MAP $gate_from_map
#define PS5_XASH_SAMPLING_PROBE $sampling_probe
#define PS5_XASH_STUDIO_AB $studio_ab
#define PS5_XASH_VIEWMODEL_QA $viewmodel_qa
#define PS5_XASH_HUD_PROBE $hud_probe
#define PS5_XASH_TITLE_ID "$title_id"
#define PS5_XASH_MODE "$mode"
#define PS5_XASH_MODE_CLIENT $([[ $mode == client ]] && echo 1 || echo 0)
#define PS5_XASH_REF "$ref_name"
#define PS5_XASH_LIBC_SMOKE $libc_smoke
#define PS5_XASH_PAD_GATE $pad_gate
#define PS5_XASH_AUDIO_GATE $audio_gate
#define PS5_XASH_AUDIO_USER_FOREGROUND $([[ $audio_user == foreground ]] && echo 1 || echo 0)
#define PS5_XASH_AUDIO "$audio_user"
#define PS5_XASH_AUDIO_GATE_FRAMES $audio_gate_frames
#define PS5_XASH_MEMORY_GATE $memory_gate
#define PS5_XASH_THREAD_TIME_GATE $thread_time_gate
#define PS5_XASH_LIBC_SHIM_GATE $libc_shim_gate
#define PS5_XASH_PRX_GATE $prx_gate
#define PS5_XASH_FILESYSTEM_PRX $filesystem_prx
#define PS5_XASH_SERVER_PRX $server_prx
#define PS5_XASH_MENU_PRX $menu_prx
#define PS5_XASH_CLIENT_PRX $client_prx
#define PS5_XASH_REF_AGC_PRX $ref_agc_prx
#define PS5_XASH_PHASE7_MENU_GATE $phase7_menu_gate
#define PS5_XASH_PHASE7_MENU_SECONDS $phase7_menu_seconds
HEADER
sed 's/@BZ_VERSION@/1.1.0-fwgs/' "$xash/3rdparty/bzip2/bzip2/bz_version.h.in" \
    > "$gen/bzip2/bz_version.h"
# generated_library_tables.h lists the module names (mode-dependent) and is
# compiled into the static half of lib_ps5.c; per-module export tables are generated later into
# $gen/helpers once each relocatable is known.
table_specs=()
if [[ $server_prx == 0 ]]; then
    table_specs+=(server="$root/xash/exports/server.txt")
fi
if [[ $filesystem_prx == 0 ]]; then
    table_specs=(filesystem_stdio="$root/xash/exports/filesystem_stdio.txt" "${table_specs[@]}")
fi
if [[ $mode == client ]]; then
    if [[ $menu_prx == 0 ]]; then
        table_specs+=(menu="$root/xash/exports/menu.txt")
    fi
    if [[ $client_prx == 0 ]]; then
        table_specs+=(client="$root/xash/exports/client.txt")
    fi
    table_specs+=(ref_null="$root/xash/exports/ref.txt" ref_soft="$root/xash/exports/ref.txt")
fi
mkdir -p "$gen/helpers"
python3 "$root/xash/tools/generate_static_library_tables.py" "$gen" "${table_specs[@]}"

cc=(env PS5_PAYLOAD_SDK="$sdk" sh "$foundation/tooling/prospero-clang18")
lld="$sdk/bin/prospero-lld"

engine_defines=(
    -DXASH_STATIC_LIBS=1 -DXASH_NO_LIBDL=1
    -DXASH_CRASHHANDLER=0 -DXASH_LOW_MEMORY=0 -DENGINE_DLL=1
    -DXASH_PS5=1 -DXASH_TIMER=TIMER_POSIX -DXASH_MESSAGEBOX=99
	-DPS5_XASH_MODE_CLIENT=$([[ $mode == client ]] && echo 1 || echo 0)
    "-DXASH_GAMEDIR=\"valve\"" "-DXASH_BUILD_COMMIT=\"$engine_commit\""
    "-DXASH_BUILD_BRANCH=\"ps5\"" "-DXASH_BUILD_COMMIT_DATE=\"$engine_date\""
    "-DSTDINT_H=<stdint.h>" "-DALLOCA_H=<stdlib.h>"
    # Exported is not the same as hardware-validated. strcasestr failed its FW
    # 12.02 execution test, so only that helper uses Xash's portable fallback.
    # The other four stay enabled and XASH_LIBC_SMOKE=1 revalidates them by
    # direct execution before engine startup.
    -DHAVE_STRCASECMP=1 -DHAVE_STRCASESTR=0 -DHAVE_STRNLEN=1 -DHAVE_STRLCPY=1 -DHAVE_STRLCAT=1
    # fs_ps5.c fills a valid FreeBSD d_type; let the engine trust it (waf sets this).
    -DHAVE_DIRENT_D_TYPE=1
)
if [[ $mode == dedicated ]]; then
    engine_defines+=(-DXASH_DEDICATED=1)
else
    # Renderer list advertised by the engine; ref_null is reachable through -ref null.
    # XASH_VIDEO=99 is the PS5 headless backend (common.h refuses VIDEO_NULL in a client).
    # XASH_SOUND=99 is the PS5 SceAudioOut backend; s_stub.c compiles itself out
    # for anything but SOUND_NULL, so the two never both define SNDDMA_*.
    engine_defines+=(-DXASH_REF_SOFT_ENABLED=1 -DXASH_VIDEO=99 -DXASH_INPUT=INPUT_NULL
        -DXASH_SOUND=$([[ $audio == 1 ]] && echo 99 || echo SOUND_NULL))
    if [[ $ref_agc_prx == 1 ]]; then
        # The live AGC backend owns a 1080p logical canvas.  The historical
        # 640x480 headless default would otherwise make MainUI occupy only the
        # upper-left corner of the native framebuffer.
        engine_defines+=(-DPS5_XASH_VIDEO_WIDTH=1920 -DPS5_XASH_VIDEO_HEIGHT=1080)
    fi
    engine_includes_client=(
        -I"$xash/3rdparty/opus/opus/include" -I"$xash/3rdparty/opusfile/opusfile/include"
        -I"$xash/3rdparty/libogg/libogg/include" -I"$gen/ogg"
        -I"$xash/3rdparty/vorbis/vorbis-src/include" -I"$xash/3rdparty/MultiEmulator/include"
    )
    mkdir -p "$gen/ogg/ogg"
    cat > "$gen/ogg/ogg/config_types.h" <<'OGG'
/* generated by xash/build_engine.sh: libogg's configure output for this target */
#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__
#include <stdint.h>
typedef int16_t ogg_int16_t; typedef uint16_t ogg_uint16_t;
typedef int32_t ogg_int32_t; typedef uint32_t ogg_uint32_t;
typedef int64_t ogg_int64_t; typedef uint64_t ogg_uint64_t;
#endif
OGG
    # The client makes far more small allocations than the server; keep more of
    # them away from the 8 MiB libc heap.
    engine_defines+=(-DPS5_LARGE_ALLOC_BYTES=65536)
fi
[[ $fs_trace == 1 ]] && engine_defines+=(-DPS5_XASH_FS_TRACE=1 "-DPS5_XASH_FS_TRACE_PATH=\"$fs_trace_path\"")
engine_includes_client=("${engine_includes_client[@]:-}")
engine_includes=(
    -I"$gen" -I"$root/include" -I"$root/xash/platform_ps5" -I"$root/native/ps5log"
    -I"$xash/3rdparty/library_suffix/include"
    -I"$xash/engine" -I"$xash/engine/common" -I"$xash/engine/common/imagelib"
    -I"$xash/engine/common/soundlib" -I"$xash/engine/server"
    -I"$xash/engine/platform" -I"$xash/engine/client" -I"$xash/engine/client/vgui" -I"$xash/common" -I"$xash/public"
    -I"$xash/pm_shared" -I"$xash/filesystem" -I"$xash/3rdparty"
    -I"$xash/3rdparty/bzip2/bzip2" -I"$gen/bzip2"
    "${engine_includes_client[@]}"
)
cflags=(-O2 -w -fno-strict-aliasing -ffunction-sections -fdata-sections)
# Modules go through ld -r; lld rejects relocatable output whose merged
# sections carry several relocation sections, so they keep whole sections.
module_cflags=(-O2 -w -fno-strict-aliasing)
if [[ $fs_trace == 1 ]]; then
    module_cflags+=(-fno-omit-frame-pointer)
    mkdir -p "$gen/fs_trace"
    python3 "$root/xash/tools/instrument_fs_trace.py" \
        --io "$xash/filesystem/io.c" \
        --searchpath "$xash/filesystem/searchpath.c" \
        --img-main "$xash/engine/common/imagelib/img_main.c" \
        --img-wad "$xash/engine/common/imagelib/img_wad.c" \
        --output "$gen/fs_trace"
fi

# Compile a list of sources (stdin, one per line, absolute) into $1 with
# the flags in the remaining arguments; prints the object paths.
compile_set() {
    local list=$1; shift
    local outdir=$1; shift
    local std=$1; shift
    mkdir -p "$outdir"
    local src
    local -a sources=()
    while IFS= read -r src; do sources+=("$src"); done
    : > "$list"
    printf '%s\0' "${sources[@]}" | xargs -0 -P "$jobs" -I{} sh -c '
        src=$1; shift; outdir=$1; shift
        obj="$outdir/$(printf "%s" "$src" | sed "s|[/ ]|_|g").o"
        "$@" -c "$src" -o "$obj" || { echo "FAILED $src" >&2; exit 1; }
    ' _ {} "$outdir" "${cc[@]}" "$std" "$@" || { echo "compilation failed" >&2; exit 1; }
    for src in "${sources[@]}"; do
        printf '%s\n' "$outdir/$(printf "%s" "$src" | sed "s|[/ ]|_|g").o" >> "$list"
    done
}

echo "== engine (dedicated, static libs)"
if [[ $mode == client ]]; then
    python3 -B "$root/xash/tools/prepare_blood_effects.py" "$xash/engine/client/cl_tent.c" "$gen/ps5_cl_tent.c"
fi
engine_sources=$(
    find "$xash/engine/common" "$xash/engine/server" -maxdepth 1 -name '*.c'
    if [[ $fs_trace == 1 ]]; then
        find "$xash/engine/common/imagelib" -name '*.c' ! -name 'img_main.c' ! -name 'img_wad.c'
        echo "$gen/fs_trace/img_main.c"
        echo "$gen/fs_trace/img_wad.c"
    else
        find "$xash/engine/common/imagelib" -name '*.c'
    fi
    find "$xash/engine/common/soundlib" "$xash/engine/common/http" -name '*.c'
    find "$xash/public" -maxdepth 1 -name '*.c'
    find "$xash/engine/platform/posix" -name '*.c' \
        ! -name 'sys_posix.c' ! -name 'crash_*.c' ! -name 'lib_posix.c'
    echo "$root/xash/platform_ps5/lib_ps5.c"
    echo "$xash/3rdparty/library_suffix/src/library_suffix.c"
    echo "$root/xash/platform_ps5/sys_ps5.c"
    echo "$root/xash/platform_ps5/libc_shims_ps5.c"
    echo "$root/xash/platform_ps5/fs_ps5.c"
    echo "$root/xash/platform_ps5/mem_ps5.c"
	echo "$root/xash/platform_ps5/memory_arena_ps5.c"
	echo "$root/xash/platform_ps5/thread_time_ps5.c"
	echo "$root/xash/platform_ps5/prx_loader_ps5.c"
	echo "$root/xash/platform_ps5/in_ps5.c"
    if [[ $audio_gate == 1 || $audio == 1 ]]; then
        echo "$root/xash/platform_ps5/audio_ps5.c"
        echo "$root/xash/platform_ps5/audio_pattern_ps5.c"
    fi
    if [[ $audio_gate == 1 ]]; then
        echo "$root/xash/platform_ps5/audio_gate_ps5.c"
    fi
    if [[ $memory_gate == 1 ]]; then
        echo "$root/xash/platform_ps5/memory_gate_ps5.c"
    fi
    if [[ $thread_time_gate == 1 ]]; then
        echo "$root/xash/platform_ps5/thread_time_gate_ps5.c"
    fi
    if [[ $prx_gate == 1 ]]; then
        echo "$root/xash/platform_ps5/prx_gate_ps5.c"
    fi
    if [[ $mode == client ]]; then
        find "$xash/engine/client" -name '*.c' ! -name 'cl_tent.c'
        echo "$gen/ps5_cl_tent.c"
        if [[ $audio == 1 ]]; then
            echo "$root/xash/platform_ps5/s_ps5.c"
        else
            echo "$xash/engine/platform/stub/s_stub.c"
        fi
        echo "$root/xash/platform_ps5/vid_ps5.c"
        find "$xash/3rdparty/MultiEmulator/src" -name '*.c'
    fi
    echo "$root/xash/platform_ps5/boot_ps5.c"
    for f in blocksort huffman crctable randtable compress decompress bzlib; do
        echo "$xash/3rdparty/bzip2/bzip2/$f.c"
    done
    echo "$root/native/ps5log/ps5log_ps5_net.c"
)
printf '%s\n' "$engine_sources" | sort -u |
    compile_set "$build/engine.objects" "$build/obj/engine" -std=gnu11 "${cflags[@]}" \
        "${engine_defines[@]}" "${engine_includes[@]}"
mapfile -t engine_objects < "$build/engine.objects"
"${cc[@]}" -std=c11 "${cflags[@]}" -I"$root/native/ps5log" \
    -include "$root/native/ps5log/ps5log_ps5_net.h" \
    -c "$root/native/ps5log/ps5log.c" -o "$build/obj/ps5log.o"
engine_objects+=("$build/obj/ps5log.o")

if [[ $mode == client ]]; then
    echo "== codecs (opus, ogg, vorbis, opusfile)"
    opus=$xash/3rdparty/opus/opus
    opus_sources=$(find "$opus/src" "$opus/celt" "$opus/silk" "$opus/silk/float" -maxdepth 1 -name '*.c' \
        ! -name 'repacketizer_demo.c' ! -name 'opus_demo.c' ! -name 'opus_compare.c' ! -name 'opus_custom_demo.c')
    printf '%s\n' "$opus_sources" | sort -u |
        compile_set "$build/opus.objects" "$build/obj/opus" -std=gnu11 "${cflags[@]}" \
            -DOPUS_BUILD=1 -DCUSTOM_MODES=1 -DHAVE_LRINT=1 -DHAVE_LRINTF=1 -DVAR_ARRAYS=1 \
            -I"$opus" -I"$opus/include" -I"$opus/celt" -I"$opus/silk" -I"$opus/silk/float"
    ogg=$xash/3rdparty/libogg/libogg
    printf '%s\n' "$ogg"/src/*.c |
        compile_set "$build/ogg.objects" "$build/obj/ogg" -std=gnu11 "${cflags[@]}" -I"$ogg/include" -I"$gen/ogg"
    vorbis=$xash/3rdparty/vorbis/vorbis-src
    vorbis_sources=$(for f in mdct smallft block envelope window lsp lpc analysis synthesis psy info floor1 floor0 \
        res0 mapping0 registry codebook sharedbook lookup bitrate vorbisfile; do echo "$vorbis/lib/$f.c"; done)
    printf '%s\n' "$vorbis_sources" |
        compile_set "$build/vorbis.objects" "$build/obj/vorbis" -std=gnu11 "${cflags[@]}" \
            -I"$vorbis/include" -I"$vorbis/lib" -I"$ogg/include" -I"$gen/ogg"
    opusfile=$xash/3rdparty/opusfile/opusfile
    printf '%s\n' "$opusfile/src/info.c" "$opusfile/src/internal.c" "$opusfile/src/opusfile.c" "$opusfile/src/stream.c" |
        compile_set "$build/opusfile.objects" "$build/obj/opusfile" -std=gnu11 "${cflags[@]}" \
            -I"$opusfile/include" -I"$opus/include" -I"$ogg/include" -I"$gen/ogg"
    for l in opus ogg vorbis opusfile; do
        mapfile -t codec_objects < "$build/$l.objects"
        engine_objects+=("${codec_objects[@]}")
    done
fi

# Relocatable module: link its objects into one object and keep only the
# generated export table global, exactly like xshlib does with ld -r/objcopy.
build_module() {
    local name=$1; shift
    local -a objs=("$@")
    "$ld_reloc" -r -o "$build/$name.unstripped.o" "${objs[@]}"
    "$objcopy" -G "lib_${name}_exports" "$build/$name.unstripped.o" "$build/$name.o"
}

echo "== filesystem_stdio module"
fs_helper=()
if [[ $filesystem_prx == 0 ]]; then
    fs_helper=("$gen/link_helper_filesystem_stdio.c")
fi
if [[ $fs_trace == 1 ]]; then
    fs_sources=$(find "$xash/filesystem" -maxdepth 1 -name '*.c' \
        ! -name 'io.c' ! -name 'searchpath.c'; \
        echo "$gen/fs_trace/io.c"; echo "$gen/fs_trace/searchpath.c"; \
        printf '%s\n' "${fs_helper[@]}")
else
    fs_sources=$(find "$xash/filesystem" -maxdepth 1 -name '*.c'; printf '%s\n' "${fs_helper[@]}")
fi
if [[ $filesystem_prx == 1 ]]; then
    fs_sources=$(printf '%s\n' "$fs_sources" | grep -vE '/(dll|searchpath)\.c$')
fi
fs_module_cflags=("${module_cflags[@]}")
if [[ $filesystem_prx == 1 ]]; then
    fs_module_cflags+=(-fPIC)
fi
printf '%s\n' "$fs_sources" | sort -u |
    compile_set "$build/filesystem.objects" "$build/obj/filesystem" -std=gnu11 "${fs_module_cflags[@]}" \
        "${engine_defines[@]}" "${engine_includes[@]}"
mapfile -t fs_objects < "$build/filesystem.objects"
"${cc[@]}" -std=gnu++11 "${fs_module_cflags[@]}" -fno-exceptions -fno-rtti \
    "${engine_defines[@]}" "${engine_includes[@]}" \
    -c "$xash/filesystem/VFileSystem009.cpp" -o "$build/obj/filesystem/VFileSystem009.o"
fs_objects+=("$build/obj/filesystem/VFileSystem009.o")
if [[ $filesystem_prx == 0 ]]; then
    build_module filesystem_stdio "${fs_objects[@]}"
else
    "${cc[@]}" -std=gnu11 "${fs_module_cflags[@]}" \
        "${engine_defines[@]}" "${engine_includes[@]}" \
        -DFS_InitStdio=PS5_FilesystemPrxOriginalInitStdio \
        -DFS_LoadGameInfo=PS5_FilesystemPrxOriginalLoadGameInfo \
        -c "$xash/filesystem/searchpath.c" -o "$build/obj/filesystem/searchpath-prx.o"
    "${cc[@]}" -std=gnu11 "${fs_module_cflags[@]}" \
        "${engine_defines[@]}" "${engine_includes[@]}" \
        -DFS_InitStdio=PS5_FilesystemPrxInitStdio \
        -DFS_LoadGameInfo=PS5_FilesystemPrxLoadGameInfo \
        -c "$xash/filesystem/dll.c" -o "$build/obj/filesystem/dll-prx.o"
    fs_objects+=("$build/obj/filesystem/searchpath-prx.o" "$build/obj/filesystem/dll-prx.o")
fi

echo "== server module (hlsdk-portable)"
server_defines=(-DCLIENT_WEAPONS -DNO_VOICEGAMEMGR -Dstricmp=strcasecmp
    -Dstrnicmp=strncasecmp -D_snprintf=snprintf -D_vsnprintf=vsnprintf)
server_module_cflags=("${module_cflags[@]}")
if [[ $server_prx == 1 ]]; then
    server_module_cflags+=(-fPIC)
fi
server_includes=(-I"$hlsdk/dlls" -I"$hlsdk/common" -I"$hlsdk/engine"
    -I"$hlsdk/pm_shared" -I"$hlsdk/game_shared" -I"$hlsdk/public")
server_cxx=$(find "$hlsdk/dlls" -name '*.cpp' \
    ! -name 'mpstubb.cpp' ! -name 'stats.cpp' ! -name 'Wxdebug.cpp')
server_c=$(find "$hlsdk/pm_shared" -name '*.c'; echo "$hlsdk/public/safe_snprintf.c"
    echo "$hlsdk/external/openbsd/strlcpy.c"; echo "$hlsdk/external/openbsd/strlcat.c"
    echo "$gen/vcs_info_server.c")
printf '%s\n' "$server_cxx" | sort -u |
    compile_set "$build/server-cxx.objects" "$build/obj/server" -std=gnu++11 "${server_module_cflags[@]}" \
        -fno-exceptions -fno-rtti "${server_defines[@]}" "${server_includes[@]}"
printf '%s\n' "$server_c" | sort -u |
    compile_set "$build/server-c.objects" "$build/obj/server" -std=gnu11 "${server_module_cflags[@]}" \
        "${server_defines[@]}" "${server_includes[@]}"
mapfile -t server_objects < "$build/server-cxx.objects"
mapfile -t server_c_objects < "$build/server-c.objects"
# The engine resolves every map entity class through COM_GetProcAddress on
# the server module, so the export table must list each LINK_ENTITY_TO_CLASS
# symbol the compiled module actually defines, not only the three entry points.
"$ld_reloc" -r -o "$build/server.stage1.o" "${server_objects[@]}" "${server_c_objects[@]}"
llvm_nm=${LLVM_NM:-$(command -v llvm-nm-18 || command -v llvm-nm)}
"$llvm_nm" -g --defined-only "$build/server.stage1.o" | awk '$2 ~ /^[TtWw]$/ {print $3}' | sort -u \
    > "$build/server.defined"
python3 - "$hlsdk" "$root/xash/exports/server.txt" "$build/server.defined" \
    "$gen/server_exports.txt" "$root/xash/tools" <<'PY'
import pathlib, re, sys
hlsdk, fixed, defined, out = (pathlib.Path(a) for a in sys.argv[1:5])
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[5])
from generate_static_library_tables import server_callback_exports
names = [l.split("#", 1)[0].strip() for l in fixed.read_text().splitlines()]
names = [n for n in names if n]
pattern = re.compile(r"LINK_ENTITY_TO_CLASS\s*\(\s*([A-Za-z0-9_]+)")
classes = set()
for folder in ("dlls", "game_shared"):
    for src in (hlsdk / folder).rglob("*.cpp"):
        classes.update(pattern.findall(src.read_text(encoding="utf-8", errors="replace")))
have = set(defined.read_text().split())
exported = names + sorted(c for c in classes if c in have and c not in names)
callbacks = server_callback_exports(have)
exported += [name for name in callbacks if name not in exported]
# The server descriptor adds six lifecycle/probe exports. Fail at build time
# rather than emitting a descriptor the runtime cannot validate.
if len(exported) + 6 > 4096:
    raise SystemExit("server descriptor exceeds PS5_PRX_MAX_EXPORTS")
missing = sorted(c for c in classes if c not in have)
out.write_text("# generated: entry points + entity factories + defined C++ save/restore symbols\n"
               + "".join(n + "\n" for n in exported))
print(f"server exports: {len(exported)} ({len(callbacks)} C++ code symbols, {len(classes)} entity classes scanned, {len(missing)} not compiled in)")
PY
if [[ $server_prx == 0 ]]; then
    python3 "$root/xash/tools/generate_static_library_tables.py" "$gen/helpers" \
        server="$gen/server_exports.txt" >/dev/null
    "${cc[@]}" -std=gnu11 "${module_cflags[@]}" -c "$gen/helpers/link_helper_server.c" \
        -o "$build/obj/server/link_helper_server.o"
    build_module server "$build/server.stage1.o" "$build/obj/server/link_helper_server.o"
fi

# Export list = fixed names that the compiled relocatable defines (+ scanned entity classes for the server).
export_intersect() {
    local name=$1 fixed=$2 stage=$3 out=$4
    "$llvm_nm" -g --defined-only "$stage" | awk '$2 ~ /^[TtWw]$/ {print $3}' | sort -u > "$build/$name.defined"
    python3 - "$fixed" "$build/$name.defined" "$out" "$name" <<'PY'
import pathlib, sys
fixed, defined, out, name = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3]), sys.argv[4]
names = [l.split("#", 1)[0].strip() for l in fixed.read_text().splitlines()]
names = [n for n in names if n]
have = set(defined.read_text().split())
kept = [n for n in names if n in have]
out.write_text(f"# generated: {name} entry points defined by the module\n" + "".join(n + "\n" for n in kept))
print(f"{name} exports: {len(kept)} of {len(names)} listed names are defined")
PY
}

module_names=()
if [[ $server_prx == 0 ]]; then
    module_names=(server)
fi
if [[ $filesystem_prx == 0 ]]; then
    module_names=(filesystem_stdio "${module_names[@]}")
fi
if [[ $mode == client ]]; then
    llvm_nm=${LLVM_NM:-$(command -v llvm-nm-18 || command -v llvm-nm)}
    cxx_flags=("${module_cflags[@]}" -fno-exceptions -fno-rtti)

    echo "== menu module (mainui)"
    mainui=$xash/3rdparty/mainui
    menu_includes=(-I"$mainui" -I"$mainui/miniutl" -I"$mainui/font" -I"$mainui/controls" -I"$mainui/menus"
        -I"$mainui/model" -I"$mainui/sdk_includes/common" -I"$mainui/sdk_includes/engine"
        -I"$mainui/sdk_includes/public" -I"$mainui/sdk_includes/pm_shared")
    menu_sources=$(find "$mainui" -maxdepth 1 -name '*.cpp'; find "$mainui/miniutl" "$mainui/font" "$mainui/menus" \
        "$mainui/model" "$mainui/controls" -name '*.cpp')
    menu_cxx_flags=("${cxx_flags[@]}")
    if [[ $menu_prx == 1 ]]; then
        menu_cxx_flags+=(-fPIC)
    fi
    printf '%s\n' "$menu_sources" | sort -u |
        compile_set "$build/menu.objects" "$build/obj/menu" -std=gnu++11 "${menu_cxx_flags[@]}" \
            -DMAINUI_USE_STB=1 -DMAINUI_USE_CUSTOM_FONT_RENDER=1 "-DSTDINT_H=<cstdint>" \
            -include keydefs.h "${menu_includes[@]}"
    mapfile -t menu_objects < "$build/menu.objects"
    "$ld_reloc" -r -o "$build/menu.stage1.o" "${menu_objects[@]}"
    export_intersect menu "$root/xash/exports/menu.txt" "$build/menu.stage1.o" "$gen/menu_exports.txt"

    echo "== client module (hlsdk-portable cl_dll)"
    python3 -B "$root/xash/tools/prepare_client_ammo.py" "$hlsdk/cl_dll/ammo.cpp" "$gen/ps5_ammo.cpp"
    client_defines=(-DCLIENT_DLL -DCLIENT_WEAPONS -Dstricmp=strcasecmp -Dstrnicmp=strncasecmp
        -D_snprintf=snprintf -D_vsnprintf=vsnprintf)
    client_includes=(-I"$hlsdk/cl_dll" -I"$hlsdk/dlls" -I"$hlsdk/common" -I"$hlsdk/engine" -I"$hlsdk/pm_shared"
        -I"$hlsdk/game_shared" -I"$hlsdk/public" -I"$hlsdk/utils/fake_vgui/include")
    client_cxx=$(find "$hlsdk/cl_dll" -name '*.cpp' ! -name 'GameStudioModelRenderer_Sample.cpp' \
            ! -name 'vgui_*.cpp' ! -name 'voice_status.cpp' ! -name 'ammo.cpp'
        echo "$gen/ps5_ammo.cpp"
        find "$hlsdk/game_shared" -maxdepth 1 -name '*.cpp' ! -name 'vgui_*.cpp' ! -name 'voice_*.cpp'
        for w in crossbow crowbar egon gauss glock handgrenade hornetgun mp5 python rpg satchel shotgun \
                 squeakgrenade tripmine; do echo "$hlsdk/dlls/$w.cpp"; done)
    cp "$gen/vcs_info_server.c" "$gen/vcs_info_client.c"
    client_c=$(find "$hlsdk/pm_shared" -name '*.c'; echo "$hlsdk/public/safe_snprintf.c"
        echo "$hlsdk/external/openbsd/strlcpy.c"; echo "$hlsdk/external/openbsd/strlcat.c"
        echo "$gen/vcs_info_client.c")
    client_cxx_flags=("${cxx_flags[@]}")
    client_c_flags=("${module_cflags[@]}")
    if [[ $client_prx == 1 ]]; then
        client_cxx_flags+=(-fPIC)
        client_c_flags+=(-fPIC)
    fi
    printf '%s\n' "$client_cxx" | sort -u |
        compile_set "$build/client-cxx.objects" "$build/obj/client" -std=gnu++11 "${client_cxx_flags[@]}" \
            "${client_defines[@]}" "${client_includes[@]}"
    printf '%s\n' "$client_c" | sort -u |
        compile_set "$build/client-c.objects" "$build/obj/client" -std=gnu11 "${client_c_flags[@]}" \
            "${client_defines[@]}" "${client_includes[@]}"
    mapfile -t client_objects < "$build/client-cxx.objects"
    mapfile -t client_c_objects < "$build/client-c.objects"
    "$ld_reloc" -r -o "$build/client.stage1.o" "${client_objects[@]}" "${client_c_objects[@]}"
    export_intersect client "$root/xash/exports/client.txt" "$build/client.stage1.o" "$gen/client_exports.txt"

    echo "== ref_null and ref_soft modules"
    ref_includes=(-I"$xash/ref/common" "${engine_includes[@]}")
    printf '%s\n' "$xash/ref/null/r_context.c" |
        compile_set "$build/ref_null.objects" "$build/obj/ref_null" -std=gnu11 "${module_cflags[@]}" \
            -DREF_DLL=1 "${engine_defines[@]}" "${ref_includes[@]}"
    mapfile -t ref_null_objects < "$build/ref_null.objects"
    "$ld_reloc" -r -o "$build/ref_null.stage1.o" "${ref_null_objects[@]}"
    export_intersect ref_null "$root/xash/exports/ref.txt" "$build/ref_null.stage1.o" "$gen/ref_null_exports.txt"
    ref_soft_sources=$(find "$xash/ref/soft" "$xash/ref/common" -maxdepth 1 -name '*.c')
    printf '%s\n' "$ref_soft_sources" | sort -u |
        compile_set "$build/ref_soft.objects" "$build/obj/ref_soft" -std=gnu11 "${module_cflags[@]}" \
            -DREF_DLL=1 "${engine_defines[@]}" -I"$xash/ref/soft" "${ref_includes[@]}"
    mapfile -t ref_soft_objects < "$build/ref_soft.objects"
    "$ld_reloc" -r -o "$build/ref_soft.stage1.o" "${ref_soft_objects[@]}"
    export_intersect ref_soft "$root/xash/exports/ref.txt" "$build/ref_soft.stage1.o" "$gen/ref_soft_exports.txt"

    client_table_specs=(ref_null="$gen/ref_null_exports.txt" ref_soft="$gen/ref_soft_exports.txt")
    client_static_modules=(ref_null ref_soft)
    if [[ $client_prx == 0 ]]; then
        client_table_specs=(client="$gen/client_exports.txt" "${client_table_specs[@]}")
        client_static_modules=(client "${client_static_modules[@]}")
    fi
    if [[ $menu_prx == 0 ]]; then
        client_table_specs=(menu="$gen/menu_exports.txt" "${client_table_specs[@]}")
        client_static_modules=(menu "${client_static_modules[@]}")
    fi
    python3 "$root/xash/tools/generate_static_library_tables.py" "$gen/helpers" \
        "${client_table_specs[@]}" >/dev/null
    for m in "${client_static_modules[@]}"; do
        "${cc[@]}" -std=gnu11 "${module_cflags[@]}" -c "$gen/helpers/link_helper_$m.c" -o "$build/obj/link_helper_$m.o"
        build_module "$m" "$build/$m.stage1.o" "$build/obj/link_helper_$m.o"
    done
    module_names=("${client_static_modules[@]}")
    if [[ $server_prx == 0 ]]; then
        module_names=(server "${module_names[@]}")
    fi
    if [[ $filesystem_prx == 0 ]]; then
        module_names=(filesystem_stdio "${module_names[@]}")
    fi
fi
module_objects=()
for m in "${module_names[@]}"; do module_objects+=("$build/$m.o"); done

if [[ $filesystem_prx == 1 ]]; then
    echo "== Phase 6 filesystem_stdio PRX"
    mkdir -p "$build/prx/filesystem"
    fs_prx_support=(
        "$xash/public/crtlib.c"
        "$xash/public/crclib.c"
        "$xash/public/miniz.c"
        "$xash/public/build_vcs.c"
        "$xash/3rdparty/library_suffix/src/library_suffix.c"
        "$root/xash/platform_ps5/fs_ps5.c"
        "$root/xash/platform_ps5/filesystem_prx_module.c"
    )
    printf '%s\n' "${fs_prx_support[@]}" |
        compile_set "$build/filesystem-prx-support.objects" "$build/prx/filesystem" -std=gnu11 \
            "${cflags[@]}" -fPIC -DPS5_FILESYSTEM_PRX_BUILD=1 \
            "${engine_defines[@]}" "${engine_includes[@]}"
    mapfile -t fs_prx_support_objects < "$build/filesystem-prx-support.objects"
    "${cc[@]}" -std=c++20 -O2 -fno-exceptions -fno-rtti -fPIC \
        -ffunction-sections -fdata-sections -c "$native/app_cpp_runtime.cpp" \
        -o "$build/prx/filesystem/app_cpp_runtime.o"
    "$lld" --shared -Bsymbolic -T "$native/ps5-pie.ld" --eh-frame-hdr \
        --version-script "$root/xash/platform_ps5/filesystem_prx_exports.map" \
        -soname filesystem_stdio.prx -o "$build/prx/filesystem_stdio.shared.elf" \
        "${fs_objects[@]}" "${fs_prx_support_objects[@]}" \
        "$build/prx/filesystem/app_cpp_runtime.o" --as-needed "$sdk"/target/lib/*.so
    "$tool" link --module --in "$build/prx/filesystem_stdio.shared.elf" \
        --out "$build/prx/filesystem_stdio.elf" --stub-dir "$sdk/target/lib" \
        --module-sdk 0x02000009 --companion-sdk 0x08050001 \
        --file-name filesystem_stdio.prx
    "$tool" self --sign --in "$build/prx/filesystem_stdio.elf" \
        --out "$dist/sce_module/filesystem_stdio.prx"
    "$tool" self --inspect --file "$dist/sce_module/filesystem_stdio.prx"
fi

if [[ $server_prx == 1 ]]; then
    echo "== Phase 6 HLSDK server PRX"
    mkdir -p "$build/prx/server"
    python3 "$root/xash/tools/generate_prx_descriptor.py" \
        --module server --exports "$gen/server_exports.txt" \
        --extra PS5_ServerPrxEngineTableMask \
        --extra PS5_ServerPrxEngineTableSmoke \
        --source "$gen/server_prx_descriptor.c" \
        --version-script "$gen/server_prx_exports.map"
    "${cc[@]}" -std=gnu11 "${cflags[@]}" -fPIC \
        -I"$root/xash/platform_ps5" -c "$gen/server_prx_descriptor.c" \
        -o "$build/prx/server/server_prx_descriptor.o"
    "${cc[@]}" -std=gnu++11 "${server_module_cflags[@]}" \
        -fno-exceptions -fno-rtti "${server_defines[@]}" "${server_includes[@]}" \
        -c "$root/xash/platform_ps5/server_prx_module.cpp" \
        -o "$build/prx/server/server_prx_module.o"
    "${cc[@]}" -std=c++20 -O2 -fno-exceptions -fno-rtti -fPIC \
        -ffunction-sections -fdata-sections -c "$native/app_cpp_runtime.cpp" \
        -o "$build/prx/server/app_cpp_runtime.o"
    "$lld" --shared -Bsymbolic -T "$native/ps5-pie.ld" --eh-frame-hdr \
        --version-script "$gen/server_prx_exports.map" \
        -soname server.prx -o "$build/prx/server.shared.elf" \
        "$build/server.stage1.o" "$build/prx/server/server_prx_descriptor.o" \
        "$build/prx/server/server_prx_module.o" \
        "$build/prx/server/app_cpp_runtime.o" --as-needed "$sdk"/target/lib/*.so
    "$tool" link --module --in "$build/prx/server.shared.elf" \
        --out "$build/prx/server.elf" --stub-dir "$sdk/target/lib" \
        --module-sdk 0x02000009 --companion-sdk 0x08050001 \
        --file-name server.prx
    "$tool" self --sign --in "$build/prx/server.elf" \
        --out "$dist/sce_module/server.prx"
    "$tool" self --inspect --file "$dist/sce_module/server.prx"
fi

if [[ $menu_prx == 1 ]]; then
    echo "== Phase 6 mainui menu PRX"
    mkdir -p "$build/prx/menu"
    python3 "$root/xash/tools/generate_prx_descriptor.py" \
        --module menu --exports "$gen/menu_exports.txt" \
        --source "$gen/menu_prx_descriptor.c" \
        --version-script "$gen/menu_prx_exports.map"
    "${cc[@]}" -std=gnu11 "${cflags[@]}" -fPIC \
        -I"$root/xash/platform_ps5" -c "$gen/menu_prx_descriptor.c" \
        -o "$build/prx/menu/menu_prx_descriptor.o"
    "${cc[@]}" -std=c++20 -O2 -fno-exceptions -fno-rtti -fPIC \
        -ffunction-sections -fdata-sections -c "$native/app_cpp_runtime.cpp" \
        -o "$build/prx/menu/app_cpp_runtime.o"
    "$lld" --shared -Bsymbolic -T "$native/ps5-pie.ld" --eh-frame-hdr \
        --version-script "$gen/menu_prx_exports.map" \
        -soname menu.prx -o "$build/prx/menu.shared.elf" \
        "$build/menu.stage1.o" "$build/prx/menu/menu_prx_descriptor.o" \
        "$build/prx/menu/app_cpp_runtime.o" --as-needed "$sdk"/target/lib/*.so
    "$tool" link --module --in "$build/prx/menu.shared.elf" \
        --out "$build/prx/menu.elf" --stub-dir "$sdk/target/lib" \
        --module-sdk 0x02000009 --companion-sdk 0x08050001 \
        --file-name menu.prx
    "$tool" self --sign --in "$build/prx/menu.elf" \
        --out "$dist/sce_module/menu.prx"
    "$tool" self --inspect --file "$dist/sce_module/menu.prx"
fi

if [[ $client_prx == 1 ]]; then
    echo "== Phase 6 GoldSrc client PRX"
    mkdir -p "$build/prx/client"
    python3 "$root/xash/tools/generate_prx_descriptor.py" \
        --module client --exports "$gen/client_exports.txt" \
        --extra PS5_ClientPrxEngineTableMask \
        --extra PS5_ClientPrxEngineTableSmoke \
        --source "$gen/client_prx_descriptor.c" \
        --version-script "$gen/client_prx_exports.map"
    "${cc[@]}" -std=gnu11 "${cflags[@]}" -fPIC \
        -I"$root/xash/platform_ps5" -c "$gen/client_prx_descriptor.c" \
        -o "$build/prx/client/client_prx_descriptor.o"
    "${cc[@]}" -std=gnu++11 "${client_cxx_flags[@]}" \
        "${client_defines[@]}" "${client_includes[@]}" \
        -c "$root/xash/platform_ps5/client_prx_module.cpp" \
        -o "$build/prx/client/client_prx_module.o"
    "${cc[@]}" -std=c++20 -O2 -fno-exceptions -fno-rtti -fPIC \
        -ffunction-sections -fdata-sections -c "$native/app_cpp_runtime.cpp" \
        -o "$build/prx/client/app_cpp_runtime.o"
    "$lld" --shared -Bsymbolic -T "$native/ps5-pie.ld" --eh-frame-hdr \
        --version-script "$gen/client_prx_exports.map" \
        -soname client.prx -o "$build/prx/client.shared.elf" \
        "$build/client.stage1.o" "$build/prx/client/client_prx_descriptor.o" \
        "$build/prx/client/client_prx_module.o" \
        "$build/prx/client/app_cpp_runtime.o" --as-needed "$sdk"/target/lib/*.so
    "$tool" link --module --in "$build/prx/client.shared.elf" \
        --out "$build/prx/client.elf" --stub-dir "$sdk/target/lib" \
        --module-sdk 0x02000009 --companion-sdk 0x08050001 \
        --file-name client.prx
    "$tool" self --sign --in "$build/prx/client.elf" \
        --out "$dist/sce_module/client.prx"
    "$tool" self --inspect --file "$dist/sce_module/client.prx"
fi

if [[ $ref_agc_prx == 1 ]]; then
    echo "== Phase 6 native AGC renderer PRX"
    ref_bundle="$root/build/bsp/map.ps5bsp"
    ref_studio_bundle="$root/build/studio/model.ps5mdl"
    for required in "$ref_bundle" "$ref_studio_bundle" \
        "$root/build/generated/goldsrc_shader_assets.S" \
        "$root/build/generated/goldsrc_shader_catalog_generated.h" \
        "$root/build/generated/pipeline_permutations.h"; do
        [[ -s $required ]] || {
            echo "XASH_REF_AGC_PRX=1 requires generated Phase 4 asset $required" >&2
            exit 2
        }
    done
    mkdir -p "$build/prx/ref_agc/import-stubs"
    python3 "$root/tools/generate_bsp_build_metadata.py" \
        --bundle "$ref_bundle" \
        --output "$root/build/generated/bsp_build_metadata.h"
    python3 "$root/tools/generate_studio_build_metadata.py" \
        --bundle "$ref_studio_bundle" \
        --output "$root/build/generated/studio_build_metadata.h"
    python3 "$root/xash/tools/generate_prx_descriptor.py" \
        --module ref_agc --exports "$root/xash/exports/ref.txt" \
        --extra PS5_RefAgcPrxRuntimeState \
        --extra PS5_RefAgcPrxRuntimeResult \
        --extra PS5_RefAgcPrxTeardownResult \
        --extra PS5_RefAgcPrxRuntimeFrames \
        --extra PS5_RefAgcPrxFrameHash \
        --extra PS5_RefAgcPrxBrightPixels \
        --extra PS5_RefAgcPrxBeginCalls \
        --extra PS5_RefAgcPrxSceneCalls \
        --extra PS5_RefAgcPrxEndCalls \
        --extra PS5_RefAgcPrxNewMapCalls \
        --extra PS5_RefAgcPrxEngineTableMask \
        --extra PS5_RefAgcPrxLiveFrames \
        --extra PS5_RefAgcPrxLiveViewFrames \
        --extra PS5_RefAgcPrxLiveViewHash \
        --extra PS5_RefAgcPrxLiveViewChanges \
        --extra PS5_RefAgcPrxLiveMapSerial \
        --extra PS5_RefAgcPrxLiveWorldSurfaces \
        --extra PS5_RefAgcPrxLiveEntityPeak \
        --extra PS5_RefAgcPrxLive2DPeak \
        --extra PS5_RefAgcPrxLiveDroppedEntities \
        --extra PS5_RefAgcPrxLiveDropped2D \
        --extra PS5_RefAgcPrxConsumedFrames \
        --extra PS5_RefAgcPrxConsumedSerial \
        --extra PS5_RefAgcPrxConsumedViewFrames \
        --extra PS5_RefAgcPrxConsumedCameraHash \
        --extra PS5_RefAgcPrxConsumedCameraChanges \
        --extra PS5_RefAgcPrxTextureRevision \
        --extra PS5_RefAgcPrxTextureCreates \
        --extra PS5_RefAgcPrxTextureUpdates \
        --extra PS5_RefAgcPrxTextureFrees \
        --extra PS5_RefAgcPrxTexturePeakBytes \
        --extra PS5_RefAgcPrxTextureHandles \
        --extra PS5_RefAgcPrxTexturePeakActive \
        --extra PS5_RefAgcPrxWorldTextureRefs \
        --extra PS5_RefAgcPrxWorldTexturesResolved \
        --source "$gen/ref_agc_prx_descriptor.c" \
        --version-script "$gen/ref_agc_prx_exports.map"
    ref_agc_defines=(
        -Dmain=ps5_ref_agc_native_main -DPS5_REF_AGC_MODULE=1
        -DPS5_REF_AGC_LIVE_PHASE7=1
        -DPS5_REF_AGC_SAMPLING_PROBE=$sampling_probe
        -DPS5_REF_AGC_STUDIO_AB=$studio_ab
        -DPS5_REF_AGC_TEXTURE_MEMORY_PROBE=$texture_memory_probe
        -DPS5_REF_AGC_HUD_TRACE=$hud_trace
        -DPS5_TEXTURE_MIB=$texture_mib
        -DPS5_TEXTURE_RESERVE_MIB=$texture_reserve_mib
        -DPS5_TEXTURE_AUTO_PERCENT=$texture_auto_percent
        -DPS5_XASH_PHASE7_MENU_GATE=$phase7_menu_gate
        -DPS5_BSP_VIEWER=1 -DPS5_BSP_NOCLIP=1 -DPS5_BSP_TEXTURED=1
        -DPS5_RESOURCE_FOUNDATION=1 -DPS5_TEXTURE_PATH=1
        -DPS5_GOLDSRC_PHASE4=1
    )
    ref_agc_includes=(
        -I"$root/include" -I"$root/src" -I"$root/native"
        -I"$root/native/ps5log" -I"$root/build/generated"
        -I"$root/xash/platform_ps5" -I"$xash/ref/common"
        "${engine_includes[@]}"
    )
    ref_agc_sources=(
        "$root/native/main.c" "$root/native/ps5_agc_native.c"
        "$root/xash/platform_ps5/ref_agc_module.c"
        "$root/xash/platform_ps5/studio_light_ps5.c"
        "$root/src/ref_agc_live_frame.c"
        "$root/src/ref_agc_live_2d.c"
        "$root/src/ref_agc_live_brush.c"
        "$root/src/ref_agc_live_studio.c"
        "$root/src/ref_agc_live_sprite.c"
        "$root/src/ref_agc_effects.c"
        "$xash/public/xash3d_mathlib.c"
        "$xash/public/matrixlib.c"
        "$root/src/ref_agc_lightmap_atlas.c"
        "$root/src/ref_agc_gpu_studio_cache.c"
        "$root/src/ref_agc_gpu_texture_cache.c"
        "$root/src/ref_agc_memory_budget.c"
        "$root/src/ps5_direct_memory.c"
        "$root/src/ref_agc_gpu_world_cache.c"
        "$root/src/ref_agc_gpu_world_draw.c"
        "$root/src/ref_agc_skybox.c"
        "$root/src/ref_agc_studio_store.c"
        "$root/src/ref_agc_texture_store.c"
        "$root/src/ref_agc_world_store.c"
        "$root/src/bsp_bundle.c" "$root/src/bsp_command_plan.c"
        "$root/src/bsp_flat_draw.c" "$root/src/bsp_dynamic_lightmap.c"
        "$root/src/bsp_alpha_test.c" "$root/src/bsp_sky.c"
        "$root/src/bsp_texture_accounting.c" "$root/src/goldsrc_pipeline_cache.c"
        "$root/src/goldsrc_render_state.c" "$root/src/bsp_noclip.c"
        "$root/src/bsp_textured_draw.c" "$root/src/bsp_flat_scene.c"
        "$root/src/bsp_runtime_plan.c" "$root/src/bsp_texture_descriptor.c"
        "$root/src/bsp_resource_frame.c" "$root/src/bsp_resource_draw.c"
        "$root/src/gears_animation.c" "$root/src/gears_draw_compose.c"
        "$root/src/gears_frame_runner.c" "$root/src/gears_frame_tracker.c"
        "$root/src/gears_mesh.c" "$root/src/gears_renderer.c"
        "$root/src/gears_rt_clear.c" "$root/src/gears_scene.c"
        "$root/src/gears_telemetry.c" "$root/src/ps5_agc_submit.c"
        "$root/src/ps5_agc_writer.c" "$root/src/ps5_color_target.c"
        "$root/src/ps5_depth_target.c" "$root/src/ps5_event_adapter.c"
        "$root/src/ps5_frame_completion.c" "$root/src/ps5_gpu_span.c"
        "$root/src/ps5_pipeline.c" "$root/src/ps5_goldsrc_render_state.c"
        "$root/src/ps5_shader_pipeline_slot.c" "$root/src/ps5_goldsrc_pipeline_runtime.c"
        "$root/src/ps5_viewport_scissor.c" "$root/src/goldsrc_state_matrix.c"
        "$root/src/goldsrc_2d.c" "$root/src/goldsrc_lightmap_lighting.c"
        "$root/src/goldsrc_sprite_particles.c" "$root/src/goldsrc_studio_bundle.c"
        "$root/src/goldsrc_studio_model.c" "$root/src/goldsrc_brush_entities.c"
        "$root/src/goldsrc_visibility.c" "$root/src/ps5_gpu_flip_timing.c"
        "$root/src/ps5_present.c" "$root/src/ps5_shader_header.c"
        "$root/src/ps5_submission.c" "$root/src/ps5_surface.c"
        "$root/src/ps5_videoout.c" "$root/src/ps5_cache_contract.c"
        "$root/src/ps5_gfx1013_descriptor.c" "$root/src/ps5_resource_pool.c"
        "$root/src/ps5_transient_ring.c" "$root/src/ps5_transient_table.c"
    )
    printf '%s\n' "${ref_agc_sources[@]}" |
        compile_set "$build/ref-agc.objects" "$build/prx/ref_agc/obj" -std=gnu11 \
            -O2 -w -fPIC "${ref_agc_defines[@]}" "${ref_agc_includes[@]}"
    mapfile -t ref_agc_objects < "$build/ref-agc.objects"
    "${cc[@]}" -std=gnu11 -O2 -w -fPIC "${ref_agc_includes[@]}" \
        -c "$gen/ref_agc_prx_descriptor.c" \
        -o "$build/prx/ref_agc/ref_agc_prx_descriptor.o"
    "${cc[@]}" -std=gnu11 -O2 -w -fPIC "${ref_agc_includes[@]}" \
        -include "$root/native/ps5log/ps5log_ps5_net.h" \
        -c "$root/native/ps5log/ps5log.c" -o "$build/prx/ref_agc/ps5log.o"
    "${cc[@]}" -std=gnu11 -O2 -w -fPIC "${ref_agc_includes[@]}" \
        -c "$root/native/ps5log/ps5log_ps5_net.c" \
        -o "$build/prx/ref_agc/ps5log_ps5_net.o"
    "${cc[@]}" -fPIC -c "$root/native/shader_assets.S" \
        -o "$build/prx/ref_agc/shader_assets.o"
    "${cc[@]}" -fPIC -c "$root/build/generated/goldsrc_shader_assets.S" \
        -o "$build/prx/ref_agc/goldsrc_shader_assets.o"
    "${cc[@]}" -std=c11 -O2 -fPIC -I"$root/include" \
        -c "$root/native/stubs/libSceAgc.c" \
        -o "$build/prx/ref_agc/agc-stub.o"
    "$lld" --shared -soname libSceAgc.prx \
        -o "$build/prx/ref_agc/import-stubs/libSceAgc.so" \
        "$build/prx/ref_agc/agc-stub.o"
    "${cc[@]}" -std=c11 -O2 -fPIC -I"$root/include" \
        -c "$root/native/stubs/libSceAgcDriver.c" \
        -o "$build/prx/ref_agc/agc-driver-stub.o"
    "$lld" --shared -soname libSceAgcDriver.prx \
        -o "$build/prx/ref_agc/import-stubs/libSceAgcDriver.so" \
        "$build/prx/ref_agc/agc-driver-stub.o"
    "${cc[@]}" -std=c++20 -O2 -fno-exceptions -fno-rtti -fPIC \
        -ffunction-sections -fdata-sections -c "$native/app_cpp_runtime.cpp" \
        -o "$build/prx/ref_agc/app_cpp_runtime.o"
    "$lld" --shared -Bsymbolic -T "$native/ps5-pie.ld" --eh-frame-hdr \
        --version-script "$gen/ref_agc_prx_exports.map" \
        -soname ref_agc.prx -o "$build/prx/ref_agc.shared.elf" \
        "${ref_agc_objects[@]}" \
        "$build/prx/ref_agc/ref_agc_prx_descriptor.o" \
        "$build/prx/ref_agc/ps5log.o" "$build/prx/ref_agc/ps5log_ps5_net.o" \
        "$build/prx/ref_agc/shader_assets.o" \
        "$build/prx/ref_agc/goldsrc_shader_assets.o" \
        "$build/prx/ref_agc/app_cpp_runtime.o" \
        --as-needed "$sdk"/target/lib/*.so \
        "$build/prx/ref_agc/import-stubs/libSceAgc.so" \
        "$build/prx/ref_agc/import-stubs/libSceAgcDriver.so"
    "$tool" link --module --in "$build/prx/ref_agc.shared.elf" \
        --out "$build/prx/ref_agc.elf" --stub-dir "$sdk/target/lib" \
        --stub "$build/prx/ref_agc/import-stubs/libSceAgc.so" \
        --stub "$build/prx/ref_agc/import-stubs/libSceAgcDriver.so" \
        --module-sdk 0x02000009 --companion-sdk 0x08050001 \
        --file-name ref_agc.prx
    "$tool" self --sign --in "$build/prx/ref_agc.elf" \
        --out "$dist/sce_module/ref_agc.prx"
    "$tool" self --inspect --file "$dist/sce_module/ref_agc.prx"
fi

if [[ $prx_gate == 1 ]]; then
    echo "== Phase 6 PRX loader probe module"
    mkdir -p "$build/prx"
    "${cc[@]}" -std=c11 -O2 -Wall -Wextra -Werror -fPIC \
        -ffunction-sections -fdata-sections -I"$root/xash/platform_ps5" \
        -c "$root/xash/platform_ps5/prx_probe_module.c" \
        -o "$build/prx/xash_prx_probe.o"
    "$lld" --shared -Bsymbolic -T "$native/ps5-pie.ld" --eh-frame-hdr \
        --version-script "$root/xash/platform_ps5/prx_probe_exports.map" \
        -soname xash_prx_probe.prx -o "$build/prx/xash_prx_probe.shared.elf" \
        "$build/prx/xash_prx_probe.o" --as-needed "$sdk"/target/lib/*.so
    "$tool" link --module --in "$build/prx/xash_prx_probe.shared.elf" \
        --out "$build/prx/xash_prx_probe.elf" --stub-dir "$sdk/target/lib" \
        --module-sdk 0x02000009 --companion-sdk 0x08050001 \
        --file-name xash_prx_probe.prx
    "$tool" self --sign --in "$build/prx/xash_prx_probe.elf" \
        --out "$dist/sce_module/xash_prx_probe.prx"
    "$tool" self --inspect --file "$dist/sce_module/xash_prx_probe.prx"
fi

echo "== link"
"${cc[@]}" -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -ffunction-sections -fdata-sections -c "$native/app_crt.cpp" \
    -o "$build/obj/app_crt.o"
"${cc[@]}" -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -ffunction-sections -fdata-sections -c "$native/app_cpp_runtime.cpp" \
    -o "$build/obj/app_cpp_runtime.o"
# Route every statically linked engine allocation through the direct-memory arena.
"$lld" -T "$native/ps5-pie.ld" --eh-frame-hdr \
    --wrap=malloc --wrap=free --wrap=realloc --wrap=calloc \
    --wrap=memalign --wrap=aligned_alloc --wrap=posix_memalign \
    --version-script "$root/xash/platform_ps5/app-symbols.map" -e _start \
    -o "$build/llvm-pie.elf" "$build/obj/app_crt.o" \
    "$build/obj/app_cpp_runtime.o" "${engine_objects[@]}" \
    "${module_objects[@]}" \
    --as-needed "$sdk"/target/lib/*.so
"$readelf" --dyn-syms "$build/llvm-pie.elf" > "$build/dynamic-symbols.txt"
if grep -qw strcasestr "$build/dynamic-symbols.txt"; then
    echo "PS5 engine must use Xash's Q_stristr fallback, not libc strcasestr" >&2
    exit 1
fi
for symbol in sceKernelReserveVirtualRange sceKernelAllocateMainDirectMemory \
    sceKernelMapDirectMemory sceKernelMunmap sceKernelReleaseDirectMemory; do
    if ! grep -qw "$symbol" "$build/dynamic-symbols.txt"; then
        echo "the direct-memory engine build did not retain $symbol" >&2
        exit 1
    fi
done
for symbol in _Znwm _Znam _ZdlPv _ZdaPv; do
    if grep -qw "$symbol" "$build/dynamic-symbols.txt"; then
        echo "C++ allocation operator escaped the direct-memory adapter: $symbol" >&2
        exit 1
    fi
done
if [[ $audio_gate == 1 || $audio == 1 ]]; then
    for symbol in sceAudioOutInit sceAudioOutOpen sceAudioOutSetVolume \
        sceAudioOutOutput sceAudioOutClose; do
        if ! grep -qw "$symbol" "$build/dynamic-symbols.txt"; then
            echo "the SceAudioOut build did not retain the $symbol dynamic import" >&2
            exit 1
        fi
    done
    # AudioOut2, Audio3d, NGS2, AJM and AudioIn are outside this gate.
    for symbol in sceAudioOut2Initialize sceAudio3dInitialize sceNgs2SystemCreate \
        sceAjmInitialize sceAudioInOpen sceAudiodecCreateDecoder; do
        if grep -qw "$symbol" "$build/dynamic-symbols.txt"; then
            echo "the SceAudioOut gate must not import $symbol" >&2
            exit 1
        fi
    done
fi
if [[ $audio_gate == 1 ]]; then
    strings "$build/llvm-pie.elf" > "$build/embedded-strings.txt"
    for marker in XASH_AUDIO_INIT XASH_AUDIO_RING_READY XASH_AUDIO_PATTERN \
        XASH_AUDIO_PROGRESS XASH_AUDIO_UNDERRUN XASH_AUDIO_SUMMARY \
        XASH_AUDIO_TEARDOWN XASH_AUDIO_COMPLETE; do
        if ! grep -qw "$marker" "$build/embedded-strings.txt"; then
            echo "XASH_AUDIO_GATE=1 did not retain marker $marker" >&2
            exit 1
        fi
    done
fi
if [[ $memory_gate == 1 ]]; then
    strings "$build/llvm-pie.elf" > "$build/embedded-strings.txt"
    for marker in XASH_MEMORY_BEGIN XASH_MEMORY_RESOURCE \
        XASH_MEMORY_COMPLETE XASH_MEMORY_SUMMARY XASH_MEMORY_TEARDOWN; do
        if ! grep -qw "$marker" "$build/embedded-strings.txt"; then
            echo "XASH_MEMORY_GATE=1 did not retain marker $marker" >&2
            exit 1
        fi
    done
fi
if [[ $thread_time_gate == 1 ]]; then
    for symbol in clock_gettime nanosleep usleep pthread_create pthread_join \
        pthread_detach pthread_self pthread_equal pthread_mutex_init \
        pthread_mutex_lock pthread_mutex_unlock pthread_mutex_destroy; do
        if ! grep -qw "$symbol" "$build/dynamic-symbols.txt"; then
            echo "XASH_THREAD_TIME_GATE=1 did not retain the $symbol dynamic import" >&2
            exit 1
        fi
    done
    strings "$build/llvm-pie.elf" > "$build/embedded-strings.txt"
    for marker in XASH_THREAD_TIME_BEGIN XASH_THREAD_RESULT XASH_CLOCK_RESULT \
        XASH_SLEEP_RESULT XASH_THREAD_TIME_COMPLETE; do
        if ! grep -qw "$marker" "$build/embedded-strings.txt"; then
            echo "XASH_THREAD_TIME_GATE=1 did not retain marker $marker" >&2
            exit 1
        fi
    done
fi
if [[ $libc_smoke == 1 ]]; then
    for symbol in strcasecmp strnlen strlcpy strlcat; do
        if ! grep -qw "$symbol" "$build/dynamic-symbols.txt"; then
            echo "XASH_LIBC_SMOKE=1 did not retain the $symbol dynamic import" >&2
            exit 1
        fi
    done
    strings "$build/llvm-pie.elf" > "$build/embedded-strings.txt"
    for marker in XASH_LIBC_SMOKE_BEGIN XASH_LIBC_SMOKE_END; do
        if ! grep -qw "$marker" "$build/embedded-strings.txt"; then
            echo "XASH_LIBC_SMOKE=1 did not retain marker $marker" >&2
            exit 1
        fi
    done
fi
if [[ $libc_shim_gate == 1 ]]; then
    for symbol in __assert getpwuid dladdr; do
        if grep -Eq "UND[[:space:]]+$symbol([@[:space:]]|$)" "$build/dynamic-symbols.txt"; then
            echo "XASH_LIBC_SHIM_GATE=1 leaked dynamic import $symbol" >&2
            exit 1
        fi
    done
    "$readelf" --syms "$build/llvm-pie.elf" > "$build/all-symbols.txt"
    for symbol in __assert getpwuid dladdr; do
        if ! grep -Eq "[[:space:]][0-9]+[[:space:]]+$symbol$" "$build/all-symbols.txt"; then
            echo "XASH_LIBC_SHIM_GATE=1 did not retain project definition $symbol" >&2
            exit 1
        fi
    done
    strings "$build/llvm-pie.elf" > "$build/embedded-strings.txt"
    for marker in XASH_LIBC_SHIM_BEGIN XASH_LIBC_SHIM_RESULT \
        XASH_LIBC_SHIM_END XASH_ASSERT_FAILURE; do
        if ! grep -qw "$marker" "$build/embedded-strings.txt"; then
            echo "XASH_LIBC_SHIM_GATE=1 did not retain marker $marker" >&2
            exit 1
        fi
    done
fi
if [[ $prx_gate == 1 ]]; then
    for symbol in sceKernelLoadStartModule sceKernelGetModuleInfo \
        sceKernelStopUnloadModule; do
        if ! grep -qw "$symbol" "$build/dynamic-symbols.txt"; then
            echo "XASH_PRX_GATE=1 did not retain dynamic import $symbol" >&2
            exit 1
        fi
    done
    strings "$build/llvm-pie.elf" > "$build/embedded-strings.txt"
    for marker in XASH_PRX_BEGIN XASH_PRX_LOAD XASH_PRX_RESOLVE \
        XASH_PRX_CALL XASH_PRX_UNLOAD XASH_PRX_COMPLETE; do
        if ! grep -qw "$marker" "$build/embedded-strings.txt"; then
            echo "XASH_PRX_GATE=1 did not retain marker $marker" >&2
            exit 1
        fi
    done
    [[ -s $dist/sce_module/xash_prx_probe.prx ]] || {
        echo "XASH_PRX_GATE=1 did not package xash_prx_probe.prx" >&2; exit 1; }
fi
if [[ $filesystem_prx == 1 ]]; then
    strings "$build/llvm-pie.elf" > "$build/embedded-strings.txt"
    for marker in XASH_FS_PRX_READY XASH_FS_PRX_STATE XASH_FS_PRX_COMPLETE; do
        if ! grep -qw "$marker" "$build/embedded-strings.txt"; then
            echo "XASH_FILESYSTEM_PRX=1 did not retain marker $marker" >&2
            exit 1
        fi
    done
    [[ -s $dist/sce_module/filesystem_stdio.prx ]] || {
        echo "XASH_FILESYSTEM_PRX=1 did not package filesystem_stdio.prx" >&2; exit 1; }
    strings "$build/prx/filesystem_stdio.shared.elf" > "$build/filesystem-prx-strings.txt"
    for marker in XASH_FS_PRX_PROBE GfX/PaLeTtE.LmP maps/c1a0.bsp; do
        if ! grep -Fq "$marker" "$build/filesystem-prx-strings.txt"; then
            echo "filesystem_stdio.prx did not retain workload marker $marker" >&2
            exit 1
        fi
    done
    "$readelf" --dyn-syms "$build/prx/filesystem_stdio.shared.elf" \
        > "$build/filesystem-prx-shared-symbols.txt"
    for symbol in GetFSAPI CreateInterface PS5_FilesystemPrxIndexCount \
        PS5_FilesystemPrxAllocatorContractResult \
        PS5_FilesystemPrxListingRefusedCount filesystem_stdio_prx_exports \
        module_start module_stop; do
        if ! grep -Eq "[[:space:]]$symbol$" "$build/filesystem-prx-shared-symbols.txt"; then
            echo "filesystem_stdio.prx did not export $symbol" >&2
            exit 1
        fi
    done
    for symbol in malloc free; do
        if ! grep -Eq "UND[[:space:]]+$symbol$" \
            "$build/filesystem-prx-shared-symbols.txt"; then
            echo "filesystem_stdio.prx lost its shared-libc $symbol contract" >&2
            exit 1
        fi
    done
    if grep -Eq '[[:space:]]__wrap_(malloc|free)$' \
        "$build/filesystem-prx-shared-symbols.txt"; then
        echo "filesystem_stdio.prx retained a private allocator wrapper" >&2
        exit 1
    fi
    "$readelf" --dyn-syms "$build/prx/filesystem_stdio.elf" \
        > "$build/filesystem-prx-dynamic-symbols.txt"
    if grep -Eq 'UND[[:space:]]+(GetFSAPI|CreateInterface|COM_|Q_|FS_)' \
        "$build/filesystem-prx-dynamic-symbols.txt"; then
        echo "filesystem_stdio.prx leaked an application-owned dynamic import" >&2
        exit 1
    fi
    python3 "$root/xash/tools/audit_dyn_imports.py" "$build/prx/filesystem_stdio.elf" \
        --readelf "$readelf" --evidence "$root/xash/ps5_import_evidence.json" \
        --stub-dir "$sdk/target/lib" \
        --output "$build/PS5_FILESYSTEM_PRX_DYNAMIC_IMPORT_AUDIT.md"
fi
if [[ $server_prx == 1 ]]; then
    [[ -s $dist/sce_module/server.prx ]] || {
        echo "XASH_SERVER_PRX=1 did not package server.prx" >&2; exit 1; }
    strings "$build/llvm-pie.elf" > "$build/embedded-strings.txt"
    for marker in XASH_SERVER_PRX_READY XASH_SERVER_PRX_STATE \
        XASH_SERVER_PRX_ABI XASH_SERVER_PRX_ABI_SMOKE XASH_SERVER_PRX_COMPLETE; do
        if ! grep -qw "$marker" "$build/embedded-strings.txt"; then
            echo "XASH_SERVER_PRX=1 did not retain marker $marker" >&2
            exit 1
        fi
    done
    "$readelf" --dyn-syms "$build/prx/server.shared.elf" \
        > "$build/server-prx-shared-symbols.txt"
    "$readelf" --symbols "$build/prx/server.shared.elf" \
        > "$build/server-prx-all-symbols.txt"
    for symbol in __init_array_start __init_array_end \
        __fini_array_start __fini_array_end; do
        if ! grep -Eq "[[:space:]]$symbol$" "$build/server-prx-all-symbols.txt"; then
            echo "server.prx did not retain lifecycle boundary $symbol" >&2
            exit 1
        fi
    done
    if ! "$readelf" --dynamic "$build/prx/server.shared.elf" |
        grep -Eq 'INIT_ARRAYSZ.*[1-9][0-9]* \(bytes\)'; then
        echo "server.prx did not retain a non-empty C++ initializer array" >&2
        exit 1
    fi
    for symbol in GiveFnptrsToDll GetEntityAPI GetEntityAPI2 \
        PS5_ServerPrxState PS5_ServerPrxExportCount PS5_ServerPrxEngineTableMask \
        PS5_ServerPrxEngineTableSmoke server_prx_exports \
        module_start module_stop; do
        if ! grep -Eq "[[:space:]]$symbol$" "$build/server-prx-shared-symbols.txt"; then
            echo "server.prx did not export $symbol" >&2
            exit 1
        fi
    done
    "$readelf" --dyn-syms "$build/prx/server.elf" \
        > "$build/server-prx-dynamic-symbols.txt"
    if grep -Eq 'UND[[:space:]]+(GiveFnptrsToDll|GetEntityAPI|GetEntityAPI2|PS5_ServerPrx)' \
        "$build/server-prx-dynamic-symbols.txt"; then
        echo "server.prx leaked an application-owned dynamic import" >&2
        exit 1
    fi
    python3 "$root/xash/tools/audit_dyn_imports.py" "$build/prx/server.elf" \
        --readelf "$readelf" --evidence "$root/xash/ps5_import_evidence.json" \
        --stub-dir "$sdk/target/lib" \
        --output "$build/PS5_SERVER_PRX_DYNAMIC_IMPORT_AUDIT.md"
fi
if [[ $menu_prx == 1 ]]; then
    [[ -s $dist/sce_module/menu.prx ]] || {
        echo "XASH_MENU_PRX=1 did not package menu.prx" >&2; exit 1; }
    strings "$build/llvm-pie.elf" > "$build/embedded-strings.txt"
    for marker in XASH_MENU_PRX_READY XASH_MENU_PRX_API XASH_MENU_PRX_EXT_API \
        XASH_MENU_PRX_INIT XASH_MENU_PRX_ACTIVE XASH_MENU_PRX_REDRAW \
        XASH_MENU_PRX_SHUTDOWN XASH_MENU_PRX_STATE XASH_MENU_PRX_COMPLETE; do
        if ! grep -qw "$marker" "$build/embedded-strings.txt"; then
            echo "XASH_MENU_PRX=1 did not retain marker $marker" >&2
            exit 1
        fi
    done
    "$readelf" --dyn-syms "$build/prx/menu.shared.elf" \
        > "$build/menu-prx-shared-symbols.txt"
    "$readelf" --symbols "$build/prx/menu.shared.elf" \
        > "$build/menu-prx-all-symbols.txt"
    for symbol in __init_array_start __init_array_end \
        __fini_array_start __fini_array_end; do
        if ! grep -Eq "[[:space:]]$symbol$" "$build/menu-prx-all-symbols.txt"; then
            echo "menu.prx did not retain lifecycle boundary $symbol" >&2
            exit 1
        fi
    done
    if ! "$readelf" --dynamic "$build/prx/menu.shared.elf" |
        grep -Eq 'INIT_ARRAYSZ.*[1-9][0-9]* \(bytes\)'; then
        echo "menu.prx did not retain a non-empty C++ initializer array" >&2
        exit 1
    fi
    for symbol in GetMenuAPI GetExtAPI PS5_MenuPrxState \
        PS5_MenuPrxExportCount menu_prx_exports module_start module_stop; do
        if ! grep -Eq "[[:space:]]$symbol$" "$build/menu-prx-shared-symbols.txt"; then
            echo "menu.prx did not export $symbol" >&2
            exit 1
        fi
    done
    "$readelf" --dyn-syms "$build/prx/menu.elf" \
        > "$build/menu-prx-dynamic-symbols.txt"
    if grep -Eq 'UND[[:space:]]+(GetMenuAPI|GetExtAPI|PS5_MenuPrx)' \
        "$build/menu-prx-dynamic-symbols.txt"; then
        echo "menu.prx leaked an application-owned dynamic import" >&2
        exit 1
    fi
    python3 "$root/xash/tools/audit_dyn_imports.py" "$build/prx/menu.elf" \
        --readelf "$readelf" --evidence "$root/xash/ps5_import_evidence.json" \
        --stub-dir "$sdk/target/lib" \
        --output "$build/PS5_MENU_PRX_DYNAMIC_IMPORT_AUDIT.md"
fi
if [[ $client_prx == 1 ]]; then
    [[ -s $dist/sce_module/client.prx ]] || {
        echo "XASH_CLIENT_PRX=1 did not package client.prx" >&2; exit 1; }
    strings "$build/llvm-pie.elf" > "$build/embedded-strings.txt"
    for marker in XASH_CLIENT_PRX_READY XASH_CLIENT_PRX_API \
        XASH_CLIENT_PRX_ABI_SMOKE XASH_CLIENT_PRX_INIT XASH_CLIENT_PRX_VID_INIT \
        XASH_CLIENT_PRX_FRAME XASH_CLIENT_PRX_REDRAW XASH_CLIENT_PRX_SHUTDOWN \
        XASH_CLIENT_PRX_STATE XASH_CLIENT_PRX_COMPLETE; do
        if ! grep -qw "$marker" "$build/embedded-strings.txt"; then
            echo "XASH_CLIENT_PRX=1 did not retain marker $marker" >&2
            exit 1
        fi
    done
    "$readelf" --dyn-syms "$build/prx/client.shared.elf" \
        > "$build/client-prx-shared-symbols.txt"
    "$readelf" --symbols "$build/prx/client.shared.elf" \
        > "$build/client-prx-all-symbols.txt"
    for symbol in __init_array_start __init_array_end \
        __fini_array_start __fini_array_end; do
        if ! grep -Eq "[[:space:]]$symbol$" "$build/client-prx-all-symbols.txt"; then
            echo "client.prx did not retain lifecycle boundary $symbol" >&2
            exit 1
        fi
    done
    if ! "$readelf" --dynamic "$build/prx/client.shared.elf" |
        grep -Eq 'INIT_ARRAYSZ.*[1-9][0-9]* \(bytes\)'; then
        echo "client.prx did not retain a non-empty C++ initializer array" >&2
        exit 1
    fi
    for symbol in Initialize HUD_Init HUD_VidInit HUD_Frame HUD_Redraw HUD_Shutdown \
        PS5_ClientPrxEngineTableMask PS5_ClientPrxEngineTableSmoke \
        PS5_ClientPrxState PS5_ClientPrxExportCount client_prx_exports \
        module_start module_stop; do
        if ! grep -Eq "[[:space:]]$symbol$" "$build/client-prx-shared-symbols.txt"; then
            echo "client.prx did not export $symbol" >&2
            exit 1
        fi
    done
    "$readelf" --dyn-syms "$build/prx/client.elf" \
        > "$build/client-prx-dynamic-symbols.txt"
    if grep -Eq 'UND[[:space:]]+(Initialize|HUD_Init|HUD_VidInit|HUD_Frame|HUD_Redraw|HUD_Shutdown|PS5_ClientPrx)' \
        "$build/client-prx-dynamic-symbols.txt"; then
        echo "client.prx leaked an application-owned dynamic import" >&2
        exit 1
    fi
    python3 "$root/xash/tools/audit_dyn_imports.py" "$build/prx/client.elf" \
        --readelf "$readelf" --evidence "$root/xash/ps5_import_evidence.json" \
        --stub-dir "$sdk/target/lib" \
        --output "$build/PS5_CLIENT_PRX_DYNAMIC_IMPORT_AUDIT.md"
fi
if [[ $ref_agc_prx == 1 ]]; then
    [[ -s $dist/sce_module/ref_agc.prx ]] || {
        echo "XASH_REF_AGC_PRX=1 did not package ref_agc.prx" >&2; exit 1; }
    strings "$build/llvm-pie.elf" > "$build/embedded-strings.txt"
    for marker in XASH_REF_AGC_PRX_READY XASH_REF_AGC_PRX_STATE \
        XASH_REF_AGC_PRX_COMPLETE; do
        if ! grep -qw "$marker" "$build/embedded-strings.txt"; then
            echo "XASH_REF_AGC_PRX=1 did not retain marker $marker" >&2
            exit 1
        fi
    done
    "$readelf" --dyn-syms "$build/prx/ref_agc.shared.elf" \
        > "$build/ref-agc-prx-shared-symbols.txt"
    for symbol in GetRefAPI PS5_RefAgcPrxRuntimeState \
        PS5_RefAgcPrxRuntimeResult PS5_RefAgcPrxTeardownResult \
        PS5_RefAgcPrxRuntimeFrames PS5_RefAgcPrxFrameHash \
        PS5_RefAgcPrxBrightPixels PS5_RefAgcPrxBeginCalls \
        PS5_RefAgcPrxSceneCalls PS5_RefAgcPrxEndCalls \
        PS5_RefAgcPrxNewMapCalls PS5_RefAgcPrxEngineTableMask \
        PS5_RefAgcPrxLiveFrames PS5_RefAgcPrxLiveViewFrames \
        PS5_RefAgcPrxLiveViewHash PS5_RefAgcPrxLiveViewChanges \
        PS5_RefAgcPrxLiveMapSerial PS5_RefAgcPrxLiveWorldSurfaces \
        PS5_RefAgcPrxLiveEntityPeak PS5_RefAgcPrxLive2DPeak \
        PS5_RefAgcPrxLiveDroppedEntities PS5_RefAgcPrxLiveDropped2D \
        PS5_RefAgcPrxConsumedFrames PS5_RefAgcPrxConsumedSerial \
        PS5_RefAgcPrxConsumedViewFrames PS5_RefAgcPrxConsumedCameraHash \
        PS5_RefAgcPrxConsumedCameraChanges \
        PS5_RefAgcPrxTextureRevision PS5_RefAgcPrxTextureCreates \
        PS5_RefAgcPrxTextureUpdates PS5_RefAgcPrxTextureFrees \
        PS5_RefAgcPrxTexturePeakBytes PS5_RefAgcPrxTextureHandles \
        PS5_RefAgcPrxTexturePeakActive PS5_RefAgcPrxWorldTextureRefs \
        PS5_RefAgcPrxWorldTexturesResolved \
        ref_agc_prx_exports module_start module_stop; do
        if ! grep -Eq "[[:space:]]$symbol$" "$build/ref-agc-prx-shared-symbols.txt"; then
            echo "ref_agc.prx did not export $symbol" >&2
            exit 1
        fi
    done
    for symbol in sceAgcInit sceAgcCreateShader sceAgcLinkShaders \
        sceAgcGetRegisterDefaults sceAgcDcbSetFlip; do
        if ! grep -qw "$symbol" "$build/ref-agc-prx-shared-symbols.txt"; then
            echo "ref_agc.prx did not retain native import $symbol" >&2
            exit 1
        fi
    done
    "$readelf" --dyn-syms "$build/prx/ref_agc.elf" \
        > "$build/ref-agc-prx-dynamic-symbols.txt"
    if grep -Eq 'UND[[:space:]]+(GetRefAPI|PS5_RefAgcPrx)' \
        "$build/ref-agc-prx-dynamic-symbols.txt"; then
        echo "ref_agc.prx leaked an application-owned dynamic import" >&2
        exit 1
    fi
    python3 "$root/xash/tools/audit_dyn_imports.py" "$build/prx/ref_agc.elf" \
        --readelf "$readelf" --evidence "$root/xash/ps5_import_evidence.json" \
        --stub-dir "$sdk/target/lib" \
        --output "$build/PS5_REF_AGC_PRX_DYNAMIC_IMPORT_AUDIT.md"
fi
python3 "$root/xash/tools/audit_dyn_imports.py" "$build/llvm-pie.elf" \
    --readelf "$readelf" --evidence "$root/xash/ps5_import_evidence.json" \
    --stub-dir "$sdk/target/lib" \
    --output "$build/PS5_DYNAMIC_IMPORT_AUDIT.md"
"$tool" link --in "$build/llvm-pie.elf" --out "$build/eboot.elf" \
    --stub-dir "$sdk/target/lib" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf
"$tool" self --sign --in "$build/eboot.elf" --out "$dist/eboot.bin" \
    --magic 0x1D3D154F
cp "$root/sce_sys/param.json" "$root/sce_sys/icon0.png" "$dist/sce_sys/"
cp "$foundation/runtime/libc.prx" "$dist/sce_module/libc.prx"
if [[ -f $dev_conf ]]; then
    cp "$dev_conf" "$dist/dev.conf"
fi
mkdir -p "$dist/xash3d"
if [[ -n $game_data ]]; then
    [[ -d $game_data/valve ]] || {
        echo "XASH_GAME_DATA must contain a valve/ directory" >&2; exit 2;
    }
    cp -R "$game_data/valve" "$dist/xash3d/valve"
    echo "staged game data: $(find "$dist/xash3d" -type f | wc -l) files (private, not published)"
fi
if [[ $ref_agc_prx == 1 ]]; then
    cp "$root/build/bsp/map.ps5bsp" "$dist/map.ps5bsp"
    cp "$root/build/studio/model.ps5mdl" "$dist/model.ps5mdl"
fi
# The packaged image cannot be enumerated on the console (getdents returns
# EINVAL under /app0); the PS5 backend lists it from this index instead.
python3 - "$dist/xash3d" <<'PY'
import os, sys
root = sys.argv[1]
lines = []
for base, dirs, files in os.walk(root):
    rel = os.path.relpath(base, root)
    rel = "" if rel == "." else rel.replace(os.sep, "/")
    for name in dirs:
        lines.append(f"{rel + '/' if rel else ''}{name}\td")
    for name in files:
        if base == root and name == ".dirindex":
            continue
        lines.append(f"{rel + '/' if rel else ''}{name}\tf")
lines.sort()
with open(os.path.join(root, ".dirindex"), "w", encoding="utf-8") as out:
    out.write("".join(line + "\n" for line in lines))
print(f"directory index: {len(lines)} entries")
PY
(cd "$root" && sha256sum "${build#"$root/"}/eboot.elf" "${dist#"$root/"}/eboot.bin") > "$build/SHA256SUMS"
"$tool" self --inspect --file "$dist/eboot.bin"
cat "$build/SHA256SUMS"
echo "mode=$mode ref=$ref_name engine=$engine_commit hlsdk=$hlsdk_commit map=$boot_map gate_seconds=$gate_seconds phase7_menu_gate=$phase7_menu_gate phase7_menu_seconds=$phase7_menu_seconds pad_gate=$pad_gate audio_gate=$audio_gate audio=$audio audio_user=$audio_user audio_gate_frames=$audio_gate_frames memory_gate=$memory_gate thread_time_gate=$thread_time_gate libc_shim_gate=$libc_shim_gate prx_gate=$prx_gate filesystem_prx=$filesystem_prx server_prx=$server_prx menu_prx=$menu_prx client_prx=$client_prx"
