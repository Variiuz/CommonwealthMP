"""Retarget CK_Test.esp -> data/CommonwealthMP.esp with EDID CMP_RemotePlayer."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "CK_Test.esp"
OUT = ROOT / "data" / "CommonwealthMP.esp"

FLAG_COMPRESSED = 0x00040000
ACBS_UNIQUE = 0x00000020
ACBS_CHARGEN = 0x00000004
ACBS_USES_TEMPLATE = 0x00000100
NEW_EDID = b"CMP_RemotePlayer\x00"


def u32(b: bytes, o: int = 0) -> int:
    return struct.unpack_from("<I", b, o)[0]


def parse_subs(data: bytes) -> list[tuple[str, bytes]]:
    out: list[tuple[str, bytes]] = []
    i = 0
    pending: int | None = None
    while i + 6 <= len(data):
        typ = data[i : i + 4].decode("ascii", "replace")
        size = struct.unpack_from("<H", data, i + 4)[0]
        i += 6
        if typ == "XXXX":
            pending = u32(data, i)
            i += size
            continue
        if pending is not None:
            size = pending
            pending = None
        out.append((typ, data[i : i + size]))
        i += size
    return out


def build_subs(subs: list[tuple[str, bytes]]) -> bytes:
    chunks: list[bytes] = []
    for typ, payload in subs:
        if len(payload) > 0xFFFF:
            chunks.append(b"XXXX" + struct.pack("<HI", 4, len(payload)))
            chunks.append(typ.encode("ascii") + struct.pack("<H", 0) + payload)
        else:
            chunks.append(typ.encode("ascii") + struct.pack("<H", len(payload)) + payload)
    return b"".join(chunks)


def main() -> int:
    data = bytearray(SRC.read_bytes())
    # Locate first NPC_ after TES4
    tes4_size = u32(data, 4)
    npc_at = data.find(b"NPC_", 24 + tes4_size + 24)
    if npc_at < 0:
        raise SystemExit("no NPC_")
    size = u32(data, npc_at + 4)
    flags = u32(data, npc_at + 8)
    formid = u32(data, npc_at + 12)
    payload = bytes(data[npc_at + 24 : npc_at + 24 + size])
    if flags & FLAG_COMPRESSED:
        payload = zlib.decompress(payload[4:])
    subs = parse_subs(payload)
    out: list[tuple[str, bytes]] = []
    for typ, body in subs:
        if typ == "EDID":
            out.append(("EDID", NEW_EDID))
            continue
        if typ == "TPLT":
            continue
        if typ == "FULL":
            out.append(("FULL", b"CMP Remote\x00"))
            continue
        if typ == "ACBS" and len(body) >= 4:
            f = u32(body) & ~ACBS_UNIQUE & ~ACBS_CHARGEN & ~ACBS_USES_TEMPLATE
            body = struct.pack("<I", f) + body[4:]
            if len(body) >= 16:
                body = body[:14] + struct.pack("<H", 0) + body[16:]
        out.append((typ, body))
    if not any(t == "EDID" for t, _ in out):
        out.insert(0, ("EDID", NEW_EDID))
    if not any(t == "FULL" for t, _ in out):
        out.insert(1, ("FULL", b"CMP Remote\x00"))
    new_payload = build_subs(out)
    # Write uncompressed NPC_ so FO4 load is simple
    new_flags = flags & ~FLAG_COMPRESSED
    hdr = (
        b"NPC_"
        + struct.pack("<IIIIHH", len(new_payload), new_flags, formid, 0, 131, 0)
    )
    # Rebuild file: TES4 + GRUP header + NPC
    tes4 = bytes(data[: 24 + tes4_size])
    grup_at = 24 + tes4_size
    assert data[grup_at : grup_at + 4] == b"GRUP"
    npc_blob = hdr + new_payload
    grup_size = 24 + len(npc_blob)
    grup = (
        b"GRUP"
        + struct.pack("<I", grup_size)
        + b"NPC_"
        + struct.pack("<IHH", 0, 0, 0)
        + struct.pack("<HH", 131, 0)
    )
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(tes4 + grup + npc_blob)
    # verify
    v = OUT.read_bytes()
    ni = v.find(b"NPC_", 24 + tes4_size + 24)
    ns = u32(v, ni + 4)
    nsubs = parse_subs(v[ni + 24 : ni + 24 + ns])
    edid = next(p.split(b"\x00", 1)[0].decode() for t, p in nsubs if t == "EDID")
    has_tplt = any(t == "TPLT" for t, _ in nsubs)
    print(f"Wrote {OUT} ({OUT.stat().st_size} bytes) form={formid:08X} edid={edid} tplt={has_tplt}")
    if edid != "CMP_RemotePlayer" or has_tplt:
        raise SystemExit("verify failed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
