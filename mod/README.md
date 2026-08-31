# MO2 pack

`scripts\pack-mo2.ps1` writes `dist/CommonwealthMP-<version>-mo2.zip`. Drag that onto Mod Organizer.

```
CommonwealthMP.esp
F4SE/Plugins/CommonwealthMP.dll
F4SE/Plugins/CommonwealthMP.ini
fomod/info.xml
fomod/ModuleConfig.xml
```

Install on the CommonwealthMP profile. Enable the mod and `CommonwealthMP.esp`.

```
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1 -Build
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1 -Build -Esp
```

`-Esp` needs this install's `Fallout4.esm` (`scripts/gen_esp.py`).

Refresh the live MO2 mod folder:

```
powershell -ExecutionPolicy Bypass -File scripts\update-mod.ps1
```

Close the game first. Address Library stays a separate mod. F4SE stays in the game root.
