#!/bin/sh
# Rebuild MapTextureRefFix.dll (SKSE plugin, Skyrim SE 1.7.104.0 only). Needs: brew install mingw-w64
cd "$(dirname "$0")" && x86_64-w64-mingw32-gcc -shared -O2 -fno-builtin -nostdlib -nostartfiles \
  -Wl,--entry=DllMain -Wl,--subsystem,windows -o MapTextureRefFix.dll MapTextureRefFix.c -lkernel32 -lshell32 && ls -la MapTextureRefFix.dll
