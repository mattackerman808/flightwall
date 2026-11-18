#!/usr/bin/env python3
"""Generate firmware assets: airline logos (32x32 RGB565, RLE) and airline names.

Inputs (downloaded into tools/.cache, not committed):
  - Airline logos from github.com/Jxck-S/airline-logos (flightaware_logos, radarbox_logos) and
    github.com/sexym0nk3y/airline-logos (logos). For each airline the most legible candidate at
    32x32 is chosen automatically (compact symbols beat wide wordmarks and thin script).
    Logos are airline trademarks; this build is for personal use on your own display.
  - OpenFlights airlines.dat (ODbL) for ICAO code -> airline name.

Outputs:
  - src/assets_logos.h
  - src/assets_airlines.h

Usage:  ../.venv/bin/python -I tools/make_assets.py
"""
import csv
import io
import pathlib
import subprocess
import sys
import urllib.request

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
CACHE = ROOT / "tools" / ".cache"
LOGO_SOURCES = [  # (name, repo, folder) in tie-break preference order
    ("flightaware", "https://github.com/Jxck-S/airline-logos.git", "flightaware_logos"),
    ("radarbox", "https://github.com/Jxck-S/airline-logos.git", "radarbox_logos"),
    ("sexym0nk3y", "https://github.com/sexym0nk3y/airline-logos.git", "logos"),
]
AIRLINES_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/airlines.dat"
SIZE = 32


def fetch_logo_dirs() -> list[tuple[str, pathlib.Path]]:
    out = []
    for name, url, folder in LOGO_SOURCES:
        repo = CACHE / url.rstrip("/").split("/")[-2]
        if not repo.exists():
            CACHE.mkdir(parents=True, exist_ok=True)
            subprocess.run(["git", "clone", "-q", "--depth", "1", "--filter=blob:none", "--sparse", url, str(repo)],
                           check=True)
        folders = {f for n, u, f in LOGO_SOURCES if u == url}
        subprocess.run(["git", "-C", str(repo), "sparse-checkout", "set", *sorted(folders)], check=True)
        out.append((name, repo / folder))
    return out


def legibility(px: list[tuple[int, int, int]]) -> float:
    """Compact, solid, bright marks score high; wide wordmarks and thin script score low."""
    lit = [(i % SIZE, i // SIZE) for i, p in enumerate(px) if max(p) > 60]
    if len(lit) < 20:
        return 0.0
    xs, ys = [x for x, _ in lit], [y for _, y in lit]
    w, h = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
    squareness = min(w, h) / max(w, h)
    density = len(lit) / (w * h)
    return squareness * density


def fetch_airlines() -> list[list[str]]:
    path = CACHE / "airlines.dat"
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        path.write_bytes(urllib.request.urlopen(AIRLINES_URL, timeout=30).read())
    return list(csv.reader(io.StringIO(path.read_text(encoding="utf-8", errors="replace"))))


def to_pixels(png: pathlib.Path) -> list[tuple[int, int, int]]:
    im = Image.open(png).convert("RGBA").resize((SIZE, SIZE), Image.LANCZOS)
    px = list(im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata())
    # Brighten logos that would be too dark on an LED panel's black background.
    vals = sorted(max(r, g, b) for r, g, b, a in px if a > 128)
    gain = 1.0
    if vals:
        p90 = vals[max(0, int(len(vals) * 0.9) - 1)]
        if p90 < 200:
            gain = min(4.0, 230 / max(p90, 1))
    out = []
    for r, g, b, a in px:
        af = a / 255
        r, g, b = (min(255, int(c * gain)) for c in (r, g, b))
        m = max(r, g, b)
        if af > 0.5 and m < 70:  # keep dark-but-opaque detail (navy text etc.) visible
            r, g, b = ((min(255, int(c * 70 / m)) if m > 8 else 70) for c in (r, g, b))
        out.append((int(r * af), int(g * af), int(b * af)))
    return out


def rgb565(p) -> int:
    r, g, b = p
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def encode_palette(px: list[tuple[int, int, int]]) -> bytes:
    """Up to 16 colors per logo. Layout: n, n x u16 RGB565 (LE), then run bytes ((len-1)<<4 | index)."""
    im = Image.new("RGB", (SIZE, SIZE))
    im.putdata(px)
    q = im.quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()
    idx = list(q.get_flattened_data() if hasattr(q, "get_flattened_data") else q.getdata())
    used = sorted(set(idx))
    remap = {old: new for new, old in enumerate(used)}
    out = bytearray([len(used)])
    for old in used:
        r, g, b = pal[old * 3:old * 3 + 3]
        c = 0 if max(r, g, b) < 12 else rgb565((r, g, b))  # keep the background truly black (not drawn)
        out += c.to_bytes(2, "little")
    i = 0
    while i < len(idx):
        j = i
        while j + 1 < len(idx) and idx[j + 1] == idx[i] and j - i < 15:
            j += 1
        out.append(((j - i) << 4) | remap[idx[i]])
        i = j + 1
    return bytes(out)


def rle(values: list[int]) -> bytes:
    """Header byte h: h&0x80 -> run of (h&0x7f)+1 copies of next u16; else literal of h+1 u16s."""
    out = bytearray()
    i, n = 0, len(values)
    while i < n:
        j = i
        while j + 1 < n and values[j + 1] == values[i] and j - i < 127:
            j += 1
        if j > i:
            out.append(0x80 | (j - i))
            out += values[i].to_bytes(2, "little")
            i = j + 1
            continue
        k = i
        lit = []
        while k < n and len(lit) < 128 and not (k + 1 < n and values[k + 1] == values[k]):
            lit.append(values[k])
            k += 1
        out.append(len(lit) - 1)
        for v in lit:
            out += v.to_bytes(2, "little")
        i = k
    return bytes(out)


def c_string(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> int:
    candidates: dict[str, list[tuple[int, str, pathlib.Path]]] = {}
    for rank, (name, folder) in enumerate(fetch_logo_dirs()):
        for p in folder.glob("*.png"):
            code = p.stem.upper()
            if len(code) == 3 and code.isascii() and code.isalnum():
                candidates.setdefault(code, []).append((rank, name, p))
    blob = bytearray()
    index = []
    picks: dict[str, str] = {}
    for code in sorted(candidates):
        best = None
        for rank, name, p in candidates[code]:
            try:
                px = to_pixels(p)
            except Exception:
                continue
            score = legibility(px) - rank * 0.03  # small tie-break toward earlier sources
            if best is None or score > best[0]:
                best = (score, name, px)
        if best is None or best[0] <= 0:
            continue
        picks[code] = best[1]
        index.append((code, len(blob)))
        blob += encode_palette(best[2])
    with open(ROOT / "src" / "assets_logos.h", "w") as f:
        f.write("// Generated by tools/make_assets.py — do not edit.\n#pragma once\n#include <Arduino.h>\n\n")
        f.write(f"#define LOGO_SIZE {SIZE}\n#define LOGO_COUNT {len(index)}\n\n")
        f.write("struct LogoEntry { char code[4]; uint32_t offset; };\n")
        f.write("static const LogoEntry LOGO_INDEX[LOGO_COUNT] PROGMEM = {\n")
        for code, off in index:
            f.write(f'  {{"{code}", {off}}},\n')
        f.write("};\n\n")
        f.write(f"static const uint8_t LOGO_DATA[{len(blob)}] PROGMEM = {{\n")
        for i in range(0, len(blob), 24):
            f.write("  " + ",".join(str(b) for b in blob[i:i + 24]) + ",\n")
        f.write("};\n")

    names = {}
    for row in fetch_airlines():
        if len(row) < 8:
            continue
        name, icao, active = row[1].strip(), row[4].strip().upper(), row[7].strip()
        if len(icao) != 3 or not (icao.isascii() and icao.isalpha()) or name in ("", "\\N"):
            continue
        # Prefer active airlines when a code is reused.
        if icao not in names or active == "Y":
            names[icao] = name[:28]
    codes = sorted(names)
    with open(ROOT / "src" / "assets_airlines.h", "w") as f:
        f.write("// Generated by tools/make_assets.py — do not edit. Source: OpenFlights airlines.dat (ODbL).\n")
        f.write("#pragma once\n#include <Arduino.h>\n\n")
        f.write(f"#define AIRLINE_COUNT {len(codes)}\n")
        f.write("struct AirlineEntry { char code[4]; const char* name; };\n")
        f.write("static const AirlineEntry AIRLINES[AIRLINE_COUNT] PROGMEM = {\n")
        for c in codes:
            f.write(f'  {{"{c}", {c_string(names[c])}}},\n')
        f.write("};\n")

    raw = len(index) * SIZE * SIZE * 2
    from collections import Counter
    print("logo sources used:", dict(Counter(picks.values())))
    print("sample picks:", {c: picks.get(c) for c in "ASA JBU FFT SKW NKS UAL AAL DAL SWA HAL QXE EJA".split()})
    print(f"logos: {len(index)}  {len(blob) / 1024:.0f} KB (raw {raw / 1024:.0f} KB, {raw / len(blob):.1f}x)")
    print(f"airline names: {len(codes)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
