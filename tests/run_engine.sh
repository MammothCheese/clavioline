#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Render the scripts on this computer and measure them (numpy needed), then compare the chip's formant filter with the
# circuit model. On Windows/MinGW: CC=gcc PYTHON=python sh tests/run_engine.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
${CC:-cc} -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o build/host/clavioline firmware/src/clavioline.c
${CC:-cc} -O2 -Wall -Ifirmware/src -o build/host/formant_test tests/formant_test.c
build/host/clavioline build/host/core tests/scripts/clav.txt >/dev/null
build/host/clavioline build/host/alias tests/scripts/clav_alias.txt >/dev/null
build/host/clavioline build/host/stage4 tests/scripts/stage4.txt >/dev/null
build/host/clavioline build/host/options tests/scripts/options.txt >/dev/null
build/host/clavioline build/host/pluck tests/scripts/pluck.txt >/dev/null
build/host/clavioline build/host/glide tests/scripts/glide.txt >/dev/null
fail=0
${PYTHON:-python3} tests/check_engine.py core build/host/core.wav build/host/alias.wav || fail=1
${PYTHON:-python3} tests/check_engine.py stage4 build/host/stage4.wav || fail=1
${PYTHON:-python3} tests/check_engine.py options build/host/options.wav || fail=1
${PYTHON:-python3} tests/check_engine.py pluck build/host/pluck.wav || fail=1
${PYTHON:-python3} tests/check_engine.py glide build/host/glide.wav || fail=1
FT=build/host/formant_test; [ -f "$FT.exe" ] && FT="$FT.exe"
${PYTHON:-python3} tests/formant_check.py "$FT" || fail=1
exit $fail
