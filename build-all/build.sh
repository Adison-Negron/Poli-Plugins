#!/bin/bash
# build-all/build.sh -- build several Poli plugins in one go, then sit back.
# Run from WSL:   cd /mnt/c/MPC-PLUGINS/build-all && ./build.sh
#
#   ./build.sh                     asks which plugins (numbers, names or "all") and which drive, then builds them all
#   ./build.sh E all               no questions: every plugin, zips to E:\local-plugins\
#   ./build.sh E Poli-q PoliPlug   no questions: just those (folder names, any case)
#   ./build.sh none all            build only, copy nowhere
#   USB_DRIVE=E ./build.sh         the drive from the environment (still asks for the plugins)
#   ./build.sh E copy Poli-q       no building: copy the zips already in those plugins' dist\ folders to the stick
#                                  ("copy" alone asks which; "copy all" = every plugin that has a zip)
#
# Plugins are found by themselves: every folder next to this one (C:\MPC-PLUGINS\<name>) whose build.sh uses the shared
# drive picker (pick_drive / copy_to_stick from ../shared/usb_stick.sh). A new plugin shows up here as soon as its
# build.sh follows that pattern; nothing to edit in this file. To keep a folder out, put a file named .no-build-all in it.
#
# Each plugin is built with its own build.sh (skin, aarch64 build, tests, release zip, copy to the stick), one after
# another. If one fails the rest still build. Full logs: build-all/logs/<time>/<plugin>.log. At the end: a summary of
# what built, what was copied and what failed, with the time each took.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
source "$ROOT/shared/usb_stick.sh"

# ---- the plugins -----------------------------------------------------------------------------------------------------
PLUGINS=()
for d in "$ROOT"/*/; do
    d="${d%/}"; n="$(basename "$d")"
    [ "$n" = "build-all" ] && continue
    [ -f "$d/build.sh" ] && [ ! -f "$d/.no-build-all" ] && grep -q "pick_drive" "$d/build.sh" && PLUGINS+=("$n")
done
if [ ${#PLUGINS[@]} -eq 0 ]; then echo "ERROR: no plugin folders with a build.sh found in $ROOT"; exit 2; fi

find_plugin() {   # case-insensitive folder name -> the real name
    local want; want="$(echo "$1" | tr 'A-Z' 'a-z')"
    for p in "${PLUGINS[@]}"; do [ "$(echo "$p" | tr 'A-Z' 'a-z')" = "$want" ] && { echo "$p"; return 0; }; done
    return 1
}

# ---- arguments: [drive] [all | plugin...] -----------------------------------------------------------------------------
DRIVE_ARG=""
if [ $# -gt 0 ] && { [[ "$1" =~ ^[A-Za-z]:?$ ]] || [ "$(echo "$1" | tr 'A-Z' 'a-z')" = "none" ]; }; then
    DRIVE_ARG="$1"; shift
fi
COPY_ONLY=0
if [ $# -gt 0 ] && [ "$(echo "$1" | tr 'A-Z' 'a-z')" = "copy" ]; then COPY_ONLY=1; shift; fi
CHOSEN=()
if [ $# -gt 0 ]; then
    for a in "$@"; do
        if [ "$(echo "$a" | tr 'A-Z' 'a-z')" = "all" ]; then CHOSEN=("${PLUGINS[@]}"); break; fi
        p="$(find_plugin "$a")" || { echo "ERROR: no plugin folder '$a' (have: ${PLUGINS[*]})"; exit 2; }
        CHOSEN+=("$p")
    done
else
    echo "Plugins in $ROOT:"
    for i in "${!PLUGINS[@]}"; do
        v="$(python3 -c "import json,sys; v=json.load(open(sys.argv[1]))['version']; print('%d.%d.%d' % (v//1000, v//100%10, v//10%10))" \
             "$ROOT/${PLUGINS[$i]}/vst/vst.json" 2>/dev/null || echo "?")"
        printf "  %2d) %-18s %s\n" $((i + 1)) "${PLUGINS[$i]}" "$v"
    done
    while [ ${#CHOSEN[@]} -eq 0 ]; do
        read -r -p "Build which? (numbers like '1 3 5', names, or 'all') [all]: " ans
        ans="${ans:-all}"
        CHOSEN=(); bad=""
        for a in ${ans//,/ }; do
            if [ "$(echo "$a" | tr 'A-Z' 'a-z')" = "all" ]; then CHOSEN=("${PLUGINS[@]}"); break
            elif [[ "$a" =~ ^[0-9]+$ ]] && [ "$a" -ge 1 ] && [ "$a" -le ${#PLUGINS[@]} ]; then CHOSEN+=("${PLUGINS[$((a - 1))]}")
            elif p="$(find_plugin "$a")"; then CHOSEN+=("$p")
            else bad="$a"; fi
        done
        if [ -n "$bad" ]; then echo "  '$bad' isn't one of them, try again."; CHOSEN=(); fi
    done
fi
# drop duplicates, keep the order
UNIQ=(); for p in "${CHOSEN[@]}"; do [[ " ${UNIQ[*]} " == *" $p "* ]] || UNIQ+=("$p"); done; CHOSEN=("${UNIQ[@]}")

pick_drive $DRIVE_ARG   # asks (with the drive list) when neither the argument nor USB_DRIVE gave one
if [ "$USB_DRIVE" != "none" ]; then
    m="/mnt/$(echo "$USB_DRIVE" | tr 'A-Z' 'a-z')"
    if ! { [ -d "$m" ] && [ -n "$(ls -A "$m" 2>/dev/null)" ]; } && \
       ! { command -v powershell.exe >/dev/null && powershell.exe -NoProfile -Command "exit !(Test-Path '${USB_DRIVE}:\\')" >/dev/null 2>&1; }; then
        echo "WARNING: $USB_DRIVE: isn't there right now. The plugins still build; plug the stick in before they finish"
        echo "         (the zips are copied together once everything is built), or drag the zips from each dist\\ folder later."
    fi
fi

# Docker runs the aarch64 build: check it now rather than after you've walked away
if [ "$COPY_ONLY" = 0 ] && ! docker info >/dev/null 2>&1; then
    echo "ERROR: Docker isn't running (start Docker Desktop, wait for it, then run this again)."; exit 2
fi

# ---- build ------------------------------------------------------------------------------------------------------------
STAMP="$(date +%Y-%m-%d_%H%M%S)"
LOGS="$HERE/logs/$STAMP"; mkdir -p "$LOGS"
echo
if [ "$COPY_ONLY" = 1 ]; then echo "Copying ${#CHOSEN[@]} (no build): ${CHOSEN[*]}"; else echo "Building ${#CHOSEN[@]}: ${CHOSEN[*]}"; fi
echo "Logs: $LOGS"
echo "You can walk away now."
START_ALL=$SECONDS
RESULT=(); TIMES=(); RCS=()
for p in "${CHOSEN[@]}"; do
    echo
    echo "################ $p ################"
    t0=$SECONDS
    if [ "$COPY_ONLY" = 1 ]; then   # the zip already built: just the stick
        ( cd "$ROOT/$p" || exit 1
          z="$(ls -t dist/*-mpc-aarch64.zip 2>/dev/null | head -1)"
          [ -n "$z" ] || { echo "no zip in $p/dist: build it first"; exit 1; }
          echo "$z"
          pre="$(basename "$z" | sed -E 's/-[0-9]+\.[0-9]+\.[0-9]+-mpc-aarch64\.zip$//')"
          if [ "$USB_DRIVE" = "none" ]; then echo "Not copying anywhere ('none'): $z"; exit 0; fi
          copy_to_stick "$z" "$pre" ) < /dev/null 2>&1 | tee "$LOGS/$p.log"
    else
        # stdin from /dev/null: nothing in a plugin's build can stop and wait for an answer
        # (builds with "none": the copy is done for all of them at the end, see "copy phase")
        ( cd "$ROOT/$p" && bash ./build.sh none ) < /dev/null 2>&1 | tee "$LOGS/$p.log"
    fi
    rc=${PIPESTATUS[0]}
    zip="$(grep -o 'dist/[^ ]*-mpc-aarch64\.zip' "$LOGS/$p.log" | tail -1)"
    RCS+=("$rc")
    if [ "$rc" -eq 0 ]; then
        if [ "$USB_DRIVE" = "none" ]; then r="OK (not copied)"; else r="OK, on $USB_DRIVE:"; fi
    elif grep -q "NOTE: couldn't write to" "$LOGS/$p.log"; then
        r="BUILT, NOT COPIED (stick?)"
    else
        r="FAILED (see logs/$STAMP/$p.log)"
    fi
    [[ "$r" == FAILED* ]] && zip=""
    RESULT+=("$r${zip:+  $zip}"); TIMES+=($((SECONDS - t0)))
done

# ---- copy phase (builds only) -----------------------------------------------------------------------------------------
# Every build above ran with "none"; the stick gets all the zips now, together, so a long build can't leave it asleep or
# stale (WSL's view of a drive that was plugged in later) before its turn, and one failed copy doesn't stop the others.
if [ "$COPY_ONLY" != 1 ] && [ "$USB_DRIVE" != "none" ]; then
    echo
    echo "################ copying to $USB_DRIVE: ################"
    for i in "${!CHOSEN[@]}"; do
        p="${CHOSEN[$i]}"
        [ "${RCS[$i]}" -eq 0 ] || continue
        ok=0
        for attempt in 1 2 3; do
            ( cd "$ROOT/$p" || exit 1
              z="$(ls -t dist/*-mpc-aarch64.zip 2>/dev/null | head -1)"
              [ -n "$z" ] || { echo "no zip in $p/dist"; exit 1; }
              pre="$(basename "$z" | sed -E 's/-[0-9]+\.[0-9]+\.[0-9]+-mpc-aarch64\.zip$//')"
              echo "--- $p: $z"
              copy_to_stick "$z" "$pre" ) < /dev/null 2>&1 | tee -a "$LOGS/$p.log"
            if [ "${PIPESTATUS[0]}" -eq 0 ]; then ok=1; break; fi
            [ "$attempt" -lt 3 ] && { echo "  (copy of $p failed, trying again in 5 s)"; sleep 5; }
        done
        zn="$(grep -o 'dist/[^ ]*-mpc-aarch64\.zip' "$LOGS/$p.log" | tail -1)"
        if [ "$ok" = 1 ]; then RESULT[$i]="OK, on $USB_DRIVE:${zn:+  $zn}"
        else RESULT[$i]="BUILT, NOT COPIED (stick?)${zn:+  $zn}"; fi
    done
fi

# ---- summary ----------------------------------------------------------------------------------------------------------
fmt() { printf "%dm%02ds" $(($1 / 60)) $(($1 % 60)); }
fails=0
for r in "${RESULT[@]}"; do [[ "$r" == OK* ]] || fails=$((fails + 1)); done
{
    echo
    echo "================ build-all summary ($STAMP) ================"
    for i in "${!CHOSEN[@]}"; do
        printf "  %-18s %-8s %s\n" "${CHOSEN[$i]}" "$(fmt "${TIMES[$i]}")" "${RESULT[$i]}"
    done
    echo "  total $(fmt $((SECONDS - START_ALL)))"
    if [ "$fails" -gt 0 ]; then
        echo "$fails of ${#CHOSEN[@]} need a look (their logs are in $LOGS)."
    elif [ "$USB_DRIVE" != "none" ]; then
        echo "All done. Eject $USB_DRIVE: in Windows, then on the MPC: Plugin Manager > Local > install each > APPLY."
    else
        echo "All done (zips in each plugin's dist folder)."
    fi
} | tee "$LOGS/summary.txt"
[ "$fails" -eq 0 ]
