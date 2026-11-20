#!/bin/sh
# CLAVIOLINE in a web browser: the firmware compiled to WebAssembly (Emscripten), for the play page docs/play/.
#   sh emu/build.sh            (needs emcc on the PATH: https://emscripten.org/docs/getting_started/)
# Out: docs/play/clavioline.js and docs/play/clavioline.wasm. The page itself is docs/play/index.html, emu.js, worklet.js.
set -e
cd "$(dirname "$0")/.."
emcc -O2 -DDINGHY_HOST -DDINGHY_WEB -DAPP_VER='"web"' -Wno-unused-function     -Ifirmware/hal -Ifirmware/src -Ifirmware/gen firmware/src/clavioline.c     -o docs/play/clavioline.js     -sASYNCIFY -sINVOKE_RUN=0 -sMODULARIZE -sEXPORT_NAME=Clavioline -sENVIRONMENT=web     -sEXPORTED_RUNTIME_METHODS=callMain,HEAPU8,HEAPU16,HEAP16     -sINITIAL_MEMORY=33554432 -sSTACK_SIZE=1048576
ls -l docs/play/clavioline.js docs/play/clavioline.wasm
