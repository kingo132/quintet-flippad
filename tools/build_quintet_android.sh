#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SDK=${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}
NDK=${ANDROID_NDK_ROOT:-$SDK/ndk/28.2.13676358}
case "$(uname -s)" in Darwin) HOST=darwin-x86_64;; Linux) HOST=linux-x86_64;; *) echo "Unsupported build host" >&2; exit 2;; esac
DEST="$ROOT/android-app/app/src/main/jniLibs/arm64-v8a"
mkdir -p "$DEST"
"$NDK/ndk-build" -C "$ROOT" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT="$ROOT/libretro/jni/Android.mk" APP_STL=c++_static APP_ABI=arm64-v8a APP_PLATFORM=android-26 APP_SUPPORT_FLEXIBLE_PAGE_SIZES=true APP_OPTIM=release -j8
CC="$NDK/toolchains/llvm/prebuilt/$HOST/bin/aarch64-linux-android26-clang"
"$CC" -shared -fPIC -O2 -Wl,-z,max-page-size=16384 -I"$ROOT/libretro" "$ROOT/android/bridge.c" -L"$ROOT/libs/arm64-v8a" -lretro -ljnigraphics -llog -o "$DEST/libtallbridge.so"
CXX="$NDK/toolchains/llvm/prebuilt/$HOST/bin/aarch64-linux-android26-clang++"
"$CXX" -shared -fPIC -O3 -std=c++17 -static-libstdc++ -Wl,-z,max-page-size=16384 "$ROOT/android/filters_jni.cpp" "$ROOT/android/filter_engine.cpp" "$ROOT/android/xbrz/xbrz.cpp" -ljnigraphics -llog -o "$DEST/libtallfilters.so"
cp "$ROOT/libs/arm64-v8a/libretro.so" "$DEST/"
