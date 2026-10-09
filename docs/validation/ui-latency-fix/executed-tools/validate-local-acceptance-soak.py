"""Run optional long local acceptance probes against a built PortBridge.

Windows, Qt 6.8.3 and the project MinGW build are required. Probes compile
separately and link the same production libraries as test_http_projects.
No source, deployed EXE, release archive or system settings are modified.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=["all", "deadline", "soak", "dpi", "overload", "faults", "waits"], default="all")
    parser.add_argument("--build-directory", type=Path, default=ROOT / "build/workflow-product")
    parser.add_argument("--output-directory", type=Path)
    parser.add_argument("--duration-seconds", type=int, default=300)
    parser.add_argument("--protocol", choices=["both", "tcp", "udp"], default="both")
    parser.add_argument("--qt-root", type=Path, default=Path("C:/Qt/6.8.3/mingw_64"))
    parser.add_argument("--mingw-root", type=Path, default=Path("C:/Qt/Tools/mingw1310_64"))
    parser.add_argument("--ninja", type=Path, default=Path("C:/Qt/Tools/Ninja/ninja.exe"))
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("These acceptance probes require Windows.")
    if not 30 <= args.duration_seconds <= 3600:
        parser.error("Duration must be 30..3600 seconds.")
    build = args.build_directory.resolve()
    output = (args.output_directory or ROOT / "build/local-acceptance" / datetime.now().strftime("%Y%m%d-%H%M%S")).resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("Choose an empty output directory; previous evidence is preserved.")
    output.mkdir(parents=True, exist_ok=True)
    required_free = args.duration_seconds * (4096000 + 1472000) * 1.2 + 256 * 1024 * 1024
    if args.suite in ("all", "soak", "dpi", "overload") and shutil.disk_usage(output).free < required_free:
        parser.error("Insufficient free space for the declared recording load.")
    env = {key: value for key, value in os.environ.items() if not key.startswith("QT_")}
    env["PATH"] = ";".join([str(args.mingw_root / "bin"), str(args.qt_root / "bin"),
                            str(ROOT / ".deps/qtserialport-install/bin"), env.get("PATH", "")])
    source_files = list((ROOT / "src").rglob("*.cpp")) + list((ROOT / "src").rglob("*.hpp")) + list((ROOT / "include").rglob("*.hpp"))
    source_hashes = {str(path.relative_to(ROOT)): digest(path) for path in source_files}
    artifact_dir = ROOT / "build/acceptance-probes" / output.name
    if artifact_dir.exists() and any(artifact_dir.iterdir()):
        parser.error("Probe build directory already contains evidence; choose a new output directory name.")
    artifact_dir.mkdir(parents=True, exist_ok=True)
    commands = subprocess.run([str(args.ninja), "-C", str(build), "-t", "commands", "test_http_projects"],
                              check=True, capture_output=True, text=True, encoding="utf-8").stdout.splitlines()[-1]
    libraries = shlex.split(commands[commands.index("  libportbridge_ui.a"):commands.index(" && cd .")].strip())
    compiler = args.mingw_root / "bin/g++.exe"
    includes = [ROOT / "src", ROOT / "include"] + [args.qt_root / "include" / subdir for subdir in ("", "QtCore", "QtWidgets", "QtGui", "QtNetwork")]
    wanted = ["deadline", "soak", "dpi", "overload", "faults", "waits"] if args.suite == "all" else [args.suite]
    compiled = {}
    probe_hashes = {}
    probe_for_suite = {"waits": "faults", "dpi": "soak", "overload": "soak"}
    for probe in dict.fromkeys(probe_for_suite.get(suite, suite) for suite in wanted):
        source = ROOT / "tools/acceptance" / f"{probe}_probe.cpp"
        probe_hashes[str(source.relative_to(ROOT))] = digest(source)
        snapshot = artifact_dir / f"{probe}_probe.cpp"
        shutil.copy2(source, snapshot)
        obj = artifact_dir / f"{probe}.o"
        exe = artifact_dir / f"{probe}.exe"
        with (output / f"{probe}-build.txt").open("wb") as log:
            subprocess.run([str(compiler), "-std=c++17", "-O2"] + ["-I" + str(path) for path in includes] +
                           ["-c", str(snapshot), "-o", str(obj)], env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run([str(compiler), str(obj), "-o", str(exe)] + libraries + (["-lpsapi"] if probe == "soak" else []),
                           cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        compiled[probe] = exe
    cases = []
    if "deadline" in wanted:
        cases.append(("deadline", "deadline", [], "offscreen", 660))
    for suite in ("soak", "dpi", "overload"):
        if suite not in wanted:
            continue
        for protocol in (("udp", "tcp") if args.protocol == "both" else (args.protocol,)):
            name = protocol if suite == "soak" else f"{protocol}-{suite}"
            capture = artifact_dir / f"{name}-captures"
            capture.mkdir()
            seconds = args.duration_seconds if suite == "soak" else 30
            flags = ["move-screens" if suite == "dpi" else "fixed-screen"]
            if suite == "overload":
                flags.append("stall-ui")
            cases.append((name, "soak", [str(capture), protocol, str(seconds * 1000)] + flags, "windows", seconds + 60))
    if "faults" in wanted or "waits" in wanted:
        modes = []
        if "faults" in wanted:
            modes.extend(("dns-stall", "file-stall", "dns-exit-stall", "file-exit-stall"))
        if "waits" in wanted:
            modes.append("wait-diagnostics")
        for mode in modes:
            capture = artifact_dir / f"{mode}-data"
            capture.mkdir()
            cases.append((mode, "faults", [str(capture)], "offscreen", 15 if mode.endswith("stall") else 90))
    receipt = {"state": "running", "scope": "local optional acceptance; hardware and clean Windows remain separate", "results": []}
    for name, probe, arguments, platform, timeout in cases:
        result_file = output / f"{name}-result.json"
        command = [str(compiled[probe]), str(result_file)] + arguments
        if probe == "faults":
            command.insert(1, name)
        env["QT_QPA_PLATFORM"] = platform
        print(f"Running {name} (up to {timeout}s); evidence: {result_file}", flush=True)
        start = time.monotonic()
        with (output / f"{name}-driver.txt").open("wb") as log:
            try:
                run = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
                exit_code = run.returncode
            except subprocess.TimeoutExpired:
                exit_code = -1
        result = json.loads(result_file.read_text(encoding="utf-8")) if result_file.exists() else {"state": "no-result"}
        if probe == "soak" and result.get("state") in ("passed", "failed") and "admittedFrames" in result:
            verification = output / f"{name}-capture-verification.json"
            verify_run = subprocess.run([os.sys.executable, str(ROOT / "scripts/verify-capture-stream.py"), "--capture-directory", arguments[0],
                                         "--protocol", arguments[1], "--expected-frames", str(int(result["admittedFrames"])), "--output", str(verification)])
            if verify_run.returncode:
                exit_code = exit_code or verify_run.returncode
        receipt["results"].append({"name": name, "exitCode": exit_code, "state": result["state"], "seconds": round(time.monotonic() - start, 3)})
        (output / "receipt.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    receipt["sourceUnchanged"] = all(digest(ROOT / name) == value for name, value in source_hashes.items())
    receipt["productionSourceSha256"] = source_hashes
    receipt["probeExecutableSha256"] = {probe: digest(path) for probe, path in compiled.items()}
    receipt["buildArtifactsDirectory"] = str(artifact_dir.relative_to(ROOT))
    receipt["probeSourceSha256"] = probe_hashes
    receipt["probeSourceStable"] = all(digest(ROOT / name) == value for name, value in probe_hashes.items())
    receipt["state"] = "passed" if receipt["sourceUnchanged"] and all(case["exitCode"] == 0 and case["state"] == "passed" for case in receipt["results"]) else "failed"
    (output / "receipt.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Acceptance {receipt['state']}: {output}", flush=True)
    return int(receipt["state"] != "passed")


if __name__ == "__main__":
    raise SystemExit(main())
