#!/bin/bash
# Runs every Desktop Melee test. The welcome window test opens the installed app on
# screen for a few seconds, so run desktop-melee/package.sh first.
cd "$(dirname "$0")/../.."
status=0
for t in disc_check_test font_bundle_test welcome_window_test island_test tweaks_test; do
    echo "== ${t}"
    "desktop-melee/tests/${t}.sh" || status=1
done
exit ${status}
