# Changelog

## [0.7.1](https://github.com/Variiuz/CommonwealthMP/compare/v0.7.0...v0.7.1) (2026-09-05)


### Features

* 0.7.0 TCP+UDP async server, drop fakes ([8db0b91](https://github.com/Variiuz/CommonwealthMP/commit/8db0b91a61eb5aa034ea55bcb1519668838e0616))
* add dedicated UDP server, probe, and CTest ([09654dd](https://github.com/Variiuz/CommonwealthMP/commit/09654dd1d87108ca7b304cbb9e48b8e3293d299e))
* add F4SE plugin for Steam 1.11.240 ([ba5d052](https://github.com/Variiuz/CommonwealthMP/commit/ba5d0523d4f0eb4abb0b8e9d847f255bf0e575b2))
* add MO2 pack data and scripts ([899bd2f](https://github.com/Variiuz/CommonwealthMP/commit/899bd2f23fa3ebd097958e4137bc1686aa23c2ba))
* add protocol 6 headers ([354155f](https://github.com/Variiuz/CommonwealthMP/commit/354155f2ccbeae2840ee032c8ac3072e1066850e))
* **interface:** add Scaleform companion menu ([1a6673c](https://github.com/Variiuz/CommonwealthMP/commit/1a6673c0f422f2d69916276488a8247836078174))
* **plugin:** modularize client and ship HUD/menu/presence ([a8f4d72](https://github.com/Variiuz/CommonwealthMP/commit/a8f4d72532e67889dc738787cce1d62d409c1558))
* **plugin:** swap Discord RPC for Game SDK ([11abb3d](https://github.com/Variiuz/CommonwealthMP/commit/11abb3d41c96aa63fca31f43c093e5454b7f98ad))
* **plugin:** wire Discord Game SDK presence ([2c68814](https://github.com/Variiuz/CommonwealthMP/commit/2c688141c5021bf4a16218af4ab6739dc6a84219))
* **protocol:** add reliable channel for protocol 11 ([cf0a11c](https://github.com/Variiuz/CommonwealthMP/commit/cf0a11c3cca0fb743d0f54d25fb26a621b93ff92))
* **server:** split modules, console, and session policy ([1653b61](https://github.com/Variiuz/CommonwealthMP/commit/1653b613094c74dcb1c3a4e72913c69261aa0ee2))
* **tools:** add cmp-reporter crash bundle app ([c34dc07](https://github.com/Variiuz/CommonwealthMP/commit/c34dc07f4882929c9685f80964ce6bafe50d2a13))


### Bug Fixes

* **plugin:** harden ghost sync for 0.6.8 ([4106562](https://github.com/Variiuz/CommonwealthMP/commit/4106562fc4f7fab700df9eb6e958fea2c2a3f1f9))

## 0.6.8

Ghost sync and join reliability: less teleport/idle snap, working puppet anims and draw, better faces, PvP hits, nametags, Steam names, and server rate limits.

- Server no longer ACKs rate-limited appearance/inventory/hit packets before accepting them (retries work again). Blob budget raised to 64/s, Hit to 24/s; Reject sent with reliable stamps; Hit relay is reliable.
- Client staggers inventory after appearance on join; Hello retries while waiting for Welcome; ReliableRetries wired into the channel.
- Ghost freeze calls EnableAI(false) once (not every ~30 ticks); dropped kAttackingDisabled; removed MoveStart refresh thrash; sticky redraw/sneak/jump retry; puppet reset before reapply.
- Motion snap threshold raised; mid-range catch-up sped up; ghosts spawn at remote pose position.
- Head parts realloc to full player count; faceNPC cleared (Minuteman clone no longer keeps a severed head). Alternate-map crash workaround kept.
- Nametag height uses actor height/scale (~128 UU) instead of +180.
- Steam persona resolved with SEH and versioned friends interfaces; placeholder "Player" is not sticky in the INI; refresh on game-ready and join.
- Plugin and server version bumped to 0.6.8.

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
