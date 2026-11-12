#!/usr/bin/env bash
set -euo pipefail

mkdir -p bin

x86_64-w64-mingw32-gcc \
  -shared -O2 -Wall -Wextra -s \
  -ffreestanding -fno-builtin -nostdlib \
  -e DllMain \
  -o bin/GameInput.dll \
  src/gameinput_shim.c \
  src/gameinput.def \
  -lkernel32

echo "Built bin/GameInput.dll successfully."
