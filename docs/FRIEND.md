# Two players

Same Fallout 4 runtime (`Fallout4.exe` FileVersion and `getf4seversion`). Same `CommonwealthMP.dll`. Overlay off. `CommonwealthMP.esp` is optional.

They do not need your save. You host from an in-world save (`cmp_join`). They Join Server from the title, or load any save and `cmp_join`.

We do not ship Bethesda files.

Success: both of you in the same cell. Each sees a named ghost that walks, looks, wears gear, and draws. Attack and reload are visual only. Different cells show a HUD note. `--fake` is a one-PC walk check; it turns off at two clients.

## Zip

- `CommonwealthMP.dll` (built for their `Fallout4.exe` version)
- `CommonwealthMP.ini`
- `CommonwealthMP.esp` (optional)
- this file

## Their install

1. Steam Fallout 4 matching yours (1.11.240 cannot join an older Steam build).
2. F4SE 0.7.9 and Address Library for that runtime.
3. Copy DLL and INI to `Data\F4SE\Plugins\`.
4. Launch `f4se_loader.exe`. Host: in-world save, then `cmp_join`. Guest: Join Server, or load a save and `cmp_join`.

## How they reach you

Both on Tailscale (or ZeroTier), friend joins your Tailscale IP `:7777`.

Or forward UDP 7777. That fails on CGNAT.

You run `CommonwealthMP.Server.exe` on this PC. You join `127.0.0.1:7777`. Friend uses `100.x.x.x:7777` or your public IP.

Console: `cmp_join 100 x x x 7777` (octets and port).

Leave `PlayerName` empty in the INI to use Steam. Older plugins are rejected (`need protocol 6`). Both sides need **0.5.7**.

Before you start: `getf4seversion`, `Fallout4.exe` FileVersion, and `cmp_probeforms` shows SourceForm `0001D323` as `NPC_`.
