#!/bin/bash
# Package a signed QLaue.app for drag-to-Applications installation.
set -euo pipefail
if [[ $# != 2 ]]; then
    echo "Usage: $0 PAYLOAD_DIRECTORY OUTPUT.dmg" >&2
    exit 2
fi
payload=$(cd "$1" && pwd)
output=$2
[[ "$output" == *.dmg && ! -e "$output" ]]
app="$payload/QLaue.app"
documents="$app/Contents/Resources/Distribution"
[[ -d "$app" && -f "$documents/LICENSE-QLaue.txt" && -f "$documents/README.txt" ]]
[[ -f "$documents/build-manifest.json" && -d "$documents/licenses" && -d "$documents/build-recipes" && -d "$documents/docs" ]]
codesign --verify --deep --strict "$app"
script_dir=$(cd "$(dirname "$0")" && pwd)
stage=$(mktemp -d "${TMPDIR:-/tmp}/qlaue-dmg.XXXXXX")
trap 'rm -rf "$stage"' EXIT
ditto --norsrc "$app" "$stage/QLaue.app"
ln -s /Applications "$stage/Applications"
# Finder reads these view and icon positions when the image is opened.
cp "$script_dir/macos-dmg-finder-settings" "$stage/.DS_Store"
mkdir -p "$(dirname "$output")"
hdiutil create -volname QLaue -srcfolder "$stage" -format UDZO -fs HFS+ "$output"
hdiutil verify "$output"
