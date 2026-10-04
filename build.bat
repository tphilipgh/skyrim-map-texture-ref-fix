@echo off
rem Build MapTextureRefFix.dll with MSVC. Run from an "x64 Native Tools Command Prompt for VS".
rem /MT links the static CRT so the DLL has no vcruntime dependency.
cd /d "%~dp0"
cl /nologo /O2 /W4 /MT /LD /Fe:MapTextureRefFix.dll MapTextureRefFix.c kernel32.lib shell32.lib /link /SUBSYSTEM:WINDOWS
if errorlevel 1 exit /b 1
del /q MapTextureRefFix.obj MapTextureRefFix.exp MapTextureRefFix.lib 2>nul
dir MapTextureRefFix.dll
