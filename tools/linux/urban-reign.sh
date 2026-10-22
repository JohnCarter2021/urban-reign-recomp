#!/bin/sh
# Starts Urban Reign on Linux (docs/LINUX.md). The same script in both releases:
# - .tar.gz (portable): next to the game/ folder; the game's files (config.ini, controls.ini, memcards, states...)
#   stay in game/, as in the Windows ZIP.
# - .deb: /usr/bin/urban-reign; the game is in /usr/lib/urban-reign and the player's files go to
#   ~/.local/share/urban-reign (URBAN_USER_DIR), with game/, mods/ and textures/ there.
# Without arguments it opens the launcher (launcher/, when the release has it and python3 is there; the launcher itself
# says what to install when GTK/WebKit is missing). --game, or any argument, starts the game directly; with no disc
# image configured yet it then asks for one (zenity or kdialog) and writes it to config.ini [Game] Iso. Arguments go to
# the game (e.g. --iso <file>).
set -e

HERE=$(dirname "$(readlink -f "$0")")
if [ -x "$HERE/game/urban" ]; then
    GAME="$HERE/game"
    LAUNCHER="$HERE/launcher"
    DATA="$GAME"
    DISCS="$GAME" # an .iso next to urban (the game looks there, as urban.exe does in the Windows release)
else
    GAME=/usr/lib/urban-reign
    LAUNCHER=/usr/lib/urban-reign/launcher
    DATA="${XDG_DATA_HOME:-$HOME/.local/share}/urban-reign"
    DISCS="$DATA/game"
    export URBAN_USER_DIR="$DATA"
    mkdir -p "$DISCS" "$DATA/mods" "$DATA/textures/SLUS-21209"
fi

if [ $# -eq 0 ] && [ -f "$LAUNCHER/urban_launcher.py" ] && command -v python3 >/dev/null 2>&1; then
    export URBAN_GAME_DIR="$GAME"
    exec python3 "$LAUNCHER/urban_launcher.py"
fi
[ "${1:-}" = "--game" ] && shift

configured_iso() {
    if [ -f "$DATA/config.ini" ]; then
        sed -n 's/^[[:space:]]*Iso[[:space:]]*=[[:space:]]*//p' "$DATA/config.ini" | head -n 1 | tr -d '\r'
    fi
}

has_disc() {
    iso=$(configured_iso)
    [ -n "$iso" ] && [ -f "$iso" ] && return 0
    for file in "$DISCS"/*.iso "$DISCS"/*.ISO; do
        [ -f "$file" ] && return 0
    done
    return 1
}

pick_disc() {
    title="Urban Reign: your disc image (USA, SLUS-21209, .iso) / sua imagem do disco"
    if command -v zenity >/dev/null 2>&1; then
        zenity --file-selection --title="$title" --file-filter="Disc image (.iso) | *.iso *.ISO" 2>/dev/null || true
    elif command -v kdialog >/dev/null 2>&1; then
        kdialog --title "$title" --getopenfilename "$HOME" "*.iso *.ISO|Disc image (.iso)" 2>/dev/null || true
    fi
}

if [ $# -eq 0 ] && [ -z "${URBAN_ISO:-}" ] && ! has_disc; then
    # (here without the launcher: started with --game, or the release has no launcher / no python3)
    iso=$(pick_disc)
    if [ -n "$iso" ] && [ -f "$iso" ]; then
        # config.ini with its defaults first (the game writes it and exits), then its Iso= line.
        [ -f "$DATA/config.ini" ] || "$GAME/urban" --write-settings >/dev/null 2>&1 || true
        ISO="$iso" awk 'done != 1 && /^[[:space:]]*Iso[[:space:]]*=/ { print "Iso=" ENVIRON["ISO"]; done = 1; next } { print }' \
            "$DATA/config.ini" > "$DATA/config.ini.new" && mv "$DATA/config.ini.new" "$DATA/config.ini"
    else
        echo "Urban Reign: no disc image. Put your own Urban Reign (USA) .iso in $DISCS, set [Game] Iso= in" >&2
        echo "$DATA/config.ini, or run: $0 --iso <file.iso>" >&2
    fi
fi

exec "$GAME/urban" "$@"
