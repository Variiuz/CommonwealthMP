# MO2 pack

`scripts\pack-mo2.ps1` writes `dist/CommonwealthMP-<version>-mo2.zip`. Drag that onto Mod Organizer.

```
CommonwealthMP.esp
F4SE/Plugins/CommonwealthMP.dll
F4SE/Plugins/CommonwealthMP.ini
Interface/CommonwealthMP_Menu.swf   (optional; build with interface/swf/build.ps1)
fomod/info.xml
fomod/ModuleConfig.xml
```

Enable the mod and `CommonwealthMP.esp`. Address Library stays a separate mod. F4SE stays in the game root.

```
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1 -Build
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1 -Build -ForceRebuild
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1 -Build -Esp -Esm D:\Steam\steamapps\common\Fallout 4\Data\Fallout4.esm
```

`-Build` is incremental (skip when fresh; otherwise only dirty TUs). `-ForceRebuild` does a full plugin rebuild. `-Esp` needs this install's `Fallout4.esm` (`scripts/gen_esp.py`).

To copy into an existing MO2 mods folder instead of making a zip, pass that install:

```
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1 -Mo2 -Mo2Path D:\Modding\MO2
```
