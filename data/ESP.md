# CommonwealthMP.esp (optional)

Ghosts do not need the ESP. The plugin clones vanilla Minuteman `0001D323` (INI `Ghost.SourceForm`) per peer. `cmp_probeforms` should show that form as `NPC_`.

The pack may still ship `data/CommonwealthMP.esp` so older installs keep it. It is unused for spawn.

`Ghost.Spawn=0` in `CommonwealthMP.ini` joins without bodies.

If you enable the ESP:

```
Fallout4.esm
... DLC ...
CommonwealthMP.esp
```
