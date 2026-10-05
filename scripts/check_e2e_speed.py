#!/usr/bin/env python3
"""Ban coupled set_agent_speed in e2e tests.

set_agent_speed also sets GameClock.debug_time_mult (see e2e_commands.h):
tests that only wanted fast walking silently raced Day->Night->Exodus.
That caused the flaky 22_stage_fill fix (a3c62dc) after set_move_speed was
added for exactly this decoupling (7e81102), yet new tests kept copying
set_agent_speed. Use set_move_speed; opt out only with a header line
'# coupled-speed-ok: <why the clock must move>'.
"""
import pathlib, sys
root = pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "tests/e2e")
bad=[]
for p in sorted(root.glob("*.e2e")):
    t=p.read_text(errors="ignore")
    if "set_agent_speed" in t and "# coupled-speed-ok:" not in t:
        bad.append(str(p))
if bad:
    print("FAIL coupled set_agent_speed (use set_move_speed, or add '# coupled-speed-ok: why'):")
    print("\n".join(bad)); sys.exit(1)
print(f"ok e2e-speed: {len(list(root.glob('*.e2e')))} files")
