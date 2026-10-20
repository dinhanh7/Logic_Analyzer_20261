#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$RP2350_ROOT"
mode="${1:-all}"
case "$mode" in all|host|firmware) ;; *) echo 'Usage: scripts/build.sh [all|host|firmware]' >&2; exit 2;; esac
python3 scripts/fetch-sources.py
mkdir -p build/logs
jobs="${JOBS:-4}"
if [[ "$mode" != firmware ]]; then
    python3 scripts/integrate.py
    if git -C third_party/pulseview apply --check "$RP2350_ROOT/patches/pulseview-title.patch" 2>/dev/null; then
        git -C third_party/pulseview apply "$RP2350_ROOT/patches/pulseview-title.patch"
    else
        git -C third_party/pulseview apply --reverse --check "$RP2350_ROOT/patches/pulseview-title.patch"
    fi
    for component in libsigrok libsigrokdecode sigrok-cli; do
        echo "Building $component (log: build/logs/$component.log)"
        (
            cd "third_party/$component"
            ./autogen.sh
            options=(--prefix="$RP2350_ROOT/.local")
            if [[ "$component" == libsigrok ]]; then
                options+=(--disable-all-drivers --enable-demo --enable-rp2350-la --enable-cxx --disable-python --disable-java --disable-ruby)
            fi
            ./configure "${options[@]}"
            make -j"$jobs"
            make install
        ) >"build/logs/$component.log" 2>&1 || { tail -n 40 "build/logs/$component.log"; exit 1; }
    done
    echo 'Building PulseView'
    cmake -S third_party/pulseview -B build/pulseview -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$RP2350_ROOT/.local" -DENABLE_DECODE=ON -DENABLE_TESTS=OFF >build/logs/pulseview.log 2>&1
    cmake --build build/pulseview -j"$jobs" >>build/logs/pulseview.log 2>&1
    cmake --install build/pulseview >>build/logs/pulseview.log 2>&1
fi
if [[ "$mode" != host ]]; then
    board="${PICO_BOARD:-pico2_w}"
    echo "Building firmware for $board"
    # Host include/library paths must never leak into the ARM compiler.
    env -u CPATH -u LIBRARY_PATH cmake -S firmware -B "build/firmware-$board" -DPICO_BOARD="$board" >"build/logs/firmware-$board.log" 2>&1
    env -u CPATH -u LIBRARY_PATH cmake --build "build/firmware-$board" -j"$jobs" >>"build/logs/firmware-$board.log" 2>&1
    echo "UF2: $RP2350_ROOT/build/firmware-$board/rp2350_la.uf2"
fi
