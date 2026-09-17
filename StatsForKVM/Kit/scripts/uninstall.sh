#!/bin/sh

set -u

HELPER_LABEL="com.wei.statsforkvm.SMC.Helper"

if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ]; then
    HOME=$(dscl . -read "/Users/$SUDO_USER" NFSHomeDirectory | awk '{print $2}')
fi

run_as_user() {
    if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ]; then
        sudo -u "$SUDO_USER" "$@"
    else
        "$@"
    fi
}

echo "Uninstalling StatsForKVM..."

run_as_user osascript -e 'quit app "StatsForKVM"' >/dev/null 2>&1 || true

echo "Removing the SMC helper (administrator privileges are required)..."
sudo launchctl bootout "system/$HELPER_LABEL" 2>/dev/null || true
sudo launchctl unload "/Library/LaunchDaemons/$HELPER_LABEL.plist" 2>/dev/null || true
sudo rm -f "/Library/LaunchDaemons/$HELPER_LABEL.plist"
sudo rm -f "/Library/PrivilegedHelperTools/$HELPER_LABEL"

for app in "/Applications/StatsForKVM.app" "$HOME/Applications/StatsForKVM.app"; do
    if [ -d "$app" ]; then
        echo "Removing $app..."
        sudo rm -rf "$app"
    fi
done

echo "Removing application data and preferences..."
rm -rf "$HOME/Library/Application Support/StatsForKVM"
rm -rf "$HOME/Library/Containers/com.wei.statsforkvm.Widgets"
rm -rf "$HOME/Library/Group Containers/"*.com.wei.statsforkvm.widgets
run_as_user defaults delete com.wei.statsforkvm >/dev/null 2>&1 || true
run_as_user defaults delete com.wei.statsforkvm.Widgets >/dev/null 2>&1 || true
rm -f "$HOME/Library/Preferences/com.wei.statsforkvm.plist"
rm -f "$HOME/Library/Preferences/com.wei.statsforkvm.Widgets.plist"

echo "StatsForKVM has been uninstalled."
echo "If fan speeds were controlled manually, they will return to automatic control after a reboot."
