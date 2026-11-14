
<img width="1920" height="720" alt="cmp-banner-transparent" src="https://github.com/user-attachments/assets/2d31d5bb-e684-4758-a808-17f9bd2cb015" />


---
> **Heads up** I am currently fully reworking CMP, leaning more into a server-sided approach instead relying on client games todo the work. I am already further than this version.
> So keep in mind what you see here is mostly research worth and not up to my standards. I will keep this updated as soon as I have a stable-ish version. Thank you :)

 
# What is CMP?
CommonwealthMP (short CMP) is a multiplayer mod for Fallout 4. It is early development but growing stable by the day.
My main goal is to deliver a fun and stable expanded Fallout 4 CO-OP experience without any downsides.

## What does expanded mean?
Adding features any co-op game would need, like:
- Reviving downed teammates
- Teleporting to players
- Chat
- Emotes
- Shared loot, instanced loot containers, shared Quests and rewards
- In-Sync NPCs
- Smooth Movements
- Settlement Building together

And much more! 


## Current State
> This applies only to the Legacy code in this repo, not any new code!

Currently it is "playable" but on a simple basis: 2+ players can join a host world and see each other.
Guests join by IP; they do not share a save; this means progress SHOULD only happen on the host. The plugin is not yet stable; crashes are expected. The server is a separate process and does not require Fallout 4. The UI is bad, I am not really familiar with swfs etc but i try my best. Main goal is animations, inventory, appareance, world sync.




## Build the server

Needs CMake and a C++20 compiler. No Fallout 4.

Windows (Visual Studio):

```
cmake -B build/server -S . -A x64
cmake --build build/server --config Release
ctest --test-dir build/server -C Release --output-on-failure
```

Linux:

```
cmake -B build/server -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build/server
ctest --test-dir build/server --output-on-failure
```

Or on Windows, `scripts\build-server.ps1`.

```
dist/server/CommonwealthMP.Server-<version>.exe    # Windows (release or git-dev suffix)
dist/server/CommonwealthMP.Server                    # Linux
```


The test suite does not involve the Fallout 4 runtime; it only runs server sanity checks.

## Setup

From the repo root (submodules + optional Flex toolchain for the menu SWF):

```
setup.bat
```

`setup.bat --deps-only` skips Flex. `setup.sh` is the same for WSL/macOS/Linux helpers.
Third-party C++: `commonlibf4` is a git submodule under `plugin/lib/`. Discord Game SDK comes from xmake (`add_requires("discord")`) and ships `discord_game_sdk.dll` next to the plugin.
The Scaleform Flex SDK lives in `interface/swf/_tools/`.


## Build the plugin

Windows only.

```
setup.bat --deps-only
cd plugin
xmake f -m releasedbg -a x64
xmake build
```

Output: `plugin/build/windows/x64/releasedbg/CommonwealthMP.dll`.

Public install is the MO2 zip:

```
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1 -Build
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1 -Build -ForceRebuild
```

`-Build` compiles incrementally (or skips when the DLL is already fresh). `-ForceRebuild` forces a full plugin rebuild. Output: `dist/CommonwealthMP-<version>-mo2.zip`. See `mod/README.md`.

We currently target: Steam Rel FO4 **1.11.240**, F4SE **0.7.9**.

Packaged in is the discord social sdk or whatever they named their 50th sdk after abandoning the other 49. Currently cannot be toggled off, as a warning.


## Disclaimer
I use AI to get work done faster and research more effective. 

## License

GPL-3.0-or-later with `EXCEPTIONS` (modding exception and linking exception).
