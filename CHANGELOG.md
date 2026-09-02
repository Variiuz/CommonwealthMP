# Changelog

## 0.6.7

Remote puppet polish: faces, weapon draw, locomotion, combat hits, menu poses, HUD indicators, and multiplayer menu UX.

- Head parts extract from the alternate list and apply to both primary and alternate maps; `originalRace` aligned on ghost clones so faces no longer show severed dummy heads.
- Weapon draw state held across inventory/appearance resync; `CMP_ReapplyGhostPuppet` after blob chunks.
- Puppet graph vars expanded (Pitch, TurnDelta, gait, slow walk); lower move epsilon and heading interpolation on ghosts and unique NPCs.
- PvP hit replication uses `hitData.aggressor`; ghost combat stimuli suppressed (`kAttackingDisabled`, fewer attack events on puppets).
- `PoseFlag::Pipboy`, `PoseFlag::Menu`, and `PoseFlag::SlowWalk` synced; menu open/close tracked for pipboy and container menus.
- HUD compass path probing and Pip-Boy map markers on ghost refs; `cmp_indicators` debug command.
- Main menu JOIN SERVER uses overlay only (no list injection into Settings). Version stamp bottom-right. Pause menu adds JOIN SERVER, HOST SERVER, and DISCONNECT overlays for in-save multiplayer.
- Plugin and server version bumped to 0.6.7.

## 0.6.1

Native in-game peer locators on the HUD compass and Pip-Boy map. Server gameplay policy toggles.

- HUD compass shows a marker for each remote peer in the same worldspace (host uses the quest-style icon, nearest peer is brighter/larger).
- Pip-Boy world map registers ghost actors as map markers while peers are visible; markers clear on leave, timeout, or disconnect.
- Replaces the old `SendHUDMessage` pointer spam with native HUD updates (`PointerHud=1` still toggles indicators).
- Wire protocol 10. Server `pvp`, `password`, `ban`/`unban`, and host-mod fingerprint checks on join (`cmp_modhash` on the client).
- Plugin and server version bumped to 0.6.1.

## 0.6.0

Friend-experience fixes: names, appearance, movement, heartbeat, host commands, custom server console, and better debug output.

- Wire protocol 9. Older plugins and servers are rejected.
- Default player name is `Player` instead of `fo4`.
- Appearance head-parts use safe in-place overwrite; `QueueUpdate3D` is called after changes; equip log line shows race/sex/head/morph/tint/gear counts.
- Inventory/equip: `AddObjectToContainer` uses a real `ExtraDataList`; `QueueUpdate3D` after applying inventory.
- Movement: `bWantGait`/`iWantGait` only set when sprinting; no more sprint false-positives from walking.
- UDP heartbeat every 5 s from client and server so tabbing out, death, or menus do not time out.
- `CMP_Leave` and client timeout now despawn ghosts and show a HUD message.
- Server forwards `Msg::Chat` (from server console `say`) and `Msg::Kick`/`Msg::Teleport` (host-only).
- New console commands: `cmp_kick <peerId>`, `cmp_teleport <peerId>` (host only).
- `cmp_status` now shows last send/receive pose times and ghost names.
- Server `status` command lists each peer id, key, name, address, last seen, pose count, and blob sizes.
- Windows server now opens a custom GUI console window with the mod icon, a scrolling log, and a command input box. Classic console is still used when `CMP_CONHOST=1` is set.
- Server executable is renamed to a versioned name (`CommonwealthMP.Server-0.6.0.exe` on a release tag, or `CommonwealthMP.Server-<git>-dirty.exe` on a dev build) and includes embedded icon plus version metadata.
- Build embeds git `describe --tags --always --dirty` in both the server and the plugin; the startup log and `status` show release version plus exact commit, which makes error reports reproducible.

## Unreleased

Unique same-cell NPCs puppet on guests. Ghost combat pose flags play on remote bodies. Shots that hit a remote player apply HP. Localhost can spawn more than one dummy.

- Wire protocol 8. Older plugins are rejected.
- JOIN SERVER has a Name field. Confirm writes `[Session] PlayerName`. Empty keeps the Steam persona.
- `Msg::Hit`: TESHitEvent on CMP ghosts, victim-only relay, HP on `PlayerCharacter`. Ghost clones stay alive locally. No VATS, no loot, no quests, no leveled encounters.
- Ghosts draw, fire, reload, ADS, and jump from pose flags. `FreezeGhost` no longer sets `kAttackingDisabled`. `PoseFlag::Dead` when the local player is dead.
- Dummy copies the local player's face and worn gear (label stays Dummy). Real peers still send their own appearance and outfit. Ghosts AddItem before EquipObject so clothes stick.
- Host publishes unique `ProcessLists` actors (`ActorPose`). Guests match by `refFormId` and puppet. No clone, no session persist.

## 0.5.7

Two Steam clients can see each other in the same cell (Commonwealth exterior or a shared interior).

- Wire protocol 6. Older plugins are rejected.
- Target: Fallout 4 1.11.240, F4SE 0.7.9.
- Dedicated server on UDP 7777. Join IP is the session.
- Ghosts walk, look, wear gear, draw and holster. Attack and reload are visual only.
- No world NPC sync. No combat damage.
