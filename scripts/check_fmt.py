#!/usr/bin/env python3
"""Guard the vendored fmt upgrade: pinned version + the fmt 8 failure mode.

fmt 8.0.1 fails with deprecated literal-operator errors at format.h
(operator"" _a / operator"" _format) under -Werror, and its FMT_STRING
path breaks on newer clang. This check pins vendor/fmt to 11.2.0 and
recompiles a probe exercising the exact API surface dance + afterhours
use (FMT_STRING, format, vformat, make_format_args, print,
dynamic_format_arg_store + arg, log.h) plus the heaviest TU
(src/render_ui.cpp, the PCH-equivalent: this repo has no PCH — see the
makefile), with every clang available on the machine.

The probe includes "log.h" FIRST, exactly like every dance TU:
FMT_HEADER_ONLY + fmt before any std header. fmt 11.2's format.h uses
unqualified ::malloc / ::free without including <cstdlib>, so a TU
that instantiates fmt without a later <cstdlib> / <iostream> (log.h
pulls in <iostream> via log/log.h) fails on clang 21 — the probe
covers that instantiation path, not just header parsing.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXPECTED_FMT_VERSION = 110200  # fmt 11.2.0
HEAVIEST_TU = "src/render_ui.cpp"

PROBE = r"""
#include "log.h"  // fmt-before-std, exactly like every dance TU.
#include <string>

int main() {
  std::string hex = fmt::format(FMT_STRING("{:x}"), 255);  // fmt 8 consteval error
  std::string s = fmt::format("{}:{}", "a", 1);
  int seven = 7;
  std::string v =
      fmt::vformat(fmt::string_view("{}"), fmt::make_format_args(seven));
  fmt::dynamic_format_arg_store<fmt::format_context> store;
  store.push_back(fmt::arg("name", "dance"));
  std::string named = fmt::vformat("{name}", store);
  fmt::print("{}{}{}\n", hex, s, v + named);
  log_info("probe {} {}", "ok", 1);
  return hex == "ff" && s == "a:1" && v == "7" && named == "dance" ? 0 : 1;
}
"""

# Flags mirroring the makefile's raylib build (minus -g/-O, plus no ccache).
TU_FLAGS = [
    "-std=c++23",
    "-Wall",
    "-Wextra",
    "-Wuninitialized",
    "-Wshadow",
    "-fblocks",
    "-DAFTER_HOURS_ENABLE_MACOS_GESTURES",
    "-DAFTERHOURS_ENABLE_PROFILING=1",
    "-DAFTER_HOURS_ENABLE_E2E_TESTING",
    "-Wno-deprecated-volatile",
    "-Wno-missing-field-initializers",
    "-Wno-c99-extensions",
    "-Wno-unused-function",
    "-Wno-sign-conversion",
    "-Wno-implicit-int-float-conversion",
    "-Ivendor",
    "-Isrc",
]

failures = []


def run(cmd, cwd):
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        failures.append(f"{' '.join(cmd)}\n{result.stdout}{result.stderr}")
    return result


def compilers():
    found = [["xcrun", "clang++"]]
    brew = subprocess.run(["brew", "--prefix"], capture_output=True, text=True)
    if brew.returncode == 0:
        llvm = Path(brew.stdout.strip()) / "opt/llvm@21/bin/clang++"
        if llvm.exists():
            found.append([str(llvm)])
    return found


def raylib_cflags():
    if shutil.which("pkg-config") is None:
        return None
    result = subprocess.run(
        ["pkg-config", "--cflags", "raylib"],
        capture_output=True,
        text=True,
    )
    return result.stdout.split() if result.returncode == 0 else None


def main():
    base_h = ROOT / "vendor/fmt/base.h"
    match = re.search(r"#define FMT_VERSION (\d+)", base_h.read_text())
    if not match or int(match.group(1)) != EXPECTED_FMT_VERSION:
        failures.append(
            f"vendor/fmt is FMT_VERSION "
            f"{match.group(1) if match else 'unknown'}, expected "
            f"{EXPECTED_FMT_VERSION} (fmt 11.2.0)"
        )

    cflags = raylib_cflags()
    if cflags is None:
        failures.append("pkg-config --cflags raylib failed; cannot compile heaviest TU")

    with tempfile.TemporaryDirectory() as tmp:
        probe = Path(tmp) / "fmt_probe.cpp"
        probe.write_text(PROBE)
        for cxx in compilers():
            name = " ".join(cxx)
            exe = Path(tmp) / "fmt_probe"
            run(
                [
                    *cxx,
                    "-std=c++23",
                    "-Werror=deprecated-literal-operator",
                    "-Ivendor",
                    "-Isrc",
                    str(probe),
                    "-o",
                    str(exe),
                ],
                cwd=ROOT,
            )
            if exe.exists():
                run([str(exe)], cwd=ROOT)
            else:
                failures.append(f"probe did not build with {name}")
            # PCH-equivalent: the heaviest TU (no PCH in this repo's makefile).
            if cflags is not None:
                run(
                    [
                        *cxx,
                        *TU_FLAGS,
                        *cflags,
                        "-c",
                        HEAVIEST_TU,
                        "-o",
                        str(Path(tmp) / "render_ui.o"),
                    ],
                    cwd=ROOT,
                )

    if failures:
        print("check_fmt FAILED:")
        for failure in failures:
            print(failure)
        return 1
    print("check_fmt OK (fmt 11.2.0, probe + heaviest TU, no PCH in makefile)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
