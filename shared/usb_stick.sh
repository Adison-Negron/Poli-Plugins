# usb_stick.sh -- sourced by every Poli plugin's build.sh: which drive the release zip goes to, and the copy itself.
#
# Pick the drive (letter only: E, D, C, ...), first match wins:
#   ./build.sh E              the drive as the first argument
#   USB_DRIVE=E ./build.sh    or in the environment
#   (neither)                 the script asks at the start, before the long build, listing the drives Windows sees;
#                             Enter takes the last one used (remembered in shared/.usb_drive, default D)
#   ./build.sh none           build only, copy nowhere
# The zip goes to <drive>:\local-plugins\ (the Plugin Manager's Local tab reads it there), replacing older zips of the
# same plugin.

STICK_MEM="$(dirname "${BASH_SOURCE[0]}")/.usb_drive"

pick_drive() {   # pick_drive "$@"  -> sets USB_DRIVE (a capital letter, or "none")
    local want="${1:-${USB_DRIVE:-}}" last="D"
    [ -f "$STICK_MEM" ] && last="$(tr -dc 'A-Z' < "$STICK_MEM" | head -c 1)"
    [ -n "$last" ] || last="D"
    if [ -z "$want" ]; then
        if [ -t 0 ]; then
            local drives=""
            command -v powershell.exe >/dev/null && drives="$(powershell.exe -NoProfile -Command \
                "(Get-PSDrive -PSProvider FileSystem | ForEach-Object { \$_.Name + ':' }) -join ' '" 2>/dev/null | tr -d '\r')"
            [ -n "$drives" ] && echo "Drives Windows sees: $drives"
            read -r -p "Copy the zip to which drive? (letter, or 'none') [$last]: " want
        fi
        want="${want:-$last}"
    fi
    want="$(echo "$want" | tr -d ' :\\/' | tr 'a-z' 'A-Z')"
    if [ "$want" = "NONE" ]; then USB_DRIVE="none"; return; fi
    if ! [[ "$want" =~ ^[A-Z]$ ]]; then
        echo "ERROR: '$want' isn't a drive letter (use E, D, C, ... or none)"; exit 2
    fi
    USB_DRIVE="$want"
    echo "$USB_DRIVE" > "$STICK_MEM" 2>/dev/null || true
    echo "The zip will go to $USB_DRIVE:\\local-plugins\\"
}

win_path() {   # /mnt/c/x/y.zip -> C:\x\y.zip without asking wslpath (it sometimes says "No such file" for a zip
               # written a moment ago, seen 2026-11-05 on Poli-q and PoliPlug); other paths: wslpath -w
    local p="$1"
    case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
    if [[ "$p" =~ ^/mnt/([a-zA-Z])(/.*)?$ ]]; then
        local d="${BASH_REMATCH[1]}" rest="${BASH_REMATCH[2]}"
        printf '%s:%s\n' "$(echo "$d" | tr 'a-z' 'A-Z')" "$(echo "${rest:-/}" | tr '/' '\\')"
    else
        wslpath -w "$p" 2>/dev/null || echo "$p"
    fi
}

copy_to_stick() {   # copy_to_stick <zip> <file prefix, e.g. Shhhhh>
    local zip="$1" prefix="$2" d="$USB_DRIVE"
    if [ "$d" = "none" ]; then echo "Not copying anywhere ('none'): $zip"; return 0; fi
    local mnt="/mnt/$(echo "$d" | tr 'A-Z' 'a-z')" dir copied=0
    dir="$mnt/local-plugins"
    echo "=== 6. Copying to $d:\\local-plugins ==="
    # 1st try: straight from WSL. WSL's view of a drive goes stale when a stick is plugged in after WSL started, and then
    # writes fail with "Permission denied"; 2nd try: let Windows do the copy (PowerShell sees the drive as it is now).
    if [ -d "$mnt" ] && [ -n "$(ls -A "$mnt" 2>/dev/null)" ]; then
        if mkdir -p "$dir" 2>/dev/null \
           && { rm -f "$dir/$prefix"-*-mpc-aarch64.zip 2>/dev/null; true; } \
           && cp "$zip" "$dir/" 2>/dev/null; then
            sync
            copied=1
        fi
    fi
    if [ "$copied" = 0 ] && command -v powershell.exe >/dev/null; then
        echo "WSL can't write to $d:, copying through Windows instead..."
        local win_zip try
        win_zip="$(win_path "$zip")"
        for try in 1 2 3; do   # a fresh zip can be held for a moment (antivirus scan): try again
            if powershell.exe -NoProfile -Command "\$ErrorActionPreference='Stop'
                try {
                    if (-not (Test-Path '${d}:\')) { throw 'no ${d}: drive: is the stick plugged in?' }
                    New-Item -ItemType Directory -Force -Path '${d}:\local-plugins' | Out-Null
                    Get-ChildItem '${d}:\local-plugins' -Filter '${prefix}-*-mpc-aarch64.zip' | Remove-Item -Force
                    Copy-Item -Force -LiteralPath '$win_zip' -Destination '${d}:\local-plugins\'
                } catch { [Console]::Error.WriteLine('PowerShell: ' + \$_.Exception.Message); exit 1 }" < /dev/null; then
                copied=1; break
            fi
            [ "$try" -lt 3 ] && { echo "  (try $try failed, again in 3 s)"; sleep 3; }
        done
    fi
    if [ "$copied" = 1 ]; then
        echo "SUCCESS: $(basename "$zip") is on $d: in local-plugins\\ (eject the stick in Windows before unplugging)"
    else
        echo "NOTE: couldn't write to $d:. Check the drive letter and that it isn't write-protected, then drag this file"
        echo "      to $d:\\local-plugins\\ in Explorer: $(win_path "$zip")"
        echo "      (or run again with another letter: ./build.sh E)"
        return 1
    fi
}
