#!/bin/bash
# Package build script for MasterDuel Transform (macOS)
# Builds version-aware upgrade installer (.pkg) with EULA license agreement

set -e

PROJ_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJ_DIR/build"
PLUGIN_BUNDLE="$BUILD_DIR/AutoTransform.plugin"
ROOT_DIR="$(cd "$PROJ_DIR/.." && pwd)"
VERSION="0.1.3"
OUTPUT_PKG="${OUTPUT_PKG:-$ROOT_DIR/macOS_MD_Transform.v${VERSION}.pkg}"

echo "=== Packaging MasterDuel Transform v${VERSION} ==="
echo "Bundle: $PLUGIN_BUNDLE"
echo "Output: $OUTPUT_PKG"

if [ ! -d "$PLUGIN_BUNDLE" ]; then
    echo "Error: $PLUGIN_BUNDLE does not exist. Run build.sh first."
    exit 1
fi

WORKDIR="$BUILD_DIR/pkg_work"
rm -rf "$WORKDIR"
mkdir -p "$WORKDIR/root" "$WORKDIR/resources"

export COPYFILE_DISABLE=1
cp -R "$PLUGIN_BUNDLE" "$WORKDIR/root/"
find "$WORKDIR/root" -name "._*" -delete
xattr -cr "$WORKDIR/root"

# Copy Welcome and License resources
cp "$PROJ_DIR/Mac/welcome.html" "$WORKDIR/resources/"
cp "$ROOT_DIR/LICENSE" "$WORKDIR/resources/license.txt"

# Generate component.plist for upgrade/version checking
cat << 'EOF' > "$WORKDIR/component.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
	<dict>
		<key>BundleHasStrictIdentifier</key>
		<true/>
		<key>BundleIsRelocatable</key>
		<false/>
		<key>BundleIsVersionChecked</key>
		<true/>
		<key>BundleOverwriteAction</key>
		<string>upgrade</string>
		<key>RootRelativeBundlePath</key>
		<string>AutoTransform.plugin</string>
	</dict>
</array>
</plist>
EOF

# Build component package with version upgrade metadata
pkgbuild \
  --root "$WORKDIR/root" \
  --component-plist "$WORKDIR/component.plist" \
  --identifier "com.masterduel.transform.pkg" \
  --version "$VERSION" \
  --install-location "/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore" \
  "$WORKDIR/component.pkg"

# Create distribution.xml
cat << EOF > "$WORKDIR/distribution.xml"
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
    <title>MasterDuel Transform v${VERSION} (macOS 전용)</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <license file="license.txt" mime-type="text/plain"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <choices-outline>
        <line choice="default">
            <line choice="com.masterduel.transform.pkg"/>
        </line>
    </choices-outline>
    <choice id="default"/>
    <choice id="com.masterduel.transform.pkg" visible="false">
        <pkg-ref id="com.masterduel.transform.pkg"/>
    </choice>
    <pkg-ref id="com.masterduel.transform.pkg" version="${VERSION}" onConclusion="none">component.pkg</pkg-ref>
</installer-gui-script>
EOF

# Build final product package
productbuild \
  --distribution "$WORKDIR/distribution.xml" \
  --resources "$WORKDIR/resources" \
  --package-path "$WORKDIR" \
  "$OUTPUT_PKG"

rm -rf "$WORKDIR"

echo ""
echo "=== Package Build Success ==="
echo "Package created at: $OUTPUT_PKG"
