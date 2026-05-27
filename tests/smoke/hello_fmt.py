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
        # The bundle is ~350 MB from GitHub Releases. Most runs finish under
        # 90s, but CDN latency to fresh Windows runners has been observed to
        # spike past 10 minutes on bad days — bump generous to avoid flakes.
        run([str(rivet_bin), "toolchain", "install", args.llvm],
            env=env, timeout=1500)

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
        # First call on a cold runner bootstraps the vcpkg index — can run
        # tens of seconds longer on a flaky network. Generous timeout to
        # ride out CI variance; the body still completes in <5s most of
        # the time.
        run([str(rivet_bin), "add", "fmt"], cwd=project, env=env, timeout=600)

        banner("step 6: rivet fetch  (vcpkg builds fmt — slow)")
        try:
            run([str(rivet_bin), "fetch"], cwd=project, env=env, timeout=1800)
        except SystemExit:
            # vcpkg writes detailed diagnostics into buildtrees/ that the
            # outer "rivet fetch" log only references by path. Inline them
            # so CI failures are debuggable without an artifact download.
            banner("=== fetch failed: dumping vcpkg diagnostics ===")
            vcpkg_root = rivet_home / "sources" / "vcpkg"
            for sub in ("buildtrees", "triplets-rivet"):
                d = vcpkg_root / sub
                if not d.exists():
                    continue
                for p in sorted(d.rglob("*")):
                    if p.is_file() and p.suffix in (".log", ".cmake", ".txt"):
                        try:
                            data = p.read_text(errors="replace")
                        except OSError:
                            continue
                        print(f"\n----- {p} -----", flush=True)
                        print(data[-4000:] if len(data) > 4000 else data, flush=True)
            raise

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

        # ── M1 multi-target validation ────────────────────────────────────
        # The hello-fmt path above exercises the single-binary src/-scan
        # build path. We also need to validate the multi-target engine
        # (rivet's own self-build will rely on it). Scaffold a tiny
        # lib + bin project on the side and build it through the same
        # rivet binary.
        banner("step 9: multi-target build (lib + bin, M1 engine)")
        m1 = workdir / "m1demo"
        m1.mkdir()
        (m1 / "src" / "lib").mkdir(parents=True)
        (m1 / "src" / "bin").mkdir(parents=True)
        (m1 / "rivet.toml").write_text(
            "[package]\n"
            "name = \"m1demo\"\n"
            "version = \"0.1.0\"\n"
            "\n"
            "[[lib]]\n"
            "name = \"greeter\"\n"
            "sources = [\"src/lib/greeter.cpp\"]\n"
            "include_dirs = [\"src/lib\"]\n"
            "\n"
            "[[bin]]\n"
            "name = \"say-hi\"\n"
            "path = \"src/bin/main.cpp\"\n"
            "depends_on = [\"greeter\"]\n"
        )
        (m1 / "src" / "lib" / "greeter.h").write_text(
            "#pragma once\n"
            "namespace greeter { void say(const char* who); }\n"
        )
        (m1 / "src" / "lib" / "greeter.cpp").write_text(
            "#include <iostream>\n"
            "#include \"greeter.h\"\n"
            "void greeter::say(const char* who) { std::cout << \"hello \" << who << \"\\n\"; }\n"
        )
        (m1 / "src" / "bin" / "main.cpp").write_text(
            "#include \"../lib/greeter.h\"\n"
            "int main() { greeter::say(\"M1\"); return 0; }\n"
        )
        run([str(rivet_bin), "build"], cwd=m1, env=env, timeout=300)

        # Locate + run the produced binary.
        m1_bin = m1 / ".rivet" / "build" / "debug" / "bin" / "say-hi"
        if not m1_bin.exists():
            for cand in m1.rglob("say-hi*"):
                if cand.is_file() and ".rivet" in cand.parts:
                    m1_bin = cand
                    break
        if not m1_bin.exists():
            sys.exit(f"M1 binary 'say-hi' not produced under {m1}")
        proc = subprocess.run([str(m1_bin)], capture_output=True, text=True,
                              timeout=30, env=env)
        print(f"M1 stdout: {proc.stdout!r}", flush=True)
        if proc.returncode != 0:
            sys.exit(f"M1 binary exited {proc.returncode}")
        if "hello M1" not in proc.stdout:
            sys.exit(f"M1 unexpected output: {proc.stdout!r}")

        # ── C2: rivet exec (binary-from-dep) sanity check ────────────────
        # vcpkg always ships a `vcpkg` tool inside the bundled-vcpkg
        # checkout we manage. It's not a dep-installed binary per se, but
        # since rivet-fetch already runs vcpkg as the build driver, the
        # binary is reliably present at <rivet_home>/sources/vcpkg/vcpkg
        # — close enough to exercise the exec discovery path on every OS.
        # The HELLO-FMT project doesn't add any binary-producing dep, so
        # we just verify the command rejects unknown binaries cleanly.
        banner("step 10: rivet exec (negative-path sanity)")
        proc = subprocess.run(
            [str(rivet_bin), "exec", "definitely-not-a-real-binary"],
            cwd=str(project), env=env, capture_output=True, text=True, timeout=30,
        )
        print(f"exit:   {proc.returncode}", flush=True)
        print(f"stderr: {proc.stderr!r}"[:400], flush=True)
        if proc.returncode == 0:
            sys.exit("rivet exec should fail on unknown binary")
        if "no binary named" not in proc.stderr:
            sys.exit(f"unexpected error message: {proc.stderr!r}")

        # ── C3: workspace (root with [workspace] + members) ──────────────
        # Scaffold a tiny monorepo: one workspace root listing two member
        # packages. `rivet build` at the root must dispatch into each
        # member, builds them via the existing single-binary path, and
        # produce two binaries. Validates workspace recursion + member
        # manifest parsing.
        banner("step 11: workspace build (C3 root + 2 members)")
        ws = workdir / "ws-demo"
        (ws / "crates" / "alpha" / "src").mkdir(parents=True)
        (ws / "crates" / "beta"  / "src").mkdir(parents=True)
        (ws / "rivet.toml").write_text(
            "[workspace]\n"
            "members = [\"crates/alpha\", \"crates/beta\"]\n"
        )
        (ws / "crates" / "alpha" / "rivet.toml").write_text(
            "[package]\n"
            "name = \"alpha\"\n"
            "version = \"0.1.0\"\n"
        )
        (ws / "crates" / "alpha" / "src" / "main.cpp").write_text(
            "#include <iostream>\n"
            "int main() { std::cout << \"alpha\\n\"; return 0; }\n"
        )
        (ws / "crates" / "beta" / "rivet.toml").write_text(
            "[package]\n"
            "name = \"beta\"\n"
            "version = \"0.1.0\"\n"
        )
        (ws / "crates" / "beta" / "src" / "main.cpp").write_text(
            "#include <iostream>\n"
            "int main() { std::cout << \"beta\\n\"; return 0; }\n"
        )
        run([str(rivet_bin), "build"], cwd=ws, env=env, timeout=300)

        for name in ("alpha", "beta"):
            bin_path = ws / "crates" / name / ".rivet" / "build" / "debug" / "bin" / name
            if not bin_path.exists():
                # Tolerate .exe suffix on Windows.
                alt = bin_path.with_suffix(".exe")
                if alt.exists():
                    bin_path = alt
            if not bin_path.exists():
                sys.exit(f"workspace member '{name}' did not produce a binary at {bin_path}")
            proc = subprocess.run([str(bin_path)], capture_output=True, text=True, timeout=30, env=env)
            if proc.returncode != 0 or name not in proc.stdout:
                sys.exit(f"workspace member '{name}' bad: rc={proc.returncode} stdout={proc.stdout!r}")

        banner("SMOKE TEST PASSED (hello-fmt + M1 multi-target + exec sanity + C3 workspace)")
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
