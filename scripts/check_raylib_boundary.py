#!/usr/bin/env python3
"""Game code must not call raylib directly; src/rl.h is the only seam.

Seven 'Decouple ... from direct raylib calls' commits (3f2a1d1, d27f4ab,
25c6189, d7b8b1e, dab8306, aa1caf2, 1f26f7d) each removed one subsystem's
raylib:: use so the Metal/Sokol backend could build. New code copying a
raylib:: example re-breaks that backend. Sole exception, allowlisted
below: e2e pixel readback, which has no backend-neutral equivalent yet.
"""
import pathlib, sys
ALLOW = {"src/testing/e2e_commands.h"}  # LoadImageFromTexture pixel readback only
bad=[]
for p in pathlib.Path("src").rglob("*"):
    if p.suffix not in (".h",".cpp",".mm") or str(p) in ALLOW or str(p)=="src/rl.h": continue
    for i,l in enumerate(p.read_text(errors="ignore").splitlines(),1):
        if "raylib::" in l or "#include <raylib" in l or '#include "raylib' in l:
            bad.append(f"{p}:{i}: {l.strip()}")
if bad:
    print("FAIL direct raylib outside src/rl.h (use afterhours gfx/vec types):"); print("\n".join(bad)); sys.exit(1)
print("ok raylib-boundary")
