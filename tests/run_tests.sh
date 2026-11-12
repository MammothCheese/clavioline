#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# CLAVIOLINE's host tests (from Dinghy's, FM-1 only; no hardware). Run from the repo root after ./build.sh:
#   sh build.sh && sh tests/run_tests.sh
# The Stage 2 sound checks (tests/run_stage2.sh), and Dinghy's update-path tests unchanged: M-UPGRADE entry,
# the update loader and its screen, the installer.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # (fm1pkg_make.py: the SDK files of a package)
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

[ -f build/clavioline.fwsc ] || { echo "run ./build.sh first"; exit 1; }

run "Stage 2: highest note, legato, attack and P, coupler, knee lever, aliasing" sh tests/run_stage2.sh

$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry (FM-1)" "$OUT/ota_test" build/clavioline.fwsc
head -c 60000 build/clavioline.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_screen_test" tests/ldr_screen_test.c
run "update loader's screen (from Skiff): UPDATE MODE, WRITING and its bar, UPDATE FAILED" "$OUT/ldr_screen_test" "$OUT"
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader (FM-1): other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/clavioline.fwsc

run "installer (tools/fm1_install.py): install and the update-mode resume check the model" python3 tests/install_test.py

[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit "$fail"
