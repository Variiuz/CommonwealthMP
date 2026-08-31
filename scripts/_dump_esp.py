from __future__ import annotations

import struct
from pathlib import Path

path = Path(r"C:\Users\alexp\Projects\commonwealthmp\data\CommonwealthMP.esp")
data = path.read_bytes()
print("size", len(data))


def u32(o: int) -> int:
    return struct.unpack_from("<I", data, o)[0]


i = 0
while i + 24 <= len(data):
    typ = data[i : i + 4].decode("ascii", "replace")
    size = u32(i + 4)
    if typ == "GRUP":
        print(f"GRUP size={size} label={data[i+8:i+12]!r} gtype={u32(i+12)} at={i}")
        i += 24
        continue
    flags = u32(i + 8)
    fid = u32(i + 12)
    ver = struct.unpack_from("<H", data, i + 20)[0]
    payload = data[i + 24 : i + 24 + size]
    print(f"{typ} size={size} flags={flags:08X} form={fid:08X} ver={ver} at={i}")
    j = 0
    pending = None
    while j + 6 <= len(payload):
        st = payload[j : j + 4].decode("ascii", "replace")
        ss = struct.unpack_from("<H", payload, j + 4)[0]
        if st == "XXXX":
            pending = struct.unpack_from("<I", payload, j + 6)[0]
            j += 6 + ss
            continue
        if pending is not None:
            ss = pending
            pending = None
        body = payload[j + 6 : j + 6 + ss]
        extra = ""
        if st in {"EDID", "CNAM", "SNAM", "MAST", "FULL"}:
            extra = body.split(b"\x00", 1)[0].decode("ascii", "replace")
        elif st == "HEDR" and len(body) >= 12:
            extra = str(struct.unpack_from("<fII", body))
        elif st == "RNAM" and len(body) >= 4:
            extra = f"race={struct.unpack_from('<I', body)[0]:08X}"
        elif st == "ACBS" and len(body) >= 4:
            extra = f"len={len(body)} flags={struct.unpack_from('<I', body)[0]:08X}"
        elif st in {"TPLT", "DOFT", "SOFT", "VTCK"} and len(body) >= 4:
            extra = f"{struct.unpack_from('<I', body)[0]:08X}"
        print(f"  {st:4} {ss:5} {extra}")
        j += 6 + ss
    i += 24 + size
