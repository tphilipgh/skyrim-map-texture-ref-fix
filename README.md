# MapTextureRefFix

## Introduction

A 10 KB SKSE64 plugin that fixes the **map-menu crash** introduced in Skyrim Special Edition
by the August 2026 "Creations" update (game versions 1.7.99 / 1.7.104). This crash affects
CrossOver Mac users and may potentially affect Proton (Linux) users as well. If you encounter
intermittent crashes when opening the map, this may solve the issue.

As a disclaimer, agentic AI (specifically Claude Code) was used to diagnose the issue and 
create this patch.

## Symptoms

The game crashes to desktop when opening the map. This plugin fixes the crash on
macOS under CrossOver with the DXMT backend, where it surfaces as `EXCEPTION_ILLEGAL_INSTRUCTION`
(`ud2`) inside `d3d11.dll` with `MapMenu` and `BSScaleformImageLoader` on the stack, and
potentially other Wine platforms as well (Proton, Whisky, plain Wine). Console, Proton,
and some Windows players report the same crash; the underlying bug is in the game executable.

## Root cause

`BSScaleformImageLoader` serves the local-map image (`img://Local_Map`) to the Scaleform UI.
In 1.7.104.0 the render-target branch of that code does this:

```
texture = renderer->renderTargets[idx].texture   ; plain array read, NO AddRef
scaleformTexture->Init(texture)                  ; AddRef + GetDesc + CreateShaderResourceView
texture->Release()                               ; <-- unbalanced
```

The sibling branch for ordinary textures obtains its pointer through `ID3D11View::GetResource()`,
which *does* hand back an owned reference, so its `Release()` is balanced. The render-target
branch is not. Every map open drops one reference on the local-map render target; once the count
reaches zero the texture is destroyed while the renderer still holds a pointer to it. The next
map open is a use-after-free.

How quickly that bites depends on how fast each platform recycles freed objects: consoles and
Wine reuse the memory promptly (deterministic crash), the Windows D3D runtime tends to leave the
corpse intact for a while (rarely crashes), and DXMT destroys resources asynchronously (a race,
roughly 1 in 10 opens).

## Bug 2: texture double release (v1.1)

`BSGraphics::Renderer::DestroyTexture` (`SkyrimSE.exe+0x100F190`) runs when a texture's last
engine reference goes away. It releases the texture's three D3D11 objects (the `ID3D11Texture2D`,
its `ID3D11ShaderResourceView`, and a third view slot) and then, in a second identical block at
`+0x100F1DE`, releases all three **again**. The matching allocation code (`+0x100DE60`) stores
the pointers straight from `CreateTexture2D` / `CreateShaderResourceView` and never AddRefs, so
every destroyed texture is over-released by one.

How it shows up depends on the D3D11 implementation: random crashes in texture cleanup with
`NiSourceTexture` on the stack, heap-corruption crashes in unrelated code (input polling, UI),
and under Apple's D3DMetal a deterministic crash at the main menu because it frees objects
immediately. The plugin replaces the 45-byte duplicate block with a short jump over it, after
verifying the surrounding bytes.

## The fix

NOP the 3-byte `call [rdx+0x10]` (`FF 52 10`) at `SkyrimSE.exe+0x117312B`. The renderer's own
reference is the one that is supposed to keep the texture alive.

The plugin:

* declares itself compatible with **1.7.104.0 only** (SKSE refuses to load it on any other build),
* verifies the 14-byte instruction sequence `48 8B 16 48 8B CE FF 52 10 B8 01 00 00 00` at
  `SkyrimSE.exe+0x1173125` before touching anything,
* patches the three bytes in memory at load time (the executable on disk is untouched, so Steam
  file verification never fights it),
* logs what it did to `Documents\My Games\Skyrim Special Edition\SKSE\MapTextureRefFix.log`.

It has no dependencies beyond SKSE64 itself: no Address Library, no CommonLib, no C runtime.
Imports are `kernel32.dll` and `shell32.dll` only.

## Install

1. Install SKSE64 2.3.1 (the build for 1.7.104).
2. Drop `MapTextureRefFix.dll` into `Data\SKSE\Plugins\`.
3. Launch through `skse64_loader.exe`. Check `skse64.log` for
   `plugin MapTextureRefFix.dll ... loaded correctly` and `MapTextureRefFix.log` for `applied`.

Tested on macOS (CrossOver 26.3, DXMT backend). It patches the same executable, so it may work
unchanged on other Wine platforms and on Windows for 1.7.104.0, but those are untested.

## Build

The source is plain C against the Win32 API and builds with any x64 Windows toolchain.

* **MSVC** (Windows): open an "x64 Native Tools Command Prompt" and run `build.bat`.
* **MinGW-w64** (Windows via MSYS2, Linux, or macOS via `brew install mingw-w64`): run `build.sh`.
* **clang-cl** works with the same command line as `build.bat`.

## Notes

* When Bethesda ships a build newer than 1.7.104, the plugin will simply not load. Re-verify
  the offsets before updating the version constant in the source.
* The game still holds a raw pointer into the renderer's render-target table. That is normal for
  this engine; the fix just stops the reference count from reaching zero underneath it.

## License

MIT.
