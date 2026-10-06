#!/usr/bin/env bash
# Stage the x86 PE farms used by the Madeira-SE Wine loader.
#
# The source tree is a normal Wine multi-arch build.  Its Unix side is never
# copied into the app: only the i386 and x86_64 PE images are staged.  The
# script is intentionally deterministic and refuses duplicate basenames so a
# partial farm cannot silently replace a system DLL with another module.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_BUILD="${MADEIRA_SE_WINE_GUEST_BUILD:-$REPO_ROOT/build/madeira-se-wine-guest}"
DEST_ROOT="${MADEIRA_SE_GUEST_DEST:-$REPO_ROOT/app/Madeira}"

if [[ $# -gt 2 ]]; then
    echo "usage: $0 [wine-guest-build] [destination-root]" >&2
    exit 2
fi
if [[ $# -ge 1 ]]; then SOURCE_BUILD="$1"; fi
if [[ $# -ge 2 ]]; then DEST_ROOT="$2"; fi

if [[ ! -f "$SOURCE_BUILD/Makefile" ]]; then
    echo "error: Wine guest build is not configured: $SOURCE_BUILD" >&2
    exit 1
fi

staged=0
for arch in i386 x86_64; do
    source_dirs=()
    while IFS= read -r -d '' dir; do source_dirs+=("$dir"); done < <(
        find "$SOURCE_BUILD/dlls" "$SOURCE_BUILD/programs" \
            -type d -name "${arch}-windows" -print0 2>/dev/null | sort -z
    )
    if [[ ${#source_dirs[@]} -eq 0 ]]; then
        echo "error: no ${arch}-windows PE output was found in $SOURCE_BUILD" >&2
        exit 1
    fi

    destination="$DEST_ROOT/${arch}-windows"
    mkdir -p "$destination"
    # A stale file from a previous build is dangerous: it can mask a missing
    # module in the newly generated Makefile. Remove only generated PE files;
    # leave metadata and developer notes in the destination alone.
    while IFS= read -r -d '' old; do rm -f "$old"; done < <(
        find "$destination" -maxdepth 1 -type f \( \
            -iname '*.dll' -o -iname '*.exe' -o -iname '*.drv' -o \
            -iname '*.sys' -o -iname '*.cpl' -o -iname '*.ocx' -o \
            -iname '*.com' -o -iname '*.scr' \) -print0
    )

    # macOS still ships Bash 3, so keep the duplicate check in a plain array.
    # These farms are small enough that the O(n²) check is negligible and it
    # keeps the staging script usable on the same machines that build Madeira.
    seen_names=()
    seen_sources=()
    for source_dir in "${source_dirs[@]}"; do
        while IFS= read -r -d '' source; do
            base="$(basename "$source")"
            base_lower="$(printf '%s' "$base" | tr '[:upper:]' '[:lower:]')"
            case "$base_lower" in
                *.dll|*.exe|*.drv|*.sys|*.cpl|*.ocx|*.com|*.scr) ;;
                *) continue ;;
            esac
            key="$(printf '%s' "$base" | tr '[:upper:]' '[:lower:]')"
            for index in "${!seen_names[@]}"; do
                if [[ "${seen_names[$index]}" == "$key" ]]; then
                    echo "error: duplicate ${arch} PE basename: $base" >&2
                    echo "  ${seen_sources[$index]}" >&2
                    echo "  $source" >&2
                    exit 1
                fi
            done
            seen_names+=("$key")
            seen_sources+=("$source")
            cp "$source" "$destination/$base"
            staged=$((staged + 1))
        done < <(find "$source_dir" -maxdepth 1 -type f -print0 | sort -z)
    done
    echo "staged ${arch}: ${#seen_names[@]} PE files -> $destination"
done

echo "Madeira-SE guest PE farms staged: $staged files"
