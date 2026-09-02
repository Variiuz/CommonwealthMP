# Plugin (F4SE)

Steam **1.11.240**, F4SE **0.7.9**, Address Library. Protocol 11.

```
setup.bat --deps-only
cd plugin
xmake f -m releasedbg -a x64
xmake build
```

Output: `plugin/build/windows/x64/releasedbg/CommonwealthMP.dll` plus `discord_game_sdk.dll` (Discord Game SDK runtime, copied by xmake after build).

Console: `cmp_status`, `cmp_join 127 0 0 1 7777`, `cmp_query`, `cmp_leave`, `cmp_dump`, `cmp_anim`, `cmp_modhash`, `cmp_kick`, `cmp_teleport`.

Overlay: `Insert` toggles ImGui chat/debug (`[UI]` in `CommonwealthMP.ini`). Presence: `[Presence]` Discord on by default (Game SDK); Steam off (`cmp_steam` before enabling).
