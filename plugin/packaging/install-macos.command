#!/bin/bash
# Installs the Harmonizer Audio Unit and VST3 for the current user.
# Double-click this file. If macOS refuses, right-click it and choose Open.

set -u
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"

echo "Harmonizer plugin installer"
echo "==========================="
echo

if [ ! -d "Harmonizer.vst3" ] || [ ! -d "Harmonizer.component" ]; then
    echo "Could not find the plugins next to this script."
    echo "Unzip the download first, then run the script from inside the unzipped folder."
    echo
    read -r -p "Press return to close." _
    exit 1
fi

mkdir -p "$VST3_DIR" "$AU_DIR"

echo "Installing to your user plug-in folders..."
rm -rf "$VST3_DIR/Harmonizer.vst3" "$AU_DIR/Harmonizer.component"
cp -R "Harmonizer.vst3" "$VST3_DIR/"
cp -R "Harmonizer.component" "$AU_DIR/"

# Anything arriving through a browser carries a quarantine flag, and macOS will
# refuse to load a quarantined plugin. Clearing it is the whole reason this
# script exists rather than "drag these two files into a folder".
echo "Clearing the download quarantine flag..."
xattr -dr com.apple.quarantine "$VST3_DIR/Harmonizer.vst3" 2>/dev/null
xattr -dr com.apple.quarantine "$AU_DIR/Harmonizer.component" 2>/dev/null

# Logic caches its plugin scan; a stale cache is the usual reason a freshly
# installed Audio Unit does not appear.
rm -rf "$HOME/Library/Caches/AudioUnitCache" 2>/dev/null

echo
echo "Done."
echo "  VST3  ->  $VST3_DIR/Harmonizer.vst3"
echo "  AU    ->  $AU_DIR/Harmonizer.component"
echo
echo "Next: quit your DAW completely and reopen it, so it rescans."
echo "In Logic the plugin appears under Audio FX as a MIDI-controlled effect."
echo
read -r -p "Press return to close." _
