#!/usr/bin/env bash
# Build the Xash3D dedicated engine boot title for PS5 (Phase 5 gate 1).
#
# Produces dist/engine-boot/<title>/ with a signed eboot.bin that boots the
# Xash3D FWGS engine in dedicated mode, statically linked with the
# filesystem_stdio module and the hlsdk-portable server, and reports through
# ps5log/1. No shaders, no renderer: this gate proves filesystem, memory,
# threads, time, sockets and the three libc shims from a real title.
#
# Environment:
#   PS5_NATIVE_FOUNDATION  boilerplate checkout (default .deps/, pinned)
#   PS5LOG_DEV_CONF        private dev.conf copied into the title (optional)
#   XASH_GAME_DATA         private directory holding valve/ (optional; staged
#                          under dist/.../xash3d, never committed)
#   XASH_BOOT_MAP          map executed after boot (default c1a0)
#   XASH_GATE_SECONDS      queue "quit" after N seconds (default 90; 0 = never)
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
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
pin=37dd53602bdead63936f718004555ba10154be48
url=https://github.com/mpereiraesaa/ps5-native-app-boilerplate.git
foundation=${PS5_NATIVE_FOUNDATION:-$root/.deps/ps5-native-app-boilerplate}
xash=$root/third_party/xash3d-fwgs
hlsdk=$root/third_party/hlsdk-portable
boot_map=${XASH_BOOT_MAP:-c1a0}
gate_seconds=${XASH_GATE_SECONDS:-90}
mode=${XASH_MODE:-dedicated}
fs_trace=${XASH_FS_TRACE:-0}
fs_trace_path=${XASH_FS_TRACE_PATH:-gfx/palette.lmp}
libc_smoke=${XASH_LIBC_SMOKE:-0}
pad_gate=${XASH_PAD_GATE:-0}
audio_gate=${XASH_AUDIO_GATE:-0}
audio_user=${XASH_AUDIO_USER:-system}
audio_gate_frames=${XASH_AUDIO_GATE_FRAMES:-0}
audio=${XASH_AUDIO:-0}
ref_name=${XASH_REF:-soft}
[[ $mode == dedicated || $mode == client ]] || { echo "XASH_MODE must be dedicated or client" >&2; exit 2; }
[[ $ref_name =~ ^[a-z0-9_]+$ ]] || { echo "XASH_REF must be a renderer short name" >&2; exit 2; }
[[ $fs_trace_path =~ ^[A-Za-z0-9_./-]+$ ]] || { echo "XASH_FS_TRACE_PATH contains unsafe characters" >&2; exit 2; }
[[ $libc_smoke == 0 || $libc_smoke == 1 ]] || { echo "XASH_LIBC_SMOKE must be 0 or 1" >&2; exit 2; }
[[ $pad_gate == 0 || $pad_gate == 1 ]] || { echo "XASH_PAD_GATE must be 0 or 1" >&2; exit 2; }
[[ $audio_gate == 0 || $audio_gate == 1 ]] || { echo "XASH_AUDIO_GATE must be 0 or 1" >&2; exit 2; }
[[ $audio == 0 || $audio == 1 ]] || { echo "XASH_AUDIO must be 0 or 1" >&2; exit 2; }
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
if [[ ! -x $tool ]]; then
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
HEADER
sed 's/@BZ_VERSION@/1.1.0-fwgs/' "$xash/3rdparty/bzip2/bzip2/bz_version.h.in" \
    > "$gen/bzip2/bz_version.h"
# generated_library_tables.h lists the module names (mode-dependent) and is
# compiled into lib_static.c; per-module export tables are generated later into
# $gen/helpers once each relocatable is known.
table_specs=(filesystem_stdio="$root/xash/exports/filesystem_stdio.txt" server="$root/xash/exports/server.txt")
if [[ $mode == client ]]; then
    table_specs+=(menu="$root/xash/exports/menu.txt" client="$root/xash/exports/client.txt"
        ref_null="$root/xash/exports/ref.txt" ref_soft="$root/xash/exports/ref.txt")
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
    echo "$xash/engine/platform/misc/lib_static.c"
    echo "$xash/3rdparty/library_suffix/src/library_suffix.c"
    echo "$root/xash/platform_ps5/sys_ps5.c"
    echo "$root/xash/platform_ps5/fs_ps5.c"
    echo "$root/xash/platform_ps5/mem_ps5.c"
	echo "$root/xash/platform_ps5/in_ps5.c"
    if [[ $audio_gate == 1 || $audio == 1 ]]; then
        echo "$root/xash/platform_ps5/audio_ps5.c"
        echo "$root/xash/platform_ps5/audio_pattern_ps5.c"
    fi
    if [[ $audio_gate == 1 ]]; then
        echo "$root/xash/platform_ps5/audio_gate_ps5.c"
    fi
    if [[ $mode == client ]]; then
        find "$xash/engine/client" -name '*.c'
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
if [[ $fs_trace == 1 ]]; then
    fs_sources=$(find "$xash/filesystem" -maxdepth 1 -name '*.c' \
        ! -name 'io.c' ! -name 'searchpath.c'; \
        echo "$gen/fs_trace/io.c"; echo "$gen/fs_trace/searchpath.c"; \
        echo "$gen/link_helper_filesystem_stdio.c")
else
    fs_sources=$(find "$xash/filesystem" -maxdepth 1 -name '*.c'; echo "$gen/link_helper_filesystem_stdio.c")
fi
printf '%s\n' "$fs_sources" | sort -u |
    compile_set "$build/filesystem.objects" "$build/obj/filesystem" -std=gnu11 "${module_cflags[@]}" \
        "${engine_defines[@]}" "${engine_includes[@]}"
mapfile -t fs_objects < "$build/filesystem.objects"
"${cc[@]}" -std=gnu++11 "${module_cflags[@]}" -fno-exceptions -fno-rtti \
    "${engine_defines[@]}" "${engine_includes[@]}" \
    -c "$xash/filesystem/VFileSystem009.cpp" -o "$build/obj/filesystem/VFileSystem009.o"
fs_objects+=("$build/obj/filesystem/VFileSystem009.o")
build_module filesystem_stdio "${fs_objects[@]}"

echo "== server module (hlsdk-portable)"
server_defines=(-DCLIENT_WEAPONS -DNO_VOICEGAMEMGR -Dstricmp=strcasecmp
    -Dstrnicmp=strncasecmp -D_snprintf=snprintf -D_vsnprintf=vsnprintf)
server_includes=(-I"$hlsdk/dlls" -I"$hlsdk/common" -I"$hlsdk/engine"
    -I"$hlsdk/pm_shared" -I"$hlsdk/game_shared" -I"$hlsdk/public")
server_cxx=$(find "$hlsdk/dlls" -name '*.cpp' \
    ! -name 'mpstubb.cpp' ! -name 'stats.cpp' ! -name 'Wxdebug.cpp')
server_c=$(find "$hlsdk/pm_shared" -name '*.c'; echo "$hlsdk/public/safe_snprintf.c"
    echo "$hlsdk/external/openbsd/strlcpy.c"; echo "$hlsdk/external/openbsd/strlcat.c"
    echo "$gen/vcs_info_server.c")
printf '%s\n' "$server_cxx" | sort -u |
    compile_set "$build/server-cxx.objects" "$build/obj/server" -std=gnu++11 "${module_cflags[@]}" \
        -fno-exceptions -fno-rtti "${server_defines[@]}" "${server_includes[@]}"
printf '%s\n' "$server_c" | sort -u |
    compile_set "$build/server-c.objects" "$build/obj/server" -std=gnu11 "${module_cflags[@]}" \
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
    "$gen/server_exports.txt" <<'PY'
import pathlib, re, sys
hlsdk, fixed, defined, out = (pathlib.Path(a) for a in sys.argv[1:5])
names = [l.split("#", 1)[0].strip() for l in fixed.read_text().splitlines()]
names = [n for n in names if n]
pattern = re.compile(r"LINK_ENTITY_TO_CLASS\s*\(\s*([A-Za-z0-9_]+)")
classes = set()
for folder in ("dlls", "game_shared"):
    for src in (hlsdk / folder).rglob("*.cpp"):
        classes.update(pattern.findall(src.read_text(encoding="utf-8", errors="replace")))
have = set(defined.read_text().split())
exported = names + sorted(c for c in classes if c in have and c not in names)
missing = sorted(c for c in classes if c not in have)
out.write_text("# generated: fixed entry points + LINK_ENTITY_TO_CLASS symbols defined by the module\n"
               + "".join(n + "\n" for n in exported))
print(f"server exports: {len(exported)} ({len(classes)} entity classes scanned, {len(missing)} not compiled in)")
PY
python3 "$root/xash/tools/generate_static_library_tables.py" "$gen/helpers" \
    server="$gen/server_exports.txt" >/dev/null
"${cc[@]}" -std=gnu11 "${module_cflags[@]}" -c "$gen/helpers/link_helper_server.c" \
    -o "$build/obj/server/link_helper_server.o"
build_module server "$build/server.stage1.o" "$build/obj/server/link_helper_server.o"

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

module_names=(filesystem_stdio server)
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
    printf '%s\n' "$menu_sources" | sort -u |
        compile_set "$build/menu.objects" "$build/obj/menu" -std=gnu++11 "${cxx_flags[@]}" \
            -DMAINUI_USE_STB=1 -DMAINUI_USE_CUSTOM_FONT_RENDER=1 "-DSTDINT_H=<cstdint>" \
            -include keydefs.h "${menu_includes[@]}"
    mapfile -t menu_objects < "$build/menu.objects"
    "$ld_reloc" -r -o "$build/menu.stage1.o" "${menu_objects[@]}"
    export_intersect menu "$root/xash/exports/menu.txt" "$build/menu.stage1.o" "$gen/menu_exports.txt"

    echo "== client module (hlsdk-portable cl_dll)"
    client_defines=(-DCLIENT_DLL -DCLIENT_WEAPONS -Dstricmp=strcasecmp -Dstrnicmp=strncasecmp
        -D_snprintf=snprintf -D_vsnprintf=vsnprintf)
    client_includes=(-I"$hlsdk/cl_dll" -I"$hlsdk/dlls" -I"$hlsdk/common" -I"$hlsdk/engine" -I"$hlsdk/pm_shared"
        -I"$hlsdk/game_shared" -I"$hlsdk/public" -I"$hlsdk/utils/fake_vgui/include")
    client_cxx=$(find "$hlsdk/cl_dll" -name '*.cpp' ! -name 'GameStudioModelRenderer_Sample.cpp' \
            ! -name 'vgui_*.cpp' ! -name 'voice_status.cpp'
        find "$hlsdk/game_shared" -maxdepth 1 -name '*.cpp' ! -name 'vgui_*.cpp' ! -name 'voice_*.cpp'
        for w in crossbow crowbar egon gauss glock handgrenade hornetgun mp5 python rpg satchel shotgun \
                 squeakgrenade tripmine; do echo "$hlsdk/dlls/$w.cpp"; done)
    cp "$gen/vcs_info_server.c" "$gen/vcs_info_client.c"
    client_c=$(find "$hlsdk/pm_shared" -name '*.c'; echo "$hlsdk/public/safe_snprintf.c"
        echo "$hlsdk/external/openbsd/strlcpy.c"; echo "$hlsdk/external/openbsd/strlcat.c"
        echo "$gen/vcs_info_client.c")
    printf '%s\n' "$client_cxx" | sort -u |
        compile_set "$build/client-cxx.objects" "$build/obj/client" -std=gnu++11 "${cxx_flags[@]}" \
            "${client_defines[@]}" "${client_includes[@]}"
    printf '%s\n' "$client_c" | sort -u |
        compile_set "$build/client-c.objects" "$build/obj/client" -std=gnu11 "${module_cflags[@]}" \
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

    python3 "$root/xash/tools/generate_static_library_tables.py" "$gen/helpers" \
        menu="$gen/menu_exports.txt" client="$gen/client_exports.txt" \
        ref_null="$gen/ref_null_exports.txt" ref_soft="$gen/ref_soft_exports.txt" >/dev/null
    for m in menu client ref_null ref_soft; do
        "${cc[@]}" -std=gnu11 "${module_cflags[@]}" -c "$gen/helpers/link_helper_$m.c" -o "$build/obj/link_helper_$m.o"
        build_module "$m" "$build/$m.stage1.o" "$build/obj/link_helper_$m.o"
    done
    module_names=(filesystem_stdio server menu client ref_null ref_soft)
fi
module_objects=()
for m in "${module_names[@]}"; do module_objects+=("$build/$m.o"); done

echo "== link"
"${cc[@]}" -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -ffunction-sections -fdata-sections -c "$native/app_crt.cpp" \
    -o "$build/obj/app_crt.o"
# Route the engine's large allocations to anonymous memory (xash/platform_ps5/mem_ps5.c).
"$lld" -T "$native/ps5-pie.ld" --eh-frame-hdr \
    --wrap=malloc --wrap=free --wrap=realloc --wrap=calloc \
    --version-script "$root/xash/platform_ps5/app-symbols.map" -e _start \
    -o "$build/llvm-pie.elf" "$build/obj/app_crt.o" "${engine_objects[@]}" \
    "${module_objects[@]}" \
    --as-needed "$sdk"/target/lib/*.so
"$readelf" --dyn-syms "$build/llvm-pie.elf" > "$build/dynamic-symbols.txt"
if grep -qw strcasestr "$build/dynamic-symbols.txt"; then
    echo "PS5 engine must use Xash's Q_stristr fallback, not libc strcasestr" >&2
    exit 1
fi
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
echo "mode=$mode ref=$ref_name engine=$engine_commit hlsdk=$hlsdk_commit map=$boot_map gate_seconds=$gate_seconds pad_gate=$pad_gate audio_gate=$audio_gate audio=$audio audio_user=$audio_user audio_gate_frames=$audio_gate_frames"
