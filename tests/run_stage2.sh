#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Render the Stage 2 scripts on this computer and measure them (numpy needed).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
${CC:-cc} -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o build/host/clavioline firmware/src/clavioline.c
build/host/clavioline build/host/stage2 tests/scripts/clav.txt >/dev/null
build/host/clavioline build/host/alias tests/scripts/clav_alias.txt >/dev/null
${PYTHON:-python3} tests/check_stage2.py build/host/stage2.wav build/host/alias.wav
