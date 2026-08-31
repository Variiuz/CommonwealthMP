# CommonwealthMP

Fallout 4 coop over a dedicated UDP server.

0.5.7 lets two Steam clients see each other in the same cell (Commonwealth exterior or a shared interior). Each player is a ghost: walk, look, worn gear, draw and holster. Attack and reload are visual only. No world NPC sync, no combat damage. Wire protocol is 6. Older plugins are rejected.

The server is `CommonwealthMP.Server.exe` (UDP 7777). The client is an F4SE plugin (`CommonwealthMP.dll`). One in-world Fallout 4 client is the host map. Guests Join IP; they do not copy a save.

## Layout

| Path | Role |
| --- | --- |
| `protocol/` | Shared UDP headers |
| `server/` | Dedicated server |
| `tools/probe/` | Fake client (no Fallout 4) |
| `tests/` | Offline CTest |
| `plugin/` | F4SE plugin (Steam 1.11.240) |
| `data/` | INI and optional ESP |
| `mod/CommonwealthMP/` | MO2 staging tree |
| `docs/STASH.md` | Stash 1.11.169, then Steam update |
| `docs/FRIEND.md` | Two-player session |
| `docs/NEXT.md` | After 0.5.7 |

## Build the server

Needs CMake and Visual Studio (C++). No Fallout 4.

```
cmake -B build/server -S . -A x64
cmake --build build/server --config Release
ctest --test-dir build/server -C Release --output-on-failure
```

If CMake asks for a generator, `cmake -G` and pick the Visual Studio one it lists. Or run `scripts\build-server.ps1`.

```
dist\server\CommonwealthMP.Server.exe
powershell -ExecutionPolicy Bypass -File scripts\start-server.ps1
build\server\tools\probe\Release\CommonwealthMP.Probe.exe 127.0.0.1 7777
```

First start writes `server.ini` next to the exe if it is missing (see `server/server.ini.example`). `--fake` (default) invents a dummy until a second client joins. Stdin: `help`, `status`, `players`, `kick`, `save`, `quit`.

## Tests

`scripts\build-server.ps1` builds the server and runs CTest. `-SkipTests` compiles only. The suite is offline and does not load Fallout 4. Ghost 3D and Join Server still need the game.

## Build the plugin

```
git submodule update --init --recursive
cd plugin
xmake f -m releasedbg -a x64
xmake build
```

Output: `plugin/build/windows/x64/releasedbg/CommonwealthMP.dll`. Target runtime is Steam **1.11.240**, F4SE **0.7.9**, Address Library `1_11_137`.

Refresh the files in your MO2 mod folder (rebuilds the DLL first):

```
powershell -ExecutionPolicy Bypass -File scripts\update-mod.ps1
```

MO2 zip:

```
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1
powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1 -Build
```

Output: `dist/CommonwealthMP-0.5.7-mo2.zip`. See `mod/README.md`.

## In-game

1. Stash and update: `docs/STASH.md`. F4SE 0.7.9 and Address Library on the live folder only.
2. Install the DLL and `data/F4SE/Plugins/CommonwealthMP.ini` (MO2 zip, or copy into `Data\F4SE\Plugins\`). `CommonwealthMP.esp` is optional.
3. Run the server. Host: load any in-world save, then `cmp_join 127 0 0 1 7777`. Guest: Join Server on the title (INI Host/Port), or load a save and `cmp_join`.
4. `cmp_status` should show `ghosts=1 3D=1` when the remote body has a mesh. A crash writes `CommonwealthMP.crash.txt` and `CommonwealthMP.dmp` under `Documents\My Games\Fallout4\F4SE\`.

First in-world joiner is host and is not warped. A new guest lands at the host. A returning player uses their last pose if that worldspace matches the host.

`cmp_leave` disconnects. Join IP is the session.

## Two players

Same Fallout 4 runtime, F4SE, and plugin build. Tailscale to `:7777`, or forward UDP 7777. See `docs/FRIEND.md`.

## License

GPL-3.0-or-later with `EXCEPTIONS` (modding exception and linking exception).
