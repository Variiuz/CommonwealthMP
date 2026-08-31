# Stash 1.11.169, then Steam-update the live install

Live game: `U:\SteamLibrary\steamapps\common\Fallout 4` at **1.11.169**. After stash and Steam update, expect **1.11.240**. If FileVersion differs, retarget the plugin.

Do not deploy the 1.11.169 Vortex list onto the updated exe.

## Before Steam Update

1. In Vortex, Purge Fallout 4 so `Data` is closer to vanilla.
2. Run `scripts/stash-fo4.ps1` (copies only; it refuses if you skipped Purge unless `-SkipPurgeCheck`).
3. Confirm `U:\FO4-Stash-1.11.169\Fallout4.exe` is still 1.11.169.0.
4. Confirm Vortex copies exist under the stash (see script output).
5. Let Steam update the live `U:\SteamLibrary\steamapps\common\Fallout 4`.

## After the update

- Live U:\ is the coop bench. Install F4SE **0.7.9**, Address Library `version-1-11-240-0.bin`, and CommonwealthMP only. Leave Vortex purged for this folder.
- Old mods: launch `U:\FO4-Stash-1.11.169\f4se_loader.exe` with Steam running. Do not use the Steam Play button for the stash.
- Confirm FileVersion on `Fallout4.exe`. If it is not 1.11.240, retarget the plugin before load.

## Script

```
powershell -ExecutionPolicy Bypass -File scripts\stash-fo4.ps1
```

Optional: `-SkipPurgeCheck`. The script does not call Steam update.
