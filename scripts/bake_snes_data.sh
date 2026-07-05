#!/usr/bin/env bash
# bake_snes_data.sh — convert GAMEDATA into SNES-linkable assets.
#
# Usage: bake_snes_data.sh <GAMEDATA/KEEN_N dir> <output dir> <episode 1|2|3>
#
# Mirrors scripts/bake_gba_data.sh: builds the host tools (staleness-
# checked), stages GAMEDATA, unlzexe's KEEN<n>.EXE (reusing the GBA host
# tool), then runs the SNES converters:
#   snes_gfx_host              tiles/font/bitmaps/sprites/screens/palettes/
#                              tileinfo (also LZ-decompresses EGALATCH/
#                              EGASPRIT in the stage when EGAHEAD says so)
#   snes_level_host            byte-identical levels + per-level tile sets
#   snes_preprocess_misc_host  text transforms + MZ-stripped exe_image.bin
#   snes_sound_host            PIT beep lists -> SPC pitch streams + BRR
#   snes_emit_data             data_ep<N>.asm + snes_data_gen.c/.h
#
# Everything lands in <output dir> (canonically build/snes/generated-ep<N>).
# snes/hdr.asm is copied into the output dir and the generated asm includes
# and incbins by absolute path, because wla-65816 resolves relative paths
# against the CWD only.
set -euo pipefail

in_dir="${1:?gamedata dir}"
out_dir="${2:?output dir}"
episode="${3:?episode (1|2|3)}"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"

case "${episode}" in
    1|2|3) ;;
    *) echo "bake_snes_data.sh: episode must be 1, 2 or 3" >&2; exit 1 ;;
esac

if [ ! -d "${in_dir}" ]; then
    echo "bake_snes_data.sh: input directory does not exist: ${in_dir}" >&2
    exit 1
fi

mkdir -p "${out_dir}"
staged_dir="${out_dir}/_staged_gamedata"
host_cc="${CC_HOST:-cc}"

# ---------------------------------------------------------------------
# Host tools (built once, staleness-checked like bake_gba_data.sh).
# ---------------------------------------------------------------------
build_tool() {
    # build_tool <output binary> <source...>
    local tool="$1"; shift
    local stale=0 src
    [ -x "${tool}" ] || stale=1
    for src in "$@"; do
        [ "${src}" -nt "${tool}" ] && stale=1
    done
    if [ "${stale}" = 1 ]; then
        echo "bake_snes_data.sh: building host tool ${tool}"
        "${host_cc}" -O2 -std=gnu99 -Wno-parentheses -I"${repo_root}/src" \
            "$@" -o "${tool}"
    fi
    return 0
}

unlzexe_tool="${out_dir}/gba_unlzexe_host"
build_tool "${unlzexe_tool}" \
    "${repo_root}/scripts/gba_unlzexe_host.c" \
    "${repo_root}/src/third_party/cgenius/fileio/compression/Cunlzexe.c"

gfx_tool="${out_dir}/snes_gfx_host"
build_tool "${gfx_tool}" \
    "${repo_root}/scripts/snes_gfx_host.c" \
    "${repo_root}/src/third_party/cgenius/fileio/lz.c"

level_tool="${out_dir}/snes_level_host"
build_tool "${level_tool}" "${repo_root}/scripts/snes_level_host.c"

misc_tool="${out_dir}/snes_preprocess_misc_host"
build_tool "${misc_tool}" "${repo_root}/scripts/snes_preprocess_misc_host.c"

sound_tool="${out_dir}/snes_sound_host"
build_tool "${sound_tool}" "${repo_root}/scripts/snes_sound_host.c"

emit_tool="${out_dir}/snes_emit_data"
build_tool "${emit_tool}" "${repo_root}/scripts/snes_emit_data.c"

# ---------------------------------------------------------------------
# Stage GAMEDATA (unlzexe the EXE on the way in).
# ---------------------------------------------------------------------
rm -rf "${staged_dir}"
mkdir -p "${staged_dir}"
for src_path in "${in_dir}"/*; do
    [ -f "${src_path}" ] || continue
    base=$(basename "${src_path}")
    upper=$(printf '%s' "${base}" | tr '[:lower:]' '[:upper:]')
    case "${upper}" in
        *\ *) continue ;;  # skip names with spaces (stray archive files)
        *.EXE)
            "${unlzexe_tool}" "${src_path}" "${staged_dir}/${upper}"
            ;;
        *)
            cp "${src_path}" "${staged_dir}/${upper}"
            ;;
    esac
done

ext="CK${episode}"

# Clear stale generated blobs so removed inputs can't linger.
rm -f "${out_dir}"/*.chr "${out_dir}"/*.map "${out_dir}"/*.bin \
      "${out_dir}"/*.tset "${out_dir}"/*.frag.c \
      "${out_dir}"/data_ep*.asm "${out_dir}"/snes_data_gen.[ch]

# ---------------------------------------------------------------------
# Converters. Order matters: preprocess writes exe_image.bin (consumed by
# snes_sound_host for ep2/3) and transforms the staged EXE's embedded
# texts before anything else snapshots it.
# ---------------------------------------------------------------------
"${misc_tool}"  "${staged_dir}" "${ext}" "${episode}" "${out_dir}"
"${gfx_tool}"   "${staged_dir}" "${ext}" "${episode}" "${out_dir}"
"${level_tool}" "${staged_dir}" "${ext}" "${episode}" "${out_dir}"
"${sound_tool}" "${staged_dir}" "${ext}" "${episode}" "${out_dir}"

# hdr.asm must be reachable from the generated asm (absolute include).
cp "${repo_root}/snes/hdr.asm" "${out_dir}/hdr.asm"

"${emit_tool}" "${out_dir}" "${episode}"

echo "bake_snes_data.sh: episode ${episode} baked into ${out_dir}"
