#!/usr/bin/env python3
"""Every e2e test must assert, or declare itself visual-only.

Past fixes removed always-true checks twice: 29ad85b (assert_* gte 0 in
15_exodus/20_full_game et al) and 7e81102 (screenshot/trivial tests).
A test with no assert_*/expect_* can only pass. Screenshot-only tests are
legitimate for rendering, but must say so: '# visual-only: <why>'.
Also bans vacuous thresholds: 'gte 0' / '>= 0' on a count that cannot be <0.
"""
import pathlib, re, sys
root = pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "tests/e2e")
bad=[]
for p in sorted(root.glob("*.e2e")):
    t=p.read_text(errors="ignore")
    if re.search(r"\b(gte|>=)\s*0\b", t): bad.append(f"{p}: vacuous >=0 assertion")
    has_assert = any(l.strip().startswith(("assert_","expect_")) for l in t.splitlines())
    if not has_assert and "# visual-only:" not in t:
        bad.append(f"{p}: no assert_*/expect_* and no '# visual-only: why'")
if bad:
    print("FAIL e2e assertions:"); print("\n".join(bad)); sys.exit(1)
print(f"ok e2e-assertions: {len(list(root.glob('*.e2e')))} files")
