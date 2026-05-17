#!/bin/bash
# Build the WebAssembly + WebGL2 version into docs/ (served by GitHub Pages).
# Needs the Emscripten SDK -- see https://emscripten.org/docs/getting_started.
set -e

if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
    source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
fi
command -v emcc >/dev/null 2>&1 || { echo "emcc not found -- install the Emscripten SDK"; exit 1; }

mkdir -p docs

emcc main.c -O2 -std=gnu11 \
    -sUSE_GLFW=3 \
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
    -sFULL_ES3=1 \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 \
    -sEXPORTED_FUNCTIONS=_main,_tk_key,_tk_scene,_tk_resize \
    -sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString \
    --preload-file bg.frag \
    --preload-file path.vert --preload-file path.frag \
    --preload-file cloud.vert --preload-file cloud.frag \
    --preload-file scenes.cfg --preload-file leapers.cfg \
    -o docs/trappedknight.js

echo "built -> docs/trappedknight.{js,wasm,data}"
