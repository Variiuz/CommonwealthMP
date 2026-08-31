# After 0.5.7

0.5.7 is protocol 6. Two clients see each other in the same cell. The host Fallout 4 process is the live map. The dedicated server is a relay and a player ledger. It does not merge `.fos` files.

## World NPCs

Player ghosts only. Settlers, enemies, and other world actors stay local.

## Animation

Pose flags drive walk, sneak, sprint, look, and draw. Attack and reload are visual only. No hit registration, no VATS.

## Interiors

Same-cell ghosts work. Menu join still needs a Commonwealth exterior host for chargen. Door attach and floating 3D nameplates can go further.

## Cell ownership

Who owns which cell, who simulates actors. Later.

## Chargen

Join Server from the title (skip vault, Looks and SPECIAL, spawn at host). Hosting is still in-world `cmp_join`.

## Server ops

- Async persist (keep disk off the UDP thread)
- Richer JSON log sink

## Steam lobby

`cmp_lobby` only checks whether Steam matchmaking exists. It does not create a lobby. Join IP is the session. Do not call `SteamAPI_Init` again, and do not call `SteamGameServer_Init` inside `Fallout4.exe`.

NAT: Tailscale, ZeroTier, or UDP 7777 forward. Partner Game Server API is not available.

## GOG 1.10.163

Separate plugin. F4SE 0.6.23. Address Library ID space `1_10_980`. Same `protocol/` and same server.

## Tilted vs independent

TiltedEvolution FO4 lives on branch `falloutTogether`; current `dev` dropped the FO4 client. GPL-3: ideas only, do not copy source. Re-evaluate after interiors and combat if staying independent is more work than a clean-room protocol plus our ghosts.

## Out of scope

Pip-Boy pause, VATS, power armor, workshops, companions, radiant quests, fast travel, save merging, dual `PlayerRef`, Game Pass, container anti-dupe, combat damage, loot.
