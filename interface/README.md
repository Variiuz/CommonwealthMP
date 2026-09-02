# Interface (Scaleform)

Companion SWF for pause/title menu rows (`JOIN` / `HOST` / `DISCONNECT`). C++ registers `root.cmp.*` and draws ImGui panels.

```bat
setup.bat --flex-only
cd interface\swf
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

Output lands in `mod/CommonwealthMP/Interface/CommonwealthMP_Menu.swf`. Toolchain is under `interface/swf/_tools/` (gitignored).
