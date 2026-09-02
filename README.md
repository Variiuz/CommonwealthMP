# CommonwealthMP

Fallout 4 coop over a dedicated server.

## Current State

Phase 0: separate saves, ghost presence, host FO4 as the live map. Guests join by IP; they do not share a save. Target later: host-owned world.

Two Steam clients can see each other in the same cell. Each remote player is a ghost: walk, look, worn gear, draw, holster, fire, reload, ADS, and jump. Hits on a remote ghost apply HP on that player. Unique NPCs in the host's current cell puppet on guests (match by ref, no clone). Leveled encounters, loot, and quests stay local. No VATS.

Wire protocol is 10. Older plugins are rejected. Target runtime is Steam **1.11.240**, F4SE **0.7.9**.

The server is `CommonwealthMP.Server` (UDP 7777 by default). The client is an F4SE plugin (`CommonwealthMP.dll`).

## Playability

Two players, same Fallout 4 runtime, F4SE, and plugin build.

Install F4SE 0.7.9 and Address Library for 1.11.240, then the MO2 zip (DLL + `CommonwealthMP.ini`). `CommonwealthMP.esp` is optional. Run the server. Host: load any in-world save, then `cmp_join 127 0 0 1 7777`. Guest: Join Server on the title (Name, Host, Port), or load a save and `cmp_join`.

First in-world joiner is host and is not warped. A new guest lands at the host. Menu join still needs a Commonwealth exterior host. Same-cell interiors work once you are both there.

`cmp_status` should show `ghosts=1 3D=1` when the remote body has a mesh. Join IP is the session. Tailscale to `:7777`, or forward UDP 7777.

A crash writes `CommonwealthMP.crash.txt` and `CommonwealthMP.dmp` under `Documents\My Games\Fallout4\F4SE\`.

## Layout

| Path | Role |
| --- | --- |
| `protocol/` | Shared UDP headers |
| `server/` | Dedicated server (Windows and Linux) |
| `tools/probe/` | Fake client (no Fallout 4) |
| `tests/` | Offline CTest |
| `plugin/` | F4SE plugin (Windows, Steam 1.11.240) |
| `data/` | INI and optional ESP |
| `mod/CommonwealthMP/` | MO2 staging tree |

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


The test suite ifor now isnt involving Fallout 4s runtime but just makes sanity checks on the server.

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
