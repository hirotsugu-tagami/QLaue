#!/bin/bash
# Package a deployed, signed QLaue.app plus its license/provenance documents.
set -euo pipefail
if [[ $# != 2 ]]; then
    echo "Usage: $0 PAYLOAD_DIRECTORY OUTPUT.dmg" >&2
    exit 2
fi
payload=$(cd "$1" && pwd)
output=$2
[[ "$output" == *.dmg && ! -e "$output" ]]
[[ -d "$payload/QLaue.app" && -f "$payload/LICENSE-QLaue.txt" ]]
[[ -f "$payload/README.txt" && -f "$payload/build-manifest.json" && -d "$payload/licenses" ]]
codesign --verify --deep --strict "$payload/QLaue.app"
stage=$(mktemp -d "${TMPDIR:-/tmp}/qlaue-dmg.XXXXXX")
trap 'rm -rf "$stage"' EXIT
ditto --norsrc "$payload" "$stage"
ln -s /Applications "$stage/Applications"
mkdir -p "$(dirname "$output")"
hdiutil create -volname QLaue -srcfolder "$stage" -format UDZO -fs HFS+ "$output"
hdiutil verify "$output"
