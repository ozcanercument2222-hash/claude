#!/bin/bash
# VSTM Tune - macOS kurulum paketi (.pkg).
#
#   installer/macos/build_pkg.sh <Release klasoru> <surum> <cikis.pkg>
#   ornek: installer/macos/build_pkg.sh build/VSTMTune_artefacts/Release 1.0.0 "paket/VSTM Tune Kurulum.pkg"
#
# Tek bir kurulum paketi uretir (cift tikla -> kurulum sihirbazi). Uc bilesen, "Ozellestir" ile secilebilir:
#   VST3 eklentisi       -> /Library/Audio/Plug-Ins/VST3/VSTM Tune.vst3
#   Audio Unit (AU)      -> /Library/Audio/Plug-Ins/Components/VSTM Tune.component
#   Uygulama (Standalone)-> /Applications/VSTM Tune.app
# macOS'ta calisir (pkgbuild, productbuild, codesign). Build klasorundeki dosyalara dokunmaz.
# Paket Apple Developer ID ile imzali / noter onayli degildir; bundle'lar ad-hoc imzalanir
# (Apple Silicon imzasiz kodu yuklemez).
set -euo pipefail

if [ $# -ne 3 ]; then
    echo "kullanim: $0 <Release klasoru> <surum> <cikis.pkg>" >&2
    exit 2
fi
REL="$1"
VER="$2"
OUT="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
ID=com.vstmaudio.vstmtune

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/root/vst3" "$WORK/root/au" "$WORK/root/app" "$WORK/pkgs"
cp -R "$REL/VST3/VSTM Tune.vst3"      "$WORK/root/vst3/"
cp -R "$REL/AU/VSTM Tune.component"   "$WORK/root/au/"
cp -R "$REL/Standalone/VSTM Tune.app" "$WORK/root/app/"

for b in "$WORK/root/vst3/VSTM Tune.vst3" "$WORK/root/au/VSTM Tune.component" "$WORK/root/app/VSTM Tune.app"; do
    xattr -cr "$b"
    if ! codesign --verify --strict "$b" 2>/dev/null; then
        echo "ad-hoc imza: $(basename "$b")"
        codesign --force --sign - "$b"
    fi
    codesign --verify --strict --verbose=1 "$b"
done

chmod 755 "$HERE/scripts/postinstall"

# part, kurulum yeri, paket kimligi
component () {
    local part="$1" location="$2" identifier="$3"
    local plist="$WORK/$part.plist"
    pkgbuild --analyze --root "$WORK/root/$part" "$plist"
    # Kurulum diskte ayni kimlikli baska bir kopyaya (ornegin bir build klasorune) "tasinmasin"
    # ve ayni surum her zaman yeniden yazilsin.
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" > /dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist"
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsVersionChecked false" "$plist"
        i=$((i + 1))
    done
    pkgbuild --root "$WORK/root/$part" --component-plist "$plist" --identifier "$identifier" \
             --version "$VER" --install-location "$location" --scripts "$HERE/scripts" \
             "$WORK/pkgs/$part.pkg"
}
component vst3 "/Library/Audio/Plug-Ins/VST3"       "$ID.pkg.vst3"
component au   "/Library/Audio/Plug-Ins/Components" "$ID.pkg.au"
component app  "/Applications"                      "$ID.pkg.app"

sed "s/@VERSION@/$VER/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"
mkdir -p "$(dirname "$OUT")"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/pkgs" \
             --resources "$HERE/resources" "$OUT"

pkgutil --check-signature "$OUT" || true          # imzasiz: "no signature" beklenir
installer -pkginfo -verbose -pkg "$OUT" || true
echo "yazildi: $OUT ($(du -h "$OUT" | cut -f1))"
