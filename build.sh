#!/bin/sh
# Build MapTextureRefFix.dll with MinGW-w64 (macOS: brew install mingw-w64; Debian/Ubuntu: apt install gcc-mingw-w64-x86-64;
# Windows: MSYS2 mingw-w64-x86_64-gcc). Freestanding: no C runtime is linked.
cd "$(dirname "$0")" && x86_64-w64-mingw32-gcc -shared -O2 -fno-builtin -nostdlib -nostartfiles \
  -Wl,--entry=DllMain -Wl,--subsystem,windows -o MapTextureRefFix.dll MapTextureRefFix.c -lkernel32 -lshell32 && ls -l MapTextureRefFix.dll
