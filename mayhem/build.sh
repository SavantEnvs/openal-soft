#!/usr/bin/env bash
#
# mayhem/build.sh — build openal-soft (static, loopback + null backends only) with the fuzz
# flags, link the harnesses in mayhem/harnesses/ (libFuzzer + standalone reproducer each,
# plus the file-input fuzz_config), and build the functional selftest with normal flags.
# Re-runnable (CMake/Ninja incremental), offline (fmt and gsl are vendored in the tree).
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export CC CXX

cd "$SRC"

# Coverage instrumentation for the library when building for libFuzzer.
COV_FLAGS=""
[ -n "$LIB_FUZZING_ENGINE" ] && COV_FLAGS="-fsanitize=fuzzer-no-link"

# Shared configure options: static lib, no hardware backends, no utils/examples, EAX on.
OAL_OPTS=(-G Ninja -DLIBTYPE=STATIC -DALSOFT_ENABLE_MODULES=OFF -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
  -DALSOFT_BACKEND_PIPEWIRE=OFF -DALSOFT_BACKEND_PULSEAUDIO=OFF -DALSOFT_BACKEND_ALSA=OFF
  -DALSOFT_BACKEND_OSS=OFF -DALSOFT_BACKEND_JACK=OFF -DALSOFT_BACKEND_SNDIO=OFF
  -DALSOFT_BACKEND_PORTAUDIO=OFF -DALSOFT_BACKEND_SDL2=OFF -DALSOFT_BACKEND_SDL3=OFF
  -DALSOFT_BACKEND_WAVE=OFF -DALSOFT_RTKIT=OFF -DALSOFT_NO_CONFIG_UTIL=ON
  -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_EAX=ON
  -DALSOFT_UPDATE_BUILD_VERSION=OFF -DALSOFT_INSTALL=OFF -DALSOFT_DLOPEN=OFF)

# 1) Sanitized, coverage-instrumented library (build-mayhem/ is gitignored via build*/).
#    Debug build type with empty per-config flags: asserts stay on (no -DNDEBUG), -O1 below.
FUZZ_CFLAGS="-O1 $SANITIZER_FLAGS $COV_FLAGS $DEBUG_FLAGS"
cmake -S . -B build-mayhem "${OAL_OPTS[@]}" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS_DEBUG= -DCMAKE_CXX_FLAGS_DEBUG= \
  -DCMAKE_C_FLAGS="$FUZZ_CFLAGS" -DCMAKE_CXX_FLAGS="$FUZZ_CFLAGS"
cmake --build build-mayhem -j"$MAYHEM_JOBS"

LIBS=("$SRC/build-mayhem/libopenal.a" "$SRC/build-mayhem/libalsoft.common.a")
[ -f "$SRC/build-mayhem/libalsoft.common.a" ] || LIBS=("$SRC/build-mayhem/libopenal.a")
HFLAGS=(-std=c++20 -O1 -DAL_ALEXT_PROTOTYPES -DAL_LIBTYPE_STATIC -I"$SRC/include"
  -I"$SRC/mayhem/harnesses")
OBJ="$SRC/build-mayhem/mayhem-obj"
mkdir -p "$OBJ"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OBJ/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/standalone_main.o"

# 2) libFuzzer harnesses + standalone reproducers.
for h in fuzz_buffer fuzz_hrtf fuzz_ambdec fuzz_attrs fuzz_effects; do
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "${HFLAGS[@]}" \
    "$SRC/mayhem/harnesses/$h.cpp" "$OBJ/lsan_off.o" "${LIBS[@]}" -lpthread -ldl -lm \
    -o "/mayhem/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${HFLAGS[@]}" \
    "$SRC/mayhem/harnesses/$h.cpp" "$OBJ/standalone_main.o" "$OBJ/lsan_off.o" "${LIBS[@]}" \
    -lpthread -ldl -lm -o "/mayhem/$h-standalone"
done
# File-input target (the config is read once per process). Coverage callbacks come from
# the sanitizer runtime.
$CXX $SANITIZER_FLAGS $COV_FLAGS $DEBUG_FLAGS "${HFLAGS[@]}" \
  "$SRC/mayhem/harnesses/fuzz_config.cpp" "$OBJ/lsan_off.o" "${LIBS[@]}" -lpthread -ldl -lm \
  -o /mayhem/fuzz_config

# 3) Functional selftest with the project's normal flags (independent build tree).
cmake -S . -B build-mayhem-test "${OAL_OPTS[@]}" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS"
cmake --build build-mayhem-test -j"$MAYHEM_JOBS"
TLIBS=("$SRC/build-mayhem-test/libopenal.a")
[ -f "$SRC/build-mayhem-test/libalsoft.common.a" ] && TLIBS+=("$SRC/build-mayhem-test/libalsoft.common.a")
$CXX -std=c++20 -O2 $COVERAGE_FLAGS -DAL_ALEXT_PROTOTYPES -DAL_LIBTYPE_STATIC -I"$SRC/include" \
  "$SRC/mayhem/selftest/oal_selftest.cpp" "${TLIBS[@]}" -lpthread -ldl -lm \
  -o "$SRC/build-mayhem-test/oal_selftest"
