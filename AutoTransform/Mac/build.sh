#!/bin/bash
# Build script for AutoTransform plugin
# Builds without Xcode IDE, using clang++, metal, and Rez directly

set -e

# Paths
PROJ_DIR="$(cd "$(dirname "$0")/.." && pwd)"
AE_SDK_DIR="$PROJ_DIR/../AdobeAfterEffectsSDK_26.5_MacOS/Examples"
PREM_SDK_DIR="$PROJ_DIR/../Premiere Pro 26.0 C++ SDK/Examples"
BUILD_DIR="$PROJ_DIR/build"
PLUGIN_DIR="$BUILD_DIR/AutoTransform.plugin"
CONTENTS_DIR="$PLUGIN_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RESOURCES_DIR="$CONTENTS_DIR/Resources"
METALLIB_DIR="$RESOURCES_DIR/MetalLib"

# SDK path for macOS
SYSROOT="$(xcrun --show-sdk-path)"

echo "=== AutoTransform Build Script (Metal Enabled) ==="
echo "Project:      $PROJ_DIR"
echo "AE SDK:       $AE_SDK_DIR"
echo "Premiere SDK: $PREM_SDK_DIR"
echo "Sysroot:      $SYSROOT"
echo ""

# Clean
rm -rf "$BUILD_DIR"
mkdir -p "$MACOS_DIR" "$RESOURCES_DIR" "$METALLIB_DIR"

# Object file output directory
OBJ_DIR="$BUILD_DIR/obj"
mkdir -p "$OBJ_DIR"

# --- Step 1: Compile Metal shader (if metal compiler is available) ---
echo "[1/5] Checking Metal compiler..."

if xcrun --find metal >/dev/null 2>&1 && xcrun --find metallib >/dev/null 2>&1; then
    echo "  Compiling Metal shader to metallib..."
    xcrun -sdk macosx metal -c \
        -mmacosx-version-min=11.0 \
        -ffast-math \
        -o "$OBJ_DIR/AutoTransform.air" \
        "$PROJ_DIR/AutoTransform.metal"

    xcrun -sdk macosx metallib \
        -o "$METALLIB_DIR/AutoTransform.metallib" \
        "$OBJ_DIR/AutoTransform.air"

    echo "  Metal library created: $METALLIB_DIR/AutoTransform.metallib"
else
    echo "  Note: 'metal' CLI not present in CommandLineTools."
    echo "  Plugin will use built-in embedded Metal runtime compilation (newLibraryWithSource)."
fi

# --- Step 2: Compile C++ and Objective-C++ sources ---
echo "[2/5] Compiling C++ and Objective-C++ sources..."

BASE_FLAGS=(
    -arch arm64
    -std=c++17
    -ObjC++
    -mmacosx-version-min=11.0
    -O2
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -isysroot "$SYSROOT"
    -include "$SYSROOT/System/Library/Frameworks/Cocoa.framework/Headers/Cocoa.h"
)

# Flags for CPU / AE plugins
AE_FLAGS=(
    "${BASE_FLAGS[@]}"
    "-I$AE_SDK_DIR/Headers"
    "-I$AE_SDK_DIR/Util"
    "-I$AE_SDK_DIR/Headers/SP"
    "-I$AE_SDK_DIR/Resources"
    "-I$PROJ_DIR"
)

# Flags for Premiere Pro GPU Filter
GPU_FLAGS=(
    "${BASE_FLAGS[@]}"
    "-I$PREM_SDK_DIR/Headers"
    "-I$PREM_SDK_DIR/Headers/SP"
    "-I$PREM_SDK_DIR/Projects/GPUVideoFilter/Utils"
    "-I$AE_SDK_DIR/Headers"
    "-I$AE_SDK_DIR/Util"
    "-I$AE_SDK_DIR/Headers/SP"
    "-I$AE_SDK_DIR/Resources"
    "-I$PROJ_DIR"
)

clang++ "${AE_FLAGS[@]}"  -c "$PROJ_DIR/AutoTransform.cpp"             -o "$OBJ_DIR/AutoTransform.o"
clang++ "${AE_FLAGS[@]}"  -c "$PROJ_DIR/AutoTransform_Strings.cpp"     -o "$OBJ_DIR/AutoTransform_Strings.o"
clang++ "${AE_FLAGS[@]}"  -c "$AE_SDK_DIR/Util/AEGP_SuiteHandler.cpp" -o "$OBJ_DIR/AEGP_SuiteHandler.o"
clang++ "${AE_FLAGS[@]}"  -c "$AE_SDK_DIR/Util/MissingSuiteError.cpp" -o "$OBJ_DIR/MissingSuiteError.o"
clang++ "${AE_FLAGS[@]}"  -c "$AE_SDK_DIR/Util/AEFX_SuiteHelper.c"     -o "$OBJ_DIR/AEFX_SuiteHelper.o"
clang++ "${GPU_FLAGS[@]}" -c "$PROJ_DIR/AutoTransform_GPU.mm"          -o "$OBJ_DIR/AutoTransform_GPU.o"

echo "  Compiled sources successfully."

# --- Step 3: Link into dynamic library ---
echo "[3/5] Linking..."

clang++ \
    -arch arm64 \
    -bundle \
    -mmacosx-version-min=11.0 \
    -isysroot "$SYSROOT" \
    -framework Cocoa \
    -framework Metal \
    -o "$MACOS_DIR/AutoTransform" \
    "$OBJ_DIR/AutoTransform.o" \
    "$OBJ_DIR/AutoTransform_Strings.o" \
    "$OBJ_DIR/AutoTransform_GPU.o" \
    "$OBJ_DIR/AEGP_SuiteHandler.o" \
    "$OBJ_DIR/MissingSuiteError.o" \
    "$OBJ_DIR/AEFX_SuiteHelper.o"

echo "  Linked: $MACOS_DIR/AutoTransform"

# --- Step 4: Compile PiPL resource ---
echo "[4/5] Compiling PiPL resource..."

Rez \
    -d __MACH__ \
    -useDF \
    -I "$AE_SDK_DIR/Headers" \
    -I "$AE_SDK_DIR/Resources" \
    "$PROJ_DIR/AutoTransformPiPL.r" \
    -o "$RESOURCES_DIR/AutoTransformPiPL.rsrc"

echo "  PiPL resource created."

# --- Step 5: Create Info.plist & PkgInfo ---
echo "[5/5] Creating bundle metadata..."

# Copy Info.plist (replace variable)
sed 's/$(PRODUCT_BUNDLE_IDENTIFIER)/com.autotransform.premiere/g' \
    "$PROJ_DIR/Mac/AutoTransform.plugin-Info.plist" > "$CONTENTS_DIR/Info.plist"

# Create PkgInfo
echo -n "eFKTFXTC" > "$CONTENTS_DIR/PkgInfo"

echo ""
echo "=== Build Complete ==="
echo "Plugin: $PLUGIN_DIR"
echo ""
echo "To install:"
echo '  sudo cp -R "'$PLUGIN_DIR'" "/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/"'
echo '  codesign --force --deep --sign - "/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/AutoTransform.plugin"'
