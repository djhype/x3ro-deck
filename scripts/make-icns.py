#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds data/icons/x3ro-deck.icns from the PNGs next to it.

ICNS is a list of (type, length, PNG) entries; macOS reads PNG payloads for
these types. Run after regenerating the PNGs (see CLAUDE.md, Quirks), with
x3ro-deck-1024.png rendered by `rsvg-convert -w 1024 -h 1024`.
"""
import struct
import sys
from pathlib import Path

icons = Path(__file__).resolve().parent.parent / "data" / "icons"
# type → source size in pixels
entries = [("ic11", 32), ("ic12", 64), ("ic07", 128), ("ic13", 256), ("ic08", 256), ("ic14", 512), ("ic09", 512), ("ic10", 1024)]
chunks = b""
for kind, size in entries:
    png = icons / f"x3ro-deck-{size}.png"
    if not png.exists():
        sys.exit(f"missing {png}")
    data = png.read_bytes()
    chunks += kind.encode() + struct.pack(">I", len(data) + 8) + data
(icons / "x3ro-deck.icns").write_bytes(b"icns" + struct.pack(">I", len(chunks) + 8) + chunks)
