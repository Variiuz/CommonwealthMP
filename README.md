# CommonwealthMP

CommonwealthMP (short CMP) is a multiplayer mod for Fallout 4. It is early development and not yet reliably playable: open-source, free, not feature-complete. Focus is the dedicated server and F4SE plugin.

## Current State
Currently it is "playable" in the streched sense: 2+ players can join a host world and see each other.
Guests join by IP; they do not share a save; this means progress SHOULD only happen on the host. The plugin is not yet stable; crashes are expected. The server is a separate process and does not require Fallout 4. The UI is bad, I am not really familiar with swfs etc but i try my best. Main goal is animations, inventory, appareance, world sync.

Wire protocol is **11**. 

Target runtime: Steam **1.11.240**, F4SE **0.7.9**.
Server: `CommonwealthMP.Server` (UDP 7777 by default). Client: F4SE plugin `CommonwealthMP.dll`.

## Playability

I would not call it "playable" yet. It is a work-in-progress, and the plugin is not stable. Expect crashes, desyncs, and other rather weird issues :D
You can run around with friends, but do not expect to be able to reliably play :)


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
Third-party C++ libs are git submodules under `plugin/lib/` (`commonlibf4`, `discord-rpc`).
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

## License

GPL-3.0-or-later with `EXCEPTIONS` (modding exception and linking exception).
