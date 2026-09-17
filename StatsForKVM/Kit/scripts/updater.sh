#!/bin/bash
set -eu

DMG_PATH="$HOME/Downloads/StatsForKVM.dmg"
MOUNT_PATH="/tmp/StatsForKVM"
APPLICATION_PATH="/Applications/"
LAUNCH_UID=""

STEP=""

while [[ "$#" -gt 0 ]]; do case "$1" in
  -s|--step) STEP="$2"; shift;;
  -d|--dmg) DMG_PATH="$2"; shift;;
  -a|--app) APPLICATION_PATH="$2"; shift;;
  -m|--mount) MOUNT_PATH="$2"; shift;;
  -u|--user) LAUNCH_UID="$2"; shift;;
  *) echo "Unknown parameter passed: $1"; exit 1;;
esac; shift; done

APP_DST="${APPLICATION_PATH%/}/StatsForKVM.app"
APP_SRC="${MOUNT_PATH%/}/StatsForKVM.app"

# When the script runs as root (admin auth path) but a target UID was passed,
# launch the new app back as the original user so it doesn't run as root.
launch_app() {
    if [[ -n "$LAUNCH_UID" && "$(id -u)" == "0" ]]; then
        /bin/launchctl asuser "$LAUNCH_UID" /usr/bin/sudo -u "#$LAUNCH_UID" "$@"
    else
        "$@"
    fi
}

install_app() {
    local parent staging old
    parent="$(/usr/bin/dirname "$APP_DST")"
    staging="$(/usr/bin/mktemp -d "$parent/.StatsForKVM-update.XXXXXX")"

    if command -v ditto >/dev/null 2>&1; then
        ditto "$APP_SRC" "$staging/StatsForKVM.app"
    else
        cp -Rf "$APP_SRC" "$staging/StatsForKVM.app"
    fi

    old=""
    if [[ -e "$APP_DST" ]]; then
        old="$(/usr/bin/mktemp -d "$parent/.StatsForKVM-old.XXXXXX")"
        mv "$APP_DST" "$old/StatsForKVM.app"
    fi
    mv "$staging/StatsForKVM.app" "$APP_DST"

    rm -rf "$staging"
    if [[ -n "$old" ]]; then
        rm -rf "$old"
    fi
}

if [[ "$STEP" == "2" ]]; then
    install_app

    launch_app "$APP_DST/Contents/MacOS/StatsForKVM" --dmg "$DMG_PATH"

    echo "New version started"
elif [[ "$STEP" == "3" ]]; then
    /usr/bin/hdiutil detach "$MOUNT_PATH"
    /bin/rm -rf "$MOUNT_PATH"
    /bin/rm -rf "$DMG_PATH"

    echo "Done"
else
    install_app

    launch_app "$APP_DST/Contents/MacOS/StatsForKVM" --dmg-path "$DMG_PATH" --mount-path "$MOUNT_PATH"

    echo "New version started"
fi
