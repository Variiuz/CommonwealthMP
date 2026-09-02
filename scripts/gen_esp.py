"""Build CommonwealthMP.esp by copying a real NPC_ from Fallout4.esm.

Does not invent an empty NPC. Source FormID is chosen after a type dump of
this install's ESM. Unique is cleared. EDID is CMP_RemotePlayer.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data" / "CommonwealthMP.esp"

REC_HDR = 24
FLAG_COMPRESSED = 0x00040000
ACBS_UNIQUE = 0x00000020
ACBS_CHARGEN = 0x00000004
ACBS_USES_TEMPLATE = 0x00000100
PROBE_IDS = (0x7, 0x20593, 0x1D323, 0xBB873, 0xAA78E)
FORBIDDEN = {0x7, 0x20593}
# Skyrim HumanRace is 0x19. This FO4 ESM stores player/minuteman race as 0x13746.
DEFAULT_HUMAN_RACE = 0x13746
NEW_FORM_ID = 0x01000800
LOCAL_FORM_ID = 0x800
FO4_VERSION = 131
HEDR_VERSION = 1.0


def u32(b: bytes, o: int = 0) -> int:
    return struct.unpack_from("<I", b, o)[0]


def read_exact(f, n: int) -> bytes:
    data = f.read(n)
    if len(data) != n:
        raise EOFError(f"short read {len(data)}/{n}")
    return data


def decompress(flags: int, payload: bytes) -> bytes:
    if not (flags & FLAG_COMPRESSED):
        return payload
    if len(payload) < 4:
        raise ValueError("compressed record too small")
    return zlib.decompress(payload[4:])


def parse_subrecords(data: bytes) -> list[tuple[str, bytes]]:
    out: list[tuple[str, bytes]] = []
    i = 0
    pending_xxxx: int | None = None
    while i + 6 <= len(data):
        typ = data[i : i + 4].decode("ascii", "replace")
        size = struct.unpack_from("<H", data, i + 4)[0]
        i += 6
        if typ == "XXXX":
            pending_xxxx = u32(data, i)
            i += size
            continue
        if pending_xxxx is not None:
            size = pending_xxxx
            pending_xxxx = None
        out.append((typ, data[i : i + size]))
        i += size
    return out


def build_subrecords(subs: list[tuple[str, bytes]]) -> bytes:
    chunks: list[bytes] = []
    for typ, payload in subs:
        if len(payload) > 0xFFFF:
            chunks.append(b"XXXX" + struct.pack("<HI", 4, len(payload)))
            chunks.append(typ.encode("ascii") + struct.pack("<H", 0) + payload)
        else:
            chunks.append(typ.encode("ascii") + struct.pack("<H", len(payload)) + payload)
    return b"".join(chunks)


def edid_of(subs: list[tuple[str, bytes]]) -> str:
    for typ, payload in subs:
        if typ == "EDID":
            return payload.split(b"\x00", 1)[0].decode("ascii", "replace")
    return ""


def acbs_flags(subs: list[tuple[str, bytes]]) -> int:
    for typ, payload in subs:
        if typ == "ACBS" and len(payload) >= 4:
            return u32(payload)
    return 0


def rnam_of(subs: list[tuple[str, bytes]]) -> int:
    for typ, payload in subs:
        if typ == "RNAM" and len(payload) >= 4:
            return u32(payload)
    return 0


def has_sub(subs: list[tuple[str, bytes]], name: str) -> bool:
    return any(typ == name for typ, _ in subs)


class Record:
    __slots__ = ("typ", "flags", "formid", "revision", "version", "unknown", "data")

    def __init__(self, typ: str, flags: int, formid: int, revision: int, version: int, unknown: int, data: bytes):
        self.typ = typ
        self.flags = flags
        self.formid = formid
        self.revision = revision
        self.version = version
        self.unknown = unknown
        self.data = data


def scan_esm(path: Path) -> tuple[dict[int, str], list[Record]]:
    types: dict[int, str] = {}
    npcs: list[Record] = []
    with path.open("rb") as f:
        f.seek(0, 2)
        end = f.tell()
        f.seek(0)

        def walk(limit: int) -> None:
            while f.tell() + REC_HDR <= limit:
                hdr = read_exact(f, REC_HDR)
                typ = hdr[0:4].decode("ascii", "replace")
                size = u32(hdr, 4)
                flags = u32(hdr, 8)
                formid = u32(hdr, 12)
                revision = u32(hdr, 16)
                version, unknown = struct.unpack_from("<HH", hdr, 20)
                if typ == "GRUP":
                    group_end = f.tell() + (size - REC_HDR)
                    if group_end > limit:
                        group_end = limit
                    walk(group_end)
                    continue
                payload = read_exact(f, size)
                types[formid] = typ
                if typ != "NPC_":
                    continue
                try:
                    data = decompress(flags, payload)
                except Exception:
                    continue
                rec = Record(typ, flags & ~FLAG_COMPRESSED, formid, revision, version, unknown, data)
                npcs.append(rec)

        walk(end)
    return types, npcs


def pick_source(npcs: list[Record]) -> Record:
    by_id = {rec.formid & 0x00FFFFFF: rec for rec in npcs}
    human_race = DEFAULT_HUMAN_RACE
    player = by_id.get(0x7)
    if player is not None:
        player_race = rnam_of(parse_subrecords(player.data))
        if player_race:
            human_race = player_race
    print(f"Human race FormID from player NPC_: {human_race:08X}")

    scored: list[tuple[int, Record, str]] = []
    for rec in npcs:
        local = rec.formid & 0x00FFFFFF
        if local in FORBIDDEN:
            continue
        subs = parse_subrecords(rec.data)
        edid = edid_of(subs)
        if not edid:
            continue
        flags = acbs_flags(subs)
        if flags & ACBS_UNIQUE:
            continue
        if flags & ACBS_CHARGEN:
            continue
        race = rnam_of(subs)
        if race != human_race:
            continue
        score = 20
        if not has_sub(subs, "VMAD"):
            score += 20
        if "Minuteman" in edid or "Settler" in edid:
            score += 15
        if "Template" in edid:
            score += 8
        if has_sub(subs, "DOFT") or has_sub(subs, "SOFT"):
            score += 12
        if flags & ACBS_USES_TEMPLATE or has_sub(subs, "TPLT"):
            continue
        scored.append((score, rec, edid))
    if not scored:
        raise SystemExit("No usable human NPC_ in Fallout4.esm")
    scored.sort(key=lambda x: (-x[0], x[1].formid))
    print("Top human NPC_ candidates:")
    for score, rec, edid in scored[:12]:
        print(f"  {rec.formid:08X}  score={score:3d}  {edid}")
    by_id = {rec.formid & 0x00FFFFFF: rec for rec in npcs}
    preferred = by_id.get(0x1D323)
    if preferred is not None:
        pref_subs = parse_subrecords(preferred.data)
        if edid_of(pref_subs):
            print("Using verified probe NPC_ 0001D323")
            return preferred
    return scored[0][1]


KEEP_SUBS = {
    "EDID",
    "OBND",
    "ACBS",
    "SNAM",
    "VTCK",
    "RNAM",
    "AIDT",
    "CNAM",
    "DATA",
    "DNAM",
    "HCLF",
    "NAM5",
    "NAM6",
    "NAM4",
    "MWGT",
    "NAM8",
    "DOFT",
    "SOFT",
    "FTST",
}
# Face morph / tint blocks are what 1.11 dropped from our earlier full copies.
DROP_PREFIX = ()
ACBS_PC_LEVEL_MULT = 0x00000080


def rewrite_npc(src: Record) -> bytes:
    # Keep a body/AI skeleton from a real ESM NPC. Drop face morphs and templates.
    # Do not invent an empty NPC: FO4 still needs race + ACBS from a live record.
    subs = parse_subrecords(src.data)
    out: list[tuple[str, bytes]] = []
    for typ, payload in subs:
        if typ.startswith("FM") or typ in {"MSDK", "MSDV", "MRSV", "TETI", "TEND", "QNAM", "PRPS", "COCT", "CNTO", "PKID"}:
            continue
        if typ not in KEEP_SUBS and typ != "FULL":
            continue
        if typ == "EDID":
            out.append(("EDID", b"CMP_RemotePlayer\x00"))
            continue
        if typ == "FULL":
            out.append(("FULL", b"CMP Remote\x00"))
            continue
        if typ == "ACBS" and len(payload) >= 4:
            flags = u32(payload) & ~ACBS_UNIQUE & ~ACBS_CHARGEN & ~ACBS_USES_TEMPLATE & ~ACBS_PC_LEVEL_MULT
            payload = struct.pack("<I", flags) + payload[4:]
            if len(payload) >= 16:
                payload = payload[:14] + struct.pack("<H", 0) + payload[16:]
        out.append((typ, payload))
    if not any(t == "EDID" for t, _ in out):
        out.insert(0, ("EDID", b"CMP_RemotePlayer\x00"))
    if not any(t == "FULL" for t, _ in out):
        out.insert(1, ("FULL", b"CMP Remote\x00"))
    if not any(t == "RNAM" for t, _ in out):
        out.append(("RNAM", struct.pack("<I", DEFAULT_HUMAN_RACE)))
    return build_subrecords(out)


def rec(typ: str, flags: int, formid: int, version: int, data: bytes) -> bytes:
    hdr = typ.encode("ascii") + struct.pack("<IIIIHH", len(data), flags, formid, 0, version, 0)
    return hdr + data


def sub(typ: str, payload: bytes) -> bytes:
    return typ.encode("ascii") + struct.pack("<H", len(payload)) + payload


def write_esp(npc_data: bytes, src_id: int) -> None:
    hedr = struct.pack("<fII", HEDR_VERSION, 1, LOCAL_FORM_ID + 1)
    tes4_data = b"".join(
        [
            sub("HEDR", hedr),
            sub("CNAM", b"CommonwealthMP\x00"),
            sub("SNAM", f"CMP_RemotePlayer copied from Fallout4.esm {src_id:08X}\x00".encode("ascii")),
            sub("MAST", b"Fallout4.esm\x00"),
            sub("DATA", struct.pack("<Q", 0)),
            sub("INTV", struct.pack("<I", 1)),
        ]
    )
    tes4 = rec("TES4", 0, 0, FO4_VERSION, tes4_data)
    npc = rec("NPC_", 0, NEW_FORM_ID, FO4_VERSION, npc_data)
    grup_size = REC_HDR + len(npc)
    grup = b"GRUP" + struct.pack("<I", grup_size) + b"NPC_" + struct.pack("<IHH", 0, 0, 0) + struct.pack(
        "<HH", FO4_VERSION, 0
    )
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(tes4 + grup + npc)
    verify_esp(OUT)
    print(f"Wrote {OUT} ({OUT.stat().st_size} bytes) FormID {NEW_FORM_ID:08X} from {src_id:08X}")


def verify_esp(path: Path) -> None:
    data = path.read_bytes()
    if data[0:4] != b"TES4":
        raise SystemExit("ESP missing TES4")
    tes4_size = u32(data, 4)
    tes4 = parse_subrecords(data[24 : 24 + tes4_size])
    hedr = next((p for t, p in tes4 if t == "HEDR"), b"")
    if len(hedr) < 4 or abs(struct.unpack_from("<f", hedr)[0] - HEDR_VERSION) > 0.01:
        raise SystemExit("ESP HEDR is not 1.0")
    npc_at = data.find(b"NPC_", 24 + tes4_size + 24)
    if npc_at < 0:
        raise SystemExit("ESP has no NPC_ record")
    size = u32(data, npc_at + 4)
    formid = u32(data, npc_at + 12)
    subs = parse_subrecords(data[npc_at + 24 : npc_at + 24 + size])
    if edid_of(subs) != "CMP_RemotePlayer":
        raise SystemExit(f"ESP EDID is {edid_of(subs)!r}")
    if formid != NEW_FORM_ID:
        raise SystemExit(f"ESP form {formid:08X}")
    if rnam_of(subs) == 0:
        raise SystemExit("ESP NPC_ has no race")
    if acbs_flags(subs) & (ACBS_UNIQUE | ACBS_CHARGEN | ACBS_USES_TEMPLATE):
        raise SystemExit("ESP NPC_ still unique/chargen/template")
    print("ESP verify OK EDID=CMP_RemotePlayer race=%08X acbs=%08X" % (rnam_of(subs), acbs_flags(subs)))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--esm", type=Path, required=True)
    ap.add_argument("--dump-only", action="store_true")
    args = ap.parse_args()
    if not args.esm.is_file():
        print("Fallout4.esm not found:", args.esm)
        return 2

    print("Scanning", args.esm, f"({args.esm.stat().st_size} bytes)")
    types, npcs = scan_esm(args.esm)
    print("Records with dumped types:", len(types), "NPC_:", len(npcs))
    npc_by_id = {rec.formid & 0x00FFFFFF: rec for rec in npcs}
    for fid in PROBE_IDS:
        rec = npc_by_id.get(fid)
        extra = ""
        if rec:
            subs = parse_subrecords(rec.data)
            extra = f" EDID={edid_of(subs)} race={rnam_of(subs):08X} acbs={acbs_flags(subs):08X}"
        print(f"  GetFormByID {fid:08X} ESM type={types.get(fid, 'MISSING')}{extra}")

    if args.dump_only:
        return 0

    src = pick_source(npcs)
    src_subs = parse_subrecords(src.data)
    print(
        f"Copying {src.formid:08X} EDID={edid_of(src_subs)} race={rnam_of(src_subs):08X} "
        f"acbs={acbs_flags(src_subs):08X}"
    )
    write_esp(rewrite_npc(src), src.formid)
    return 0


if __name__ == "__main__":
    sys.exit(main())
