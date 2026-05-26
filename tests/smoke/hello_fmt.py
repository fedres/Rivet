"""End-to-end smoke test for rivet — `hello-fmt`.

Drives the freshly built `rivet` CLI through the cargo/npm-style workflow:

    rivet --version
    rivet toolchain install <llvm-version>
    rivet new hello-fmt
    # (overwrite main.cpp to use fmt)
    rivet add fmt
    rivet fetch                  # vcpkg bootstraps + builds fmt with bundled clang
    rivet build
    ./.rivet/build/debug/bin/hello-fmt

Designed to run on Linux, macOS, and Windows GitHub Actions runners. The
script keeps `RIVET_HOME` isolated under the workdir so a CI run never
mutates the host's `~/.rivet`.

Usage:
    python hello_fmt.py <path-to-rivet-binary> [--llvm 19.1.7] [--keep]
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def banner(msg: str) -> None:
    line = "=" * (len(msg) + 4)
    print(f"\n{line}\n  {msg}\n{line}", flush=True)


def run(
    cmd: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    timeout: int = 600,
    capture: bool = False,
) -> subprocess.CompletedProcess:
    pretty = " ".join(str(c) for c in cmd)
    print(f"\n$ {pretty}" + (f"   (cwd={cwd})" if cwd else ""), flush=True)
    t0 = time.monotonic()
    proc = subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        env=env,
        timeout=timeout,
        text=True,
        capture_output=capture,
    )
    dt = time.monotonic() - t0
    if capture:
        if proc.stdout:
            print(proc.stdout, end="", flush=True)
        if proc.stderr:
            print(proc.stderr, end="", file=sys.stderr, flush=True)
    print(f"[exit={proc.returncode}  elapsed={dt:.1f}s]", flush=True)
    if proc.returncode != 0:
        sys.exit(f"command failed: {pretty}")
    return proc


def find_executable(project: Path, name: str) -> Path:
    candidates = [
        project / ".rivet" / "build" / "debug" / "bin" / name,
        project / ".rivet" / "build" / "debug" / "bin" / (name + ".exe"),
    ]
    for c in candidates:
        if c.is_file():
            return c
    found = [p for p in project.rglob(name + "*")
             if p.is_file() and ".rivet" in p.parts]
    if found:
        return found[0]
    sys.exit(f"could not find built binary '{name}' under {project}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("rivet", help="path to the rivet CLI binary")
    ap.add_argument("--llvm", default="19.1.7",
                    help="LLVM toolchain version to install (default: 19.1.7)")
    ap.add_argument("--keep", action="store_true",
                    help="keep workdir on success for inspection")
    args = ap.parse_args()

    rivet_bin = Path(args.rivet).resolve()
    if not rivet_bin.is_file():
        sys.exit(f"rivet binary not found at {rivet_bin}")

    workdir = Path(tempfile.mkdtemp(prefix="rivet-smoke-"))
    print(f"workdir: {workdir}", flush=True)

    rivet_home = workdir / "rivet-home"
    rivet_home.mkdir()
    env = os.environ.copy()
    env["RIVET_HOME"] = str(rivet_home)

    failed = False
    try:
        banner("step 1: rivet --version")
        run([str(rivet_bin), "--version"], env=env, timeout=30, capture=True)

        banner(f"step 2: rivet toolchain install {args.llvm}")
        run([str(rivet_bin), "toolchain", "install", args.llvm],
            env=env, timeout=600)

        banner("step 3: rivet new hello-fmt")
        run([str(rivet_bin), "new", "hello-fmt"], cwd=workdir, env=env, timeout=60)
        project = workdir / "hello-fmt"
        manifest = project / "rivet.toml"
        if not manifest.is_file():
            sys.exit(f"rivet new did not create {manifest}")

        banner("step 4: overwrite src/main.cpp to use fmt")
        main_cpp = project / "src" / "main.cpp"
        main_cpp.write_text(
            "#include <fmt/core.h>\n"
            "int main() {\n"
            "    fmt::print(\"hello from fmt {}\\n\", 42);\n"
            "    return 0;\n"
            "}\n"
        )
        print(f"wrote {main_cpp}", flush=True)

        banner("step 5: rivet add fmt")
        run([str(rivet_bin), "add", "fmt"], cwd=project, env=env, timeout=120)

        banner("step 6: rivet fetch  (vcpkg builds fmt — slow)")
        run([str(rivet_bin), "fetch"], cwd=project, env=env, timeout=1800)

        banner("step 7: rivet build")
        run([str(rivet_bin), "build"], cwd=project, env=env, timeout=600)

        banner("step 8: run the binary and verify output")
        binary = find_executable(project, "hello-fmt")
        print(f"binary: {binary}", flush=True)
        proc = subprocess.run([str(binary)], capture_output=True, text=True,
                              timeout=30, env=env)
        print(f"stdout: {proc.stdout!r}", flush=True)
        print(f"stderr: {proc.stderr!r}", flush=True)
        print(f"exit:   {proc.returncode}", flush=True)
        if proc.returncode != 0:
            sys.exit(f"binary exited {proc.returncode}")
        if "hello from fmt 42" not in proc.stdout:
            sys.exit(f"unexpected output: {proc.stdout!r}")

        banner("SMOKE TEST PASSED")
    except BaseException:
        failed = True
        raise
    finally:
        if args.keep or failed:
            print(f"\nworkdir retained: {workdir}", flush=True)
        else:
            shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    main()
